// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <ctime>

// ---------------------------------------------------------------------------
// compat.hpp — Cross-platform portability shims.
//
// Include in any .cpp that calls popen(), pclose(), or uses WEXITSTATUS(),
// localtime_r(), or pathBasename().
// Safe to include on all three platforms; most shims are no-ops on Linux/macOS.
//
// Linux/macOS: popen, pclose, WEXITSTATUS, and localtime_r are standard.
// Windows (MSVC): popen/pclose → _popen/_pclose; WEXITSTATUS is a pass-through;
//   localtime_r does not exist — shimmed to localtime_s (reversed param order).
// Windows (MinGW): usually has popen and localtime_r natively; defines harmless.
// ---------------------------------------------------------------------------

#ifdef _WIN32
#  ifndef popen
#    define popen  _popen
#    define pclose _pclose
#  endif
#  ifndef WEXITSTATUS
#    define WEXITSTATUS(s) (s)
#  endif
#endif

// localtime_r — MSVC only (MinGW provides it natively via <time.h>).
#ifdef _MSC_VER
inline struct tm* localtime_r(const time_t* timep, struct tm* result) {
    localtime_s(result, timep);   // MSVC: reversed params vs POSIX
    return result;
}
#endif

// NULL_REDIRECT — suppress stderr in popen() shell commands.
// cmd.exe does not have /dev/null; use the NUL device instead.
#ifdef _WIN32
#  define NULL_REDIRECT "2>NUL"
#else
#  define NULL_REDIRECT "2>/dev/null"
#endif

// ---------------------------------------------------------------------------
// pathBasename — return the filename portion of a path, handling both
// forward-slash (Linux/macOS) and backslash (Windows) separators.
// Used everywhere segment absolute paths are displayed as basenames.
// ---------------------------------------------------------------------------
inline std::string pathBasename(const std::string& p) {
    auto pos = p.find_last_of("/\\");
    return (pos != std::string::npos) ? p.substr(pos + 1) : p;
}

// SN: 00083
