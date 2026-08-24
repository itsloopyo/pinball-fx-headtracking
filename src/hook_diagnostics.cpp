// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hook_diagnostics.h"

#include <atomic>
#include <cstddef>
#include <cstring>

#include <windows.h>

#include "logging.h"

namespace pinballfx_ht::diagnostics
{
    namespace
    {
        constexpr std::uint64_t kLoggedEntries = 8;
        constexpr std::uint64_t kLoggedReentries = 64;
        constexpr std::uint64_t kHeartbeatIntervalMs = 30000;

        // Pose sampling runs dense to begin with and then settles to the
        // heartbeat's cadence. The opening burst is what proves the whole
        // pipeline in a log the user sends - what the tracker put on the wire,
        // what survived smoothing, what reached the camera - and a handful of
        // samples answers that. Holding that rate for a three-hour session
        // writes about a megabyte of it repeating itself, so the sample then
        // slows rather than stopping, which still catches a problem that only
        // starts an hour in.
        constexpr std::uint64_t kPoseBurstSamples = 15;
        constexpr std::uint64_t kPoseBurstIntervalMs = 2000;
        constexpr std::uint64_t kPoseSteadyIntervalMs = kHeartbeatIntervalMs;

        // A table plays a scripted camera move around ordinary events, and each
        // one moves the gate twice, so "log every change" grows without bound
        // over a session. The early transitions are the ones that explain a mod
        // that looks dead; past that the heartbeat's gate= field carries the
        // live verdict.
        constexpr std::uint64_t kLoggedGateTransitions = 40;

        std::atomic<float> g_lastGameFov{0.0f};

        // The gate's last verdict and the state it reached it from, so the
        // heartbeat can say why tracking is held without re-reading the game's
        // state on a caller that is not the render path.
        std::atomic<bool>          g_gateTracking{true};
        std::atomic<std::uint32_t> g_gateState{0};
        std::atomic<bool>          g_gateResolved{false};

        // Logs the gate the first time it is evaluated and on every change
        // after, so the log shows what the game was doing when tracking stopped
        // rather than one line per frame. Called from the hook thread only.
        void LogGateTransition(const GameplayState& state, bool tracking)
        {
            static bool logged = false;
            static GameplayState previous;
            static bool previousTracking = false;
            static std::uint64_t transitions = 0;

            if (logged && tracking == previousTracking &&
                state.handlerResolved == previous.handlerResolved &&
                state.yupState == previous.yupState &&
                state.paused == previous.paused &&
                state.cameraSequence == previous.cameraSequence)
                return;

            logged = true;
            previous = state;
            previousTracking = tracking;

            // Counted after the change is recorded above, so capping the lines
            // never makes a later change read as no change at all.
            if (transitions >= kLoggedGateTransitions) return;
            ++transitions;
            Log::Line("gate: tracking %s - gameState=%s paused=%d cameraSequence=%d%s",
                tracking ? "ON" : "held",
                state.handlerResolved ? YupGameStateName(state.yupState) : "unresolved",
                state.paused ? 1 : 0, state.cameraSequence ? 1 : 0,
                transitions == kLoggedGateTransitions
                    ? " (last gate line this session; the heartbeat carries the live verdict)"
                    : "");
        }
    }

    void LogHookEntry(std::uint64_t entryNo, bool reentered, std::uintptr_t retRva)
    {
        if (entryNo > kLoggedEntries) return;
        Log::EmergencyLine("GPV-ENTRY #%llu reentered=%d retRVA=0x%08llx",
            static_cast<unsigned long long>(entryNo), reentered ? 1 : 0,
            static_cast<unsigned long long>(retRva));
    }

    void LogHookReentry(std::uint64_t entryNo, std::uintptr_t retRva)
    {
        if (entryNo > kLoggedReentries) return;
        Log::EmergencyLine("GPV-REENTRY retRVA=0x%08llx",
            static_cast<unsigned long long>(retRva));
    }

    void RecordGameFov(float gameFov)
    {
        g_lastGameFov.store(gameFov, std::memory_order_relaxed);
    }

    void ReportGateVerdict(const GameplayState& state, bool tracking)
    {
        g_gateTracking.store(tracking, std::memory_order_relaxed);
        g_gateResolved.store(state.handlerResolved, std::memory_order_relaxed);
        g_gateState.store(state.yupState, std::memory_order_relaxed);
        LogGateTransition(state, tracking);
    }

    void LogHeartbeat(std::uint64_t call, std::uintptr_t retRva, int mode,
                      const cameraunlock::UdpReceiver& receiver)
    {
        static std::atomic<std::uint64_t> s_lastTick{0};
        const std::uint64_t now = GetTickCount64();
        if (call != 1 && (now - s_lastTick.load(std::memory_order_relaxed)) < kHeartbeatIntervalMs)
            return;
        s_lastTick.store(now, std::memory_order_relaxed);

        float yaw = 0, pitch = 0, roll = 0;
        const bool haveData = receiver.GetRotation(yaw, pitch, roll);
        Log::Line("heartbeat hook=%llu retRVA=0x%08llx enabled=%s udpData=%s raw=(Y=%.2f P=%.2f R=%.2f) yawMode=%s injectMode=%d gameFov=%.2f gate=%s(%s)",
            static_cast<unsigned long long>(call),
            static_cast<unsigned long long>(retRva),
            Runtime().trackingEnabled.load() ? "ON" : "OFF",
            haveData ? "YES" : "NO", yaw, pitch, roll,
            Runtime().worldSpaceYaw.load() ? "world" : "local", mode,
            static_cast<double>(g_lastGameFov.load(std::memory_order_relaxed)),
            g_gateTracking.load(std::memory_order_relaxed) ? "ON" : "held",
            g_gateResolved.load(std::memory_order_relaxed)
                ? YupGameStateName(g_gateState.load(std::memory_order_relaxed))
                : "unresolved");
    }

