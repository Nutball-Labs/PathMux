#ifndef PLATFORM_HPP
#define PLATFORM_HPP

// ---------------------------------------------------------------------------
// platform.hpp — OS abstraction layer.
// Isolates POSIX/Windows differences so library code stays portable.
// Currently implemented for Linux/POSIX only; Windows stubs are comments.
// ---------------------------------------------------------------------------

#include <string>

namespace Pathmux {
namespace Platform {

// Returns the user's home directory path (no trailing slash).
// Linux/macOS: $HOME
// Windows: %USERPROFILE%
// Exits with a fatal error if the value cannot be determined.
std::string getHomePath();

// Returns the pathmux configuration directory path (with trailing slash).
// Linux: ~/.config/pathmux/
// macOS: ~/Library/Application Support/pathmux/
// Windows: %APPDATA%/pathmux/
// Creates the directory if it does not exist.
std::string getConfigDir();

// Returns the current terminal width in columns.
// Linux/POSIX: ioctl(TIOCGWINSZ)
// Windows: GetConsoleScreenBufferInfo
// Falls back to 65 if terminal width cannot be determined (e.g. piped output).
int getTerminalWidth();

} // namespace Platform
} // namespace Pathmux

#endif
// SN: 00071
