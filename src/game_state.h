// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "builds/build_profile.h"

// Whether the game is somewhere head tracking belongs. Pinball FX renders a
// live 3D table behind its menus and plays scripted camera moves at the start
// of a table and around table events, and head tracking during either is
// unwanted: the menu camera is a composed shot, and a scripted move already
// owns the camera, so adding head motion on top fights it.
//
// Both answers come from the game's own state - the EYUPGameState the game
// handler holds, and the play-room camera manager's IsCameraSequencePlaying -
// rather than from watching the camera move, which cannot tell a scripted
// sweep from a table view that follows the ball.
//
// Reading the state and deciding from it is all this module does; reporting it
// belongs to the caller (hook_diagnostics for the per-frame verdict, bootstrap
// for the one-off coverage warning).

namespace pinballfx_ht
{
    struct GameplayState
    {
        // False until the game-handler pointer chain resolves, which is the
        // ordinary state before the game instance exists (early boot) and the
        // permanent state on a build whose profile does not pin the chain.
        bool          handlerResolved = false;
        std::uint32_t yupState        = 0;
        bool          paused          = false;
        bool          cameraSequence  = false;
    };

    struct GameStateGate
    {
        bool gameplay_only = true;
        bool suppress_during_camera_sequences = true;
    };

    // Reads the live game state through the pinned chains. Every read is fault
    // guarded; a link that is null or implausible leaves its half of the state
    // at the defaults above.
    GameplayState ReadGameplayState(const OffsetTable& offsets);

    // The verdict, pure so it can be exercised without a game process.
    // Unresolved fails the gameplay test: "we cannot see the game state" is
    // not "we are in a game". The camera-sequence test only ever suppresses,
    // so a build that does not pin that chain keeps tracking rather than
    // losing it.
    inline bool ShouldTrackNow(const GameplayState& state, std::uint32_t inGameValue,
                               const GameStateGate& gate)
    {
        if (gate.gameplay_only &&
            !(state.handlerResolved && state.yupState == inGameValue && !state.paused))
            return false;
        if (gate.suppress_during_camera_sequences && state.cameraSequence)
            return false;
        return true;
    }

    // EYUPGameState name for the log, or "unknown(N)" for a value this mod has
    // no name for - which is what a patch that adds a state looks like.
    const char* YupGameStateName(std::uint32_t value);

    // Whether the active profile pins each half of the gate. A half the profile
    // leaves unpinned cannot suppress anything, so a config that asks for it
    // gets a warning at init rather than silently doing nothing.
    inline bool GameplayGateIsPinned(const OffsetTable& offsets)
    {
        return offsets.kPfxGameInstancePtrRva != 0 &&
               offsets.kGameInstancePfxGameHandlerOffset != 0 &&
               offsets.kPfxGameHandlerGameStateOffset != 0 &&
               offsets.kPfxGameHandlerPauseOffset != 0;
    }

    inline bool CameraSequenceGateIsPinned(const OffsetTable& offsets)
    {
        return offsets.kPlayRoomCameraManagerPtrRva != 0 &&
               offsets.kIsCameraSequencePlayingRva != 0;
    }
}
