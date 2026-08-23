// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "exe_paths.h"

#include <windows.h>

namespace pinballfx_ht
{
    namespace
    {
        // Everything before the last path separator, with no trailing
        // separator. "." when the path has none.
        std::wstring DirectoryOf(const std::wstring& path)
        {
            const auto slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? L"." : path.substr(0, slash);
        }

        std::string DirectoryOf(const std::string& path)
        {
            const auto slash = path.find_last_of("\\/");
            return slash == std::string::npos ? "." : path.substr(0, slash);
        }
    }

    // A zero length is failure and a length of MAX_PATH is truncation - and on
    // truncation some Windows versions do not terminate the buffer at all, so
    // constructing a string from it reads off the end of the array. Neither
    // result names a real directory, so both fall back to the working directory
    // the same way a path with no separator does.
    std::wstring ExeDirectory()
    {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return L".";
        return DirectoryOf(std::wstring(path, length));
    }

    std::string ExeDirectoryNarrow()
    {
        char path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) return ".";
        return DirectoryOf(std::string(path, length));
    }
}
