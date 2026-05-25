// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef VERSION_HPP
#define VERSION_HPP

// Global application identifiers
#define APP_NAME "CamClops Dashcam Explorer"

// 1. Define discrete version components
#define VERSION_MAJOR 2
#define VERSION_MINOR 9
#define VERSION_PATCH 2
#define VERSION_BUILD 0
#define VERSION_SUFFIX "a"
#define VERSION_HWM "00123"   // SN high-water mark at build time; update with each release

// 2. Stringification macros
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

// 3. Automatically build the APP_VERSION string
#define APP_VERSION STRINGIFY(VERSION_MAJOR) "." \
                    STRINGIFY(VERSION_MINOR) "." \
                    STRINGIFY(VERSION_PATCH) VERSION_SUFFIX

// Embedded license canary — find with: strings camclops | grep -A 15 "GNU General"
#ifdef _MSC_VER
static const char CAMCLOPS_LICENSE_NOTICE[] =
#else
static const char CAMCLOPS_LICENSE_NOTICE[] __attribute__((used)) =
#endif
    "CamClops " APP_VERSION ", HWM " VERSION_HWM "\n"
    "| Copyright (C) 2026 Nutball Labs / Stephen Berg\n"
    "| GNU General Public License v3 or later\n"
    "| https://github.com/Nutball-Labs/CamClops\n"
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
// SN: 00123
