// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

// Where the game EXE lives - the log and the INI sit beside it. Both character
// widths exist because the log path is wide and core's IniReader is ANSI
// (GetPrivateProfile*A).
//
// Both return the directory with no trailing separator, and "." when the path
// cannot be resolved.

namespace pinballfx_ht
{
    std::wstring ExeDirectory();
    std::string  ExeDirectoryNarrow();
}
