// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkeys.h"

#include <cameraunlock/input/chord_hotkeys.h>

#include "builds/build_registry.h"
#include "logging.h"
#include "view_injection.h"

namespace pinballfx_ht
{
    namespace
    {
        using cameraunlock::TrackingMode;
        using cameraunlock::input::ChordGuarded;
        using cameraunlock::input::NavGuarded;

        constexpr int kVkEnd    = 0x23;
        constexpr int kVkPageUp = 0x21;
        constexpr int kVkY = 0x59;
        constexpr int kVkG = 0x47;
        constexpr int kVkH = 0x48;
        constexpr int kVkU = 0x55;
        constexpr int kVkJ = 0x4A;

        constexpr int kPollIntervalMs = 16;

        void ToggleTracking()
        {
            const bool enabled = !Runtime().trackingEnabled.load();
            Runtime().trackingEnabled.store(enabled);
            Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
        }

        void CycleTrackingMode(Session& session)
        {
            const char* name = "normal (rotation + position)";
            switch (session.CycleMode()) {
                case TrackingMode::RotationOnly: name = "rotation only (position off)"; break;
                case TrackingMode::PositionOnly: name = "position only (rotation off)"; break;
                case TrackingMode::RotationAndPosition: break;
            }
            Log::Line("hotkey: tracking mode -> %s", name);
        }

        void ToggleYawMode()
        {
            const bool worldSpace = !Runtime().worldSpaceYaw.load();
            Runtime().worldSpaceYaw.store(worldSpace);
            Log::Line("hotkey: yaw mode %s", worldSpace ? "world" : "local");
        }

        void CycleInject(int direction)
        {
            const int mode = CycleInjectMode(Runtime().injectMode.load(), direction);
            Runtime().injectMode.store(mode);
            Log::Line("hotkey: inject mode -> %d (caller RVA 0x%08llx)", mode,
                static_cast<unsigned long long>(
                    CallerRvaForMode(mode, Offsets().kKnownCallerRvas)));
        }
    }

    std::unique_ptr<cameraunlock::input::HotkeyPoller> StartHotkeys(Session& session,
                                                                    int yawModeKey)
    {
        auto poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

        // Nav-cluster (AGENTS.md default bindings). Suppressed while Ctrl+Shift is
        // held so the chord path is the sole trigger for a Ctrl+Shift+<nav> press.
        poller->AddHotkey(kVkEnd,    NavGuarded([] { ToggleTracking(); }));
        poller->AddHotkey(kVkPageUp, NavGuarded([&session] { CycleTrackingMode(session); }));
        poller->AddHotkey(yawModeKey, NavGuarded([] { ToggleYawMode(); }));

        // Ctrl+Shift chord alternatives (Y/G/H cluster).
        poller->AddHotkey(kVkY, ChordGuarded([] { ToggleTracking(); }));
        poller->AddHotkey(kVkG, ChordGuarded([&session] { CycleTrackingMode(session); }));
        poller->AddHotkey(kVkH, ChordGuarded([] { ToggleYawMode(); }));

        // Dev: re-confirm the render caller in-game (cycle which GPV caller is
        // injected) without a rebuild. Ctrl+Shift+U next / Ctrl+Shift+J prev.
        poller->AddHotkey(kVkU, ChordGuarded([] { CycleInject(+1); }));
        poller->AddHotkey(kVkJ, ChordGuarded([] { CycleInject(-1); }));

        poller->Start(kPollIntervalMs);
        return poller;
    }
}
