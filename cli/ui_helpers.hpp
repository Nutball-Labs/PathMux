#ifndef UI_HELPERS_HPP
#define UI_HELPERS_HPP

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <climits>
#include <filesystem>
#include <unistd.h>

// Pure math/format helpers (no platform dependencies)
#include "format_helpers.hpp"
// Platform abstraction (terminal width)
#include "platform.hpp"

// ---------------------------------------------------------------------------
// Terminal UI helpers -- plain ASCII box drawing using / - \ | characters.
// Works reliably across all terminals, fonts, and locales.
// ---------------------------------------------------------------------------
namespace UI {

using Pathmux::truncate;
using Pathmux::utf8DisplayWidth;

constexpr char CORNER = '+';  // all four corners
constexpr char H      = '=';  // outer border horizontal (top / bottom)
constexpr char HD     = '-';  // inner divider horizontal
constexpr char V      = '|';  // vertical border

constexpr int BOX_MIN = 65;   // hard-deck: matches pre-dynamic baseline
constexpr int BOX_MAX = 120;  // cap: avoid absurd widths on huge monitors

// boxWidth() -- queries terminal width at call time so menus respond to
// resize.  Falls back to BOX_MIN if terminal size can't be determined
// (e.g. output is piped or redirected).
inline int boxWidth() {
    int w = Pathmux::Platform::getTerminalWidth();
    if (w < BOX_MIN) w = BOX_MIN;
    if (w > BOX_MAX) w = BOX_MAX;
    return w;
}

inline int innerWidth() {
    return boxWidth() - 6;  // 1(|) + 2(spaces) + content + 2(spaces) + 1(|)
}

// ---------------------------------------------------------------------------
// printTop, printBottom, printDivider
// ---------------------------------------------------------------------------
inline void printTop() {
    std::cout << CORNER << std::string(boxWidth() - 2, H) << CORNER << "\n";
}
inline void printBottom() {
    std::cout << CORNER << std::string(boxWidth() - 2, H) << CORNER << "\n";
}
inline void printDivider() {
    std::cout << CORNER << std::string(boxWidth() - 2, HD) << CORNER << "\n";
}

// ---------------------------------------------------------------------------
// printLine — one interior row: "|  <content padded to innerWidth()>  |"
// ---------------------------------------------------------------------------
inline void printLine(const std::string& text = "") {
    std::string content = truncate(text, innerWidth());
    int pad = innerWidth() - (int)content.size();
    std::cout << V << "  " << content << std::string(pad, ' ') << "  " << V << "\n";
}

// ---------------------------------------------------------------------------
// printCenteredLine — interior row with title centered left-to-right
// ---------------------------------------------------------------------------
inline void printCenteredLine(const std::string& text = "") {
    std::string content = truncate(text, innerWidth());
    int leftPad  = (innerWidth() - (int)content.size()) / 2;
    if (leftPad < 0) leftPad = 0;
    int rightPad = innerWidth() - (int)content.size() - leftPad;
    if (rightPad < 0) rightPad = 0;
    std::cout << V << "  " << std::string(leftPad, ' ')
              << content << std::string(rightPad, ' ') << "  " << V << "\n";
}

// ---------------------------------------------------------------------------
// printTitle — top + left-aligned title line + divider
// ---------------------------------------------------------------------------
inline void printTitle(const std::string& title) {
    printTop();
    printLine(title);
    printDivider();
}

// ---------------------------------------------------------------------------
// printTitleWithStatus — top + title left / status right on same line + divider
// ---------------------------------------------------------------------------
inline void printTitleWithStatus(const std::string& title, const std::string& status) {
    printTop();
    int inner = innerWidth();
    int used = utf8DisplayWidth(title) + utf8DisplayWidth(status);
    int pad  = inner - used;
    if (pad < 1) pad = 1;
    std::cout << V << "  " << title << std::string(pad, ' ') << status << "  " << V << "\n";
    printDivider();
}

// ---------------------------------------------------------------------------
// printCenteredTitle — top + centered title line + divider
// ---------------------------------------------------------------------------
inline void printCenteredTitle(const std::string& title) {
    printTop();
    printCenteredLine(title);
    printDivider();
}

// ---------------------------------------------------------------------------
// printFooter — divider + footer line(s) + bottom.
// If the footer string exceeds innerWidth(), splits on "   " (triple space)
// between options and wraps to additional lines rather than truncating.
// ---------------------------------------------------------------------------
inline void printFooter(const std::string& footer) {
    printDivider();
    if ((int)footer.size() <= innerWidth()) {
        printLine(footer);
    } else {
        // Split on triple-space separators between options
        std::vector<std::string> tokens;
        std::string rem = footer;
        const std::string sep = "   ";
        size_t pos;
        while ((pos = rem.find(sep)) != std::string::npos) {
            tokens.push_back(rem.substr(0, pos));
            rem = rem.substr(pos + sep.size());
        }
        tokens.push_back(rem);

        // Greedily pack tokens onto lines
        std::string line;
        for (const auto& tok : tokens) {
            std::string candidate = line.empty() ? tok : line + "   " + tok;
            if ((int)candidate.size() <= innerWidth()) {
                line = candidate;
            } else {
                printLine(line);
                line = tok;
            }
        }
        if (!line.empty()) printLine(line);
    }
    printBottom();
}

// ---------------------------------------------------------------------------
// warnMissingTools — checks ffmpeg and exiftool availability and prints
// prominent warnings if either can't be found.  Call after printTitle()
// in any menu that may invoke those tools.  Bare names are resolved via PATH;
// absolute paths are checked directly with access(X_OK).
// ---------------------------------------------------------------------------
inline void warnMissingTools(const std::string& ffmpegPath,
                              const std::string& exiftoolPath) {
    auto check = [](const std::string& configured,
                    const std::string& bare) -> bool {
        const std::string& path = configured.empty() ? bare : configured;
        if (path.find('/') != std::string::npos)
            return access(path.c_str(), X_OK) == 0;
        std::string cmd = "command -v " + path + " >/dev/null 2>&1";
        return system(cmd.c_str()) == 0;
    };

    bool ffmpegOk   = check(ffmpegPath,   "ffmpeg");
    bool exiftoolOk = check(exiftoolPath, "exiftool");

    if (!ffmpegOk || !exiftoolOk) {
        // Full-width warning block — hard to miss
        int bw = boxWidth();
        std::string bar(bw - 2, '!');
        std::cout << CORNER << bar << CORNER << "\n";
        printCenteredLine("*** CONFIGURATION WARNING ***");
        printLine();
        if (!ffmpegOk)
            printCenteredLine("\u26a0  ffmpeg not found -- video build will NOT work");
        if (!exiftoolOk)
            printCenteredLine("\u26a0  exiftool not found -- GPS extraction unavailable");
        printLine();
        printCenteredLine("Use [F]/[E] in Preferences to set correct paths");
        std::cout << CORNER << bar << CORNER << "\n";
    }
}

// ---------------------------------------------------------------------------
// readCommand — prints "PathMux: " prompt and reads a line, retrying on
// blank input so the user gets a fresh prompt without a full menu redraw.
// Returns the trimmed, non-empty input string.
// ---------------------------------------------------------------------------
inline std::string readCommand() {
    while (true) {
        std::cout << "PathMux: " << std::flush;
        std::string input;
        std::getline(std::cin, input);
        // Trim leading whitespace
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start != std::string::npos)
            return input.substr(start);
        // Blank — just reprint the prompt
    }
}

