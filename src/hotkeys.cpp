// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "hotkeys.h"

#include <cstddef>
#include <cstdio>

#include <cameraunlock/input/chord_hotkeys.h>

#include "exe_paths.h"
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
        constexpr int kVkX = 0x58;
        // Not C: the game binds it to the in-game camera change and does not
        // check modifiers, so Ctrl+Shift+C changes the view as well.
        constexpr int kVkV = 0x56;
        constexpr int kVkM = 0x4D;

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
            {"OffsetForward", "cm",  1.0f,  kMaxCameraOffset},
            {"OffsetUp",      "cm",  1.0f,  kMaxCameraOffset},
            {"OffsetRight",   "cm",  1.0f,  kMaxCameraOffset},
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

        float ClampToKnob(FramingKnob knob, float wanted)
        {
            const float limit = kKnobs[static_cast<std::size_t>(knob)].limit;
            return wanted < -limit ? -limit : wanted > limit ? limit : wanted;
        }

        void AdjustFraming(FramingKnob knob, int direction)
        {
            const KnobSpec& spec = kKnobs[static_cast<std::size_t>(knob)];
            std::atomic<float>& value = KnobValue(knob);

            const float clamped = ClampToKnob(knob, value.load()
                                             + static_cast<float>(direction) * spec.step);
            value.store(clamped);

            char message[96];
            std::snprintf(message, sizeof(message), "%s -> %g %s",
                spec.iniKey, static_cast<double>(clamped), spec.unit);
            LogFraming(message);
        }

        // Deliberately a key of its own rather than a write on every adjustment:
        // walking a value out to where it looks right is a hundred presses, and
        // none of them should touch the file. It also means a nudge landing on
        // the wrong key changes nothing permanent.
        void SaveFraming(Config& config)
        {
            const float fov     = Runtime().fovOffset.load();
            const float forward = Runtime().offsetForward.load();
            const float up      = Runtime().offsetUp.load();
            const float right   = Runtime().offsetRight.load();
            if (!SaveCameraFraming(ExeDirectoryNarrow(), fov, forward, up, right)) return;

            // What the INI says is now this, so the reset key has to agree -
            // otherwise Ctrl+Shift+Z undoes a save rather than an experiment.
            config.fov_offset = fov;
            config.camera_offset_forward = forward;
            config.camera_offset_up = up;
            config.camera_offset_right = right;
            LogFraming("saved to HeadTracking.ini");
        }

        void RestoreSavedFraming(const Config& config)
        {
            Runtime().fovOffset.store(config.fov_offset);
            Runtime().offsetForward.store(config.camera_offset_forward);
            Runtime().offsetUp.store(config.camera_offset_up);
            Runtime().offsetRight.store(config.camera_offset_right);
            LogFraming("back to the last saved values");
        }

        // Hand the lean the player is holding over to the framing offsets, so
        // that when they sit back up the view stays where they put it. The lean
        // arrives already converted into framing values, and it is ADDED rather
        // than assigned because the two stack on screen: a capture that
        // replaced the framing would throw away everything already dialled in
        // and move the camera the moment it was pressed.
        void CaptureFraming()
        {
            const float forward = Runtime().liveLeanForward.load();
            const float up      = Runtime().liveLeanUp.load();
            const float right   = Runtime().liveLeanRight.load();
            if (forward == 0.0f && up == 0.0f && right == 0.0f) {
                LogFraming("nothing to capture - no lean is being applied right now");
                return;
            }

            Runtime().offsetForward.store(ClampToKnob(FramingKnob::OffsetForward,
                Runtime().offsetForward.load() + forward));
            Runtime().offsetUp.store(ClampToKnob(FramingKnob::OffsetUp,
                Runtime().offsetUp.load() + up));
            Runtime().offsetRight.store(ClampToKnob(FramingKnob::OffsetRight,
                Runtime().offsetRight.load() + right));

            char message[128];
            std::snprintf(message, sizeof(message),
                "captured the lean you are holding (%.1f fwd, %.1f up, %.1f right cm)",
                static_cast<double>(forward), static_cast<double>(up),
                static_cast<double>(right));
            LogFraming(message);
        }

        // All four to zero is the camera exactly as the game placed it, which
        // makes this the other half of a comparison rather than a panic key:
        // clear to see the stock shot, restore to see the tuned one, with the
        // two keys side by side so the flip is one finger.
        void ClearFraming()
        {
            Runtime().fovOffset.store(0.0f);
            Runtime().offsetForward.store(0.0f);
            Runtime().offsetUp.store(0.0f);
            Runtime().offsetRight.store(0.0f);
            LogFraming("cleared to the camera the game placed");
        }
    }

    std::unique_ptr<cameraunlock::input::HotkeyPoller> StartHotkeys(Session& session,
                                                                    Config& config)
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
        // dolly, E/D height, R/F sideways. Under them: Z back to the game's own
        // camera, because Ctrl+Z is undo everywhere else and the thing a player
        // reaches for it to undo is the whole experiment. X back to the last
        // saved set, V to keep the lean being held. Save is the only one that
        // writes a file, so it sits away from all of them, at the other end of
        // the same row.
        poller->AddHotkey(kVkQ, ChordGuarded([] { AdjustFraming(FramingKnob::FovOffset, +1); }));
        poller->AddHotkey(kVkA, ChordGuarded([] { AdjustFraming(FramingKnob::FovOffset, -1); }));
        poller->AddHotkey(kVkW, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetForward, +1); }));
        poller->AddHotkey(kVkS, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetForward, -1); }));
        poller->AddHotkey(kVkE, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetUp, +1); }));
        poller->AddHotkey(kVkD, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetUp, -1); }));
        poller->AddHotkey(kVkR, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetRight, +1); }));
        poller->AddHotkey(kVkF, ChordGuarded([] { AdjustFraming(FramingKnob::OffsetRight, -1); }));
        poller->AddHotkey(kVkZ, ChordGuarded([] { ClearFraming(); }));
        poller->AddHotkey(kVkX, ChordGuarded([&config] { RestoreSavedFraming(config); }));
        poller->AddHotkey(kVkV, ChordGuarded([] { CaptureFraming(); }));
        poller->AddHotkey(kVkM, ChordGuarded([&config] { SaveFraming(config); }));

        poller->Start(kPollIntervalMs);
        return poller;
    }
}
