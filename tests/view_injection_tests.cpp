// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Characterization tests for the decisions and maths the GetPlayerViewPoint
// hook makes. These lock behaviour that is otherwise only observable by playing
// the game with a tracker attached: which caller gets the head pose, how the
// two yaw modes compose, and which way a 6DOF lean moves the camera in world
// space.

#include "view_injection.h"

#include <cmath>
#include <initializer_list>

#include "test_support.h"

namespace {

using namespace pinballfx_ht;
using pinballfx_tests::Check;
using pinballfx_tests::NearEqual;

CallerRvaTable MakeCallers(std::initializer_list<std::uintptr_t> rvas)
{
    CallerRvaTable table{};
    std::size_t i = 0;
    for (auto rva : rvas) table[i++] = rva;
    return table;
}

void InjectGateTests(int& failures)
{
    const CallerRvaTable callers = MakeCallers({0x01111111, 0x02222222, 0x03333333});

    Check(failures, ShouldInjectForCaller(0xdead, kInjectModeAllCallers, callers),
          "mode 0 injects for every caller (diagnostic)");
    Check(failures, ShouldInjectForCaller(0x01111111, 1, callers),
          "mode 1 injects for the render-path caller");
    Check(failures, !ShouldInjectForCaller(0x02222222, 1, callers),
          "mode 1 rejects every other caller, keeping the head pose out of game queries");
    Check(failures, ShouldInjectForCaller(0x03333333, 3, callers),
          "mode N selects the Nth pinned caller");

    // An empty slot must never match, or a profile that pins fewer callers than
    // the table holds would inject on a return address of 0.
    Check(failures, !ShouldInjectForCaller(0, 5, callers),
          "an unpinned (zero) slot never matches");
    Check(failures, !ShouldInjectForCaller(0x01111111, kInjectModeLastCaller + 1, callers),
          "a mode past the end injects for nothing");
    Check(failures, !ShouldInjectForCaller(0x01111111, -1, callers),
          "a negative mode injects for nothing");

    Check(failures, CallerRvaForMode(1, callers) == 0x01111111,
          "CallerRvaForMode reports the pinned RVA");
    Check(failures, CallerRvaForMode(kInjectModeAllCallers, callers) == 0
                 && CallerRvaForMode(kInjectModeLastCaller + 1, callers) == 0,
          "CallerRvaForMode reports 0 for the modes that pin no caller");
}

void RotationCompositionTests(int& failures)
{
    const FRotator4f clean{10.0f, 20.0f, 5.0f};

    // World-space yaw is plain FRotator addition, and roll is SUBTRACTED - the
    // engine's roll runs opposite the tracker's.
    const FRotator4f world = ComposeTrackedRotation(clean, 3.0f, 4.0f, 2.0f, true);
    Check(failures, NearEqual(world.Yaw, 23.0f) && NearEqual(world.Pitch, 14.0f)
                 && NearEqual(world.Roll, 3.0f),
          "world yaw adds yaw/pitch and subtracts roll");

    // A zero head pose must leave the game's own rotation untouched in both
    // modes, or enabling tracking would nudge the view before the user moves.
    const FRotator4f worldIdle = ComposeTrackedRotation(clean, 0, 0, 0, true);
    const FRotator4f localIdle = ComposeTrackedRotation(clean, 0, 0, 0, false);
    Check(failures, NearEqual(worldIdle.Yaw, clean.Yaw) && NearEqual(worldIdle.Pitch, clean.Pitch)
                 && NearEqual(worldIdle.Roll, clean.Roll),
          "world yaw with no head pose is the identity");
    Check(failures, NearEqual(localIdle.Yaw, clean.Yaw, 1e-3) && NearEqual(localIdle.Pitch, clean.Pitch, 1e-3)
                 && NearEqual(localIdle.Roll, clean.Roll, 1e-3),
          "local yaw with no head pose round-trips through the quaternion unchanged");

    // From a level view the two modes agree; the point of the local mode is
    // that it leans once the game camera is pitched. A pinball camera looks
    // DOWN at the table, so this is the normal case here rather than a corner
    // one - it is why world yaw is the default.
    const FRotator4f level{0.0f, 0.0f, 0.0f};
    const FRotator4f levelLocal = ComposeTrackedRotation(level, 30.0f, 0, 0, false);
    Check(failures, NearEqual(levelLocal.Yaw, 30.0f, 1e-3) && NearEqual(levelLocal.Pitch, 0.0f, 1e-3)
                 && NearEqual(levelLocal.Roll, 0.0f, 1e-3),
          "local yaw from a level view is a plain yaw");

    const FRotator4f pitched{-45.0f, 0.0f, 0.0f};
    const FRotator4f pitchedLocal = ComposeTrackedRotation(pitched, 30.0f, 0, 0, false);
    const FRotator4f pitchedWorld = ComposeTrackedRotation(pitched, 30.0f, 0, 0, true);
    Check(failures, NearEqual(pitchedWorld.Roll, 0.0f) && NearEqual(pitchedWorld.Yaw, 30.0f),
          "world yaw stays horizon-locked on a camera angled down at the table");
    Check(failures, std::fabs(pitchedLocal.Roll) > 1.0f,
          "local yaw leans on a pitched camera (the reason world yaw is the default)");

    // Cabinet mode rolls the view 90 degrees for a rotated monitor. Local yaw
    // composes around that roll rather than through it: yawing the head must
    // still yaw the view, not pitch it, and the display roll must come out the
    // far side intact or the table renders sideways.
    const FRotator4f cabinet{-27.0f, -93.2f, 90.0f};
    const FRotator4f cabinetIdle = ComposeTrackedRotation(cabinet, 0, 0, 0, false);
    Check(failures, NearEqual(cabinetIdle.Roll, 90.0f, 1e-3)
                 && NearEqual(cabinetIdle.Pitch, -27.0f, 1e-3)
                 && NearEqual(cabinetIdle.Yaw, -93.2f, 1e-3),
          "local yaw preserves a 90-degree display roll when the head is still");

    // 30 degrees of head yaw comes out as 33 of view yaw and 4 of pitch (the
    // lean local yaw always has on a pitched camera). Composing THROUGH the
    // roll instead sent the whole 30 into pitch and left the yaw where it was.
    const FRotator4f cabinetYawed = ComposeTrackedRotation(cabinet, 30.0f, 0, 0, false);
    Check(failures, cabinetYawed.Yaw - cabinet.Yaw > 25.0f
                 && std::fabs(cabinetYawed.Pitch - cabinet.Pitch) < 10.0f,
          "local yaw under a display roll yaws the view rather than pitching it");
}

void PositionOffsetTests(int& failures)
{
    // Level view: UE forward is +X, right +Y, up +Z.
    const FRotator4f level{0.0f, 0.0f, 0.0f};

    // The processor's forward lean is NEGATIVE z, and the offset is metres
    // while UE works in centimetres. Leaning in must move the camera FORWARD
    // by the full amount - a mirrored sign here is the "leaning in barely
    // moves" bug AGENTS.md calls out, and on a table it is the difference
    // between seeing past a ramp and not.
    const ue::FVector lean = PositionOffsetUE(level, 0.0f, 0.0f, -0.40f);
    Check(failures, NearEqual(lean.X, 40.0) && NearEqual(lean.Y, 0.0) && NearEqual(lean.Z, 0.0),
          "a full forward lean moves 40cm along the flattened camera-forward");

    const ue::FVector back = PositionOffsetUE(level, 0.0f, 0.0f, 0.10f);
    Check(failures, NearEqual(back.X, -10.0), "a backward lean moves 10cm backwards");

    const ue::FVector sway = PositionOffsetUE(level, 0.30f, 0.0f, 0.0f);
    Check(failures, NearEqual(sway.Y, -30.0) && NearEqual(sway.X, 0.0),
          "sway runs opposite UE camera-right");

    const ue::FVector heave = PositionOffsetUE(level, 0.0f, 0.20f, 0.0f);
    Check(failures, NearEqual(heave.Z, 20.0) && NearEqual(heave.X, 0.0),
          "heave runs along world up");

    // The basis follows where the camera FACES: yawed 90 degrees, forward is
    // world +Y.
    const ue::FVector leanYawed = PositionOffsetUE(FRotator4f{0.0f, 90.0f, 0.0f},
                                                   0.0f, 0.0f, -0.40f);
    Check(failures, NearEqual(leanYawed.X, 0.0, 1e-3) && NearEqual(leanYawed.Y, 40.0, 1e-3),
          "the offset follows the camera's yaw, not world axes");

    // ...and nothing else about the camera. Pitch first: every table view looks
    // down at the playfield, some of them by nearly 60 degrees. A basis that
    // inherited that pitch would turn a lean forward into a dive at the glass,
    // which is not what the head did.
    const ue::FVector leanPitched = PositionOffsetUE(FRotator4f{-58.7f, 0.0f, 0.0f},
                                                     0.0f, 0.0f, -0.40f);
    Check(failures, NearEqual(leanPitched.X, 40.0, 1e-3) && NearEqual(leanPitched.Z, 0.0, 1e-3),
          "a forward lean stays horizontal under a nose-down camera");

    // Roll second, and this is the cabinet-mode case: the game rolls the view
    // 90 degrees so the table stands upright on a rotated monitor. Inheriting
    // that roll swapped sway with heave - a lean to the right lifted the camera
    // 27cm and moved it 14cm sideways, on the game's own cabinet view.
    const FRotator4f cabinet{-27.0f, -93.2f, 90.0f};
    const ue::FVector swayCabinet = PositionOffsetUE(cabinet, 0.30f, 0.0f, 0.0f);
    Check(failures, NearEqual(swayCabinet.Z, 0.0, 1e-3)
                 && NearEqual(std::sqrt(swayCabinet.X * swayCabinet.X
                                      + swayCabinet.Y * swayCabinet.Y), 30.0, 1e-3),
          "sway stays horizontal under a 90-degree display roll");

    const ue::FVector heaveCabinet = PositionOffsetUE(cabinet, 0.0f, 0.20f, 0.0f);
    Check(failures, NearEqual(heaveCabinet.Z, 20.0, 1e-3)
                 && NearEqual(heaveCabinet.X, 0.0, 1e-3)
                 && NearEqual(heaveCabinet.Y, 0.0, 1e-3),
          "heave stays vertical under a 90-degree display roll");

    // Same head movement, same world offset, whatever the display roll is.
    const ue::FVector rolled = PositionOffsetUE(FRotator4f{-27.0f, -93.2f, 0.0f},
                                                0.12f, -0.05f, -0.21f);
    const ue::FVector unrolled = PositionOffsetUE(cabinet, 0.12f, -0.05f, -0.21f);
    Check(failures, NearEqual(rolled.X, unrolled.X, 1e-6)
                 && NearEqual(rolled.Y, unrolled.Y, 1e-6)
                 && NearEqual(rolled.Z, unrolled.Z, 1e-6),
          "the position basis is roll-independent");
}

void CameraFramingOffsetTests(int& failures)
{
    const FRotator4f level{0.0f, 0.0f, 0.0f};

    Check(failures, NearEqual(CameraFramingOffsetUE(level, 0.0f, 0.0f, 0.0f).X, 0.0)
                 && NearEqual(CameraFramingOffsetUE(level, 0.0f, 0.0f, 0.0f).Z, 0.0),
          "an unconfigured framing offset moves the camera nowhere");

    // Forward runs along the line of sight - that is what makes a dolly hold
    // the aim point while the lens widens under it.
    const ue::FVector dollyBack = CameraFramingOffsetUE(FRotator4f{-30.0f, 0.0f, 0.0f},
                                                        -100.0f, 0.0f, 0.0f);
    Check(failures, NearEqual(dollyBack.X, -86.603, 1e-3) && NearEqual(dollyBack.Z, 50.0, 1e-3),
          "a negative forward offset backs the camera off along the line of sight");

    const ue::FVector up = CameraFramingOffsetUE(level, 0.0f, 25.0f, 0.0f);
    Check(failures, NearEqual(up.Z, 25.0) && NearEqual(up.X, 0.0),
          "the up offset raises the camera");

    const ue::FVector right = CameraFramingOffsetUE(level, 0.0f, 0.0f, 25.0f);
    Check(failures, NearEqual(right.Y, 25.0) && NearEqual(right.X, 0.0),
          "the right offset slides the camera across the view");

    // Cabinet mode again: the framing knobs are how a player answers the stock
    // portrait camera, so up must mean up to them and not to the rotated
    // framebuffer.
    const ue::FVector upCabinet = CameraFramingOffsetUE(FRotator4f{-27.0f, -93.2f, 90.0f},
                                                        0.0f, 25.0f, 0.0f);
    const ue::FVector upUnrolled = CameraFramingOffsetUE(FRotator4f{-27.0f, -93.2f, 0.0f},
                                                         0.0f, 25.0f, 0.0f);
    Check(failures, NearEqual(upCabinet.X, upUnrolled.X, 1e-6)
                 && NearEqual(upCabinet.Y, upUnrolled.Y, 1e-6)
                 && NearEqual(upCabinet.Z, upUnrolled.Z, 1e-6),
          "the framing basis is roll-independent too");
}

// Capturing a lean hands a world-space offset built in one basis over to
// framing values read in another, and the whole point of the key is that the
// view does not move when it is pressed. That only holds if the conversion is
// exact, so these check the round trip rather than any particular number.
void CaptureFramingTests(int& failures)
{
    const FRotator4f views[] = {
        FRotator4f{0.0f, 0.0f, 0.0f},        // level
        FRotator4f{-27.0f, -93.2f, 0.0f},    // a desktop table view
        FRotator4f{-59.0f, 143.0f, 90.0f},   // cabinet, on its side
    };

    for (const FRotator4f& view : views) {
        // A lean in every axis at once: forward 12cm, up 4cm, right 7cm as the
        // processor reports it (metres, its own sign convention).
        const ue::FVector lean = PositionOffsetUE(view, -0.07f, 0.04f, -0.12f);
        const FramingComponents captured = FramingComponentsOf(view, lean);
        const ue::FVector rebuilt = CameraFramingOffsetUE(view, captured.forwardCm,
                                                          captured.upCm, captured.rightCm);
        Check(failures, NearEqual(rebuilt.X, lean.X, 1e-3)
                     && NearEqual(rebuilt.Y, lean.Y, 1e-3)
                     && NearEqual(rebuilt.Z, lean.Z, 1e-3),
              "a captured lean reproduces the same world offset, so the view does not "
              "move when it is captured");
    }

    // The lean is horizon-locked and the framing basis is pitched, so on a
    // nose-down view a purely horizontal lean has to come back as a mix of
    // forward and up. Equal numbers would mean the conversion had been skipped.
    const FRotator4f pitched{-45.0f, 0.0f, 0.0f};
    const ue::FVector flat = PositionOffsetUE(pitched, 0.0f, 0.0f, -0.20f);
    const FramingComponents mixed = FramingComponentsOf(pitched, flat);
    Check(failures, NearEqual(flat.Z, 0.0, 1e-9),
          "a surge lean stays horizontal whatever the view is pitched to");
    Check(failures, mixed.forwardCm > 1.0f && mixed.upCm > 1.0f,
          "a horizontal lean captured on a nose-down view splits into forward and up");

    Check(failures, FramingComponentsOf(views[1], ue::FVector{0.0, 0.0, 0.0}).forwardCm == 0.0f,
          "no lean captures nothing");
}

void FieldOfViewTests(int& failures)
{
    // The default INI configures neither, and that has to be indistinguishable
    // from the mod not being installed: the game's angle comes back byte for
    // byte, including one outside the bounds a configured FOV is held to.
    Check(failures, EffectiveFov(63.5f, 0.0f, 0.0f) == 63.5f,
          "configuring neither leaves the game's FOV exactly as it is");
    Check(failures, EffectiveFov(15.0f, 0.0f, 0.0f) == 15.0f,
          "an unconfigured FOV is not dragged into the configured range");

    Check(failures, NearEqual(EffectiveFov(60.0f, 90.0f, 0.0f), 90.0f),
          "an override replaces the game's angle");
    Check(failures, NearEqual(EffectiveFov(60.0f, 0.0f, 12.0f), 72.0f),
          "an offset adds to whatever the current view asks for");
    Check(failures, NearEqual(EffectiveFov(60.0f, 90.0f, 12.0f), 90.0f),
          "the override wins when both are set");
    Check(failures, NearEqual(EffectiveFov(60.0f, 0.0f, -20.0f), 40.0f),
          "a negative offset narrows the view");

    // Both bounds are typo stops. Without them a big offset reaches the
    // projection matrix, where a degenerate angle is a black or inside-out
    // frame the player cannot fix without finding the INI again.
    Check(failures, NearEqual(EffectiveFov(60.0f, 0.0f, 150.0f), kMaxConfiguredFov),
          "an offset past the top bound clamps");
    Check(failures, NearEqual(EffectiveFov(60.0f, 0.0f, -150.0f), kMinConfiguredFov),
          "an offset past the bottom bound clamps");

    // Reaching the FOV field at all is a per-caller decision: only one call
    // site passes an FMinimalViewInfo, and reading the offset off any other
    // caller's OutLocation lands on an unrelated stack local. A profile that
    // pins neither half must leave FOV alone rather than write through a
    // pointer it cannot identify.
    constexpr std::uintptr_t kViewInfoCaller = 0x032cc95a;
    constexpr std::size_t kFovOffset = 0x18;
    Check(failures, CallerCarriesViewInfo(kViewInfoCaller, kViewInfoCaller, kFovOffset),
          "the view-info caller carries an FMinimalViewInfo");
    Check(failures, !CallerCarriesViewInfo(0x031e31d6, kViewInfoCaller, kFovOffset),
          "another GetPlayerViewPoint caller does not, so FOV is left alone there");
    Check(failures, !CallerCarriesViewInfo(0, 0, kFovOffset),
          "a profile that pins no view-info caller never reads the FOV field");
    Check(failures, !CallerCarriesViewInfo(kViewInfoCaller, kViewInfoCaller, 0),
          "a profile with no derived FOV offset never reads the FOV field");

    // The plausibility gate decides whether the mod writes into the view info
    // at all, so it has to reject everything that is not an angle - a wrong
    // offset on a future build reads as a coordinate, a flag or a NaN.
    Check(failures, FovLooksSane(60.0f) && FovLooksSane(120.0f),
          "ordinary camera angles pass the plausibility gate");
    Check(failures, !FovLooksSane(0.0f) && !FovLooksSane(-90.0f) && !FovLooksSane(4000.0f),
          "zero, negative and out-of-scale values are rejected");
    Check(failures, !FovLooksSane(std::nanf("")) && !FovLooksSane(INFINITY)
                 && !FovLooksSane(-INFINITY),
          "NaN and infinity are rejected rather than compared into range");
}

}  // namespace

int RunViewInjectionTests()
{
    int failures = 0;
    std::cout << "View injection tests\n";
    InjectGateTests(failures);
    RotationCompositionTests(failures);
    PositionOffsetTests(failures);
    CameraFramingOffsetTests(failures);
    CaptureFramingTests(failures);
    FieldOfViewTests(failures);
    return pinballfx_tests::Report("View injection tests", failures);
}
