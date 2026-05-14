// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "platform.hpp"
#include <cstdlib>
#include <iostream>
#include <filesystem>

#ifdef _WIN32
#  include <windows.h>
#elif defined(__APPLE__)
#  include <unistd.h>
#  include <sys/ioctl.h>
#else
#  include <unistd.h>
#  include <sys/ioctl.h>
#endif

namespace fs = std::filesystem;

namespace CamClops {
namespace Platform {

std::string getHomePath() {
#ifdef _WIN32
    const char* home = getenv("USERPROFILE");
    if (!home) {
        std::cerr << "Fatal: USERPROFILE environment variable is not set.\n";
        std::exit(1);
    }
#else
    const char* home = getenv("HOME");
    if (!home) {
        std::cerr << "Fatal: HOME environment variable is not set.\n";
        std::exit(1);
    }
#endif
    return std::string(home);
}

std::string getConfigDir() {
#ifdef _WIN32
    // %APPDATA% is the conventional location on Windows (%USERPROFILE%\AppData\Roaming).
    const char* appdata = getenv("APPDATA");
    std::string dir = appdata ? std::string(appdata) + "\\camclops\\"
                              : getHomePath() + "\\AppData\\Roaming\\camclops\\";
#elif defined(__APPLE__)
    // macOS convention: ~/Library/Application Support/<app>/
    std::string dir = getHomePath() + "/Library/Application Support/camclops/";
#else
    // Linux / other POSIX: XDG base dir convention
    std::string dir = getHomePath() + "/.config/camclops/";
#endif
    std::error_code ec;
    if (!fs::exists(dir, ec))
        fs::create_directories(dir, ec);
    return dir;
}

int getTerminalWidth() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    // Linux and macOS both support TIOCGWINSZ via sys/ioctl.h
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return static_cast<int>(ws.ws_col);
#endif
    return 65; // BOX_MIN fallback
}

} // namespace Platform
} // namespace CamClops

// SN: 00089
