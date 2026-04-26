// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
// pm_probe — camera compatibility profiler (PathMux suite)
//
// Usage:
//   pm_probe <file.ts>               Probe a single segment
//   pm_probe MID:TID                 Probe first Front segment of a known trip
//   pm_probe --card <path>           Fingerprint a full dashcam storage root
//   pm_probe --card <path> --json    Machine-readable fingerprint
//   pm_probe --json <file.ts>        Machine-readable single-file probe
//
// Single-file mode reports video characteristics and GPS metadata format.
// Card mode fingerprints the full storage root and produces a report
// suitable for filing a GitHub issue to request support for a new camera.

#include "pathmux.hpp"
#include "platform.hpp"
#include "compat.hpp"
#include "json.hpp"
#include "version.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <set>
#include <map>
#include <regex>
#include <cstdlib>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace Pathmux;

// ---------------------------------------------------------------------------
// Structures
// ---------------------------------------------------------------------------

struct StreamInfo {
    int         index      = 0;
    std::string codecType;      // "video", "audio", "data"
    std::string codecName;      // "h264", "aac", "bin_data"
    std::string codecTag;       // "LIGO", "mp4a", etc.
    std::string sampleRate;     // audio only, e.g. "48000"
    int         channels   = 0; // audio only
};

struct GpsInfo {
    std::string method;         // "LIGOGPSINFO", "standard", "none"
    int         streamIndex = -1;
    std::string exiftoolNote;   // version requirement if any
    std::string firstTimestamp;
    double      firstLat    = 0.0;
    double      firstLon    = 0.0;
    bool        hasFix      = false;
};

struct FileProbe {
    std::string filePath;
    std::string container;      // "mpegts", "mov,mp4,..."
    std::string containerExt;   // ".ts", ".mp4"
    int         durationSec = 0;
    VideoProfile video;
    std::vector<StreamInfo> streams;
    GpsInfo     gps;
};

// ---------------------------------------------------------------------------
// printUsage
// ---------------------------------------------------------------------------
static void printUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << " [--json] <file.ts>\n"
        << "       " << argv0 << " [--json] MID:TID\n"
        << "       " << argv0 << " --card <path> [--json]\n"
        << "       " << argv0 << " --wizard <path>\n\n"
        << "  <file.ts>      Probe a single segment\n"
        << "  MID:TID        Probe first Front segment of a known trip\n"
        << "  --card PATH    Fingerprint a dashcam storage root\n"
        << "  --json         Machine-readable JSON output\n"
        << "  --wizard PATH  Interactive camera profile builder\n\n"
        << "  --card output is suitable for a GitHub issue to request\n"
        << "  support for a new camera model.\n\n"
        << "  -v, --version  Show version and exit\n";
}

// ---------------------------------------------------------------------------
// normalizeId
// ---------------------------------------------------------------------------
static std::string normalizeId(const std::string& s)
{
    std::string out;
    for (char c : s) {
        char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if      (u == 'O') u = '0';
        else if (u == 'I') u = '1';
        else if (u == 'L') u = '1';
        out += u;
    }
    return out;
}

// ---------------------------------------------------------------------------
// resolveMidTid — MID:TID → absolute path of first Front segment
// ---------------------------------------------------------------------------
static std::string resolveMidTid(const std::string& midtid)
{
    auto colon = midtid.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == midtid.size()) {
        std::cerr << "Error: '" << midtid << "' is not a valid MID:TID address.\n";
        return "";
    }
    std::string mid = normalizeId(midtid.substr(0, colon));
    std::string tid = normalizeId(midtid.substr(colon + 1));

    ConfigManager config;
    for (const auto& entry : config.loadManifestIndex()) {
        if (normalizeId(entry.id) != mid) continue;
        for (const auto& trip : config.loadTripCache(entry.manifestFile)) {
            if (normalizeId(trip.id) != tid) continue;
            if (trip.segments.empty()) {
                std::cerr << "Error: " << midtid << " has no segments.\n";
                return "";
            }
            return camPath(trip.segments[0], "front");
        }
        std::cerr << "Error: trip '" << tid << "' not found in '" << mid << "'.\n";
        return "";
    }
    std::cerr << "Error: manifest '" << mid << "' not found.\n";
    return "";
}

// ---------------------------------------------------------------------------
// runCmd — run a shell command and return stdout as a string
// ---------------------------------------------------------------------------
static std::string runCmd(const std::string& cmd)
{
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    std::string out;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe))
        out += buf;
    pclose(pipe);
    return out;
}

// ---------------------------------------------------------------------------
// probeStreams — get all streams from a file via ffprobe JSON
// ---------------------------------------------------------------------------
static std::vector<StreamInfo> probeStreams(const std::string& filePath,
                                             const std::string& ffprobePath)
{
    std::vector<StreamInfo> result;
    std::string cmd = ffprobePath
        + " -v quiet -print_format json"
        + " -show_entries stream=index,codec_type,codec_name,codec_tag_string,sample_rate,channels"
        + " \"" + filePath + "\" " NULL_REDIRECT;

    std::string raw = runCmd(cmd);
    if (raw.empty()) return result;

    try {
        json j = json::parse(raw);
        if (!j.contains("streams")) return result;
        for (const auto& s : j["streams"]) {
            StreamInfo si;
            si.index      = s.value("index",            0);
            si.codecType  = s.value("codec_type",       "");
            si.codecName  = s.value("codec_name",       "");
            si.codecTag   = s.value("codec_tag_string", "");
            si.sampleRate = s.value("sample_rate",      "");
            si.channels   = s.value("channels",         0);
            result.push_back(si);
        }
    } catch (...) {}
    return result;
}

// ---------------------------------------------------------------------------
// probeContainerAndDuration — get format_name and duration via ffprobe
// ---------------------------------------------------------------------------
static void probeContainerAndDuration(const std::string& filePath,
                                       const std::string& ffprobePath,
                                       std::string& container,
                                       int& durationSec)
{
    std::string cmd = ffprobePath
        + " -v quiet -print_format json"
        + " -show_entries format=format_name,duration"
        + " \"" + filePath + "\" " NULL_REDIRECT;

    std::string raw = runCmd(cmd);
    if (raw.empty()) return;
    try {
        json j = json::parse(raw);
        if (j.contains("format")) {
            container   = j["format"].value("format_name", "");
            std::string d = j["format"].value("duration", "0");
            try { durationSec = (int)std::round(std::stod(d)); } catch (...) {}
        }
    } catch (...) {}
}

