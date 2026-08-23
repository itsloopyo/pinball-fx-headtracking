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
    Check(failures, !ShouldInjectForCaller(0x01111111, kInjectModeNone, callers),
          "the none-mode injects for nothing");

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
    Check(failures, !ShouldInjectForCaller(0x01111111, kInjectModeCount, callers),
          "a mode past the end injects for nothing");
    Check(failures, !ShouldInjectForCaller(0x01111111, -1, callers),
          "a negative mode injects for nothing");

    Check(failures, CallerRvaForMode(1, callers) == 0x01111111,
          "CallerRvaForMode reports the pinned RVA");
    Check(failures, CallerRvaForMode(kInjectModeAllCallers, callers) == 0
                 && CallerRvaForMode(kInjectModeNone, callers) == 0,
          "CallerRvaForMode reports 0 for the modes that pin no caller");

    Check(failures, CycleInjectMode(1, +1) == 2, "cycling forward steps one mode");
    Check(failures, CycleInjectMode(kInjectModeNone, +1) == kInjectModeAllCallers,
          "cycling forward past the last mode wraps to 0");
    Check(failures, CycleInjectMode(kInjectModeAllCallers, -1) == kInjectModeNone,
          "cycling back from 0 wraps to the last mode");
    Check(failures, kInjectModeNone == static_cast<int>(kMaxKnownCallers) + 1,
          "the none-mode sits one past the last caller slot");
}

void RotationCompositionTests(int& failures)
{
    const FRotator4f clean{10.0f, 20.0f, 5.0f};
    const ue::FQuat4d baseQuat = ViewQuat(clean);

    // World-space yaw is plain FRotator addition, and roll is SUBTRACTED - the
    // engine's roll runs opposite the tracker's.
    const FRotator4f world = ComposeTrackedRotation(clean, baseQuat, 3.0f, 4.0f, 2.0f, true);
    Check(failures, NearEqual(world.Yaw, 23.0f) && NearEqual(world.Pitch, 14.0f)
                 && NearEqual(world.Roll, 3.0f),
          "world yaw adds yaw/pitch and subtracts roll");

    // A zero head pose must leave the game's own rotation untouched in both
    // modes, or enabling tracking would nudge the view before the user moves.
    const FRotator4f worldIdle = ComposeTrackedRotation(clean, baseQuat, 0, 0, 0, true);
    const FRotator4f localIdle = ComposeTrackedRotation(clean, baseQuat, 0, 0, 0, false);
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
    const ue::FQuat4d levelQuat = ViewQuat(level);
    const FRotator4f levelLocal = ComposeTrackedRotation(level, levelQuat, 30.0f, 0, 0, false);
    Check(failures, NearEqual(levelLocal.Yaw, 30.0f, 1e-3) && NearEqual(levelLocal.Pitch, 0.0f, 1e-3)
                 && NearEqual(levelLocal.Roll, 0.0f, 1e-3),
          "local yaw from a level view is a plain yaw");

    const FRotator4f pitched{-45.0f, 0.0f, 0.0f};
    const ue::FQuat4d pitchedQuat = ViewQuat(pitched);
    const FRotator4f pitchedLocal = ComposeTrackedRotation(pitched, pitchedQuat, 30.0f, 0, 0, false);
    const FRotator4f pitchedWorld = ComposeTrackedRotation(pitched, pitchedQuat, 30.0f, 0, 0, true);
    Check(failures, NearEqual(pitchedWorld.Roll, 0.0f) && NearEqual(pitchedWorld.Yaw, 30.0f),
          "world yaw stays horizon-locked on a camera angled down at the table");
    Check(failures, std::fabs(pitchedLocal.Roll) > 1.0f,
          "local yaw leans on a pitched camera (the reason world yaw is the default)");
}

void PositionOffsetTests(int& failures)
{
    // Identity view: UE camera-forward is +X, right +Y, up +Z.
    const ue::FQuat4d identity = ViewQuat(FRotator4f{0.0f, 0.0f, 0.0f});

    // The processor's forward lean is NEGATIVE z, and the offset is metres
    // while UE works in centimetres. Leaning in must move the camera FORWARD
    // by the full amount - a mirrored sign here is the "leaning in barely
    // moves" bug AGENTS.md calls out, and on a table it is the difference
    // between seeing past a ramp and not.
    const ue::FVector lean = PositionOffsetUE(identity, 0.0f, 0.0f, -0.40f);
    Check(failures, NearEqual(lean.X, 40.0) && NearEqual(lean.Y, 0.0) && NearEqual(lean.Z, 0.0),
          "a full forward lean moves 40cm along camera-forward");

    const ue::FVector back = PositionOffsetUE(identity, 0.0f, 0.0f, 0.10f);
    Check(failures, NearEqual(back.X, -10.0), "a backward lean moves 10cm backwards");

    const ue::FVector sway = PositionOffsetUE(identity, 0.30f, 0.0f, 0.0f);
    Check(failures, NearEqual(sway.Y, -30.0) && NearEqual(sway.X, 0.0),
          "sway runs opposite UE camera-right");

    const ue::FVector heave = PositionOffsetUE(identity, 0.0f, 0.20f, 0.0f);
    Check(failures, NearEqual(heave.Z, 20.0) && NearEqual(heave.X, 0.0),
          "heave runs along camera-up");

    // The offset is built in the CLEAN camera basis, so it follows where the
    // body faces: yawed 90 degrees, forward is world +Y.
    const ue::FQuat4d yawed = ViewQuat(FRotator4f{0.0f, 90.0f, 0.0f});
    const ue::FVector leanYawed = PositionOffsetUE(yawed, 0.0f, 0.0f, -0.40f);
    Check(failures, NearEqual(leanYawed.X, 0.0, 1e-3) && NearEqual(leanYawed.Y, 40.0, 1e-3),
          "the offset follows the camera basis, not world axes");
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
    FieldOfViewTests(failures);
    return pinballfx_tests::Report("View injection tests", failures);
}
