// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

#include "builds/build_profile.h"
#include "ue4_types.h"

// What the hook decides and what it writes, with no state of its own: which
// GetPlayerViewPoint caller may be injected, and what the tracked rotation and
// the world-space position offset come out as. Everything here is a pure
// function of its arguments so it can be exercised without a game process -
// see tests/view_injection_tests.cpp.

namespace pinballfx_ht
{
    namespace ue = ::cameraunlock::unreal;

    // ---- inject-mode caller gate ----------------------------------------
    // 0                        = all callers (diagnostic only - lets the census
    //                            record every call chain so the render caller
    //                            can be re-confirmed after a patch)
    // 1..kMaxKnownCallers      = inject only for kKnownCallerRvas[mode-1]
    // kInjectModeNone          = none (tracking disabled at the hook)
    inline constexpr int kInjectModeAllCallers = 0;
    inline constexpr int kInjectModeFirstCaller = 1;
    inline constexpr int kInjectModeLastCaller = static_cast<int>(kMaxKnownCallers);
    inline constexpr int kInjectModeNone = kInjectModeLastCaller + 1;
    inline constexpr int kInjectModeCount = kInjectModeNone + 1;

    // 0 for a mode that pins no single caller (all-callers, none, out of range).
    inline std::uintptr_t CallerRvaForMode(int mode, const CallerRvaTable& callers)
    {
        if (mode < kInjectModeFirstCaller || mode > kInjectModeLastCaller) return 0;
        return callers[static_cast<std::size_t>(mode - kInjectModeFirstCaller)];
    }

    inline bool ShouldInjectForCaller(std::uintptr_t retRva, int mode,
                                      const CallerRvaTable& callers)
    {
        if (mode == kInjectModeAllCallers) return true;
        const std::uintptr_t rva = CallerRvaForMode(mode, callers);
        return rva != 0 && retRva == rva;
    }

    // Wraps at both ends so the dev hotkeys cycle the whole range in either
    // direction.
    inline int CycleInjectMode(int mode, int direction)
    {
        return (mode + direction + kInjectModeCount) % kInjectModeCount;
    }

    // ---- field of view ---------------------------------------------------
    // Pinball FX has no FOV control of its own: nothing in GameUserSettings.ini
    // touches it, and the shipping binary registers no SetFOV exec/UFUNCTION to
    // drive from a console. Each table view carries its own FOV from the camera
    // manager, and the player gets whatever it hands them. These two knobs are
    // the whole of the mod's answer to that, applied to the render-path copy of
    // the view info and nowhere else.
    //
    // Nothing else in this mod's maths is FOV-dependent - the rotation
    // composition and the position parallax above are pure world-space
    // geometry, and the game draws no crosshair, so there is no screen-space
    // projection to keep in step. What does have to stay in step is the view
    // info itself: the FOV write lands on the same struct, at the same call
    // site, in the same frame as the rotation and position writes, so every
    // consumer downstream of ULocalPlayer::GetViewPoint - the scene view and
    // the projection data that ProjectWorldToScreen deprojects through - sees
    // one self-consistent camera rather than a new FOV with an old view point.

    // What the engine can plausibly have put in the FOV field, in degrees. A
    // value outside this is not a camera FOV, which means the pinned offset
    // does not describe this build; writing through it would then corrupt
    // whatever does live there, so the mod leaves FOV alone instead. NaN and
    // infinity fail both comparisons.
    inline constexpr float kMinPlausibleFov = 1.0f;
    inline constexpr float kMaxPlausibleFov = 179.0f;

    inline bool FovLooksSane(float fov)
    {
        return fov > kMinPlausibleFov && fov < kMaxPlausibleFov;
    }

    // FMinimalViewInfo::FOV, in degrees: what the engine put there (game) and
    // what the frame is projected with after the mod has had its say (render).
    // Both zero for a caller that does not carry a view info.
    struct FovSample { float game, render; };

    // Whether this call site's OutLocation is the base of an FMinimalViewInfo,
    // i.e. whether the FOV field is reachable from it at all. Only one caller
    // passes one; the rest pass a bare FVector local, where the offset would
    // land on unrelated stack memory. The gate is the caller RVA itself rather
    // than the inject mode, which in its diagnostic all-callers setting matches
    // everything. A profile that pins neither RVA nor offset leaves FOV alone.
    inline bool CallerCarriesViewInfo(std::uintptr_t retRva, std::uintptr_t viewInfoCallerRva,
                                      std::size_t fovFieldOffset)
    {
        return viewInfoCallerRva != 0 && fovFieldOffset != 0 && retRva == viewInfoCallerRva;
    }

