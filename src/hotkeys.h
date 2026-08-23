// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <memory>

#include <cameraunlock/input/hotkey_poller.h>

#include "runtime_state.h"

namespace pinballfx_ht
{
    // Binds the nav-cluster keys, their Ctrl+Shift chord alternatives and the
    // dev inject-mode cycling, then starts polling. The returned poller owns the
    // polling thread; Stop() it before the session goes away.
    std::unique_ptr<cameraunlock::input::HotkeyPoller> StartHotkeys(Session& session,
                                                                    int yawModeKey);
}
