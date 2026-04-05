// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef VERSION_HPP
#define VERSION_HPP

// Global application identifiers
#define APP_NAME "PathMux Dashcam Explorer"

// 1. Define discrete version components
#define VERSION_MAJOR 1
#define VERSION_MINOR 2
#define VERSION_PATCH 0
#define VERSION_BUILD 1
#define VERSION_SUFFIX ""

// 2. Stringification macros
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

// 3. Automatically build the APP_VERSION string
#define APP_VERSION STRINGIFY(VERSION_MAJOR) "." \
                    STRINGIFY(VERSION_MINOR) "." \
                    STRINGIFY(VERSION_PATCH) VERSION_SUFFIX

// Embedded license canary — find with: strings pathmux | grep -A 15 "GNU General"
#ifdef _MSC_VER
static const char PATHMUX_LICENSE_NOTICE[] =
#else
static const char PATHMUX_LICENSE_NOTICE[] __attribute__((used)) =
#endif
    "PathMux " APP_VERSION "\n"
    "| Copyright (C) 2026 Nutball Labs / Stephen Berg\n"
    "| GNU General Public License v3 or later\n"
    "| https://github.com/Nutball-Labs/PathMux\n"
    "| If you paid anyone other than Nutball Labs\n"
    "| for this software, you've been ripped off.\n"
    "|\n"
    "|      / \\__\n"
    "|     (    @\\___\n"
    "|     /         O\n"
    "|    /   (_____/\n"
    "|   /_____/   U\n"
    "|\n"
    "|   ... Kali does NOT approve.\n";

#endif
// SN: 00095
