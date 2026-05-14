// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef PLATFORM_HPP
#define PLATFORM_HPP

// ---------------------------------------------------------------------------
// platform.hpp — OS abstraction layer.
// Isolates POSIX/Windows differences so library code stays portable.
// Currently implemented for Linux/POSIX only; Windows stubs are comments.
// ---------------------------------------------------------------------------

#include <string>

namespace CamClops {
namespace Platform {

// Returns the user's home directory path (no trailing slash).
// Linux/macOS: $HOME
// Windows: %USERPROFILE%
// Exits with a fatal error if the value cannot be determined.
std::string getHomePath();

// Returns the camclops configuration directory path (with trailing slash).
// Linux: ~/.config/camclops/
// macOS: ~/Library/Application Support/camclops/
// Windows: %APPDATA%/camclops/
// Creates the directory if it does not exist.
std::string getConfigDir();

// Returns the current terminal width in columns.
// Linux/POSIX: ioctl(TIOCGWINSZ)
// Windows: GetConsoleScreenBufferInfo
// Falls back to 65 if terminal width cannot be determined (e.g. piped output).
int getTerminalWidth();

} // namespace Platform
} // namespace CamClops

#endif
// SN: 00089
