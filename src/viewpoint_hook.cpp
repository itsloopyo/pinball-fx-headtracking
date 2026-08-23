// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "viewpoint_hook.h"

#include <atomic>
#include <cstdint>

#include <intrin.h>

#include <cameraunlock/hooks/hook_manager.h>
#include <cameraunlock/time/frame_clock.h>
#include <cameraunlock/unreal/ue_runtime.h>

#include "builds/build_registry.h"
#include "caller_trace.h"
#include "hook_diagnostics.h"
#include "logging.h"
#include "ue4_types.h"
#include "view_injection.h"

namespace pinballfx_ht
{
    namespace
    {
        namespace hooks = cameraunlock::hooks;
        using cameraunlock::time::FrameClock;

        // Set once by InstallViewPointHook, before the detour can run.
        const Config*                    g_config = nullptr;
        Session*                         g_session = nullptr;
        const cameraunlock::UdpReceiver* g_receiver = nullptr;
        GameStateGate                    g_gate;

        GetPlayerViewPoint_t g_origGetPlayerViewPoint = nullptr;
        std::atomic<std::uint64_t> g_hookCallCount{0};

        // Ticked only by the injected render-path caller, so the session sees
        // one dt per rendered frame.
        FrameClock g_frameClock;

        CallerCensus g_callerCensus;

        // The detour must never re-enter itself: the trampoline runs the
        // engine's own GetPlayerViewPoint, and any path from there back to the
        // hooked address would recurse until the stack faults. A re-entrant call
        // is passed straight through to the original.
        class ReentrancyGuard
        {
        public:
            ReentrancyGuard() : m_reentered(s_inside) { s_inside = true; }
            ~ReentrancyGuard()
            {
                if (!m_reentered) s_inside = false;
            }
            ReentrancyGuard(const ReentrancyGuard&) = delete;
            ReentrancyGuard& operator=(const ReentrancyGuard&) = delete;

            bool Reentered() const { return m_reentered; }

        private:
            static thread_local bool s_inside;
            bool m_reentered;
        };
        thread_local bool ReentrancyGuard::s_inside = false;

        // The FOV is read on every render-path call, so the log always shows the
        // live angle: it belongs to the table's current view and changes when
        // the player switches view, and someone asking "what FOV is this" gets
        // an answer whether or not a tracker is connected. The write is gated on
        // tracking being enabled, so End still restores an exactly vanilla view,
        // FOV included.
        FovSample UpdateFieldOfView(const OffsetTable& offsets, std::uintptr_t retRva,
                                    FVector4f* outLocation)
        {
            if (!CallerCarriesViewInfo(retRva, offsets.kViewInfoCallerRva,
                                       offsets.kMinimalViewInfoFovOffset))
                return FovSample{0.0f, 0.0f};

            float* const fov = reinterpret_cast<float*>(
                reinterpret_cast<char*>(outLocation) + offsets.kMinimalViewInfoFovOffset);
            const float gameFov = *fov;
            if (!FovLooksSane(gameFov)) {
                diagnostics::LogImplausibleFov(offsets.kMinimalViewInfoFovOffset, gameFov);
                return FovSample{gameFov, gameFov};
            }
            diagnostics::RecordGameFov(gameFov);

            if (!Runtime().trackingEnabled.load(std::memory_order_relaxed))
                return FovSample{gameFov, gameFov};

            const float renderFov =
                EffectiveFov(gameFov, g_config->fov_override, g_config->fov_offset);
            if (renderFov != gameFov) *fov = renderFov;
            diagnostics::LogFovApplied(gameFov, renderFov,
                                       g_config->fov_override, g_config->fov_offset);
            return FovSample{gameFov, renderFov};
        }

        ue::FVector ApplyPositionOffset(const ue::FQuat4d& baseQuat, FVector4f* outLocation)
        {
            float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;
            if (!g_session->GetPositionOffset(offsetX, offsetY, offsetZ))
                return ue::FVector{0.0, 0.0, 0.0};

            const ue::FVector offset = PositionOffsetUE(baseQuat, offsetX, offsetY, offsetZ);
            outLocation->X += static_cast<float>(offset.X);
            outLocation->Y += static_cast<float>(offset.Y);
            outLocation->Z += static_cast<float>(offset.Z);
            return offset;
        }

        std::uintptr_t ReturnAddressRva(const void* returnAddress)
        {
            const auto address = reinterpret_cast<std::uintptr_t>(returnAddress);
            return ue::ModuleBase() != 0 ? address - ue::ModuleBase() : address;
        }

