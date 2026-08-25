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

    // The framing values, one Ctrl+Shift chord pair each (Q/A, W/S, E/D, R/F).
    enum class FramingKnob { FovOffset, OffsetForward, OffsetUp, OffsetRight, Count };

    // Toggles the hotkey thread writes and the hook reads on every frame, so
    // every member is atomic. Seeded from the INI at bootstrap
    // (ApplyConfigToSession) and from the active build profile (injectMode).
    struct RuntimeState
    {
        std::atomic<bool> trackingEnabled{true};
        // true = world-space yaw (horizon-locked, FRotator addition); false =
        // camera-local yaw (quaternion post-multiply, leans on pitched turns).
        std::atomic<bool> worldSpaceYaw{true};
        // From the build profile at bootstrap, and nothing moves it after
        // that. Atomic because the hook reads it beside the members the hotkey
        // thread does write.
        std::atomic<int>  injectMode{kInjectModeFirstCaller};

        // The camera framing, seeded from the INI at bootstrap and moved from
        // there by the tuning hotkeys. The hook reads these rather than the
        // config, so a value found by eye takes effect on the next frame, and
        // only the save hotkey puts one back in the file.
        std::atomic<float> fovOffset{0.0f};
        std::atomic<float> offsetForward{0.0f};
        std::atomic<float> offsetUp{0.0f};
        std::atomic<float> offsetRight{0.0f};

        // The lean the player is holding right now, already expressed in the
        // framing values that would reproduce it, so the capture hotkey adds
        // three numbers and needs nothing from the game thread. Written every
        // frame by the hook: zeroed as the frame starts and set again only if
        // that frame actually applies a lean, so a frame that tracks nothing -
        // a menu, a held gate, no tracker - publishes nothing to capture.
        std::atomic<float> liveLeanForward{0.0f};
        std::atomic<float> liveLeanUp{0.0f};
        std::atomic<float> liveLeanRight{0.0f};
    };

    inline RuntimeState& Runtime()
    {
        static RuntimeState state;
        return state;
    }
}
