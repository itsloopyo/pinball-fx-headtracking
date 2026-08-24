// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkeys.h"

#include <cstddef>
#include <cstdio>

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

        // Framing: the Q/W/E/R row raises, the A/S/D/F row under it lowers, one
        // column per value. Nothing to select first and nothing to remember -
        // the key you are holding says which value it moves.
        constexpr int kVkQ = 0x51;
        constexpr int kVkW = 0x57;
        constexpr int kVkE = 0x45;
        constexpr int kVkR = 0x52;
        constexpr int kVkA = 0x41;
        constexpr int kVkS = 0x53;
        constexpr int kVkD = 0x44;
        constexpr int kVkF = 0x46;
        constexpr int kVkZ = 0x5A;

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

        // Everything needed to move one framing value and to print it back in
        // the form it has to be typed into the INI.
        struct KnobSpec
        {
            const char* iniKey;
            const char* unit;
            float step;
            float limit;
        };

        constexpr KnobSpec kKnobs[] = {
            {"FovOffset",     "deg", 1.0f,  kMaxConfiguredFov},
            {"OffsetForward", "cm",  10.0f, kMaxCameraOffset},
            {"OffsetUp",      "cm",  10.0f, kMaxCameraOffset},
            {"OffsetRight",   "cm",  10.0f, kMaxCameraOffset},
        };
        static_assert(sizeof(kKnobs) / sizeof(kKnobs[0])
                          == static_cast<std::size_t>(FramingKnob::Count),
                      "every framing knob needs a spec");

        std::atomic<float>& KnobValue(FramingKnob knob)
        {
            std::atomic<float>* const values[] = {
                &Runtime().fovOffset,
                &Runtime().offsetForward,
                &Runtime().offsetUp,
                &Runtime().offsetRight,
            };
            return *values[static_cast<std::size_t>(knob)];
        }

        // One line per change, carrying the whole set rather than the value
        // that moved: whichever line the player copies out of the log is the
        // complete answer, with no need to reconstruct it from the ones above.
        void LogFraming(const char* what)
        {
            Log::Line("framing: %s | [Camera] FovOffset=%g OffsetForward=%g OffsetUp=%g "
                      "OffsetRight=%g", what,
                static_cast<double>(Runtime().fovOffset.load()),
                static_cast<double>(Runtime().offsetForward.load()),
                static_cast<double>(Runtime().offsetUp.load()),
                static_cast<double>(Runtime().offsetRight.load()));
        }

        void AdjustFraming(FramingKnob knob, int direction)
        {
            const KnobSpec& spec = kKnobs[static_cast<std::size_t>(knob)];
            std::atomic<float>& value = KnobValue(knob);

            const float wanted = value.load() + static_cast<float>(direction) * spec.step;
            const float clamped = wanted < -spec.limit ? -spec.limit
                                : wanted >  spec.limit ?  spec.limit : wanted;
            value.store(clamped);

            char message[96];
            std::snprintf(message, sizeof(message), "%s -> %g %s",
                spec.iniKey, static_cast<double>(clamped), spec.unit);
            LogFraming(message);
        }

        void ResetFraming(const Config& config)
        {
            Runtime().fovOffset.store(config.fov_offset);
            Runtime().offsetForward.store(config.camera_offset_forward);
            Runtime().offsetUp.store(config.camera_offset_up);
            Runtime().offsetRight.store(config.camera_offset_right);
            LogFraming("reset to the values in the INI");
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
                                                                    const Config& config)
    {
        auto poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

        // Nav-cluster (AGENTS.md default bindings). Suppressed while Ctrl+Shift is
        // held so the chord path is the sole trigger for a Ctrl+Shift+<nav> press.
        poller->AddHotkey(kVkEnd,    NavGuarded([] { ToggleTracking(); }));
        poller->AddHotkey(kVkPageUp, NavGuarded([&session] { CycleTrackingMode(session); }));
        poller->AddHotkey(config.yaw_mode_key, NavGuarded([] { ToggleYawMode(); }));

        // Ctrl+Shift chord alternatives (Y/G/H cluster).
        poller->AddHotkey(kVkY, ChordGuarded([] { ToggleTracking(); }));
        poller->AddHotkey(kVkG, ChordGuarded([&session] { CycleTrackingMode(session); }));
        poller->AddHotkey(kVkH, ChordGuarded([] { ToggleYawMode(); }));

        // Camera framing, tuned in game and read back out of the log. Two rows
        // under the left hand, one column per value: Q/A field of view, W/S
        // dolly, E/D height, R/F sideways, and Z to put them all back.
        poller->AddHotkey(kVkQ, ChordGuarded([] { AdjustFraming(FramingKnob::FovOffset, +1); }));
        poller->AddHotkey(kVkA, ChordGuarded([] { AdjustFraming(FramingKnob::FovOffset, -1); }));
        poller->AddHotkey(kVkW, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetForward, +1); }));
        poller->AddHotkey(kVkS, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetForward, -1); }));
        poller->AddHotkey(kVkE, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetUp, +1); }));
        poller->AddHotkey(kVkD, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetUp, -1); }));
        poller->AddHotkey(kVkR, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetRight, +1); }));
        poller->AddHotkey(kVkF, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetRight, -1); }));
        poller->AddHotkey(kVkZ, ChordGuarded([&config] { ResetFraming(config); }));

        // Dev: re-confirm the render caller in-game (cycle which GPV caller is
        // injected) without a rebuild. Ctrl+Shift+U next / Ctrl+Shift+J prev.
        poller->AddHotkey(kVkU, ChordGuarded([] { CycleInject(+1); }));
        poller->AddHotkey(kVkJ, ChordGuarded([] { CycleInject(-1); }));

        poller->Start(kPollIntervalMs);
        return poller;
    }
}
