// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <windows.h>
#include <psapi.h>

#include <cameraunlock/diagnostics/crash_handler.h>
#include <cameraunlock/unreal/ue_runtime.h>

#include "builds/build_registry.h"
#include "config.h"
#include "exe_paths.h"
#include "game_state.h"
#include "hotkeys.h"
#include "logging.h"
#include "runtime_state.h"
#include "viewpoint_hook.h"

// Bootstrap and teardown. Everything that happens once: read the INI, identify
// the build, publish the module range, open the tracker socket, install the
// hook, bind the hotkeys. The per-frame work lives in viewpoint_hook.cpp.

namespace pinballfx_ht
{
    namespace
    {
        using cameraunlock::TrackingMode;
        namespace ue = ::cameraunlock::unreal;

        constexpr wchar_t kLogFileName[] = L"\\HeadTracking.log";

        HANDLE g_bootstrapThread = nullptr;

        // Set by Shutdown before it tears anything down. The bootstrap runs on
        // its own thread, so on an explicit FreeLibrary it can be part-way
        // through when the teardown starts - and the two things it has left to
        // do are install a detour and start a thread, both of which would then
        // be pointing into a module the loader is about to unmap. Cooperative
        // rather than a join: Shutdown runs under the loader lock, and the
        // bootstrap takes it (MinHook, WSAStartup), so waiting here deadlocks.
        std::atomic<bool> g_shuttingDown{false};

        // Owned for the life of the module: the hook holds references to all
        // three and runs until the process exits.
        Config g_config;
        GameStateGate g_gate;
        std::unique_ptr<cameraunlock::UdpReceiver> g_receiver;
        std::unique_ptr<Session> g_session;
        std::unique_ptr<cameraunlock::input::HotkeyPoller> g_hotkeys;

        // The gate the hook consults, and a warning for either half the config
        // asks for that the active profile leaves unpinned - so a user who
        // wonders why menus still move has the answer before they read a single
        // hook line. Reads the active profile, so it runs after
        // SelectBuildProfile.
        void ConfigureGameStateGate()
        {
            g_gate.gameplay_only = g_config.gameplay_only;
            g_gate.suppress_during_camera_sequences = g_config.suppress_during_camera_sequences;

            const OffsetTable& offsets = Offsets();
            if (g_gate.gameplay_only && !GameplayGateIsPinned(offsets))
                Log::Line("WARNING: GameplayOnly is set but this build profile does not pin "
                          "the game-state chain, so head tracking also applies in menus. "
                          "Set GameplayOnly=0 to stop asking for it.");
            if (g_gate.suppress_during_camera_sequences && !CameraSequenceGateIsPinned(offsets))
                Log::Line("WARNING: SuppressDuringCameraSequences is set but this build "
                          "profile does not pin the camera-sequence chain, so head tracking "
                          "also applies during scripted camera moves.");
        }

        void ApplyConfigToSession()
        {
            Runtime().trackingEnabled.store(g_config.enable_on_startup);
            Runtime().worldSpaceYaw.store(g_config.world_space_yaw);

            cameraunlock::SensitivitySettings sens;
            sens.yaw          = g_config.yaw_sensitivity;
            sens.pitch        = g_config.pitch_sensitivity;
            sens.roll         = g_config.roll_sensitivity;
            sens.invert_yaw   = g_config.invert_yaw;
            sens.invert_pitch = g_config.invert_pitch;
            sens.invert_roll  = g_config.invert_roll;
            g_session->GetProcessor().SetSensitivity(sens);
            // Both smoothing parameters cover rotation and position; the session
            // picks between them per connection from the receiver's
            // source-address check, so a switch from a local OpenTrack instance
            // to a phone on WiFi mid-session needs no restart.
            g_session->SetLocalSmoothing(g_config.local_smoothing);
            g_session->SetRemoteSmoothing(g_config.remote_smoothing);

            // Through the session rather than the position processor directly:
            // the session owns the two smoothing values and recomposes them onto
            // whatever settings it is handed.
            cameraunlock::PositionSettings position = g_session->GetPositionSettings();
            position.sensitivity_x = g_config.position_sensitivity_x;
            position.sensitivity_y = g_config.position_sensitivity_y;
            position.sensitivity_z = g_config.position_sensitivity_z;
            position.limit_x       = g_config.limit_x;
            // The INI exposes one vertical limit, so it has to reach both sides
            // of the clamp - the processor's is [-limit_y_down, +limit_y], and
            // leaving the down side at its struct default silently caps a raised
            // LimitY at 0.20m downward.
            position.limit_y       = g_config.limit_y;
            position.limit_y_down  = g_config.limit_y;
            position.limit_z       = g_config.limit_z;
            position.limit_z_back  = g_config.limit_z_back;
            g_session->SetPositionSettings(position);

            g_session->SetMode(g_config.position_enabled
                ? TrackingMode::RotationAndPosition
                : TrackingMode::RotationOnly);
        }

