// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/protocol/udp_receiver.h>

#include "game_state.h"
#include "runtime_state.h"
#include "view_injection.h"

// Everything the GetPlayerViewPoint hook writes to the log, and the small
// amount of state that only exists so those lines can be written. Split out of
// the hook itself so the detour reads as the decisions it makes rather than as
// the diagnostics around them.
//
// Every writer here is bounded - by wall clock for the recurring lines, by a
// count for the ones that would otherwise grow with session length, by a
// once-flag for the ones that answer a question a single time. Nothing here is
// written per frame. The receiver and session are read lazily, AFTER the
// throttle, so a rejected call costs a clock read and nothing else on the hot
// path.

namespace pinballfx_ht::diagnostics
{
    // The first few entries into the detour, written through the crash-safe
    // path: if the hook is going to fault it does so here, before the ordinary
    // buffered log has been flushed. Both cap themselves on the entry number
    // they are handed, so the hook carries none of the limits.
    void LogHookEntry(std::uint64_t entryNo, bool reentered, std::uintptr_t retRva);
    void LogHookReentry(std::uint64_t entryNo, std::uintptr_t retRva);

    // Last plausible FOV seen on the render path, so the heartbeat can report
    // the live angle no matter which caller happens to trigger it.
    void RecordGameFov(float gameFov);

    // Publishes the gate's verdict for the heartbeat, and writes a line the
    // first time the gate is evaluated and whenever it changes after - so the
    // log shows what the game was doing when tracking stopped, rather than one
    // line per frame. Capped per session, because a table's scripted camera
    // moves make the gate change all evening; the heartbeat reports the live
    // verdict once the cap is reached.
    void ReportGateVerdict(const GameplayState& state, bool tracking);

    // Distinguishes "hook never fired" from "no tracker data". Written on the
    // first call and every kHeartbeatIntervalMs after.
    void LogHeartbeat(std::uint64_t call, std::uintptr_t retRva, int mode,
                      const cameraunlock::UdpReceiver& receiver);

    // Time-based sampling, not every-Nth-call: the hook runs for several
    // distinct callers at different rates, so a call-count stride samples them
    // unevenly and makes the log impossible to line up against wall-clock
    // events (screenshots, tracker steps). Dense for an opening burst, then at
    // the heartbeat's cadence for the rest of the session.
    void LogPoseSample(std::uint64_t call, std::uintptr_t retRva,
                       const FRotator4f& cleanRotation, const FVector4f& cleanLocation,
                       const HeadPose& pose, const FRotator4f& trackedRotation,
                       const ue::FVector& positionOffset, const FovSample& fov,
                       const cameraunlock::UdpReceiver& receiver, const Session& session);

    // One line, the first time a plausible FOV is seen, because it answers the
    // whole question at once: what the table's view asks for, and what the mod
    // is rendering it at.
    void LogFovApplied(float gameFov, float renderFov, float fovOverride, float fovOffset);

    // Not a camera angle means the pinned offset does not describe this build,
    // so the mod stops touching FOV rather than writing through a pointer whose
    // target it has just proved it cannot identify. Rotation and position are
    // unaffected - they come through the hook's own parameters, not this struct.
    // The rest of the FMinimalViewInfo the render caller hands over, read once
    // per distinct FOV so a session covers every view it visited. It answers
    // the question the FOV alone cannot: whether a view that looks flattened is
    // a long lens (perspective projection, narrow angle - something the FOV and
    // dolly knobs can answer) or an aspect ratio the game is constraining to
    // something the window is not, which is a real geometric squash and a
    // different fix entirely. Read-only, and past FOV the offsets are the
    // stock UE 4.27 field order rather than anything derived from this build,
    // so the line is a lead to confirm and not a fact.
    void LogViewInfoFields(const void* viewInfo, float gameFov);

    void LogImplausibleFov(std::size_t fovFieldOffset, float gameFov);
}
