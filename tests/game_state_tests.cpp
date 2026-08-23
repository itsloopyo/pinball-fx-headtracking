// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The gameplay gate's verdict. Reading the game's state needs a live process,
// but the decision made from it does not, and every case below has a failure
// mode that reaches a user: an unresolved chain that reads as "in gameplay"
// puts head tracking back in the menus, and a camera-sequence test that can
// deny on its own takes tracking away on a build that never pins it.

#include "game_state.h"

#include <cstring>

#include <cameraunlock/unreal/ue_runtime.h>

#include "test_support.h"

namespace {

using pinballfx_tests::Check;

constexpr std::uint32_t kInGame = 4;
constexpr std::uint32_t kPreGameStart = 3;

pinballfx_ht::GameplayState InGameplay()
{
    pinballfx_ht::GameplayState state;
    state.handlerResolved = true;
    state.yupState = kInGame;
    return state;
}

// A stand-in for the game module: ReadGameplayState resolves every pinned RVA
// against the range SetRuntime was given, so pointing that range at a buffer we
// control makes the chain walk exercisable without a game process.
//
// Layout, mirroring the shipped profile's shape:
//   +0x40  UPFXGameInstance*            -> +0x80
//   +0x50  APlayRoomCameraManager*      -> +0xC0
//   +0x90  the instance's game handler  -> +0xC0   (instance + 0x10)
//   +0xC8  EYUPGameState                (handler + 0x08)
//   +0xCC  paused flag                  (handler + 0x0C)
alignas(16) unsigned char g_fakeImage[256];

constexpr std::uintptr_t kInstanceGlobalRva = 0x40;
constexpr std::uintptr_t kCameraManagerGlobalRva = 0x50;
constexpr std::size_t kHandlerOffset = 0x10;
constexpr std::size_t kGameStateOffset = 0x08;
constexpr std::size_t kPauseOffset = 0x0C;

void Poke(std::size_t at, std::uintptr_t value)
{
    std::memcpy(g_fakeImage + at, &value, sizeof(value));
}

void PokeU32(std::size_t at, std::uint32_t value)
{
    std::memcpy(g_fakeImage + at, &value, sizeof(value));
}

std::uintptr_t InstallFakeImage()
{
    std::memset(g_fakeImage, 0, sizeof(g_fakeImage));
    const auto base = reinterpret_cast<std::uintptr_t>(g_fakeImage);

    Poke(kInstanceGlobalRva, base + 0x80);                    // -> instance
    Poke(0x80 + kHandlerOffset, base + 0xC0);                 // instance -> handler
    PokeU32(0xC0 + kGameStateOffset, kInGame);
    PokeU32(0xC0 + kPauseOffset, 0);
    Poke(kCameraManagerGlobalRva, base + 0xC0);               // -> camera manager

    cameraunlock::unreal::SetRuntime(base, base + sizeof(g_fakeImage),
                                     cameraunlock::unreal::UObjectGlobalsLayout{});
    return base;
}

pinballfx_ht::OffsetTable FakeOffsets()
{
    pinballfx_ht::OffsetTable offsets{};
    offsets.kPfxGameInstancePtrRva = kInstanceGlobalRva;
    offsets.kGameInstancePfxGameHandlerOffset = kHandlerOffset;
    offsets.kPfxGameHandlerGameStateOffset = kGameStateOffset;
    offsets.kPfxGameHandlerPauseOffset = kPauseOffset;
    offsets.kYupGameStateInGame = kInGame;
    offsets.kPlayRoomCameraManagerPtrRva = kCameraManagerGlobalRva;
    // Deliberately left 0 by default: CameraSequenceGateIsPinned is then false
    // and nothing is called. Each test sets it to what it wants to exercise.
    offsets.kIsCameraSequencePlayingRva = 0;
    return offsets;
}

// The camera-sequence half CALLS through its pinned RVA. An RVA outside the
// module does not name code in this build and in a loaded process lands in
// whatever DLL occupies that address, so it must never be called - if this
// guard is removed, this case does not fail, it faults.
void OutOfModuleRvaTests(int& failures)
{
    InstallFakeImage();
    pinballfx_ht::OffsetTable offsets = FakeOffsets();

    // The manager global resolves (it is in-module and holds a plausible
    // pointer), so the function RVA is the only thing left standing between
    // this call and executing an address 1 GB past the end of the image.
    offsets.kIsCameraSequencePlayingRva = 0x40000000;
    pinballfx_ht::GameplayState state = pinballfx_ht::ReadGameplayState(offsets);
    Check(failures, !state.cameraSequence,
          "an IsCameraSequencePlaying RVA outside the module is not called");

    // Same rule for the globals: reading one from outside the image yields
    // whatever is there, and LooksLikePointer would happily accept it.
    offsets = FakeOffsets();
    offsets.kPfxGameInstancePtrRva = 0x40000000;
    state = pinballfx_ht::ReadGameplayState(offsets);
    Check(failures, !state.handlerResolved,
          "a game-instance global RVA outside the module resolves to nothing");
}

// The guards above must not cost the working path: a profile whose RVAs do fit
// the module still walks the chain and reports the state it finds.
void ChainWalkTests(int& failures)
{
    InstallFakeImage();
    const pinballfx_ht::OffsetTable offsets = FakeOffsets();

    pinballfx_ht::GameplayState state = pinballfx_ht::ReadGameplayState(offsets);
    Check(failures, state.handlerResolved && state.yupState == kInGame && !state.paused,
          "an in-module chain resolves to the game state it points at");

    PokeU32(0xC0 + kPauseOffset, 1);
    state = pinballfx_ht::ReadGameplayState(offsets);
    Check(failures, state.handlerResolved && state.paused,
          "the paused flag is read from the handler");

    // A null global is the ordinary state before the game instance exists.
    Poke(kInstanceGlobalRva, 0);
    state = pinballfx_ht::ReadGameplayState(offsets);
    Check(failures, !state.handlerResolved,
          "a game instance that has not been created yet reads as unresolved");
}

}  // namespace