    // Bounds on the configured result. Far wider than anything worth choosing -
    // they stop a typo reaching the projection matrix, not a preference.
    inline constexpr float kMinConfiguredFov = 20.0f;
    inline constexpr float kMaxConfiguredFov = 170.0f;

    // fovOverride > 0 replaces the game's angle outright; otherwise fovOffset
    // is added to it, which widens every view by the same amount and so keeps
    // the per-table camera design (each view ships its own FOV) intact.
    inline float EffectiveFov(float gameFov, float fovOverride, float fovOffset)
    {
        // Configuring neither must leave the game's value untouched, including
        // when the game itself chooses one outside the configured bounds.
        if (fovOverride <= 0.0f && fovOffset == 0.0f) return gameFov;

        const float wanted = fovOverride > 0.0f ? fovOverride : gameFov + fovOffset;
        if (wanted < kMinConfiguredFov) return kMinConfiguredFov;
        if (wanted > kMaxConfiguredFov) return kMaxConfiguredFov;
        return wanted;
    }

    // ---- pose composition ------------------------------------------------
    // The tracker pose the session hands out, in degrees.
    struct HeadPose { float yaw, pitch, roll; };

    inline ue::FQuat4d ViewQuat(const FRotator4f& rotation)
    {
        return ue::QuatFromEulerDeg(rotation.Pitch, rotation.Yaw, rotation.Roll);
    }

    // baseQ must be ViewQuat(clean); it is passed in because the caller also
    // needs it for the position offset and the conversion is not free.
    inline FRotator4f ComposeTrackedRotation(const FRotator4f& clean, const ue::FQuat4d& baseQ,
                                             float yaw, float pitch, float roll,
                                             bool worldSpaceYaw)
    {
        if (worldSpaceYaw) {
            // Horizon-locked: FRotator addition about the world up-axis.
            return FRotator4f{clean.Pitch + pitch, clean.Yaw + yaw, clean.Roll - roll};
        }
        // Camera-local: quaternion post-multiply, which leans on pitched turns.
        const ue::FQuat4d headLocalQ = ue::QuatFromEulerDeg(
            static_cast<double>(pitch), static_cast<double>(yaw), -static_cast<double>(roll));
        const ue::FRotator composed = ue::QuatToRotator(ue::QuatMul(baseQ, headLocalQ));
        return FRotator4f{
            static_cast<float>(composed.Pitch),
            static_cast<float>(composed.Yaw),
            static_cast<float>(composed.Roll),
        };
    }

    // Build a world-space camera-location offset (UE units = cm) from the
    // session's processed offset (metres) in the CLEAN camera frame, so head
    // sway follows the body rather than the head-rotated view. On a pinball
    // table this is the parallax that makes leaning in to see past a ramp work
    // the way it does on a real cabinet.
    inline ue::FVector PositionOffsetUE(const ue::FQuat4d& baseQ, float offX, float offY, float offZ)
    {
        const ue::FVector camFwd   = ue::QuatRotateVec(baseQ, ue::FVector{1.0, 0.0, 0.0});
        const ue::FVector camRight = ue::QuatRotateVec(baseQ, ue::FVector{0.0, 1.0, 0.0});
        const ue::FVector camUp    = ue::QuatRotateVec(baseQ, ue::FVector{0.0, 0.0, 1.0});
        constexpr double kMetresToUE = 100.0;
        // Sign flips are the core-to-engine convention boundary, not user
        // inversion: the processor's forward lean is NEGATIVE z (that is the
        // axis carrying the generous limit_z), and its sway runs opposite UE's
        // camera-right.
        const double s = -static_cast<double>(offZ) * kMetresToUE;  // surge -> forward
        const double r = -static_cast<double>(offX) * kMetresToUE;  // sway  -> right
        const double u =  static_cast<double>(offY) * kMetresToUE;  // heave -> up
        return ue::FVector{
            camFwd.X * s + camRight.X * r + camUp.X * u,
            camFwd.Y * s + camRight.Y * r + camUp.Y * u,
            camFwd.Z * s + camRight.Z * r + camUp.Z * u,
        };
    }
}
