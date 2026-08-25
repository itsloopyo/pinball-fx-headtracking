// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <memory>

#include <cameraunlock/input/hotkey_poller.h>

#include "config.h"
#include "runtime_state.h"

namespace pinballfx_ht
{
    // Binds the nav-cluster keys, their Ctrl+Shift chord alternatives and the
    // camera framing tuning, then starts polling. The config is held by reference as the record of what the INI
    // says: the framing restore key reads from it, and the framing save writes
    // to it as well as to the file, so the two never disagree. It must outlive the
    // poller. The returned poller owns the polling thread; Stop() it before the
    // session goes away.
    std::unique_ptr<cameraunlock::input::HotkeyPoller> StartHotkeys(Session& session,
                                                                    Config& config);
}
