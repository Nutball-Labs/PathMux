// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
// clops_findgpslock — scan raw dashcam .ts files and report GPS lock acquisition
//
// For each .ts file prints a "# filename  parsed-timestamp" header line,
// then the first GPS-locked sample with lock offset from file start (+Ns).
//
// The D90 firmware does not write GPS records to the LIGO stream until
// lock is acquired, so there are no pre-lock packets to scan.  The offset
// is computed by comparing the GPS lock epoch (UTC, via timegm) against
// the file-start epoch derived from the filename (local, via mktime).
//
// Usage:
//   clops_findgpslock [--verbose] <file.ts> [file2.ts ...]
//
// Exit 0: at least one file processed without error.
// Exit 1: argument/file error.

#include "camclops.hpp"
#include "compat.hpp"
#include "json.hpp"
#include "version.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cstdio>
#include <ctime>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace CamClops;

// ---------------------------------------------------------------------------
// printUsage
// ---------------------------------------------------------------------------
static void printUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << " [--verbose] <file.ts> [file2.ts ...]\n\n"
        << "  -v, --version  Show version and exit\n"
        << "  --verbose      Show exiftool stderr during scan\n\n"
        << "  Prints a header line per file, then the first locked GPS\n"
        << "  record with lock offset (+Ns) from file start.\n";
}

// ---------------------------------------------------------------------------
// tsFromFilename — "20260226_084009F.ts" → "2026-02-26 08:40:09"
// Returns empty string if the filename doesn't match the expected pattern.
// ---------------------------------------------------------------------------
static std::string tsFromFilename(const std::string& fname)
{
    if (fname.size() < 15 || fname[8] != '_') return "";
    for (int i = 0; i < 8; ++i)
        if (!std::isdigit(static_cast<unsigned char>(fname[i]))) return "";
    for (int i = 9; i < 15; ++i)
        if (!std::isdigit(static_cast<unsigned char>(fname[i]))) return "";

    return fname.substr(0, 4) + "-" + fname.substr(4, 2) + "-" + fname.substr(6, 2)
         + " " + fname.substr(9, 2) + ":" + fname.substr(11, 2) + ":" + fname.substr(13, 2);
}

// ---------------------------------------------------------------------------
// filenameToEpoch — "20260226_084009F.ts" → time_t (local time, via mktime)
// Mirrors the library's stringToTimestamp().  Returns 0 on parse failure.
// ---------------------------------------------------------------------------
static time_t filenameToEpoch(const std::string& fname)
{
    if (fname.size() < 15 || fname[8] != '_') return 0;
    struct tm t = {};
    std::istringstream ss(fname.substr(0, 15));
    ss >> std::get_time(&t, "%Y%m%d_%H%M%S");
    if (ss.fail()) return 0;
    t.tm_isdst = -1;
    return std::mktime(&t);
}

// ---------------------------------------------------------------------------
// gpsToEpoch — exiftool GPS date/time (UTC) → time_t via timegm
// datePart: "YYYY:MM:DD"   timePart: "HH:MM:SS"
// Returns 0 on parse failure.
// ---------------------------------------------------------------------------
static time_t gpsToEpoch(const std::string& datePart, const std::string& timePart)
{
    if (datePart.size() < 10 || timePart.size() < 8) return 0;
    struct tm t = {};
    std::string combined = datePart + " " + timePart;
    std::istringstream ss(combined);
    ss >> std::get_time(&t, "%Y:%m:%d %H:%M:%S");
    if (ss.fail()) return 0;
    return timegm(&t);
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
        else if (arg == "-v" || arg == "--version") {
            std::cout << APP_NAME << " clops_findgpslock v" << APP_VERSION << "\n";
            return 0;
        }
        else if (arg == "--verbose")           { verbose = true; }
        else if (arg[0] != '-')                { files.push_back(arg); }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
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

    // ---- Exiftool command prefix from CamClops prefs ----
    ConfigManager config;
    const std::string exifCmd = config.getExiftoolPath() + " -ee3";

    // ---- Process each file ----
    for (const auto& path : files) {
        std::string fname          = fs::path(path).filename().string();
        std::string fts            = tsFromFilename(fname);
        time_t      fileStartEpoch = filenameToEpoch(fname);

        // Header line
        std::cout << "# " << fname;
        if (!fts.empty()) std::cout << "  " << fts;
        std::cout << "\n";

        std::string cmd = exifCmd
                          + (verbose ? "" : " -q")
                          + " \"" + path + "\""
                          + (verbose ? "" : " " NULL_REDIRECT);

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            std::cerr << "Error: popen failed — is exiftool installed?\n";
            return 1;
        }

        char linebuf[512];
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

            if (noPos || noTime) continue;

            // --- GPS lock acquired ---
            locked = true;

            time_t gpsEpoch   = gpsToEpoch(datePart, timePart);
            long   lockOffset = (fileStartEpoch > 0 && gpsEpoch > 0)
                                ? static_cast<long>(gpsEpoch - fileStartEpoch)
                                : -1;

            // Reformat YYYY:MM:DD → YYYYMMDD for compact, consistent output
            std::string dateCompact = (datePart.size() >= 10)
                ? datePart.substr(0, 4) + datePart.substr(5, 2) + datePart.substr(8, 2)
                : datePart;

            std::cout << "  lock";
            if (lockOffset >= 0)
                std::cout << " +" << lockOffset << "s";
            std::cout << "  " << dateCompact << " " << timePart << "  "
                      << std::fixed << std::setprecision(6)
                      << std::setw(11) << lat << "  "
                      << std::setw(11) << lon << "\n";
        }
        pclose(pipe);

        if (!locked)
            std::cout << "  (no GPS lock found)\n";
    }

    return 0;
}
// SN: 00112
