// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace pinballfx_ht
{
    // Entry points called from DllMain. Initialize spins up a bootstrap thread
    // so the heavy work (config, fingerprinting, UDP, MinHook) never runs under
    // the loader lock.
    void Initialize();
    void Shutdown();
}