        void LoadAndLogConfig()
        {
            const std::string exeDir = ExeDirectoryNarrow();
            WriteDefaultConfigIfMissing(exeDir);
            LoadConfig(exeDir, g_config);
            Log::Line("config: udp_port=%d enable=%d yaw_sens=%.2f local_smoothing=%.2f remote_smoothing=%.2f position=%d fov_override=%.1f fov_offset=%.1f yaw_mode=%s yaw_mode_key=0x%02X gameplay_only=%d suppress_camera_sequences=%d",
                g_config.udp_port, g_config.enable_on_startup ? 1 : 0,
                g_config.yaw_sensitivity, g_config.local_smoothing, g_config.remote_smoothing,
                g_config.position_enabled ? 1 : 0,
                g_config.fov_override, g_config.fov_offset,
                g_config.world_space_yaw ? "world" : "local", g_config.yaw_mode_key,
                g_config.gameplay_only ? 1 : 0,
                g_config.suppress_during_camera_sequences ? 1 : 0);
        }

        // False = this build is not one the mod knows how to touch. The caller
        // must then install nothing at all, leaving the game vanilla.
        bool SelectBuildProfile(HMODULE host)
        {
            switch (builds::SelectProfile(host)) {
                case builds::MatchResult::Matched:
                    return true;
                case builds::MatchResult::HostNewer:
                    Log::Line("build-check: this game build is NEWER than any profile this "
                              "mod knows about - check the releases page for an update. "
                              "Staying dormant; game runs vanilla.");
                    return false;
                case builds::MatchResult::HostOlder:
                    Log::Line("build-check: this game build is OLDER than the profile - let "
                              "Steam finish updating. Staying dormant; game runs vanilla.");
                    return false;
                default:
                    Log::Line("build-check: no matching/complete profile - staying dormant; "
                              "game runs vanilla.");
                    return false;
            }
        }

        // Publishes the module range that every RVA in this mod is relative to.
        bool PublishModuleRange(HMODULE host)
        {
            MODULEINFO info{};
            if (!GetModuleInformation(GetCurrentProcess(), host, &info, sizeof(info))) {
                Log::Line("FATAL: GetModuleInformation failed - cannot resolve RVAs");
                return false;
            }
            const auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
            // The lean hook does no UObject reflection, so the globals layout is
            // left zeroed - only the module range and SafeRead* guards are used.
            ue::SetRuntime(base, base + info.SizeOfImage, ue::UObjectGlobalsLayout{});
            Log::Line("module base=0x%llx size=0x%x",
                static_cast<unsigned long long>(base), info.SizeOfImage);
            return true;
        }

        void StartTracking()
        {
            g_receiver = std::make_unique<cameraunlock::UdpReceiver>();
            g_receiver->SetLog([](const std::string& message) { Log::Line("udp: %s", message.c_str()); });
            if (!g_receiver->Start(static_cast<uint16_t>(g_config.udp_port)))
                Log::Line("udp: port %d busy - retrying in background", g_config.udp_port);

            g_session = std::make_unique<Session>(*g_receiver);
            ApplyConfigToSession();
        }

        // Dormant from here on, so hand the tracker port back rather than
        // squatting on it: the next game the user opens needs it, and a receiver
        // nothing reads from would deny it for the whole session.
        void StopTracking()
        {
            g_session.reset();
            g_receiver->Stop();
            g_receiver.reset();
        }

        // One log per session, beside the game EXE. Open() truncates, so a
        // session never appends to the last one, and it rotates the outgoing
        // generation to HeadTracking.prev.log first - the crash handler asks the
        // user to send this log, and they relaunch the game before going to look
        // for it, which would otherwise truncate away the session being reported.
        void OpenSessionLog()
        {
            Log::Open(ExeDirectory() + kLogFileName);
            Log::Line("=== Pinball FX Head Tracking v%s (UE 4.27) ===", PINBALLFX_HT_VERSION);
        }

        DWORD WINAPI BootstrapThread(LPVOID)
        {
            OpenSessionLog();
            cameraunlock::diagnostics::InstallCrashHandler();

            LoadAndLogConfig();

            HMODULE host = GetModuleHandleW(nullptr);
            if (!SelectBuildProfile(host)) return 0;

            Runtime().injectMode.store(Offsets().kDefaultInjectMode);
            ConfigureGameStateGate();

            if (!PublishModuleRange(host)) return 0;

            if (g_shuttingDown.load()) return 0;
            StartTracking();

            // Re-checked after the socket is up: everything past this point
            // outlives the call, so it must not start once teardown has begun.
            if (g_shuttingDown.load() ||
                !InstallViewPointHook(g_config, *g_session, *g_receiver, g_gate)) {
                StopTracking();
                return 0;
            }

            g_hotkeys = StartHotkeys(*g_session, g_config.yaw_mode_key);
            Log::Line("init complete. End=toggle PageUp=cycle tracking mode "
                      "PageDown=yawmode (chords Ctrl+Shift+Y/G/H). Waiting for OpenTrack on UDP %d.",
                g_config.udp_port);
            return 0;
        }
    }

    void Initialize()
    {
        g_bootstrapThread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
    }

    void Shutdown()
    {
        g_shuttingDown.store(true);
        if (g_hotkeys) g_hotkeys->Stop();
        if (g_receiver) g_receiver->Stop();
        RemoveViewPointHook();
        Log::Line("shutdown");
        Log::Close();
        if (g_bootstrapThread) {
            CloseHandle(g_bootstrapThread);
            g_bootstrapThread = nullptr;
        }
    }
}
