// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include <cameraunlock/memory/pe_fingerprint.h>

// One BuildProfile describes a single shipped build of Pinball FX: the
// PE-header fingerprint that uniquely identifies it, plus every per-build RVA
// the camera hook needs. The registry holds one profile per supported build; at
// startup the mod fingerprints the live module and selects the matching
// profile. No match leaves the mod fully dormant (no hooks installed, game runs
// vanilla) - see AGENTS.md "Maintain compatibility across new patches": never
// edit an existing profile's RVAs in place, ADD a new one.
//
// Pinball FX is UE 4.27, which predates Large World Coordinates, so FVector /
// FRotator are 3-float structs (12 bytes each). The hook reads and writes the
// GetPlayerViewPoint out-params as floats - see ue4_types.h.
//
// The game draws no crosshair and the player aims nothing: the ball is driven
// by physics and the flippers by button presses, neither of which reads the
// camera. So there is no aim to decouple from look, and the caller gate exists
// purely to keep tracking out of any non-render consumer of the view point
// (audio listener placement, culling queries) that would otherwise inherit the
// head pose.

namespace pinballfx_ht
{
    // PE-header build fingerprint (TimeDateStamp + SizeOfImage + CheckSum); the
    // shared type keeps reading/matching/classification in core.
    using PeFingerprint = ::cameraunlock::memory::PeFingerprint;

    // Call-site slots a profile can pin. The inject-mode numbering is derived
    // from this (see view_injection.h), so widening the table widens the modes
    // the dev hotkeys cycle through with no other edit.
    inline constexpr std::size_t kMaxKnownCallers = 16;
    using CallerRvaTable = std::array<std::uintptr_t, kMaxKnownCallers>;

    struct OffsetTable
    {
        // Hook target: APlayerController::GetPlayerViewPoint. RVA from the
        // module base. Zero = profile incomplete (mod stays dormant).
        std::uintptr_t kGetPlayerViewPointRva;

        // Return-address RVAs of the distinct GetPlayerViewPoint call sites.
        // Head tracking is injected ONLY for the caller selected by the active
        // inject mode; every other caller reads the clean camera rotation.
        // 0-valued trailing entries are unused padding.
        CallerRvaTable kKnownCallerRvas;

        // The one entry in kKnownCallerRvas whose OutLocation argument is the
        // base of an FMinimalViewInfo rather than a bare FVector local, so it
        // is the only call site where the camera's FOV can be read or written
        // through that pointer. Held separately from the caller table because
        // the two answer different questions: the table is "call sites we can
        // inject rotation for" and the inject mode walks it, while this is
        // "the call site that carries a whole view info". Reading +FovOffset
        // off any other caller's OutLocation would touch an unrelated stack
        // local. Zero = this build has no such caller pinned; the mod then
        // leaves FOV alone.
        std::uintptr_t kViewInfoCallerRva;

        // Byte offset of FMinimalViewInfo::FOV (horizontal, degrees) from the
        // struct base, which is also its Location field. Zero = not derived
        // for this build; the mod then leaves FOV alone.
        std::size_t kMinimalViewInfoFovOffset;

        // ---- gameplay gate ----------------------------------------------
        // Head tracking is only wanted while a table is actually being played.
        // Two pinned chains answer that, both of them the game's own
        // accessors rather than anything inferred from camera motion:
        //
        //   kPfxGameInstancePtrRva        global UPFXGameInstance*, the object
        //                                 GetPFXGameInstance() returns
        //     + kGameInstancePfxGameHandlerOffset -> the PFX game handler, i.e.
        //                                 GetPfxGameHandlerPtr()
        //       + kPfxGameHandlerGameStateOffset  -> EYUPGameState, what
        //                                 GetGameState() reads
        //       + kPfxGameHandlerPauseOffset      -> nonzero while paused, what
        //                                 IsPaused() tests
        //
        // Zero on any of these disables the gameplay half of the gate for the
        // build (logged once at init); it never silently disables tracking.
        std::uintptr_t kPfxGameInstancePtrRva;
        std::size_t    kGameInstancePfxGameHandlerOffset;
        std::size_t    kPfxGameHandlerGameStateOffset;
        std::size_t    kPfxGameHandlerPauseOffset;

        // EYUPGameState::InGame for this build. Pinned per build because it is
        // an enumerator value in shipped game code, not an engine constant, so
        // a patch that inserts a state ahead of it renumbers it.
        std::uint32_t  kYupGameStateInGame;

        // The scripted-camera half of the gate: the global the game's
        // GetPlayRoomCameraManager() returns, and the manager's own
        // IsCameraSequencePlaying(). Calling the game's accessor rather than
        // re-deriving it keeps the soft-object-pointer resolution it does in
        // one place - the game's. Zero on either disables this half.
        std::uintptr_t kPlayRoomCameraManagerPtrRva;
        std::uintptr_t kIsCameraSequencePlayingRva;

        // Default inject mode at startup. 0 = all callers (diagnostic only),
        // 1..kMaxKnownCallers = inject only for kKnownCallerRvas[mode-1] (the
        // render-path caller), and one past that = none. Ctrl+Shift+U / J cycle
        // this live so the render caller can be re-confirmed in game after a
        // patch without a rebuild.
        int kDefaultInjectMode;
    };

    struct BuildProfile
    {
        const char*   Name;
        PeFingerprint Fingerprint;
        OffsetTable   Offsets;
    };
}