    void LogPoseSample(std::uint64_t call, std::uintptr_t retRva,
                       const FRotator4f& cleanRotation, const FVector4f& cleanLocation,
                       const HeadPose& pose, const FRotator4f& trackedRotation,
                       const ue::FVector& positionOffset, const FovSample& fov,
                       const cameraunlock::UdpReceiver& receiver, const Session& session)
    {
        static std::atomic<std::uint64_t> s_lastSample{0};
        static std::atomic<std::uint64_t> s_samples{0};
        const std::uint64_t taken = s_samples.load(std::memory_order_relaxed);
        const std::uint64_t interval =
            taken < kPoseBurstSamples ? kPoseBurstIntervalMs : kPoseSteadyIntervalMs;
        const std::uint64_t now = GetTickCount64();
        // taken != 0 rather than leaning on the zero-initialised timestamp:
        // GetTickCount64 is uptime, so on a machine that has just booted the
        // first sample would otherwise fall inside the interval and be dropped.
        if (taken != 0 && now - s_lastSample.load(std::memory_order_relaxed) < interval) return;
        s_lastSample.store(now, std::memory_order_relaxed);
        s_samples.store(taken + 1, std::memory_order_relaxed);

        // Position is reported at three stages because "the camera moves too
        // much / too little" can start at any of them: rawPos is what the
        // tracker actually sent (metres, straight off the wire), procPos is
        // after smoothing, and posOff is the world vector finally added to the
        // camera. Since the mod maps the pose 1:1, rawPos is also the answer to
        // "is this the mod or my tracker profile" - which is nearly always the
        // question being asked.
        float rawX = 0, rawY = 0, rawZ = 0;
        receiver.GetPosition(rawX, rawY, rawZ);
        float procX = 0, procY = 0, procZ = 0;
        session.GetPositionOffset(procX, procY, procZ);

        Log::Line("hook #%llu retRVA=0x%08llx clean_rot=(Y=%.2f P=%.2f R=%.2f) clean_loc=(%.0f,%.0f,%.0f) tracker=(Y=%.2f P=%.2f R=%.2f) rawPos=(%.3f,%.3f,%.3f) procPos=(%.3f,%.3f,%.3f) result=(Y=%.2f P=%.2f R=%.2f) posOff=(%.1f,%.1f,%.1f) fov=%.2f->%.2f",
            static_cast<unsigned long long>(call),
            static_cast<unsigned long long>(retRva),
            cleanRotation.Yaw, cleanRotation.Pitch, cleanRotation.Roll,
            cleanLocation.X, cleanLocation.Y, cleanLocation.Z,
            pose.yaw, pose.pitch, pose.roll,
            rawX, rawY, rawZ,
            procX, procY, procZ,
            trackedRotation.Yaw, trackedRotation.Pitch, trackedRotation.Roll,
            positionOffset.X, positionOffset.Y, positionOffset.Z,
            static_cast<double>(fov.game), static_cast<double>(fov.render));
    }

    void LogFovApplied(float gameFov, float renderFov, float fovOverride, float fovOffset)
    {
        static std::atomic<bool> s_logged{false};
        if (s_logged.exchange(true, std::memory_order_relaxed)) return;
        Log::Line("camera: FMinimalViewInfo.FOV reads %.2f deg (horizontal); rendering at "
                  "%.2f deg with FovOverride=%.1f FovOffset=%.1f",
            static_cast<double>(gameFov), static_cast<double>(renderFov),
            static_cast<double>(fovOverride), static_cast<double>(fovOffset));
    }

    void LogViewInfoFields(const void* viewInfo, float gameFov)
    {
        // One dump per distinct FOV, capped: a table switches view often, and
        // the interesting comparison is desktop against cabinet rather than
        // every cut-in in between.
        constexpr int kMaxDumps = 8;
        static std::atomic<int> s_dumps{0};
        static std::atomic<float> s_lastFov{0.0f};
        if (s_lastFov.load(std::memory_order_relaxed) == gameFov) return;
        if (s_dumps.fetch_add(1, std::memory_order_relaxed) >= kMaxDumps) return;
        s_lastFov.store(gameFov, std::memory_order_relaxed);

        const auto* const bytes = static_cast<const unsigned char*>(viewInfo);
        const auto ReadFloat = [bytes](std::size_t offset) {
            float value = 0.0f;
            std::memcpy(&value, bytes + offset, sizeof(value));
            return static_cast<double>(value);
        };

        Log::Line("view-info: FOV=%.2f DesiredFOV=%.2f OrthoWidth=%.1f AspectRatio=%.4f "
                  "flags=0x%02X projMode=0x%02X (offsets past FOV are the stock UE 4.27 "
                  "field order)",
            ReadFloat(0x18), ReadFloat(0x1C), ReadFloat(0x20), ReadFloat(0x2C),
            bytes[0x30], bytes[0x31]);
    }

    void LogImplausibleFov(std::size_t fovFieldOffset, float gameFov)
    {
        static std::atomic<bool> s_logged{false};
        if (s_logged.exchange(true, std::memory_order_relaxed)) return;
        Log::Line("camera: FOV field at +0x%02llx of the view info reads %g, which is not "
                  "an angle - leaving FOV alone for this session. Head tracking itself is "
                  "unaffected. Please report this with the build-check lines above.",
            static_cast<unsigned long long>(fovFieldOffset),
            static_cast<double>(gameFov));
    }
}
