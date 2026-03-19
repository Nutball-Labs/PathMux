// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <ctime>
#ifdef _WIN32
#  define NOMINMAX        // prevent windows.h from defining min/max macros
#  include <windows.h>   // GetComputerNameA — no Winsock init required
#else
#  include <unistd.h>    // gethostname
#endif

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

// timegm — POSIX/GNU extension not available on MSVC; _mkgmtime is equivalent.
inline time_t timegm(struct tm* t) {
    return _mkgmtime(t);
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
// getShortHostname — return the machine hostname with no domain suffix.
// Windows: GetComputerNameA (no Winsock init required).
// Linux/macOS: gethostname() + strip at first dot.
// ---------------------------------------------------------------------------
inline std::string getShortHostname() {
#ifdef _WIN32
    char buf[256] = {};
    DWORD sz = sizeof(buf);
    if (GetComputerNameA(buf, &sz)) return std::string(buf);
    return "localhost";
#else
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf)) == 0) {
        std::string hn = buf;
        auto dot = hn.find('.');
        if (dot != std::string::npos) hn = hn.substr(0, dot);
        return hn;
    }
    return "localhost";
#endif
}

// ---------------------------------------------------------------------------
// pathBasename — return the filename portion of a path, handling both
// forward-slash (Linux/macOS) and backslash (Windows) separators.
// Used everywhere segment absolute paths are displayed as basenames.
// ---------------------------------------------------------------------------
inline std::string pathBasename(const std::string& p) {
    auto pos = p.find_last_of("/\\");
    return (pos != std::string::npos) ? p.substr(pos + 1) : p;
}

// SN: 00087
