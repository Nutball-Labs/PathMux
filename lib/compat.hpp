// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#pragma once

#include <string>
#include <ctime>
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX      // prevent windows.h from defining min/max macros
#  endif
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
// Windows: popen/pclose → windowsPopen/windowsPclose (CREATE_NO_WINDOW so no
//   console window steals focus on each ffprobe/exiftool call).
//   WEXITSTATUS is a pass-through.
//   localtime_r does not exist — shimmed to localtime_s (reversed param order).
// ---------------------------------------------------------------------------

#ifdef _WIN32
#  include <io.h>      // _open_osfhandle
#  include <fcntl.h>   // _O_RDONLY

// windowsPopen — CreateProcess-based popen that suppresses the console window.
// Supports "r" mode only (all current callers read stdout).
inline FILE* windowsPopen(const char* cmd, const char* /*mode*/) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle       = TRUE;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return nullptr;
    // Don't let the child inherit the read end.
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si  = {};
    si.cb            = sizeof(si);
    si.dwFlags       = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput     = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput    = hWrite;
    si.hStdError     = GetStdHandle(STD_ERROR_HANDLE);
    si.wShowWindow   = SW_HIDE;

    // cmd.exe /c lets the shell handle redirection tokens (2>NUL, 2>&1 etc.)
    // already embedded in `cmd`.
    std::string cmdStr = std::string("cmd.exe /c ") + cmd;

    PROCESS_INFORMATION pi = {};
    // CreateProcessA requires a writable buffer for lpCommandLine.
    // std::string::data() is non-const in C++17.
    BOOL ok = CreateProcessA(nullptr, cmdStr.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);   // close write end regardless — child owns it now (or failed)
    if (!ok) {
        CloseHandle(hRead);
        return nullptr;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hRead), _O_RDONLY);
    if (fd == -1) {
        CloseHandle(hRead);
        return nullptr;
    }
    return _fdopen(fd, "r");
}

inline int windowsPclose(FILE* f) {
    if (f) fclose(f);
    return 0;
}

// Override any MinGW or MSVC definition so all callers get the hidden-window version.
#  undef  popen
#  undef  pclose
#  define popen  windowsPopen
#  define pclose windowsPclose

#  ifndef WEXITSTATUS
#    define WEXITSTATUS(s) (s)
#  endif
#elif defined(__APPLE__)
// Apple's sys/wait.h defines WEXITSTATUS via _W_INT(w) = *(int*)&(w), which
// requires an lvalue.  Pull the header in now so its definition is processed,
// then replace it with the portable bit-shift form that accepts rvalues.
#  include <sys/wait.h>
#  undef  WEXITSTATUS
#  define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#endif

// localtime_r — not available on Windows (MSVC or MinGW); shim via localtime_s.
#ifdef _WIN32
inline struct tm* localtime_r(const time_t* timep, struct tm* result) {
    localtime_s(result, timep);   // Windows: reversed params vs POSIX
    return result;
}

// timegm — POSIX/GNU extension not available on Windows; _mkgmtime is equivalent.
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

// SN: 00094
