// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <atomic>

#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/tracking/head_tracking_session.h>

#include "view_injection.h"

namespace pinballfx_ht
{
    using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;

    // The session picks between LocalSmoothing and RemoteSmoothing from the
    // receiver's source-address check. That wiring is compile-time detected, so
    // a receiver without IsRemoteConnection() would silently pin every session
    // to the local value instead of failing to build.
    static_assert(Session::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() for per-connection smoothing");

    // Toggles the hotkey thread writes and the hook reads on every frame, so
    // every member is atomic. Seeded from the INI at bootstrap
    // (ApplyConfigToSession) and from the active build profile (injectMode).
    struct RuntimeState
    {
        std::atomic<bool> trackingEnabled{true};
        // true = world-space yaw (horizon-locked, FRotator addition); false =
        // camera-local yaw (quaternion post-multiply, leans on pitched turns).
        std::atomic<bool> worldSpaceYaw{true};
        std::atomic<int>  injectMode{kInjectModeFirstCaller};
    };

    inline RuntimeState& Runtime()
    {
        static RuntimeState state;
        return state;
    }
}
