// pm_findgpslock — scan raw dashcam .ts files and report GPS lock acquisition
//
// For each .ts file prints a "# filename  parsed-timestamp" header line,
// then one packet line per GPS sample until the first fully-locked sample
// (valid lat/lon AND synchronized clock), then moves on to the next file.
//
// Normal output is two lines per file — the header and sample 0 already
// locked. Files where GPS was still acquiring show extra pre-lock lines
// labelled NO_POS, NO_TIME, or NO_POS+NO_TIME before the LOCKED line.
//
// Usage:
//   pm_findgpslock [--verbose] <file.ts> [file2.ts ...]
//
// Exit 0: at least one file processed without error.
// Exit 1: argument/file error.

#include "pathmux.hpp"
#include "json.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cstdio>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace Pathmux;

// ---------------------------------------------------------------------------
// printUsage
// ---------------------------------------------------------------------------
static void printUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << " [--verbose] <file.ts> [file2.ts ...]\n\n"
        << "  --verbose  Show exiftool stderr during scan\n\n"
        << "  Prints a header line per file, then GPS samples until the\n"
        << "  first locked reading. Normal output: header + sample 0.\n"
        << "  Outlier files grow downward with pre-lock labels.\n";
}

// ---------------------------------------------------------------------------
// tsFromFilename — "20260226_084009F.ts" → "2026-02-26 08:40:09"
// Returns empty string if the filename doesn't match the expected pattern.
// ---------------------------------------------------------------------------
static std::string tsFromFilename(const std::string& fname)
{
    // Expect at least YYYYMMDD_HHMMSS (15 chars) at the start
    if (fname.size() < 15 || fname[8] != '_') return "";
    for (int i = 0; i < 8; ++i)
        if (!std::isdigit(static_cast<unsigned char>(fname[i]))) return "";
    for (int i = 9; i < 15; ++i)
        if (!std::isdigit(static_cast<unsigned char>(fname[i]))) return "";

    return fname.substr(0, 4) + "-" + fname.substr(4, 2) + "-" + fname.substr(6, 2)
         + " " + fname.substr(9, 2) + ":" + fname.substr(11, 2) + ":" + fname.substr(13, 2);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    if (argc < 2) { printUsage(argv[0]); return 1; }

    bool verbose = false;
    std::vector<std::string> files;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")    { printUsage(argv[0]); return 0; }
        else if (arg == "--verbose")           { verbose = true; }
        else if (arg[0] != '-')                { files.push_back(arg); }
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (files.empty()) {
        std::cerr << "Error: at least one .ts file required.\n";
        printUsage(argv[0]);
        return 1;
    }

    for (const auto& f : files) {
        if (!fs::exists(f)) {
            std::cerr << "Error: file not found: " << f << "\n";
            return 1;
        }
    }

    // ---- Exiftool command prefix from PathMux prefs ----
    ConfigManager config;
    const std::string exifCmd = config.getExiftoolPath() + " " + config.getExiftoolOptions();

    // ---- Process each file ----
    for (const auto& path : files) {
        std::string fname = fs::path(path).filename().string();
        std::string fts   = tsFromFilename(fname);

        // Header line
        std::cout << "# " << fname;
        if (!fts.empty()) std::cout << "  " << fts;
        std::cout << "\n";

        std::string cmd = exifCmd
                          + (verbose ? "" : " -q")
                          + " \"" + path + "\""
                          + (verbose ? "" : " 2>/dev/null");

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            std::cerr << "Error: popen failed — is exiftool installed?\n";
            return 1;
        }

        char linebuf[512];
        int  idx    = 0;
        bool locked = false;

        while (fgets(linebuf, sizeof(linebuf), pipe) && !locked) {
            std::string line(linebuf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'
                                     || line.back() == ' '))
                line.pop_back();
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string datePart, timePart;
            double lat, lon, alt, speed, heading, accelX, accelY, accelZ;

            if (!(iss >> datePart >> timePart >> lat >> lon >> alt >> speed >> heading
                      >> accelX >> accelY >> accelZ))
                continue;

            bool noPos  = (lat == 0.0 && lon == 0.0);
            int  year   = (datePart.size() >= 4) ? std::stoi(datePart.substr(0, 4)) : 0;
            bool noTime = (year < 2000);

            // Reformat YYYY:MM:DD → YYYYMMDD for compact, consistent output
            std::string dateCompact = (datePart.size() >= 10)
                ? datePart.substr(0, 4) + datePart.substr(5, 2) + datePart.substr(8, 2)
                : datePart;
            std::string ts = dateCompact + " " + timePart;
            std::string status;
            if      (noPos && noTime) status = "NO_POS+NO_TIME";
            else if (noPos)           status = "NO_POS";
            else if (noTime)          status = "NO_TIME";
            else                      { status = "LOCKED"; locked = true; }

            std::cout << "  " << std::setw(4) << idx << "  "
                      << ts << "  "
                      << std::fixed << std::setprecision(6)
                      << std::setw(11) << lat << "  "
                      << std::setw(11) << lon << "  "
                      << status << "\n";
            ++idx;
        }
        pclose(pipe);

        if (!locked)
            std::cout << "  (no lock found in " << idx << " sample(s))\n";
    }

    return 0;
}
// SN: 00081
