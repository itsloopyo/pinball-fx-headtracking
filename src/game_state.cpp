// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <cstdio>

#include <cameraunlock/unreal/ue_runtime.h>

#include "logging.h"

namespace pinballfx_ht
{
    namespace
    {
        namespace ue = ::cameraunlock::unreal;

        // APlayRoomCameraManager::IsCameraSequencePlaying. A member function
        // with no arguments, so the x64 convention puts `this` in rcx and
        // nothing else is passed.
        using IsCameraSequencePlayingFn = bool (*)(void* self);

        // A pinned RVA has to resolve inside the loaded module. Profiles are
        // hand-appended per game patch, so a transposed digit is the realistic
        // failure, and the camera-sequence half CALLS through its RVA rather
        // than reading it: an address past the end of the image lands in
        // whatever DLL happens to occupy that range in this process and gets
        // executed. InstallViewPointHook makes the same check on the hook
        // target for the same reason; every pinned address wants it.
        //
        // Warned once per address rather than per frame, and the affected half
        // of the gate then stays off for the session instead of the profile
        // bug being invisible.
        bool RvaIsInModule(std::uintptr_t base, std::uintptr_t rva, const char* what,
                           bool& warned)
        {
            const std::uintptr_t address = base + rva;
            if (address >= base && address < ue::ModuleEnd()) return true;
            if (!warned) {
                warned = true;
                Log::Line("WARNING: the active build profile puts %s at RVA 0x%08llx, which "
                          "is outside the module - that half of the game-state gate is "
                          "disabled for this session.",
                    what, static_cast<unsigned long long>(rva));
            }
            return false;
        }

        // Follows one link of a pinned pointer chain. Returns 0 for a link
        // that is null or not a plausible heap pointer, so a global that has
        // not been filled in yet reads as "no object" rather than being
        // dereferenced.
        std::uintptr_t ReadObjectPtr(std::uintptr_t address)
        {
            std::uintptr_t value = 0;
            if (!ue::SafeReadPtr(address, value)) return 0;
            return ue::LooksLikePointer(value) ? value : 0;
        }

        // Global UPFXGameInstance* -> PFX game handler -> EYUPGameState and the
        // paused flag. Leaves @p state untouched unless the whole chain resolves
        // and both fields read, so a half-resolved chain never reports a state.
        void ReadHandlerState(std::uintptr_t base, const OffsetTable& offsets,
                              GameplayState& state)
        {
            if (!GameplayGateIsPinned(offsets)) return;

            static bool warned = false;
            if (!RvaIsInModule(base, offsets.kPfxGameInstancePtrRva,
                               "the PFX game instance global", warned))
                return;

            const std::uintptr_t instance =
                ReadObjectPtr(base + offsets.kPfxGameInstancePtrRva);
            if (instance == 0) return;

            const std::uintptr_t handler =
                ReadObjectPtr(instance + offsets.kGameInstancePfxGameHandlerOffset);
            if (handler == 0) return;

            std::uint32_t yup = 0;
            std::uint32_t pause = 0;
            if (!ue::SafeReadU32(handler + offsets.kPfxGameHandlerGameStateOffset, yup) ||
                !ue::SafeReadU32(handler + offsets.kPfxGameHandlerPauseOffset, pause))
                return;

            state.handlerResolved = true;
            state.yupState = yup;
            state.paused = pause != 0;
        }

        // The game's own IsCameraSequencePlaying, called rather than
        // re-derived, so the soft-object-pointer resolution it does stays in
        // one place - the game's.
        bool ReadCameraSequence(std::uintptr_t base, const OffsetTable& offsets)
        {
            if (!CameraSequenceGateIsPinned(offsets)) return false;

            // Both are checked before either is used: the manager pointer is
            // the `this` the call receives, and reading it from outside the
            // module would hand LooksLikePointer whatever happened to be there.
            static bool warnedManager = false;
            static bool warnedFunction = false;
            if (!RvaIsInModule(base, offsets.kPlayRoomCameraManagerPtrRva,
                               "the play-room camera manager global", warnedManager) ||
                !RvaIsInModule(base, offsets.kIsCameraSequencePlayingRva,
                               "IsCameraSequencePlaying", warnedFunction))
                return false;

            const std::uintptr_t manager =
                ReadObjectPtr(base + offsets.kPlayRoomCameraManagerPtrRva);
            if (manager == 0) return false;

            const auto isPlaying = reinterpret_cast<IsCameraSequencePlayingFn>(
                base + offsets.kIsCameraSequencePlayingRva);
            return isPlaying(reinterpret_cast<void*>(manager));
        }
    }

    GameplayState ReadGameplayState(const OffsetTable& offsets)
    {
        GameplayState state;
        const std::uintptr_t base = ue::ModuleBase();
        if (base == 0) return state;

        ReadHandlerState(base, offsets, state);
        state.cameraSequence = ReadCameraSequence(base, offsets);
        return state;
    }

    const char* YupGameStateName(std::uint32_t value)
    {
        switch (value) {
            case 0: return "Invalid";
            case 1: return "Loading";
            case 2: return "LoadingFinished";
            case 3: return "PreGameStart";
            case 4: return "InGame";
            case 5: return "GameOver";
            case 6: return "PostGameOver";
            case 7: return "Aborted";
            case 8: return "OperatorsMenu";
            case 9: return "TableGuide";
            default: break;
        }
        // Thread-local rather than a plain static: the hook can be reached from
        // more than one thread, and two of them formatting into one shared
        // buffer while a third reads it back through a %s hands the log a
        // spliced string. Per-thread, the line is written before the same
        // thread can call again.
        static thread_local char unknown[24];
        std::snprintf(unknown, sizeof(unknown), "unknown(%u)", value);
        return unknown;
    }
}
