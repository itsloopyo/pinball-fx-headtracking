// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/protocol/udp_receiver.h>

#include "config.h"
#include "game_state.h"
#include "runtime_state.h"

// The APlayerController::GetPlayerViewPoint detour: the one place this mod
// writes to the game. Everything it needs is handed to it once at install time,
// so the detour reads no state the bootstrap has not explicitly given it.

namespace pinballfx_ht
{
    // Installs the detour at the active profile's kGetPlayerViewPointRva.
    // False means nothing was hooked and the game is running vanilla; the
    // reason is logged.
    //
    // All four references must outlive the hooked process - the bootstrap owns
    // them for the life of the module.
    bool InstallViewPointHook(const Config& config, Session& session,
                              const cameraunlock::UdpReceiver& receiver,
                              const GameStateGate& gate);

    // Removes the detour and tears down MinHook. Safe to call when
    // InstallViewPointHook was never called or returned false.
    void RemoveViewPointHook();
}
