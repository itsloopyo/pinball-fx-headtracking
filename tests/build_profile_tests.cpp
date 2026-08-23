// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Consistency checks on the shipped build profile. The RVAs themselves can
// only be confirmed against the game, but the relationships between them are
// checkable here, and each one has a failure mode that reaches a user: a
// default inject mode pointing at an empty slot silently disables tracking,
// and a zero hook RVA leaves the mod permanently dormant.

#include "builds/build_profile.h"
#include "view_injection.h"

#include "test_support.h"

namespace pinballfx_ht::builds {
    extern const BuildProfile kSteamProfile_20260521;
}

namespace {

using pinballfx_tests::Check;

}  // namespace

int RunBuildProfileTests()
{
    int failures = 0;
    std::cout << "Build profile tests\n";

    const pinballfx_ht::BuildProfile& profile = pinballfx_ht::builds::kSteamProfile_20260521;
    const pinballfx_ht::OffsetTable& offsets = profile.Offsets;

    // Read from the shipped PinballFX-Win64-Shipping.exe (see .lab/NOTES.md).
    Check(failures, profile.Fingerprint.TimeDateStamp == 0x6A0E94F2u
                 && profile.Fingerprint.SizeOfImage == 0x06703000u
                 && profile.Fingerprint.CheckSum == 0x0616DBCCu,
          "the Steam 2026-05-21 fingerprint is unchanged");

    Check(failures, offsets.kGetPlayerViewPointRva != 0,
          "the profile is complete, so the mod activates rather than staying dormant");

    Check(failures, offsets.kDefaultInjectMode >= pinballfx_ht::kInjectModeFirstCaller
                 && offsets.kDefaultInjectMode <= pinballfx_ht::kInjectModeLastCaller,
          "the default inject mode selects a single caller, not all or none");

    Check(failures, pinballfx_ht::CallerRvaForMode(offsets.kDefaultInjectMode,
                                                   offsets.kKnownCallerRvas) != 0,
          "the default inject mode points at a pinned caller slot");

    // The view-info caller is the render path, so it has to be the same call
    // site the default inject mode selects. If the two ever named different
    // RVAs, the FOV would be rewritten on one frame path and the head pose on
    // another.
    Check(failures, offsets.kViewInfoCallerRva == pinballfx_ht::CallerRvaForMode(
                        offsets.kDefaultInjectMode, offsets.kKnownCallerRvas),
          "the view-info caller and the injected caller are the same call site");

    // A view-info offset without a caller to read it through, or the reverse,
    // is a half-derived profile: FOV would silently do nothing.
    Check(failures, (offsets.kViewInfoCallerRva != 0)
                 == (offsets.kMinimalViewInfoFovOffset != 0),
          "the view-info caller and the FOV offset are derived together");

    // UE 4.27's FMinimalViewInfo puts FVector Location at 0x00 and FRotator
    // Rotation at 0x0c, so FOV cannot sit inside the 24 bytes they occupy.
    Check(failures, offsets.kMinimalViewInfoFovOffset >= 0x18,
          "the FOV offset sits past the view info's location and rotation fields");

    // The gameplay gate is on by default, so an unpinned chain here is not a
    // dormant feature - it is head tracking still running in the menus while
    // the config says it should not.
    Check(failures, offsets.kPfxGameInstancePtrRva != 0
                 && offsets.kGameInstancePfxGameHandlerOffset != 0
                 && offsets.kPfxGameHandlerGameStateOffset != 0
                 && offsets.kPfxGameHandlerPauseOffset != 0,
          "the game-state chain is pinned, so GameplayOnly can be honoured");

    Check(failures, offsets.kPlayRoomCameraManagerPtrRva != 0
                 && offsets.kIsCameraSequencePlayingRva != 0,
          "the camera-sequence chain is pinned, so scripted camera moves can be "
          "detected");

    // The registered enumerator table for EYUPGameState runs Invalid 0 through
    // TableGuide 9, so a value outside that is not a state this build ships and
    // the gate would never open.
    Check(failures, offsets.kYupGameStateInGame <= 9,
          "the pinned EYUPGameState::InGame value is inside the shipped enum");

    // Every RVA is relative to a module whose size this profile also states, so
    // one that does not fit is a typo, checkable here rather than in the game.
    // It matters most for kIsCameraSequencePlayingRva, which the gate CALLS: an
    // address past the end of the image lands in an unrelated loaded DLL. The
    // hook and the gate both refuse to use an out-of-module address at runtime;
    // this is the same check made where a mistyped digit is introduced.
    const std::uintptr_t imageSize = profile.Fingerprint.SizeOfImage;
    auto fits = [imageSize](std::uintptr_t rva) { return rva != 0 && rva < imageSize; };

    bool allFit = fits(offsets.kGetPlayerViewPointRva)
               && fits(offsets.kViewInfoCallerRva)
               && fits(offsets.kPfxGameInstancePtrRva)
               && fits(offsets.kPlayRoomCameraManagerPtrRva)
               && fits(offsets.kIsCameraSequencePlayingRva);
    for (std::uintptr_t caller : offsets.kKnownCallerRvas) {
        if (caller != 0 && !fits(caller)) allFit = false;
    }
    Check(failures, allFit,
          "every pinned RVA resolves inside the image size the fingerprint declares");

    return pinballfx_tests::Report("Build profile tests", failures);
}
