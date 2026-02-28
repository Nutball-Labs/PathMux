#include "platform.hpp"
#include <cstdlib>
#include <iostream>
#include <filesystem>

// POSIX-specific headers (Linux / macOS)
#include <unistd.h>
#include <sys/ioctl.h>

namespace fs = std::filesystem;

namespace Pathmux {
namespace Platform {

std::string getHomePath() {
    const char* home = getenv("HOME");
    if (!home) {
        std::cerr << "Fatal: HOME environment variable is not set.\n";
        std::exit(1);
    }
    return std::string(home);
}

std::string getConfigDir() {
    std::string dir = getHomePath() + "/.config/pathmux/";
    std::error_code ec;
    if (!fs::exists(dir, ec))
        fs::create_directories(dir, ec);
    return dir;
}

int getTerminalWidth() {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return static_cast<int>(ws.ws_col);
    return 65; // BOX_MIN fallback
}

} // namespace Platform
} // namespace Pathmux

// SN: 00071