int RunGameStateTests()
{
    int failures = 0;
    std::cout << "Game state tests\n";

    const pinballfx_ht::GameStateGate on;
    pinballfx_ht::GameStateGate off;
    off.gameplay_only = false;
    off.suppress_during_camera_sequences = false;

    Check(failures, pinballfx_ht::ShouldTrackNow(InGameplay(), kInGame, on),
          "a table in play tracks");

    pinballfx_ht::GameplayState menu = InGameplay();
    menu.yupState = kPreGameStart;
    Check(failures, !pinballfx_ht::ShouldTrackNow(menu, kInGame, on),
          "any state other than InGame holds tracking");

    pinballfx_ht::GameplayState paused = InGameplay();
    paused.paused = true;
    Check(failures, !pinballfx_ht::ShouldTrackNow(paused, kInGame, on),
          "the pause screen holds tracking");

    pinballfx_ht::GameplayState unresolved;
    unresolved.yupState = kInGame;
    Check(failures, !pinballfx_ht::ShouldTrackNow(unresolved, kInGame, on),
          "an unresolved game-handler chain holds tracking rather than reading its "
          "zeroed state as a game");

    pinballfx_ht::GameplayState sequence = InGameplay();
    sequence.cameraSequence = true;
    Check(failures, !pinballfx_ht::ShouldTrackNow(sequence, kInGame, on),
          "a scripted camera move holds tracking even mid-game");
    Check(failures, pinballfx_ht::ShouldTrackNow(sequence, kInGame, off),
          "both gates off tracks through a camera move");

    // A build profile that pins neither chain leaves cameraSequence false and
    // handlerResolved false. Only the gameplay half may deny on that, so a user
    // who turns it off is not left with tracking silently disabled.
    pinballfx_ht::GameStateGate sequenceOnly;
    sequenceOnly.gameplay_only = false;
    Check(failures, pinballfx_ht::ShouldTrackNow(pinballfx_ht::GameplayState{}, kInGame,
                                                 sequenceOnly),
          "the camera-sequence gate alone never denies on an unpinned build");

    Check(failures, std::string(pinballfx_ht::YupGameStateName(kInGame)) == "InGame",
          "EYUPGameState 4 is named InGame in the log");
    Check(failures, std::string(pinballfx_ht::YupGameStateName(99)) == "unknown(99)",
          "a state this build has no name for is reported by value");

    // Runs last: it publishes a fake module range through SetRuntime, which is
    // process-wide.
    OutOfModuleRvaTests(failures);
    ChainWalkTests(failures);

    return pinballfx_tests::Report("Game state tests", failures);
}