// ---------------------------------------------------------------------------
// waitEnter — print "Press Enter to continue..." and wait for bare Enter.
// Does NOT skip blank input — bare Enter proceeds immediately.
// ---------------------------------------------------------------------------
inline void waitEnter() {
    std::cout << "\n  Press Enter to continue..." << std::flush;
    std::string dummy;
    std::getline(std::cin, dummy);
}

// ---------------------------------------------------------------------------
// promptLine — prompt with optional default, bare Enter returns default.
// Does NOT use cin >> ws — safe to call after readCommand/getline.
// ---------------------------------------------------------------------------
inline std::string promptLine(const std::string& label,
                               const std::string& defaultVal = "") {
    if (defaultVal.empty())
        std::cout << "  " << label << ": " << std::flush;
    else
        std::cout << "  " << label << " [" << defaultVal << "]: " << std::flush;
    std::string input;
    std::getline(std::cin, input);
    // Trim trailing whitespace
    while (!input.empty() && (input.back() == '\r' || input.back() == ' '))
        input.pop_back();
    return input.empty() ? defaultVal : input;
}

// ---------------------------------------------------------------------------
// promptString
// ---------------------------------------------------------------------------
inline std::string promptString(const std::string& label,
                                 const std::string& defaultVal = "") {
    if (defaultVal.empty())
        std::cout << label << ": ";
    else
        std::cout << label << " [" << defaultVal << "]: ";

    std::string input;
    std::getline(std::cin >> std::ws, input);
    if (input.empty() && !defaultVal.empty()) return defaultVal;
    return input;
}