        void __fastcall GetPlayerViewPoint_Hook(void* self, FVector4f* outLocation,
                                               FRotator4f* outRotation)
        {
            const std::uintptr_t retRva = ReturnAddressRva(_ReturnAddress());

            static std::atomic<std::uint64_t> s_entries{0};
            const std::uint64_t entryNo = s_entries.fetch_add(1, std::memory_order_relaxed) + 1;
            ReentrancyGuard guard;
            diagnostics::LogHookEntry(entryNo, guard.Reentered(), retRva);
            if (guard.Reentered()) {
                diagnostics::LogHookReentry(entryNo, retRva);
                g_origGetPlayerViewPoint(self, outLocation, outRotation);
                return;
            }

            g_origGetPlayerViewPoint(self, outLocation, outRotation);
            const FRotator4f cleanRotation = *outRotation;
            const FVector4f  cleanLocation = *outLocation;

            const std::uint64_t call = g_hookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
            const int mode = Runtime().injectMode.load(std::memory_order_relaxed);
            const OffsetTable& offsets = Offsets();

            if (mode == kInjectModeAllCallers)
                g_callerCensus.RecordAndMaybeDump(call);

            // Ahead of every early return below: the FOV knob is a camera
            // setting, not a pose, so it applies with no tracker connected and
            // while the head is still. It lands on the same view info in the
            // same frame as the rotation and position writes, which is what
            // keeps the projection matrix and the view point the game
            // deprojects through describing one camera.
            const FovSample fov = UpdateFieldOfView(offsets, retRva, outLocation);

            diagnostics::LogHeartbeat(call, retRva, mode, *g_receiver);

            if (!Runtime().trackingEnabled.load(std::memory_order_relaxed))
                return;

            // Only the render-path caller(s) get the head pose written back.
            // Every other GetPlayerViewPoint caller - audio listener placement,
            // culling and streaming queries - keeps the clean camera rotation,
            // so nothing the game computes changes when tracking is on.
            if (!ShouldInjectForCaller(retRva, mode, offsets.kKnownCallerRvas))
                return;

            // Menus, loading, the pause screen and the scripted camera moves a
            // table plays are all places the camera belongs to the game rather
            // than to the player's head. Evaluated before the session is updated
            // but acted on after, so the tracker pose and its smoothing keep
            // running while tracking is held and the view does not lurch when
            // gameplay resumes.
            const GameplayState gameplay = ReadGameplayState(offsets);
            const bool inGameplay = ShouldTrackNow(gameplay, offsets.kYupGameStateInGame, g_gate);
            diagnostics::ReportGateVerdict(gameplay, inGameplay);

            if (!g_session->Update(g_frameClock.Tick()))
                return;

            HeadPose pose{};
            if (!g_session->GetRotation(pose.yaw, pose.pitch, pose.roll))
                return;

            if (!inGameplay)
                return;

            const ue::FQuat4d baseQuat = ViewQuat(cleanRotation);
            *outRotation = ComposeTrackedRotation(cleanRotation, baseQuat,
                pose.yaw, pose.pitch, pose.roll,
                Runtime().worldSpaceYaw.load(std::memory_order_relaxed));

            const ue::FVector positionOffset = ApplyPositionOffset(baseQuat, outLocation);

            diagnostics::LogPoseSample(call, retRva, cleanRotation, cleanLocation, pose,
                                       *outRotation, positionOffset, fov, *g_receiver, *g_session);
        }

        // Every failure path in InstallViewPointHook leaves the mod dormant and
        // the bootstrap then tears the session and receiver back down, so the
        // detour's references must not survive it. A hook that was created
        // before the failure comes out with them: it is not enabled, but it
        // still holds a trampoline pointing at a detour whose state has just
        // been abandoned.
        bool AbandonInstall()
        {
            hooks::HookManager::Instance().Shutdown();
            g_config = nullptr;
            g_session = nullptr;
            g_receiver = nullptr;
            return false;
        }
    }

    bool InstallViewPointHook(const Config& config, Session& session,
                              const cameraunlock::UdpReceiver& receiver,
                              const GameStateGate& gate)
    {
        g_config = &config;
        g_session = &session;
        g_receiver = &receiver;
        g_gate = gate;

        auto& manager = hooks::HookManager::Instance();
        if (auto status = manager.Initialize(); status != hooks::HookStatus::Ok) {
            Log::Line("FATAL: MinHook init failed: %s", hooks::HookStatusToString(status));
            return AbandonInstall();
        }

        const std::uintptr_t targetAddress = ue::ModuleBase() + Offsets().kGetPlayerViewPointRva;
        // An RVA past the end of the module does not name code in this build,
        // and in a loaded process it can land inside an unrelated DLL that would
        // be hooked instead. Checked before the address is read or written to.
        if (targetAddress < ue::ModuleBase() || targetAddress >= ue::ModuleEnd()) {
            Log::Line("FATAL: profile %s puts GetPlayerViewPoint at RVA 0x%08llx, outside the "
                      "module - staying dormant; game runs vanilla.",
                builds::ActiveProfile().Name,
                static_cast<unsigned long long>(Offsets().kGetPlayerViewPointRva));
            return AbandonInstall();
        }

        void* target = reinterpret_cast<void*>(targetAddress);
        if (auto status = manager.CreateHook(target,
                reinterpret_cast<void*>(&GetPlayerViewPoint_Hook),
                reinterpret_cast<void**>(&g_origGetPlayerViewPoint));
            status != hooks::HookStatus::Ok) {
            Log::Line("FATAL: CreateHook(GetPlayerViewPoint) failed: %s",
                hooks::HookStatusToString(status));
            return AbandonInstall();
        }
        if (auto status = manager.EnableHook(target); status != hooks::HookStatus::Ok) {
            Log::Line("FATAL: EnableHook failed: %s", hooks::HookStatusToString(status));
            return AbandonInstall();
        }
        Log::Line("GetPlayerViewPoint hooked at RVA 0x%08llx (default inject mode %d)",
            static_cast<unsigned long long>(Offsets().kGetPlayerViewPointRva),
            Runtime().injectMode.load());
        return true;
    }

    void RemoveViewPointHook()
    {
        hooks::HookManager::Instance().Shutdown();
    }
}
