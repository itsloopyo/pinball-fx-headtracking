// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Steam Win64 build of Pinball FX (PinballFX-Win64-Shipping.exe, UE 4.27, PE
// build date 2026-05-21, Steam buildid 23337994). RVAs derived in Ghidra via
// scripts/ghidra/discover_gpv.java against the matching binary.
//
// The install root holds a BootstrapPackagedGame shim (PinballFX.exe); the
// module these RVAs are relative to is the real shipping EXE under
// PinballFX\Binaries\Win64, which is also where the ASI loader goes.
//
// To add support for a new Steam build: do NOT edit kSteamProfile_<date> in
// place. Append a new `extern const BuildProfile kSteamProfile_YYYYMMDD = {...}`
// below, register it at the top of kKnownProfiles in build_registry.cpp, and
// keep older profiles forever (the PE fingerprint routes each user to theirs).

namespace pinballfx_ht::builds
{
    extern const BuildProfile kSteamProfile_20260521;

    // ---- Steam Win64 build (PE TimeDateStamp 0x6A0E94F2, 2026-05-21) ----
    const BuildProfile kSteamProfile_20260521 = {
        /* Name        */ "steam-win64-20260521",
        /* Fingerprint */ { 0x6A0E94F2u, 0x06703000u, 0x0616DBCCu },
        /* Offsets     */ {
            // APlayerController::GetPlayerViewPoint @ RVA 0x03476CE0, derived
            // by scripts/derive_gpv.py. GetPlayerViewPoint is not a UFUNCTION
            // in 4.27 and UE ships engine classes without RTTI, so it is
            // reached from the one reflection name that IS in the binary:
            // "GetActorEyesViewPoint" -> its FNameNativePtrPair -> the exec
            // thunk (0x0366E1C0), which closes on a virtual dispatch through
            // slot 0x608, giving AActor's vtable slot for it -> the one-line
            // forwarder at 0x03149A90 whose whole body dispatches through that
            // same slot, which is AController::GetPlayerViewPoint -> its
            // single direct caller.
            //
            // Confirmed by inspection: takes (this, FVector* OutLoc,
            // FRotator* OutRot) in rcx/rdx/r8, reads this->PlayerCameraManager
            // at +0x2E0, gates on the camera-cache TimeStamp at +0x1B00 being
            // > 0, and otherwise falls through to the view target and finally
            // to the AController base. Both out-params are written as 12-byte
            // (3-float) structs - UE4, pre-LWC; see ue4_types.h. It sits in 8
            // vtables (APlayerController plus the subclasses that inherit it).
            /* kGetPlayerViewPointRva */ 0x03476CE0ULL,
            // GetPlayerViewPoint call sites, captured at runtime with inject
            // mode 0 (which logs every call CHAIN plus counts) while a table
            // was in play. All of them dispatch through vtable slot 0x718.
            // The rest are kept so a single in-game session can A/B them with
            // Ctrl+Shift+U, and so the next person can see what was rejected.
            //
            //  [0] 0x032cc95a  ULocalPlayer::GetViewPoint. THE RENDER PATH,
            //      and the only caller whose two out-params are fields of one
            //      FMinimalViewInfo: it dispatches through slot 0x718 with
            //      one base pointer as OutLocation and that base + 0x0c as
            //      OutRotation, so they are &ViewInfo.Location and
            //      &ViewInfo.Rotation. It fills the struct from the camera
            //      manager (a preceding dispatch through slot 0x6d0) before
            //      asking for the view point. Four
            //      distinct chains reach it - the scene-view builder, the
            //      projection-data path and two others - and the gate matches
            //      the immediate return address, so pinning it covers all of
            //      them.
            //  [1] 0x031e31d6  out-params 0x48 apart on the frame, so two
            //      unrelated locals rather than one view info.
            //  [2] 0x032b5e5d  out-params on different bases (rbp and rsp).
            //  [3] 0x03472264  out-params 0x10 apart.
            //  [4] 0x00ebd4e3  writes into an array element rather than a
            //      fixed local - a per-listener/per-item loop.
            //  [5] 0x00eb26ac  low-rate sibling of [4].
            /* kKnownCallerRvas */ {{
                0x032cc95aULL, 0x031e31d6ULL, 0x032b5e5dULL, 0x03472264ULL,
                0x00ebd4e3ULL, 0x00eb26acULL, 0x0ULL, 0x0ULL,
                0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            }},
            // Same call site as kKnownCallerRvas[0], named separately for the
            // one thing only it can do: its OutLocation is &ViewInfo.Location,
            // so the whole FMinimalViewInfo is reachable from it. Two facts
            // about this call site, both read off the binary and recorded here
            // as the measurements they are:
            //
            //   - Its two out-params are the Location and Rotation fields of a
            //     single FMinimalViewInfo: the base pointer passed as
            //     OutLocation is the struct base, and OutRotation is that base
            //     + 0x0c.
            //   - The camera manager's FOV angle is stored into that same
            //     struct at +0x18 immediately before the view point is asked
            //     for, and the engine does not read it again before building
            //     the projection matrix from it.
            //
            // So by the time the hook runs, +0x18 already holds the FOV this
            // frame will be projected with.
            /* kViewInfoCallerRva */ 0x032cc95aULL,
            // FMinimalViewInfo::FOV, from the store described above. Matches the UE 4.27
            // struct (FVector Location @ 0x00, FRotator Rotation @ 0x0c, float
            // FOV @ 0x18), which the binary also confirms by reflection: the
            // property names "DesiredFOV" (@ 0x50b11a0) and
            // "OffCenterProjectionOffset" (@ 0x50b11b0) are registered
            // adjacently, i.e. this build ships the stock struct.
            /* kMinimalViewInfoFovOffset */ 0x18u,
            // ---- gameplay gate ------------------------------------
            // Derived from the shipping EXE's own reflection tables, so each
            // one is the address the game itself reads, not a guess:
            //
            //   GetPFXGameInstance()  @ exec 0x0135AE40 is a static getter
            //     whose whole body loads one RIP-relative global and returns
            //     it; that global is the address below.
            //   GetPfxGameHandlerPtr()@ exec 0x0135AFC0 returns [this+0x438].
            //   GetGameState()        @ exec 0x017271F0 returns the enum at
            //     [this+0x330]. Corroborated by IsTableGuideActive()
            //     (exec 0x01727870) comparing the same field against 9, which
            //     is exactly EYUPGameState::TableGuide in the registered
            //     enumerator table - so both the offset and the numbering are
            //     confirmed from two independent places in the binary.
            //   IsPaused()            @ exec 0x01727850 tests [this+0x6C] != 0.
            /* kPfxGameInstancePtrRva            */ 0x05C168A0ULL,
            /* kGameInstancePfxGameHandlerOffset */ 0x438u,
            /* kPfxGameHandlerGameStateOffset    */ 0x330u,
            /* kPfxGameHandlerPauseOffset        */ 0x6Cu,
            // EYUPGameState::InGame, read off the registered enumerator table:
            // Invalid 0, Loading 1, LoadingFinished 2, PreGameStart 3,
            // InGame 4, GameOver 5, PostGameOver 6, Aborted 7, OperatorsMenu 8,
            // TableGuide 9. The table intro camera moves play in PreGameStart,
            // which is why menus and intros are both excluded by the same test.
            /* kYupGameStateInGame               */ 4u,
            // GetPlayRoomCameraManager() @ exec 0x01397E80, the second static
            // getter of the same shape. IsCameraSequencePlaying() @ exec
            // 0x013980C0 is a one-line forwarder to the member function below,
            // which resolves the manager's CurrentCameraSequence soft pointer
            // and ends in the level-sequence player's IsPlaying().
            /* kPlayRoomCameraManagerPtrRva      */ 0x05C17370ULL,
            /* kIsCameraSequencePlayingRva       */ 0x0122A7D0ULL,
            // 1 = inject only kKnownCallerRvas[0], the render path.
            /* kDefaultInjectMode */ 1,
        },
    };
}