// ---------------------------------------------------------------------------
// promptInt
// ---------------------------------------------------------------------------
inline int promptInt(const std::string& label,
                      int defaultVal,
                      int minVal = INT_MIN,
                      int maxVal = INT_MAX) {
    while (true) {
        std::cout << label << " [" << defaultVal << "]: ";
        std::string input;
        std::getline(std::cin >> std::ws, input);
        if (input.empty()) return defaultVal;
        try {
            int v = std::stoi(input);
            if (v < minVal || v > maxVal) {
                std::cout << "  Value must be between " << minVal
                          << " and " << maxVal << ".\n";
                continue;
            }
            return v;
        } catch (...) {
            std::cout << "  Please enter a valid integer.\n";
        }
    }
}

// ---------------------------------------------------------------------------
// promptChoice — numbered list, returns selected index.
// ---------------------------------------------------------------------------
inline int promptChoice(const std::string& label,
                         const std::vector<std::string>& options,
                         int currentIdx) {
    std::cout << label << "\n";
    for (int i = 0; i < (int)options.size(); ++i)
        std::cout << "  [" << (i + 1) << "] " << options[i]
                  << (i == currentIdx ? "  <--" : "") << "\n";
    std::cout << "  Select [1-" << options.size()
              << "] or Enter to keep current: " << std::flush;
    std::string input;
    std::getline(std::cin, input);  // bare getline — Enter returns current
    if (input.empty() || input.find_first_not_of(" \t\r\n") == std::string::npos)
        return currentIdx;
    try {
        int v = std::stoi(input) - 1;
        if (v >= 0 && v < (int)options.size()) return v;
    } catch (...) {}
    std::cout << "  Invalid selection -- keeping current.\n";
    return currentIdx;
}

// ---------------------------------------------------------------------------
// confirmExists — warn if a path doesn't exist (for directories/files)
// ---------------------------------------------------------------------------
inline void confirmExists(const std::string& path) {
    if (path.empty()) return;
    std::ifstream test(path);
    if (!test.good())
        std::cout << "  Warning: path does not exist or is not accessible: "
                  << path << "\n";
}

// ---------------------------------------------------------------------------
// confirmExecutable — warn if a path is not a regular executable file.
// Bare names (no '/') are skipped — they rely on PATH resolution at runtime.
// ---------------------------------------------------------------------------
inline void confirmExecutable(const std::string& path) {
    if (path.empty()) return;
    if (path.find('/') == std::string::npos) return; // bare name, trust PATH
    if (access(path.c_str(), X_OK) != 0)
        std::cout << "  Warning: not found or not executable: " << path << "\n";
}

// ---------------------------------------------------------------------------
// confirmOutputPath — if path exists, find next free name by appending .1, .2
// etc. before the extension. Returns original path if it doesn't exist.
// ---------------------------------------------------------------------------
inline std::string confirmOutputPath(const std::string& path) {
    if (!std::filesystem::exists(path)) return path;
    auto dotPos = path.rfind('.');
    std::string base = (dotPos != std::string::npos) ? path.substr(0, dotPos) : path;
    std::string ext  = (dotPos != std::string::npos) ? path.substr(dotPos)    : "";
    for (int n = 1; n < 100; ++n) {
        std::string candidate = base + "." + std::to_string(n) + ext;
        if (!std::filesystem::exists(candidate)) return candidate;
    }
    return base + ".99" + ext;
}

} // namespace UI

#endif
// SN: 00071