// ---------------------------------------------------------------------------
// detectGpsMethod — examine streams and probe exiftool for GPS info
// ---------------------------------------------------------------------------
static GpsInfo detectGps(const std::vector<StreamInfo>& streams,
                          const std::string& filePath,
                          const std::string& exiftoolPath,
                          const std::string& exiftoolOptions)
{
    GpsInfo g;
    g.method = "none";

    // Pass 1: check stream codec_tag for explicit LIGO marker
    for (const auto& s : streams) {
        if (s.codecType == "data" && s.codecTag.find("LIGO") != std::string::npos) {
            g.method      = "LIGOGPSINFO";
            g.streamIndex = s.index;
            g.exiftoolNote = "GPS in private LIGO stream — requires exiftool with LIGOGPSINFO support";
            break;
        }
    }

    // Pass 2: if not found by tag, ask exiftool to list group names (-G1 -s -ee3)
    // and look for a [LIGO] group — more reliable than stream tag alone.
    if (g.method == "none") {
        std::string tagCmd = exiftoolPath + " -ee3 -G1 -s \""
                           + filePath + "\" 2>/dev/null";
        FILE* tp = popen(tagCmd.c_str(), "r");
        if (tp) {
            char tbuf[512];
            while (fgets(tbuf, sizeof(tbuf), tp)) {
                std::string tline(tbuf);
                if (tline.find("[LIGO]") != std::string::npos) {
                    g.method       = "LIGOGPSINFO";
                    g.exiftoolNote = "GPS in private LIGO stream — requires exiftool with LIGOGPSINFO support";
                    break;
                }
            }
            pclose(tp);
        }
    }

    // Pass 3: run format-string extraction to grab a sample fix and confirm.
    // Also catches cameras with standard GPS metadata (no data stream, no LIGO).
    std::string cmd = exiftoolPath + " " + exiftoolOptions
        + " \"" + filePath + "\" " NULL_REDIRECT;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return g;

    char linebuf[512];
    while (fgets(linebuf, sizeof(linebuf), pipe)) {
        std::string line(linebuf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'
                                 || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string datePart, timePart;
        double lat, lon, alt, speed, heading, ax, ay, az;
        if (!(iss >> datePart >> timePart >> lat >> lon >> alt >> speed >> heading
                  >> ax >> ay >> az))
            continue;
        if (lat == 0.0 && lon == 0.0) continue;

        g.hasFix          = true;
        g.firstTimestamp  = datePart + " " + timePart;
        g.firstLat        = lat;
        g.firstLon        = lon;

        // If still unidentified after both stream checks, call it standard GPS
        if (g.method == "none")
            g.method = "standard";
        break;
    }
    pclose(pipe);

    return g;
}

// ---------------------------------------------------------------------------
// containerLabel — human-readable container name
// ---------------------------------------------------------------------------
static std::string containerLabel(const std::string& formatName,
                                   const std::string& ext)
{
    if (formatName.find("mpegts") != std::string::npos) return "MPEG-TS (" + ext + ")";
    if (formatName.find("mp4")    != std::string::npos) return "MPEG-4 ("  + ext + ")";
    if (formatName.find("avi")    != std::string::npos) return "AVI ("     + ext + ")";
    if (formatName.find("mov")    != std::string::npos) return "QuickTime (" + ext + ")";
    return formatName.empty() ? ext : formatName + " (" + ext + ")";
}

// ---------------------------------------------------------------------------
// probeFile — full single-file probe
// ---------------------------------------------------------------------------
static FileProbe probeFile(const std::string& filePath,
                            const std::string& ffprobePath,
                            const std::string& exiftoolPath,
                            const std::string& exiftoolOptions)
{
    FileProbe p;
    p.filePath = filePath;

    // Extension
    auto dot = filePath.rfind('.');
    p.containerExt = (dot != std::string::npos) ? filePath.substr(dot) : "";

    // Video profile via ffprobe CSV — parse first line only
    {
        std::string cmd = ffprobePath +
            " -v error -select_streams v:0"
            " -show_entries stream=width,height,pix_fmt,color_range,color_space,r_frame_rate"
            " -of csv=p=0"
            " \"" + filePath + "\" " NULL_REDIRECT;
        std::string raw = runCmd(cmd);
        // Extract first non-empty line to avoid multi-row bleed
        std::string firstLine;
        {
            std::istringstream tmp(raw);
            while (std::getline(tmp, firstLine)) {
                while (!firstLine.empty() && (firstLine.back() == '\r' || firstLine.back() == ' '))
                    firstLine.pop_back();
                if (!firstLine.empty()) break;
            }
        }
        std::istringstream ss(firstLine);
        std::string tok;
        std::vector<std::string> fields;
        while (std::getline(ss, tok, ',')) {
            while (!tok.empty() && (tok.back() == '\n' || tok.back() == '\r'
                                    || tok.back() == ' '))
                tok.pop_back();
            fields.push_back(tok);
        }
        if (fields.size() >= 1 && !fields[0].empty())
            try { p.video.width  = std::stoi(fields[0]); } catch (...) {}
        if (fields.size() >= 2 && !fields[1].empty())
            try { p.video.height = std::stoi(fields[1]); } catch (...) {}
        if (fields.size() >= 3 && !fields[2].empty()) p.video.pixFmt     = fields[2];
        if (fields.size() >= 4 && !fields[3].empty()) p.video.colorRange = fields[3];
        if (fields.size() >= 5 && !fields[4].empty()) p.video.colorSpace = fields[4];
        if (fields.size() >= 6 && !fields[5].empty()) p.video.frameRate  = fields[5];
    }

    // Container and duration
    probeContainerAndDuration(filePath, ffprobePath, p.container, p.durationSec);

    // All streams
    p.streams = probeStreams(filePath, ffprobePath);

    // GPS
    p.gps = detectGps(p.streams, filePath, exiftoolPath, exiftoolOptions);

    return p;
}

// ---------------------------------------------------------------------------
// printFileProbe — text output for single-file mode
// ---------------------------------------------------------------------------
static void printFileProbe(const FileProbe& p)
{
    // Basename for display
    std::string fname = pathBasename(p.filePath);

    std::cout << "File:         " << p.filePath << "\n"
              << "Container:    " << containerLabel(p.container, p.containerExt) << "\n"
              << "Resolution:   " << p.video.width << "x" << p.video.height << "\n"
              << "Frame rate:   " << Pathmux::formatFrameRate(p.video.frameRate) << "\n"
              << "Pixel format: " << p.video.pixFmt;
    if (p.video.colorRange == "pc" || p.video.colorRange == "jpeg")
        std::cout << "  (full-range)";
    std::cout << "\n"
              << "Color space:  " << p.video.colorSpace << "\n";
    if (p.durationSec > 0)
        std::cout << "Duration:     " << p.durationSec << "s\n";

    if (!p.streams.empty()) {
        std::cout << "Streams:     ";
        for (const auto& s : p.streams) {
            std::cout << " " << s.codecType << "(" << s.index << ")";
            if (!s.codecTag.empty() && s.codecTag != "0x0000"
                    && s.codecTag != "[0][0][0][0]")
                std::cout << "[" << s.codecTag << "]";
        }
        std::cout << "\n";
    }

    if (p.gps.method == "LIGOGPSINFO") {
        std::cout << "GPS method:   LIGOGPSINFO";
        if (p.gps.streamIndex >= 0)
            std::cout << " (stream " << p.gps.streamIndex << ")";
        if (!p.gps.exiftoolNote.empty())
            std::cout << "  — " << p.gps.exiftoolNote;
        std::cout << "\n";
    } else if (p.gps.method == "standard") {
        std::cout << "GPS method:   Standard GPS metadata\n";
    } else {
        std::cout << "GPS method:   Not detected\n";
    }

    if (p.gps.hasFix) {
        std::cout << std::fixed << std::setprecision(6)
                  << "GPS first fix: " << p.gps.firstTimestamp
                  << "  lat=" << p.gps.firstLat
                  << "  lon=" << p.gps.firstLon << "\n";
    }
}

// ---------------------------------------------------------------------------
// Card mode helpers
// ---------------------------------------------------------------------------

// Returns true for hidden/system dotfiles (names starting with '.').
// Covers macOS resource forks (._foo), .DS_Store, .Spotlight-V100, etc.
// These must be skipped in every file-enumeration loop.
static bool isDotFile(const fs::path& p) {
    const std::string n = p.filename().string();
    return !n.empty() && n[0] == '.';
}

// List video files in a directory, sorted by name
static std::vector<std::string> listVideoFiles(const std::string& dir)
{
    std::vector<std::string> files;
    std::set<std::string> videoExts = {".ts", ".mp4", ".avi", ".mov", ".mkv"};
    if (!fs::is_directory(dir)) return files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (isDotFile(entry.path())) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (videoExts.count(ext))
            files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Find candidate camera directories under root.
// Depth-1 first; if none found, searches depth-2 and updates root to the
// best-matching subdirectory (prefers "video/", then first alphabetically).
static std::vector<std::string> findCameraDirs(std::string& root)
{
    std::vector<std::string> dirs;
    if (!fs::is_directory(root)) return dirs;

    // Depth-1: direct subdirs of root that contain video files
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        auto vids = listVideoFiles(entry.path().string());
        if (!vids.empty())
            dirs.push_back(entry.path().string());
    }
    if (!dirs.empty()) {
        std::sort(dirs.begin(), dirs.end());
        return dirs;
    }

    // Depth-2: group camera-like subdirs by their immediate parent
    std::map<std::string, std::vector<std::string>> byParent;
    for (const auto& entry1 : fs::directory_iterator(root)) {
        if (!entry1.is_directory()) continue;
        std::string name1 = entry1.path().filename().string();
        if (name1.empty() || name1[0] == '.') continue;
        try {
            for (const auto& entry2 : fs::directory_iterator(entry1.path())) {
                if (!entry2.is_directory()) continue;
                std::string name2 = entry2.path().filename().string();
                if (name2.empty() || name2[0] == '.') continue;
                auto vids = listVideoFiles(entry2.path().string());
                if (!vids.empty())
                    byParent[entry1.path().string()].push_back(entry2.path().string());
            }
        } catch (...) {}
    }
    if (byParent.empty()) {
        // Flat-layout fallback: video files are directly in root (no subdirs)
        if (!listVideoFiles(root).empty())
            dirs.push_back(root);
        return dirs;
    }

    // Prefer "video" parent; otherwise first alphabetically
    std::string chosen;
    for (const auto& [parent, _] : byParent) {
        std::string pname = fs::path(parent).filename().string();
        std::string plower = pname;
        std::transform(plower.begin(), plower.end(), plower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (plower == "video") { chosen = parent; break; }
    }
    if (chosen.empty()) chosen = byParent.begin()->first;

    if (byParent.size() > 1) {
        std::cerr << "Note: camera directories found under multiple subdirectories:\n";
        for (const auto& [parent, _] : byParent)
            std::cerr << "  " << fs::path(parent).filename().string() << "/\n";
        std::cerr << "Using: " << fs::path(chosen).filename().string() << "/\n";
    }

    root = chosen;
    dirs = byParent[chosen];
    std::sort(dirs.begin(), dirs.end());
    return dirs;
}

// NOTE: if findCameraDirs returns empty, callers should check for flat layout
// (video files directly in root) via listVideoFiles(root).
static bool isFlatLayout(const std::vector<std::string>& cameraDirs,
                          const std::string& scanRoot)
{
    return cameraDirs.size() == 1 &&
           fs::path(cameraDirs[0]) == fs::path(scanRoot);
}

// Detect file extensions present across a set of files
static std::vector<std::string> detectExtensions(const std::string& dir)
{
    std::set<std::string> exts;
    if (!fs::is_directory(dir)) return {};
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (isDotFile(entry.path())) continue;
        std::string ext = entry.path().extension().string();
        if (!ext.empty()) exts.insert(ext);
    }
    return std::vector<std::string>(exts.begin(), exts.end());
}

// Compute segment duration from ffprobe (seconds)
static int segDurationFfprobe(const std::string& filePath,
                                const std::string& ffprobePath)
{
    std::string cmd = ffprobePath
        + " -v error -show_entries format=duration -of csv=p=0"
        + " \"" + filePath + "\" " NULL_REDIRECT;
    std::string raw = runCmd(cmd);
    if (raw.empty()) return 0;
    try { return (int)std::round(std::stod(raw)); } catch (...) { return 0; }
}

// ---------------------------------------------------------------------------
// probeCard — SD card / dashcam storage root fingerprint
// ---------------------------------------------------------------------------
static std::string guessTimestampFormat(const std::string& filename); // defined below

static void probeCard(const std::string& root, bool jsonMode,
                       const std::string& ffprobePath,
                       const std::string& exiftoolPath,
                       const std::string& exiftoolOptions)
{
    if (!fs::is_directory(root)) {
        std::cerr << "Error: not a directory: " << root << "\n";
        return;
    }

    std::string scanRoot = root;
    auto cameraDirs = findCameraDirs(scanRoot);
    if (cameraDirs.empty()) {
        std::cerr << "No camera directories with video files found under: " << root << "\n";
        return;
    }
    if (scanRoot != root)
        std::cerr << "Note: using subdirectory: " << scanRoot << "\n";

    // Primary camera: prefer Front/, otherwise first dir found
    std::string primaryDir;
    for (const auto& d : cameraDirs) {
        std::string name = fs::path(d).filename().string();
        if (name == "Front" || name == "front" || name == "FRONT") {
            primaryDir = d; break;
        }
    }
    if (primaryDir.empty()) primaryDir = cameraDirs[0];

    auto primaryFiles = listVideoFiles(primaryDir);
    if (primaryFiles.empty()) {
        std::cerr << "No video files found in: " << primaryDir << "\n";
        return;
    }

    // Extensions across primary dir
    auto allExts = detectExtensions(primaryDir);

    // Sample up to 5 files for duration measurements
    std::vector<int> durations;
    int sampleCount = std::min((int)primaryFiles.size(), 5);
    for (int i = 0; i < sampleCount; ++i) {
        int d = segDurationFfprobe(primaryFiles[i], ffprobePath);
        if (d > 0) durations.push_back(d);
    }

    // Full probe on first segment
    FileProbe probe = probeFile(primaryFiles[0], ffprobePath,
                                exiftoolPath, exiftoolOptions);

    // Sample filenames (up to 3) with relative paths from scanRoot
    std::vector<std::string> sampleFiles;
    for (int i = 0; i < std::min((int)primaryFiles.size(), 3); ++i) {
        std::string rel = fs::relative(primaryFiles[i], scanRoot).string();
        sampleFiles.push_back(rel);
        // Also show matching thumbnail if present
        std::string thumbPath = primaryFiles[i];
        auto dot = thumbPath.rfind('.');
        if (dot != std::string::npos) thumbPath = thumbPath.substr(0, dot) + ".jpg";
        if (fs::exists(thumbPath))
            sampleFiles.push_back(fs::relative(thumbPath, scanRoot).string());
    }

    // ---- JSON output ----
    if (jsonMode) {
        json j;
        j["root"] = scanRoot;

        json jDirs = json::array();
        for (const auto& d : cameraDirs)
            jDirs.push_back(fs::path(d).filename().string() + "/");
        j["camera_dirs"] = jDirs;

        j["extensions"]   = allExts;
        j["sample_files"] = sampleFiles;
        j["segment_durations_sec"] = durations;

        j["video"] = {
            {"resolution",   std::to_string(probe.video.width) + "x" + std::to_string(probe.video.height)},
            {"frame_rate",   probe.video.frameRate},
            {"pix_fmt",      probe.video.pixFmt},
            {"color_range",  probe.video.colorRange},
            {"color_space",  probe.video.colorSpace}
        };
        j["container"] = probe.container;

        json jGps;
        jGps["method"]       = probe.gps.method;
        jGps["stream_index"] = probe.gps.streamIndex;
        jGps["exiftool_note"] = probe.gps.exiftoolNote;
        if (probe.gps.hasFix) {
            jGps["first_fix"] = {
                {"timestamp", probe.gps.firstTimestamp},
                {"lat",       probe.gps.firstLat},
                {"lon",       probe.gps.firstLon}
            };
        }
        j["gps"] = jGps;

        // Timestamp source detection
        std::string tsFmtCard = sampleFiles.empty() ? ""
                              : guessTimestampFormat(sampleFiles[0]);
        bool tsIsFilename = !tsFmtCard.empty() && tsFmtCard != "YYYYMMDD_NNNN";
        j["timestamp_source"] = tsIsFilename ? "filename" : "exiftool_metadata";
        if (!tsFmtCard.empty()) j["timestamp_format_guess"] = tsFmtCard;

        j["submit_to"] = "https://github.com/Nutball-Labs/PathMux/issues";

        std::cout << j.dump(2) << "\n";
        return;
    }

    // ---- Text output ----
    std::cout << "--- pm_probe SD Card Fingerprint ---\n"
              << "Root:         " << scanRoot << "\n";

    std::cout << "Camera dirs: ";
    for (const auto& d : cameraDirs)
        std::cout << " " << fs::path(d).filename().string() << "/";
    std::cout << "\n";

    std::cout << "Extensions:  ";
    for (const auto& e : allExts) std::cout << " " << e;
    std::cout << "\n";

    if (!sampleFiles.empty()) {
        std::cout << "Filename pattern (sample):\n";
        for (const auto& f : sampleFiles)
            std::cout << "  " << f << "\n";
    }

    if (!durations.empty()) {
        std::cout << "Segment lengths observed:";
        for (int d : durations) std::cout << " " << d << "s";
        // Flag the last one if it looks like a stub
        if (durations.size() > 1 && durations.back() < durations[0] / 2)
            std::cout << " (stub at end)";
        std::cout << "\n";
    }

    std::cout << "Video profile (" << fs::path(primaryDir).filename().string()
              << ", first segment):\n"
              << "  Resolution:   " << probe.video.width << "x" << probe.video.height << "\n"
              << "  Frame rate:   " << Pathmux::formatFrameRate(probe.video.frameRate) << "\n"
              << "  Pixel format: " << probe.video.pixFmt;
    if (probe.video.colorRange == "pc" || probe.video.colorRange == "jpeg")
        std::cout << " (full-range)";
    std::cout << "\n"
              << "  Color space:  " << probe.video.colorSpace << "\n";

    {
        std::string tsFmtCard = sampleFiles.empty() ? ""
                              : guessTimestampFormat(sampleFiles[0]);
        if (!tsFmtCard.empty() && tsFmtCard != "YYYYMMDD_NNNN")
            std::cout << "Timestamp:    filename (" << tsFmtCard << ")\n";
        else
            std::cout << "Timestamp:    file metadata (no time-of-day in filename)\n";
    }

    if (probe.gps.method == "LIGOGPSINFO") {
        std::cout << "GPS method:   LIGOGPSINFO";
        if (probe.gps.streamIndex >= 0)
            std::cout << " (stream " << probe.gps.streamIndex << ")";
        if (!probe.gps.exiftoolNote.empty())
            std::cout << "  — " << probe.gps.exiftoolNote;
        std::cout << "\n";
    } else if (probe.gps.method == "standard") {
        std::cout << "GPS method:   Standard GPS metadata\n";
    } else {
        std::cout << "GPS method:   Not detected\n";
    }

    if (probe.gps.hasFix) {
        std::cout << std::fixed << std::setprecision(6)
                  << "GPS first fix: " << probe.gps.firstTimestamp
                  << "  lat=" << probe.gps.firstLat
                  << "  lon=" << probe.gps.firstLon << "\n";
    }

    std::cout << "\n--- Submit this report to: "
              << "https://github.com/Nutball-Labs/PathMux/issues ---\n";
}

// ---------------------------------------------------------------------------
// Wizard helpers
// ---------------------------------------------------------------------------

static std::string promptWizard(const std::string& prompt,
                                  const std::string& defaultVal = "")
{
    std::cout << prompt;
    if (!defaultVal.empty()) std::cout << " [" << defaultVal << "]";
    std::cout << ": ";
    std::cout.flush();
    std::string line;
    if (!std::getline(std::cin, line)) return defaultVal;
    auto ltrim = line.find_first_not_of(" \t");
    auto rtrim = line.find_last_not_of(" \t");
    if (ltrim == std::string::npos) return defaultVal;
    line = line.substr(ltrim, rtrim - ltrim + 1);
    return line.empty() ? defaultVal : line;
}

static std::string sanitizeProfileName(const std::string& name)
{
    std::string out;
    for (char c : name) {
        if (std::isalnum((unsigned char)c) || c == '-')
            out += static_cast<char>(std::tolower((unsigned char)c));
        else if (c == ' ' || c == '_')
            out += '_';
    }
    return out;
}

static std::string guessCameraRole(const std::string& dirName)
{
    std::string lower = dirName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (lower == "front")                              return "front";
    if (lower == "rear"  || lower == "back")           return "rear";
    if (lower == "left"  || lower == "left_repeater")  return "left";
    if (lower == "right" || lower == "right_repeater") return "right";
    return "";
}

// Guess camera role from a filename token (flat layout).
// Handles single-letter (F/R/L/B) and CAMn conventions.
static std::string guessCameraRoleFromToken(const std::string& token)
{
    std::string up = token;
    std::transform(up.begin(), up.end(), up.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    if (up == "F" || up == "FRONT" || up == "CAM1") return "front";
    if (up == "B" || up == "BACK"  || up == "REAR" || up == "CAM2") return "rear";
    if (up == "L" || up == "LEFT")                   return "left";
    if (up == "R" || up == "RIGHT")                  return "right";
    return "";
}

static int         timestampTokenLen(const std::string& fmt);   // defined below
static std::string timestampRegex(const std::string& fmt);      // defined below
static std::string escapeRegex(const std::string& s);           // defined below

// Camera-token analysis for a single directory.
// Detects multi-camera layouts where cameras are distinguished by a token
// embedded in each filename — as a suffix, embedded field, or prefix.
struct FlatTokenAnalysis {
    bool                     isMultiCamera    = false;
    std::vector<std::string> tokens;          // unique camera identifiers
    std::string              suffixGroup2;    // regex fragment APPENDED after timestamp (suffix/embedded)
    std::string              prefixGroup1;    // regex fragment PREPENDED before timestamp (prefix)
    bool                     isPrefix        = false;  // true when token precedes the timestamp
    std::string              tokenDesc;       // human-readable description (e.g. "filename suffix")
    int                      tokenCaptureGroup = 2;    // 1 for prefix layouts
    int                      tsCaptureGroup    = 1;    // 2 for prefix layouts
};

static FlatTokenAnalysis analyzeCameraTokens(const std::string& dir,
                                              const std::string& tsFmt)
{
    FlatTokenAnalysis result;
    int tsLen = timestampTokenLen(tsFmt);
    if (tsLen <= 0) return result;

    auto files = listVideoFiles(dir);
    if (files.size() < 2) return result;

    // -----------------------------------------------------------------------
    // Suffix analysis: collect portion between timestamp end and extension.
    // Assumes timestamp starts at position 0 of the filename.
    // -----------------------------------------------------------------------
    std::set<std::string> suffixes;
    for (const auto& f : files) {
        std::string base = pathBasename(f);
        if ((int)base.size() <= tsLen) continue;
        auto dot = base.rfind('.');
        if (dot == std::string::npos || (int)dot <= tsLen) continue;
        suffixes.insert(base.substr(tsLen, dot - tsLen));
    }

    if (suffixes.size() >= 2) {
        // Pattern A: bare single uppercase letter  e.g. F, R, L, B  (D90 flat)
        bool allSingleUpper = true;
        for (const auto& s : suffixes)
            if (s.size() != 1 || !std::isupper((unsigned char)s[0]))
                { allSingleUpper = false; break; }
        if (allSingleUpper) {
            result.isMultiCamera     = true;
            result.suffixGroup2      = "([A-Za-z])";
            result.tokenDesc         = "filename suffix";
            result.tokenCaptureGroup = 2;
            result.tsCaptureGroup    = 1;
            for (const auto& s : suffixes) result.tokens.push_back(s);
            return result;
        }

        // Pattern B: _X where X is a single uppercase letter  e.g. _F, _R
        bool allUnderscoreLetter = true;
        for (const auto& s : suffixes)
            if (s.size() != 2 || s[0] != '_' || !std::isupper((unsigned char)s[1]))
                { allUnderscoreLetter = false; break; }
        if (allUnderscoreLetter) {
            result.isMultiCamera     = true;
            result.suffixGroup2      = "_([A-Za-z])";
            result.tokenDesc         = "filename suffix";
            result.tokenCaptureGroup = 2;
            result.tsCaptureGroup    = 1;
            for (const auto& s : suffixes) result.tokens.push_back(s.substr(1));
            return result;
        }

        // Pattern C: underscore-delimited fields with exactly one varying segment.
        // e.g. _CAM1_VID / _CAM2_VID → tokens CAM1, CAM2
        {
            std::vector<std::vector<std::string>> splits;
            for (const auto& sfx : suffixes) {
                std::vector<std::string> parts;
                std::string seg;
                for (char ch : sfx) {
                    if (ch == '_') { parts.push_back(seg); seg.clear(); }
                    else           seg += ch;
                }
                parts.push_back(seg);
                splits.push_back(parts);
            }
            size_t nParts = splits[0].size();
            bool allSameLen = true;
            for (const auto& sp : splits)
                if (sp.size() != nParts) { allSameLen = false; break; }
            if (allSameLen && nParts >= 1) {
                std::vector<int> varyPos;
                for (int i = 0; i < (int)nParts; ++i) {
                    std::set<std::string> vals;
                    for (const auto& sp : splits) vals.insert(sp[i]);
                    if (vals.size() > 1) varyPos.push_back(i);
                }
                if (varyPos.size() == 1) {
                    int vi = varyPos[0];
                    std::set<std::string> tokSet;
                    for (const auto& sp : splits) tokSet.insert(sp[vi]);
                    // Build regex: fixed_prefix + (tok1|tok2|...) + fixed_suffix
                    std::string sg2;
                    for (int i = 0; i < vi; ++i) {
                        if (i > 0) sg2 += "_";
                        sg2 += escapeRegex(splits[0][i]);
                    }
                    if (vi > 0) sg2 += "_";
                    sg2 += "(";
                    bool first = true;
                    for (const auto& t : tokSet) {
                        if (!first) sg2 += "|";
                        sg2 += escapeRegex(t);
                        first = false;
                    }
                    sg2 += ")";
                    for (int i = vi + 1; i < (int)nParts; ++i) {
                        sg2 += "_";
                        sg2 += escapeRegex(splits[0][i]);
                    }
                    result.isMultiCamera     = true;
                    result.suffixGroup2      = sg2;
                    result.tokenDesc         = "embedded filename token";
                    result.tokenCaptureGroup = 2;
                    result.tsCaptureGroup    = 1;
                    for (const auto& t : tokSet) result.tokens.push_back(t);
                    return result;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Pattern P: token BEFORE the timestamp (prefix layout).
    // e.g.  CAM1_20260223_0005.MOV  /  CAM2_20260223_0005.MOV
    // -----------------------------------------------------------------------
    {
        std::regex tsSearch(timestampRegex(tsFmt));
        std::set<std::string> prefixes;
        bool anyNonZero = false;
        for (const auto& f : files) {
            std::string base = pathBasename(f);
            std::smatch m;
            if (!std::regex_search(base, m, tsSearch)) continue;
            size_t tsOff = static_cast<size_t>(m.position(0));
            if (tsOff == 0) continue;
            anyNonZero = true;
            std::string pre = base.substr(0, tsOff);
            while (!pre.empty() && (pre.back() == '_' || pre.back() == '-'))
                pre.pop_back();
            if (!pre.empty()) prefixes.insert(pre);
        }
        if (anyNonZero && prefixes.size() >= 2) {
            // Detect separator character between prefix and timestamp
            char sep = '_';
            {
                std::string base0 = pathBasename(files[0]);
                std::smatch m0;
                if (std::regex_search(base0, m0, tsSearch)) {
                    size_t tsOff0 = static_cast<size_t>(m0.position(0));
                    std::string tok0 = *prefixes.begin();
                    if (tsOff0 > tok0.size()) sep = base0[tok0.size()];
                }
            }
            result.isMultiCamera     = true;
            result.isPrefix          = true;
            result.tokenCaptureGroup = 1;
            result.tsCaptureGroup    = 2;
            result.tokenDesc         = "filename prefix";
            for (const auto& p : prefixes) result.tokens.push_back(p);
            result.prefixGroup1 = "(";
            bool first = true;
            for (const auto& t : prefixes) {
                if (!first) result.prefixGroup1 += "|";
                result.prefixGroup1 += escapeRegex(t);
                first = false;
            }
            result.prefixGroup1 += ")";
            result.prefixGroup1 += escapeRegex(std::string(1, sep));
            return result;
        }
    }

    return result;  // unrecognised pattern — treat as single camera
}

// Guess the timestamp format string from a sample filename (path or basename).
static std::string guessTimestampFormat(const std::string& filename)
{
    std::string base = pathBasename(filename);

    // YYYYMMDD_HHMMSS — e.g. 20260225_044424F.ts  (6-digit time field)
    if (base.size() >= 15
        && std::isdigit((unsigned char)base[0])
        && std::isdigit((unsigned char)base[7])
        && base[8] == '_'
        && std::isdigit((unsigned char)base[9])
        && std::isdigit((unsigned char)base[14]))
        return "YYYYMMDD_HHMMSS";

    // YYYY-MM-DD_HH-MM-SS — e.g. 2024-01-15_12-30-45-front.mp4
    if (base.size() >= 19
        && std::isdigit((unsigned char)base[0])
        && base[4] == '-' && base[7] == '-' && base[10] == '_'
        && std::isdigit((unsigned char)base[11])
        && base[13] == '-' && base[16] == '-')
        return "YYYY-MM-DD_HH-MM-SS";

    // YYYYMMDD_NNNN — date + 4-digit sequential counter, no time-of-day
    // e.g. 20260223_0005_CAM1_VID.MOV  (base[13] is not a digit → only 4 counter digits)
    if (base.size() >= 13
        && std::isdigit((unsigned char)base[0])
        && std::isdigit((unsigned char)base[7])
        && base[8] == '_'
        && std::isdigit((unsigned char)base[9])
        && std::isdigit((unsigned char)base[12])
        && (base.size() <= 13 || !std::isdigit((unsigned char)base[13])))
        return "YYYYMMDD_NNNN";

    return "";
}

// Number of characters the timestamp occupies in the basename.
static int timestampTokenLen(const std::string& fmt)
{
    if (fmt == "YYYYMMDD_HHMMSS")     return 15;
    if (fmt == "YYYY-MM-DD_HH-MM-SS") return 19;
    if (fmt == "YYYYMMDD_NNNN")       return 13;
    return 0;
}

// Regex fragment for the timestamp portion (single capture group = full timestamp string).
static std::string timestampRegex(const std::string& fmt)
{
    if (fmt == "YYYYMMDD_HHMMSS")
        return "(\\d{8}_\\d{6})";
    if (fmt == "YYYY-MM-DD_HH-MM-SS")
        return "(\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}-\\d{2})";
    if (fmt == "YYYYMMDD_NNNN")
        return "(\\d{8}_\\d{4})";
    return "(.+)";
}

// Escape special regex metacharacters in a literal string fragment.
static std::string escapeRegex(const std::string& s)
{
    std::string out;
    for (char c : s) {
        if (std::string(".+*?^${}[]()|\\").find(c) != std::string::npos)
            out += '\\';
        out += c;
    }
    return out;
}

// Convert wizard tsFmt → strptime format string (empty for exiftool_metadata).
static std::string strptimeFmt(const std::string& fmt)
{
    if (fmt == "YYYYMMDD_HHMMSS")     return "%Y%m%d_%H%M%S";
    if (fmt == "YYYY-MM-DD_HH-MM-SS") return "%Y-%m-%d_%H-%M-%S";
    return "";  // YYYYMMDD_NNNN and unknown → no strptime, use exiftool_metadata
}

// Convert a file extension (e.g. ".ts", ".MOV") → case-insensitive regex fragment.
static std::string extRegex(const std::string& ext)
{
    if (ext.empty()) return "";
    std::string out;
    for (char c : ext) {
        if (c == '.') { out += "\\."; continue; }
        if (std::isalpha((unsigned char)c)) {
            char lo = static_cast<char>(std::tolower((unsigned char)c));
            char hi = static_cast<char>(std::toupper((unsigned char)c));
            out += '[';
            out += lo;
            out += hi;
            out += ']';
        } else {
            out += c;
        }
    }
    return out;
}

// Convert wizard thumbSource + thumbPattern → CameraProfile thumbnail_method.
static std::string thumbnailMethodFrom(const std::string& thumbSource,
                                        const std::string& thumbPattern)
{
    if (thumbSource == "generate_ffmpeg") return "generate_ffmpeg";
    if (thumbSource != "sidecar_jpg")    return "none";
    // _ths suffix in pattern → ths_sidecar
    if (thumbPattern.find("_ths") != std::string::npos) return "ths_sidecar";
    return "replace_ext";
}

// Characterize a set of observed suffix strings into a regex fragment.
// Handles: empty, constant literal, all-single-uppercase ([A-Z]),
// uppercase + constant remainder (e.g. F_ths/R_ths → [A-Z]_ths),
// separator + variable (-front/-back → -.+), or generic .+
static std::string characterizeSuffixes(const std::set<std::string>& suffixes,
                                         bool& determined)
{
    determined = true;
    if (suffixes.empty())                                   { determined = false; return ""; }
    if (suffixes.size() == 1 && suffixes.begin()->empty())  return "";
    if (suffixes.size() == 1)                               return escapeRegex(*suffixes.begin());

    // All single uppercase letters (e.g. F, R, L)
    bool allSingleUpper = true;
    for (const auto& s : suffixes)
        if (s.size() != 1 || !std::isupper((unsigned char)s[0]))
            { allSingleUpper = false; break; }
    if (allSingleUpper) return "[A-Z]";

    // Uppercase letter + constant remainder (e.g. F_ths, R_ths → [A-Z]_ths)
    bool allStartUpper = true;
    for (const auto& s : suffixes)
        if (s.empty() || !std::isupper((unsigned char)s[0]))
            { allStartUpper = false; break; }
    if (allStartUpper) {
        std::string remainder = suffixes.begin()->substr(1);
        bool sameRemainder = true;
        for (const auto& s : suffixes)
            if (s.substr(1) != remainder) { sameRemainder = false; break; }
        if (sameRemainder)
            return "[A-Z]" + escapeRegex(remainder);
        return "[A-Z].+";
    }

    // Separator + variable (e.g. -front, -back)
    char first = (*suffixes.begin())[0];
    bool allSameSep = true;
    for (const auto& s : suffixes)
        if (s.empty() || s[0] != first) { allSameSep = false; break; }
    return (allSameSep && (first == '-' || first == '_'))
           ? std::string(1, first) + ".+"
           : ".+";
}

// Suffix analysis for video filenames, sampled across all camera dirs.
struct SuffixAnalysis {
    std::string pattern;    // regex fragment (may be empty string = no suffix)
    bool        determined; // false if analysis failed
};

static SuffixAnalysis analyzeSuffix(const std::vector<std::string>& cameraDirs,
                                     const std::string& tsFmt)
{
    SuffixAnalysis result;
    int tsLen = timestampTokenLen(tsFmt);
    if (tsLen == 0) { result.determined = false; return result; }

    std::set<std::string> suffixes;
    for (const auto& dir : cameraDirs) {
        auto files = listVideoFiles(dir);
        int checked = 0;
        for (const auto& f : files) {
            if (checked >= 3) break;
            std::string stem = fs::path(f).stem().string();
            if ((int)stem.size() < tsLen) continue;
            suffixes.insert(stem.substr(tsLen));
            ++checked;
        }
    }

    result.pattern = characterizeSuffixes(suffixes, result.determined);
    return result;
}

// Suffix analysis for thumbnail (.jpg) filenames, sampled across all camera dirs.
// Separate from video suffix because cameras can use different conventions
// (e.g. D90: video = 20260306_143418F.ts, thumb = 20260306_143418F_ths.jpg).
static SuffixAnalysis analyzeThumbSuffix(const std::vector<std::string>& cameraDirs,
                                          const std::string& tsFmt)
{
    SuffixAnalysis result;
    int tsLen = timestampTokenLen(tsFmt);
    if (tsLen == 0) { result.determined = false; return result; }

    std::set<std::string> suffixes;
    for (const auto& dir : cameraDirs) {
        if (!fs::is_directory(dir)) continue;
        int count = 0;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file() || count >= 3) continue;
            if (isDotFile(entry.path())) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (ext != ".jpg" && ext != ".jpeg") continue;
            std::string stem = entry.path().stem().string();
            if ((int)stem.size() >= tsLen) {
                suffixes.insert(stem.substr(tsLen));
                ++count;
            }
        }
    }

    result.pattern = characterizeSuffixes(suffixes, result.determined);
    return result;
}

// Check whether sidecar .jpg files exist in a directory.
static bool detectSidecarJpg(const std::string& dir)
{
    if (!fs::is_directory(dir)) return false;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (isDotFile(entry.path())) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (ext == ".jpg" || ext == ".jpeg") return true;
    }
    return false;
}

// Check whether >=minMatch video files have a .jpg sidecar whose timestamp
// portion (first tsLen chars of stem) matches. Ignores any suffix differences
// between video and thumbnail filenames (e.g. F.ts vs F_ths.jpg on D90).
static bool sidecarTimestampMatch(const std::string& dir,
                                   const std::string& tsFmt,
                                   int minMatch = 3)
{
    int tsLen = timestampTokenLen(tsFmt);
    if (tsLen <= 0) return false;

    // Collect timestamp strings from all .jpg files in the directory
    std::set<std::string> jpgTimestamps;
    if (!fs::is_directory(dir)) return false;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (isDotFile(entry.path())) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (ext != ".jpg" && ext != ".jpeg") continue;
        std::string stem = entry.path().stem().string();
        if ((int)stem.size() >= tsLen)
            jpgTimestamps.insert(stem.substr(0, tsLen));
    }
    if (jpgTimestamps.empty()) return false;

    auto videos = listVideoFiles(dir);
    int matched = 0;
    for (const auto& v : videos) {
        if (matched >= minMatch) break;
        std::string stem = fs::path(v).stem().string();
        if ((int)stem.size() >= tsLen &&
            jpgTimestamps.count(stem.substr(0, tsLen)))
            ++matched;
    }
    return matched >= minMatch;
}

// ---------------------------------------------------------------------------
// Wizard box drawing helpers
// ---------------------------------------------------------------------------
static constexpr int WIZ_BOX = 68;
static constexpr int WIZ_INN = WIZ_BOX - 4;  // "| " + content + " |"

static std::string wizSep(char fill = '=')
{
    return "+" + std::string(WIZ_BOX - 2, fill) + "+";
}
static std::string wizRow(const std::string& s = "")
{
    std::string c = s;
    if ((int)c.size() > WIZ_INN) c = c.substr(0, WIZ_INN);
    return "| " + c + std::string(WIZ_INN - (int)c.size(), ' ') + " |";
}

// ---------------------------------------------------------------------------
// wizardCard — interactive profile builder
// ---------------------------------------------------------------------------
static void wizardCard(const std::string& root,
                        const std::string& ffprobePath,
                        const std::string& exiftoolPath,
                        const std::string& exiftoolOptions)
{
    if (!fs::is_directory(root)) {
        std::cerr << "Error: not a directory: " << root << "\n";
        return;
    }

    std::cout << "--- pm_probe: Camera Profile Wizard ---\n"
              << "Scanning: " << root << "\n";
    std::cout.flush();

    // ---- Silent data collection ----
    std::string scanRoot = root;
    auto cameraDirs = findCameraDirs(scanRoot);
    if (cameraDirs.empty()) {
        std::cerr << "No camera directories with video files found under: " << root << "\n";
        return;
    }
    if (scanRoot != root)
        std::cout << "Camera directories found under: " << scanRoot << "\n";

    // Identify primary camera dir (prefer Front/)
    std::string primaryDir;
    for (const auto& d : cameraDirs) {
        std::string name = fs::path(d).filename().string();
        if (name == "Front" || name == "front" || name == "FRONT")
            { primaryDir = d; break; }
    }
    if (primaryDir.empty()) primaryDir = cameraDirs[0];

    auto primaryFiles = listVideoFiles(primaryDir);
    if (primaryFiles.empty()) {
        std::cerr << "No video files found in primary camera directory.\n";
        return;
    }

    // Primary file extension
    std::string primaryExt;
    {
        auto dot = primaryFiles[0].rfind('.');
        if (dot != std::string::npos) primaryExt = primaryFiles[0].substr(dot);
    }

    // Timestamp format — computed here so sidecar matching during cam loop can use it
    std::string tsFmt = guessTimestampFormat(primaryFiles[0]);

    // Segment durations from primary dir (up to 5 samples)
    std::vector<int> durations;
    for (int i = 0; i < std::min((int)primaryFiles.size(), 5); ++i) {
        int d = segDurationFfprobe(primaryFiles[i], ffprobePath);
        if (d > 0) durations.push_back(d);
    }

    // Probe first segment of each camera dir for video + audio
    struct CamData {
        std::string dirName;
        std::string dirPath;
        std::string sampleFile;           // basename of first video file
        int         fileCount   = 0;
        bool        hasSidecar  = false;
        bool        sidecarOk   = false;  // basenames confirmed >=3 samples
        FileProbe   probe;
    };
    std::vector<CamData> cams;

    for (const auto& d : cameraDirs) {
        CamData c;
        c.dirName    = fs::path(d).filename().string();
        c.dirPath    = d;
        auto files   = listVideoFiles(d);
        c.fileCount  = static_cast<int>(files.size());
        c.sampleFile = files.empty() ? "" : fs::path(files[0]).filename().string();
        c.hasSidecar = detectSidecarJpg(d);
        c.sidecarOk  = c.hasSidecar && sidecarTimestampMatch(d, tsFmt);
        if (!files.empty()) {
            std::cout << "  Probing " << (c.dirName.empty() ? "." : c.dirName) << "/ ...\n";
            std::cout.flush();
            c.probe = probeFile(files[0], ffprobePath, exiftoolPath, exiftoolOptions);
        }
        cams.push_back(c);
    }

    // Locate primary cam data
    const CamData* primaryCam = nullptr;
    for (const auto& c : cams)
        if (c.dirPath == primaryDir) { primaryCam = &c; break; }

    // Suffix analysis (driven by all dirs)
    SuffixAnalysis suffix = analyzeSuffix(cameraDirs, tsFmt);

    // Flat-layout detection
    bool             flatLayout  = isFlatLayout(cameraDirs, scanRoot);
    FlatTokenAnalysis flatAnalysis;
    // Analyse tokens for any single-directory layout (flat root or single subdir like 100_DSC/).
    if (cameraDirs.size() == 1) flatAnalysis = analyzeCameraTokens(cameraDirs[0], tsFmt);
    bool isFlatMulti = (cameraDirs.size() == 1) && flatAnalysis.isMultiCamera;
    // For single-subdir token layouts (e.g. all cameras in 100_DSC/), record the subdir.
    std::string tokenScanSubdir = (isFlatMulti && !flatLayout && !cams.empty())
                                   ? cams[0].dirName : "";

    // Sample filename per camera token (for mapping UI).
    std::map<std::string, std::string> tokenSampleFile;
    if (isFlatMulti && !cameraDirs.empty()) {
        const std::string& tokenDir = cams.empty() ? cameraDirs[0] : cams[0].dirPath;
        auto flatFiles = listVideoFiles(tokenDir);
        for (const auto& tok : flatAnalysis.tokens) {
            for (const auto& f : flatFiles) {
                std::string base = pathBasename(f);
                bool hit = false;
                if (flatAnalysis.isPrefix) {
                    hit = base.size() >= tok.size()
                       && base.substr(0, tok.size()) == tok
                       && (base.size() == tok.size()
                           || base[tok.size()] == '_' || base[tok.size()] == '-');
                } else {
                    auto dot = base.rfind('.');
                    int tsLen = timestampTokenLen(tsFmt);
                    if (dot != std::string::npos && (int)dot > tsLen)
                        hit = base.substr(tsLen, dot - tsLen).find(tok) != std::string::npos;
                }
                if (hit) { tokenSampleFile[tok] = base; break; }
            }
        }
    }

    // ---- Summary of what was found ----
    std::string layoutNote = isFlatMulti
        ? " (" + flatAnalysis.tokenDesc + ", " + std::to_string(flatAnalysis.tokens.size()) + " cameras)"
        : flatLayout ? " (flat layout)" : "";
    std::cout << "\nFound " << cams.size() << " camera director"
              << (cams.size() == 1 ? "y" : "ies") << layoutNote << ":\n";
    for (const auto& c : cams) {
        std::string displayName = c.dirName.empty() ? "." : c.dirName;
        std::cout << "  " << displayName << "/  ("
                  << c.fileCount << " video file" << (c.fileCount == 1 ? "" : "s");
        if (c.hasSidecar) std::cout << ", .jpg sidecars";
        if (isFlatMulti) {
            std::cout << "  tokens:";
            for (const auto& t : flatAnalysis.tokens) std::cout << "  " << t;
        }
        std::cout << ")\n";
        if (c.probe.video.width > 0)
            std::cout << "    video: " << c.probe.video.width << "x"
                      << c.probe.video.height << " @ "
                      << Pathmux::formatFrameRate(c.probe.video.frameRate)
                      << ", " << c.probe.video.pixFmt << "\n";
        for (const auto& s : c.probe.streams) {
            if (s.codecType != "audio") continue;
            std::cout << "    audio: " << s.codecName;
            if (!s.sampleRate.empty()) std::cout << " " << s.sampleRate << "Hz";
            if (s.channels > 0)        std::cout << " " << s.channels << "ch";
            std::cout << "\n";
        }
    }
    if (!durations.empty()) {
        bool allSame = std::all_of(durations.begin(), durations.end(),
                                    [&](int d){ return d == durations[0]; });
        std::cout << "Segment duration: ";
        if (allSame) std::cout << durations[0] << "s";
        else for (int d : durations) std::cout << d << "s ";
        std::cout << " (" << durations.size() << " samples)\n";
    }
    if (primaryCam) {
        std::cout << "GPS: " << primaryCam->probe.gps.method;
        if (primaryCam->probe.gps.streamIndex >= 0)
            std::cout << " (stream " << primaryCam->probe.gps.streamIndex << ")";
        std::cout << "\n";
    }

    // ---- Initialize state from detected data ----

    // Camera role slots (empty = unassigned).
    // Subdir layout: hold dirName.  Flat multi-camera: hold filenameToken.
    std::string roleFront, roleRear, roleLeft, roleRight;
    // Attention flags — set when a user-entered value fails validation
    bool frontAttn = false, rearAttn = false, leftAttn = false, rightAttn = false;
    if (isFlatMulti) {
        for (const auto& tok : flatAnalysis.tokens) {
            std::string r = guessCameraRoleFromToken(tok);
            if      (r == "front") roleFront = tok;
            else if (r == "rear")  roleRear  = tok;
            else if (r == "left")  roleLeft  = tok;
            else if (r == "right") roleRight = tok;
        }
    } else {
        for (const auto& c : cams) {
            std::string r = guessCameraRole(c.dirName);
            if      (r == "front") roleFront = c.dirName;
            else if (r == "rear")  roleRear  = c.dirName;
            else if (r == "left")  roleLeft  = c.dirName;
            else if (r == "right") roleRight = c.dirName;
        }
    }

    // Derived filename pattern
    std::string fnPattern = "^" + timestampRegex(tsFmt)
                          + suffix.pattern + "\\" + primaryExt + "$";

    // Thumbnail state
    bool anySidecar = std::any_of(cams.begin(), cams.end(),
                                   [](const CamData& c){ return c.hasSidecar; });
    bool allSidecarOk = anySidecar &&
        std::all_of(cams.begin(), cams.end(),
                    [](const CamData& c){ return !c.hasSidecar || c.sidecarOk; });
    std::string thumbSource;
    std::string thumbPattern;
    bool thumbUnconfirmed = false;
    if (anySidecar) {
        thumbSource = "sidecar_jpg";
        if (allSidecarOk) {
            SuffixAnalysis thumbSfx = analyzeThumbSuffix(cameraDirs, tsFmt);
            thumbPattern = "^" + timestampRegex(tsFmt) + thumbSfx.pattern + "\\.jpg$";
        } else {
            thumbUnconfirmed = true;
        }
    } else {
        thumbSource = "generate_ffmpeg";
    }

    // GPS, timezone, lock offset, profile name
    std::string gpsMethod = primaryCam ? primaryCam->probe.gps.method : "none";
    if (gpsMethod == "LIGOGPSINFO") gpsMethod = "exiftool_ligogps";
    int         gpsOffset  = 0;
    std::string tzLower    = "local";
    std::string profileName;

    // Sample filenames for display — shown as examples instead of regex
    std::string sampleVideoFile = fs::path(primaryFiles[0]).filename().string();
    std::string sampleThumbFile;
    for (const auto& c : cams) {
        if (!c.hasSidecar || !sampleThumbFile.empty()) continue;
        for (const auto& entry : fs::directory_iterator(c.dirPath)) {
            if (!entry.is_regular_file()) continue;
            if (isDotFile(entry.path())) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char ch){ return std::tolower(ch); });
            if (ext == ".jpg" || ext == ".jpeg")
                { sampleThumbFile = entry.path().filename().string(); break; }
        }
    }

    // Rebuild derived patterns when tsFmt changes
    auto rebuildPatterns = [&]() {
        suffix    = analyzeSuffix(cameraDirs, tsFmt);
        fnPattern = "^" + timestampRegex(tsFmt) + suffix.pattern + "\\" + primaryExt + "$";
        if (allSidecarOk) {
            SuffixAnalysis thumbSfx = analyzeThumbSuffix(cameraDirs, tsFmt);
            thumbPattern = "^" + timestampRegex(tsFmt) + thumbSfx.pattern + "\\.jpg$";
        }
    };

    // Trim whitespace from a raw input line
    auto trimLine = [](const std::string& raw) -> std::string {
        auto lt = raw.find_first_not_of(" \t");
        if (lt == std::string::npos) return "";
        return raw.substr(lt, raw.find_last_not_of(" \t") - lt + 1);
    };

    // Draw the main settings table
    auto drawTable = [&]() {
        std::cout << "\n" << wizSep('=') << "\n";
        std::cout << wizRow("  <path> = " + scanRoot) << "\n";

        // [1] Camera mappings
        std::string mappingLabel = isFlatMulti
            ? "  [1]  Camera tokens      (flat layout)"
            : "  [1]  Camera mappings";
        std::cout << wizRow(mappingLabel) << "\n";
        auto showCam = [&](const std::string& label, const std::string& val, bool attn) {
            std::string s = "         " + label;
            while ((int)s.size() < 16) s += ' ';
            if (val.empty())        s += "->  (unassigned)";
            else if (isFlatMulti)   s += "->  token " + val;
            else                    s += "->  <path>/" + val + "/";
            if (attn)               s += "  [!] needs attention";
            return s;
        };
        std::cout << wizRow(showCam("front",  roleFront, frontAttn)) << "\n";
        std::cout << wizRow(showCam("rear",   roleRear,  rearAttn))  << "\n";
        std::cout << wizRow(showCam("left",   roleLeft,  leftAttn))  << "\n";
        std::cout << wizRow(showCam("right",  roleRight, rightAttn)) << "\n";
        // Any dirs/tokens not yet assigned to a role
        if (isFlatMulti) {
            for (const auto& tok : flatAnalysis.tokens)
                if (tok != roleFront && tok != roleRear &&
                    tok != roleLeft  && tok != roleRight)
                    std::cout << wizRow("         (unassigned)   ->  token " + tok) << "\n";
        } else {
            for (const auto& c : cams)
                if (c.dirName != roleFront && c.dirName != roleRear &&
                    c.dirName != roleLeft  && c.dirName != roleRight)
                    std::cout << wizRow("         (unassigned)   ->  <path>/" + c.dirName + "/") << "\n";
        }

        std::cout << wizSep('-') << "\n";

        // [2] Timezone
        std::cout << wizRow("  [2]  Timezone            " + tzLower) << "\n";

        // [3] Timestamp format — show detected example filename, not regex
        std::cout << wizRow("  [3]  Timestamp format    " + tsFmt) << "\n";
        if (!sampleVideoFile.empty())
            std::cout << wizRow("         example:  " + sampleVideoFile) << "\n";

        // [4] Thumbnails — show detected example jpg, not regex
        {
            std::string disp = thumbSource;
            if (thumbUnconfirmed) disp += "  (unconfirmed)";
            std::cout << wizRow("  [4]  Thumbnails          " + disp) << "\n";
        }
        if (!sampleThumbFile.empty() && thumbSource == "sidecar_jpg")
            std::cout << wizRow("         example:  " + sampleThumbFile) << "\n";
        else if (thumbUnconfirmed)
            std::cout << wizRow("         run --card and file issue to confirm") << "\n";

        std::cout << wizSep('-') << "\n";

        // [5] GPS method
        std::cout << wizRow("  [5]  GPS method          " + gpsMethod) << "\n";

        // [6] GPS lock offset
        std::cout << wizRow("  [6]  GPS lock offset     "
                            + std::to_string(gpsOffset) + "s") << "\n";

        std::cout << wizSep('-') << "\n";

        // [7] Profile name (required before CONFIRM)
        std::string nd = profileName.empty() ? "(not set -- required)" : profileName;
        std::cout << wizRow("  [7]  Profile name        " + nd) << "\n";

        // Observed segment durations — informational, no item number
        if (!durations.empty()) {
            std::string sl = "       Observed segments:  ";
            bool allSame = std::all_of(durations.begin(), durations.end(),
                                        [&](int d){ return d == durations[0]; });
            if (allSame) sl += std::to_string(durations[0]) + "s";
            else for (int d : durations) sl += std::to_string(d) + "s ";
            sl += "  (" + std::to_string(durations.size()) + " samples)";
            std::cout << wizRow(sl) << "\n";
        }

        std::cout << wizSep('-') << "\n"
                  << wizRow("  [1-7] to edit  |  CONFIRM to accept  |  Q to quit") << "\n"
                  << wizSep('=') << "\n"
                  << "> ";
        std::cout.flush();
    };

    // ---- Main settings loop ----
    while (true) {
        drawTable();

        std::string cmd;
        if (!std::getline(std::cin, cmd)) break;
        cmd = trimLine(cmd);

        std::string cmdUp = cmd;
        std::transform(cmdUp.begin(), cmdUp.end(), cmdUp.begin(),
                       [](unsigned char c){ return std::toupper(c); });

        if (cmdUp == "Q") {
            std::cout << "  Wizard cancelled. No profile saved.\n";
            return;
        }

        if (cmdUp == "CONFIRM") {
            if (profileName.empty()) {
                std::cout << "  Profile name is required — select [7] to set it.\n";
                continue;
            }
            break;
        }

        int sel = -1;
        try { sel = std::stoi(cmd); } catch (...) {}

        // [1] Camera mappings — two-panel UI (slot assignments + numbered sources)
        if (sel == 1) {
            // Build numbered sources list once
            struct Source {
                std::string id;          // dirName or token
                std::string label;       // display string for sources panel
                std::string sampleFile;  // basename of first video file
                int         fileCount = 0;
            };
            std::vector<Source> sources;
            if (isFlatMulti) {
                for (const auto& tok : flatAnalysis.tokens) {
                    Source s;
                    s.id         = tok;
                    s.label      = tok + "  (" + flatAnalysis.tokenDesc + ")";
                    s.sampleFile = tokenSampleFile.count(tok) ? tokenSampleFile.at(tok) : "";
                    sources.push_back(s);
                }
            } else {
                for (const auto& c : cams) {
                    Source s;
                    s.id         = c.dirName;
                    s.label      = c.dirName + "/";
                    s.sampleFile = c.sampleFile;
                    s.fileCount  = c.fileCount;
                    sources.push_back(s);
                }
            }

            // State: SLOT_SELECT waits for F/B/L/R; SOURCE_SELECT waits for a number
            enum class MapPhase { SLOT_SELECT, SOURCE_SELECT };
            MapPhase     phase       = MapPhase::SLOT_SELECT;
            std::string* pendingSlot = nullptr;
            bool*        pendingAttn = nullptr;
            std::string  pendingLabel;
            char         pendingKey  = ' ';

            // Two-pane layout: left=slot assignments (30), right=sources (31)
            // "| " + left(30) + " | " + right(31) + " |" = 68
            auto p30 = [](std::string s) -> std::string {
                if ((int)s.size() > 30) s = s.substr(0, 30);
                else s += std::string(30 - (int)s.size(), ' ');
                return s;
            };
            auto p31 = [](std::string s) -> std::string {
                if ((int)s.size() > 31) s = s.substr(0, 31);
                else s += std::string(31 - (int)s.size(), ' ');
                return s;
            };
            auto paneRow = [&](const std::string& left, const std::string& right) -> std::string {
                return "| " + p30(left) + " | " + p31(right) + " |";
            };
            // Column separator: "+" + 32="=" + "+" + 33="=" + "+"  (= 68)
            const std::string colSep = "+" + std::string(32, '=') + "+" + std::string(33, '=') + "+";

            // Build flat source-line list: one label row + one sample row per source
            std::vector<std::string> srcLines;
            for (int i = 0; i < (int)sources.size(); ++i) {
                const auto& src = sources[i];
                std::string ln = "[" + std::to_string(i + 1) + "]  " + src.label;
                if (src.fileCount > 0)
                    ln += "  " + std::to_string(src.fileCount) + " file" + (src.fileCount == 1 ? "" : "s");
                srcLines.push_back(ln);
                if (!src.sampleFile.empty())
                    srcLines.push_back("     " + src.sampleFile);
            }

            // Left-column slot data
            const char        slotKeys[]  = {'F', 'B', 'L', 'R'};
            const char* const slotNames[] = {"front", "rear", "left", "right"};
            const std::string* slotVals[] = {&roleFront, &roleRear, &roleLeft, &roleRight};

            auto drawMap = [&]() {
                int totalRows = std::max(4, (int)srcLines.size());
                std::cout << "\n  <path> = " << scanRoot << "\n" << colSep << "\n";
                for (int row = 0; row < totalRows; ++row) {
                    // Left pane: slot row or blank
                    std::string left;
                    if (row < 4) {
                        bool hl = (phase == MapPhase::SOURCE_SELECT
                                   && pendingSlot == slotVals[row]);
                        left  = (hl ? ">>" : "  ");
                        left += "["; left += slotKeys[row]; left += "] ";
                        left += slotNames[row];
                        left += "  ->  ";
                        if (slotVals[row]->empty())    left += "(unassigned)";
                        else if (isFlatMulti)          left += *slotVals[row];
                        else                           left += *slotVals[row] + "/";
                    }
                    // Right pane: source line or blank
                    std::string right = (row < (int)srcLines.size()) ? srcLines[row] : "";
                    std::cout << paneRow(left, right) << "\n";
                }
                std::cout << wizSep('-') << "\n";
                if (phase == MapPhase::SLOT_SELECT) {
                    std::cout << wizRow("  CONFIRM  |  [F/B/L/R] select slot  |  Q quit") << "\n";
                } else {
                    std::string act = "  Assign [";
                    act += pendingKey; act += "]"; act += pendingLabel;
                    act += ":  [1-"; act += std::to_string(sources.size());
                    act += "] pick  |  0 unassign  |  Q cancel";
                    std::cout << wizRow(act) << "\n";
                }
                std::cout << wizSep('=') << "\n> ";
                std::cout.flush();
            };

            while (true) {
                drawMap();
                std::string c2;
                if (!std::getline(std::cin, c2)) break;
                c2 = trimLine(c2);
                std::string u = c2;
                std::transform(u.begin(), u.end(), u.begin(),
                               [](unsigned char ch){ return std::toupper(ch); });
                if (phase == MapPhase::SLOT_SELECT) {
                    if (u == "CONFIRM" || u == "Q") break;
                    if      (u == "F") { pendingSlot = &roleFront; pendingAttn = &frontAttn; pendingLabel = "front"; pendingKey = 'F'; phase = MapPhase::SOURCE_SELECT; }
                    else if (u == "B") { pendingSlot = &roleRear;  pendingAttn = &rearAttn;  pendingLabel = "rear";  pendingKey = 'B'; phase = MapPhase::SOURCE_SELECT; }
                    else if (u == "L") { pendingSlot = &roleLeft;  pendingAttn = &leftAttn;  pendingLabel = "left";  pendingKey = 'L'; phase = MapPhase::SOURCE_SELECT; }
                    else if (u == "R") { pendingSlot = &roleRight; pendingAttn = &rightAttn; pendingLabel = "right"; pendingKey = 'R'; phase = MapPhase::SOURCE_SELECT; }
                } else {
                    if (u == "Q") { phase = MapPhase::SLOT_SELECT; pendingSlot = nullptr; continue; }
                    if (u == "0") {
                        *pendingSlot = ""; *pendingAttn = false;
                        phase = MapPhase::SLOT_SELECT; pendingSlot = nullptr; continue;
                    }
                    int pick = 0;
                    try { pick = std::stoi(u); } catch (...) {}
                    if (pick >= 1 && pick <= (int)sources.size()) {
                        const std::string& chosen = sources[pick - 1].id;
                        if (roleFront == chosen) { roleFront = ""; frontAttn = false; }
                        if (roleRear  == chosen) { roleRear  = ""; rearAttn  = false; }
                        if (roleLeft  == chosen) { roleLeft  = ""; leftAttn  = false; }
                        if (roleRight == chosen) { roleRight = ""; rightAttn = false; }
                        *pendingSlot = chosen; *pendingAttn = false;
                        phase = MapPhase::SLOT_SELECT; pendingSlot = nullptr;
                    }
                }
            }
        }
        // [2] Timezone
        else if (sel == 2) {
            tzLower = promptWizard("  Timezone (local/utc)", tzLower);
            std::transform(tzLower.begin(), tzLower.end(), tzLower.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (tzLower != "utc") tzLower = "local";
        }
        // [3] Timestamp format
        else if (sel == 3) {
            std::cout << "  Sample: " << sampleVideoFile << "\n";
            tsFmt = promptWizard("  Timestamp format", tsFmt);
            rebuildPatterns();
        }
        // [4] Thumbnails
        else if (sel == 4) {
            std::cout << "  Options: sidecar_jpg  generate_ffmpeg  none\n";
            thumbSource = promptWizard("  Thumbnail source", thumbSource);
            if (thumbSource == "sidecar_jpg")
                thumbPattern = promptWizard("  Thumbnail pattern", thumbPattern);
            else
                thumbPattern = "";
        }
        // [5] GPS method
        else if (sel == 5) {
            std::cout << "  [1] exiftool_ligogps   (LIGO private data stream)\n"
                      << "  [2] standard           (standard GPS metadata tracks)\n"
                      << "  [3] none               (no GPS in video)\n";
            std::string g = promptWizard("  Select [1-3] or type method", gpsMethod);
            if      (g == "1") gpsMethod = "exiftool_ligogps";
            else if (g == "2") gpsMethod = "standard";
            else if (g == "3") gpsMethod = "none";
            else               gpsMethod = g;
        }
        // [6] GPS lock offset
        else if (sel == 6) {
            std::string s = promptWizard("  GPS lock offset (seconds)",
                                          std::to_string(gpsOffset));
            try { gpsOffset = std::stoi(s); } catch (...) {}
        }
        // [7] Profile name
        else if (sel == 7) {
            profileName = promptWizard("  Profile name", profileName);
        }
        else {
            std::cout << "  Unknown input: \"" << cmd << "\"  -- enter 1-7, CONFIRM, or Q\n";
        }
    }

    // Build dirRoles from confirmed slots
    std::vector<std::pair<std::string,std::string>> dirRoles;
    if (!roleFront.empty()) dirRoles.push_back({roleFront, "front"});
    if (!roleRear.empty())  dirRoles.push_back({roleRear,  "rear"});
    if (!roleLeft.empty())  dirRoles.push_back({roleLeft,  "left"});
    if (!roleRight.empty()) dirRoles.push_back({roleRight, "right"});

    // ---- Build CameraProfile-format JSON ----
    json j;

    std::string sanitized = sanitizeProfileName(profileName);
    if (sanitized.empty()) sanitized = "custom_profile";

    j["name"]        = profileName;
    j["profile_id"]  = sanitized;

    // filename_regex:
    //   subdir layout — group 1 = timestamp; camera identity from scan_subdir
    //   flat multi-camera — group 1 = timestamp, group 2 = camera token
    if (isFlatMulti) {
        if (flatAnalysis.isPrefix)
            j["filename_regex"] = flatAnalysis.prefixGroup1
                                 + timestampRegex(tsFmt) + extRegex(primaryExt);
        else
            j["filename_regex"] = timestampRegex(tsFmt)
                                 + flatAnalysis.suffixGroup2 + extRegex(primaryExt);
        if (flatAnalysis.tokenCaptureGroup != 2)
            j["token_capture_group"]     = flatAnalysis.tokenCaptureGroup;
        if (flatAnalysis.tsCaptureGroup != 1)
            j["timestamp_capture_group"] = flatAnalysis.tsCaptureGroup;
    } else {
        j["filename_regex"] = timestampRegex(tsFmt) + suffix.pattern + extRegex(primaryExt);
    }

    // timestamp_format / timestamp_source
    std::string spFmt = strptimeFmt(tsFmt);
    if (spFmt.empty()) {
        // No time-of-day in filename (e.g. YYYYMMDD_NNNN) — read from metadata
        j["timestamp_format"] = "";
        j["timestamp_source"] = "exiftool_metadata";
    } else {
        j["timestamp_format"] = spFmt;
        j["timestamp_source"] = "filename";
    }

    j["timestamp_timezone"] = tzLower;
    j["container_ext"]      = primaryExt;
    j["thumbnail_method"]   = thumbnailMethodFrom(thumbSource, thumbPattern);
    j["gps_method"]         = gpsMethod;

    // default_layout from camera count
    int camCount = static_cast<int>(dirRoles.size());
    j["default_layout"] = (camCount == 1) ? "single"
                        : (camCount == 2) ? "side_by_side"
                        : "2x2";

    // slots array — standard role order
    const std::vector<std::string> roleOrder = {"front", "rear", "left", "right"};
    json jSlots = json::array();

    auto capitalize = [](const std::string& s) -> std::string {
        if (s.empty()) return s;
        return std::string(1, static_cast<char>(std::toupper((unsigned char)s[0]))) + s.substr(1);
    };

    if (isFlatMulti) {
        // Flat multi-camera: filenameToken discriminates cameras; scan_subdir is empty
        // (all cameras share the same directory, which is the footage root itself)
        for (const auto& role : roleOrder) {
            const std::string& tok = (role == "front") ? roleFront
                                   : (role == "rear")  ? roleRear
                                   : (role == "left")  ? roleLeft
                                   : roleRight;
            if (tok.empty()) continue;
            json s;
            s["name"]           = role;
            s["display"]        = capitalize(role);
            s["filename_token"] = tok;
            s["scan_subdir"]    = tokenScanSubdir;
            s["is_primary"]     = (role == "front");
            jSlots.push_back(s);
        }
    } else {
        // Subdir layout: scan_subdir is the camera directory; filenameToken unused
        for (const auto& role : roleOrder) {
            for (const auto& [dir, r] : dirRoles) {
                if (r != role) continue;
                json s;
                s["name"]           = role;
                s["display"]        = capitalize(role);
                s["filename_token"] = "";
                s["scan_subdir"]    = dir;
                s["is_primary"]     = (role == "front");
                jSlots.push_back(s);
                break;
            }
        }
        for (const auto& [dir, role] : dirRoles) {
            bool standard = false;
            for (const auto& r : roleOrder) if (r == role) { standard = true; break; }
            if (!standard) {
                json s;
                s["name"]           = role;
                s["display"]        = role;
                s["filename_token"] = "";
                s["scan_subdir"]    = dir;
                s["is_primary"]     = false;
                jSlots.push_back(s);
            }
        }
    }
    j["slots"] = jSlots;

    // ---- Save ----
    std::string profilesDir = Pathmux::Platform::getConfigDir() + "profiles";
    std::error_code ec;
    fs::create_directories(profilesDir, ec);
    if (ec) {
        std::cerr << "Error creating profiles directory: " << ec.message() << "\n";
        return;
    }

    std::string outPath = profilesDir + "/" + sanitized + ".json";
    std::ofstream ofs(outPath);
    if (!ofs) {
        std::cerr << "Error: cannot write to " << outPath << "\n";
        return;
    }
    ofs << j.dump(2) << "\n";
    ofs.close();

    std::cout << "Profile saved: " << outPath << "\n";

    // ---- Trial scan ----
    std::cout << "\nRunning trial scan on: " << scanRoot << "\n";
    try {
        CameraProfile cp = CameraProfile::loadFromFile(outPath);
        if (!cp.isValid()) {
            std::cerr << "Warning: saved profile failed validation check — skipping trial scan.\n";
            return;
        }
        TripDetection td;
        auto trips = td.detectTrips(
            scanRoot,
            cp,
            900,    // default gap threshold
            5,      // fuzzy window seconds
            ffprobePath,
            exiftoolPath
        );
        std::cout << "Trial scan complete: " << trips.size() << " trip(s) detected.\n";
        for (size_t i = 0; i < trips.size() && i < 10; ++i) {
            const auto& t = trips[i];
            std::cout << "  Trip " << (i + 1) << ": " << t.date << " " << t.startTime
                      << "  " << t.segments.size() << " segment(s)"
                      << "  " << t.duration << "\n";
        }
        if (trips.size() > 10)
            std::cout << "  ... (" << trips.size() - 10 << " more)\n";
    } catch (const std::exception& e) {
        std::cerr << "Trial scan error: " << e.what() << "\n";
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    bool jsonMode   = false;
    bool cardMode   = false;
    bool wizardMode = false;
    std::string cardPath;
    std::string wizardPath;
    std::string targetArg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { printUsage(argv[0]); return 0; }
        else if (arg == "-v" || arg == "--version") {
            std::cout << APP_NAME << " pm_probe v" << APP_VERSION << "\n"; return 0;
        }
        else if (arg == "--json")   { jsonMode = true; }
        else if (arg == "--card") {
            cardMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') cardPath = argv[++i];
        } else if (arg == "--wizard") {
            wizardMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') wizardPath = argv[++i];
        } else if (arg[0] != '-') {
            targetArg = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n"; printUsage(argv[0]); return 1;
        }
    }

    ConfigManager config;
    std::string ffprobePath  = config.getFfprobePath();
    std::string exiftoolPath = config.getExiftoolPath();
    std::string exiftoolOpts = "-ee3";

    // ---- Wizard mode ----
    if (wizardMode) {
        if (wizardPath.empty()) wizardPath = targetArg;
        if (wizardPath.empty()) {
            std::cerr << "Error: --wizard requires a path argument.\n";
            return 1;
        }
        wizardCard(wizardPath, ffprobePath, exiftoolPath, exiftoolOpts);
        return 0;
    }

    // ---- Card mode ----
    if (cardMode) {
        if (cardPath.empty()) {
            // Allow: pm_probe --card /path  OR  pm_probe /path --card
            cardPath = targetArg;
        }
        if (cardPath.empty()) {
            std::cerr << "Error: --card requires a path argument.\n";
            return 1;
        }
        probeCard(cardPath, jsonMode, ffprobePath, exiftoolPath, exiftoolOpts);
        return 0;
    }

    // ---- Single-file / MID:TID mode ----
    if (targetArg.empty()) { printUsage(argv[0]); return 1; }

    std::string filePath = targetArg;

    // MID:TID resolution
    if (targetArg.find(':') != std::string::npos && targetArg[0] != '/') {
        filePath = resolveMidTid(targetArg);
        if (filePath.empty()) return 1;
    }

    if (!fs::exists(filePath)) {
        std::cerr << "Error: file not found: " << filePath << "\n";
        return 1;
    }

    FileProbe probe = probeFile(filePath, ffprobePath, exiftoolPath, exiftoolOpts);

    if (jsonMode) {
        json j;
        j["file"]      = probe.filePath;
        j["container"] = probe.container;
        j["duration_sec"] = probe.durationSec;
        j["video"] = {
            {"width",        probe.video.width},
            {"height",       probe.video.height},
            {"frame_rate",   probe.video.frameRate},
            {"pix_fmt",      probe.video.pixFmt},
            {"color_range",  probe.video.colorRange},
            {"color_space",  probe.video.colorSpace}
        };
        json jStreams = json::array();
        for (const auto& s : probe.streams)
            jStreams.push_back({{"index", s.index}, {"type", s.codecType},
                                {"codec", s.codecName}, {"tag", s.codecTag}});
        j["streams"] = jStreams;
        j["gps"] = {
            {"method",        probe.gps.method},
            {"stream_index",  probe.gps.streamIndex},
            {"exiftool_note", probe.gps.exiftoolNote},
            {"has_fix",       probe.gps.hasFix},
            {"first_timestamp", probe.gps.firstTimestamp},
            {"first_lat",     probe.gps.firstLat},
            {"first_lon",     probe.gps.firstLon}
        };
        std::cout << j.dump(2) << "\n";
    } else {
        printFileProbe(probe);
    }

    return 0;
}
// SN: 00104
