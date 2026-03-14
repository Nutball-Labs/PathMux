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
            return trip.segments[0].front;
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
              << "Frame rate:   " << p.video.frameRate << "\n"
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

// List video files in a directory, sorted by name
static std::vector<std::string> listVideoFiles(const std::string& dir)
{
    std::vector<std::string> files;
    std::set<std::string> videoExts = {".ts", ".mp4", ".avi", ".mov", ".mkv"};
    if (!fs::is_directory(dir)) return files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
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
    if (byParent.empty()) return dirs;

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

// Detect file extensions present across a set of files
static std::vector<std::string> detectExtensions(const std::string& dir)
{
    std::set<std::string> exts;
    if (!fs::is_directory(dir)) return {};
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
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
              << "  Frame rate:   " << probe.video.frameRate << "\n"
              << "  Pixel format: " << probe.video.pixFmt;
    if (probe.video.colorRange == "pc" || probe.video.colorRange == "jpeg")
        std::cout << " (full-range)";
    std::cout << "\n"
              << "  Color space:  " << probe.video.colorSpace << "\n";

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

// Guess the timestamp format string from a sample filename (path or basename).
static std::string guessTimestampFormat(const std::string& filename)
{
    std::string base = pathBasename(filename);

    // YYYYMMDD_HHMMSS — e.g. 20260225_044424F.ts
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

    return "";
}

// Number of characters the timestamp occupies in the basename.
static int timestampTokenLen(const std::string& fmt)
{
    if (fmt == "YYYYMMDD_HHMMSS")     return 15;
    if (fmt == "YYYY-MM-DD_HH-MM-SS") return 19;
    return 0;
}

// Regex fragment for the timestamp portion.
static std::string timestampRegex(const std::string& fmt)
{
    if (fmt == "YYYYMMDD_HHMMSS")
        return "(\\d{8})_(\\d{6})";
    if (fmt == "YYYY-MM-DD_HH-MM-SS")
        return "(\\d{4})-(\\d{2})-(\\d{2})_(\\d{2})-(\\d{2})-(\\d{2})";
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
        c.hasSidecar = detectSidecarJpg(d);
        c.sidecarOk  = c.hasSidecar && sidecarTimestampMatch(d, tsFmt);
        if (!files.empty()) {
            std::cout << "  Probing " << c.dirName << "/ ...\n";
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

    // ---- Summary of what was found ----
    std::cout << "\nFound " << cams.size() << " camera director"
              << (cams.size() == 1 ? "y" : "ies") << ":\n";
    for (const auto& c : cams) {
        std::cout << "  " << c.dirName << "/  ("
                  << c.fileCount << " video file" << (c.fileCount == 1 ? "" : "s");
        if (c.hasSidecar) std::cout << ", .jpg sidecars";
        std::cout << ")\n";
        if (c.probe.video.width > 0)
            std::cout << "    video: " << c.probe.video.width << "x"
                      << c.probe.video.height << " @ " << c.probe.video.frameRate
                      << " fps, " << c.probe.video.pixFmt << "\n";
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

    // Camera role slots (empty = unassigned)
    std::string roleFront, roleRear, roleLeft, roleRight;
    // Attention flags — set when a user-entered dir fails validation
    bool frontAttn = false, rearAttn = false, leftAttn = false, rightAttn = false;
    for (const auto& c : cams) {
        std::string r = guessCameraRole(c.dirName);
        if      (r == "front") roleFront = c.dirName;
        else if (r == "rear")  roleRear  = c.dirName;
        else if (r == "left")  roleLeft  = c.dirName;
        else if (r == "right") roleRight = c.dirName;
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

    // Validate a camera subdirectory — must exist under scanRoot and contain video files
    auto validateCamDir = [&](const std::string& dirname) -> bool {
        if (dirname.empty()) return false;
        fs::path p = fs::path(scanRoot) / dirname;
        if (!fs::is_directory(p)) return false;
        return !listVideoFiles(p.string()).empty();
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

        // [1] Camera mappings — one per line, displayed as <path>/DirName/
        std::cout << wizRow("  [1]  Camera mappings") << "\n";
        auto showCam = [](const std::string& label, const std::string& dir, bool attn) {
            std::string s = "         " + label;
            while ((int)s.size() < 16) s += ' ';
            if (dir.empty()) s += "->  (unassigned)";
            else             s += "->  <path>/" + dir + "/";
            if (attn)        s += "  [!] needs attention";
            return s;
        };
        std::cout << wizRow(showCam("front",  roleFront, frontAttn)) << "\n";
        std::cout << wizRow(showCam("rear",   roleRear,  rearAttn))  << "\n";
        std::cout << wizRow(showCam("left",   roleLeft,  leftAttn))  << "\n";
        std::cout << wizRow(showCam("right",  roleRight, rightAttn)) << "\n";
        // Any dirs not yet assigned to a role
        for (const auto& c : cams)
            if (c.dirName != roleFront && c.dirName != roleRear &&
                c.dirName != roleLeft  && c.dirName != roleRight)
                std::cout << wizRow("         (unassigned)   ->  <path>/" + c.dirName + "/") << "\n";

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

        // [1] Camera mappings — F/B/L/R sub-menu (redraws until CONFIRM)
        if (sel == 1) {
            auto drawCams = [&]() {
                std::cout << "\n" << wizSep('-') << "\n";
                std::cout << "  <path> = " << scanRoot << "\n\n";
                auto showRole = [](const std::string& label, const std::string& dir, bool attn) {
                    std::string row = "  " + label;
                    while ((int)row.size() < 8) row += ' ';
                    row += " ->  ";
                    if (dir.empty()) row += "(unassigned)";
                    else             row += "<path>/" + dir + "/";
                    if (attn)        row += "  [!] needs attention";
                    std::cout << row << "\n";
                };
                showRole("front", roleFront, frontAttn);
                showRole("rear",  roleRear,  rearAttn);
                showRole("left",  roleLeft,  leftAttn);
                showRole("right", roleRight, rightAttn);
                std::vector<std::string> ua;
                for (const auto& c : cams)
                    if (c.dirName != roleFront && c.dirName != roleRear &&
                        c.dirName != roleLeft  && c.dirName != roleRight)
                        ua.push_back(c.dirName);
                if (!ua.empty()) {
                    std::cout << "\n  Unassigned:";
                    for (const auto& d : ua) std::cout << "  <path>/" << d << "/";
                    std::cout << "\n";
                }
                std::cout << "\n  CONFIRM to accept"
                          << "  |  [F]ront  [B]ack/rear  [L]eft  [R]ight  to remap\n> ";
                std::cout.flush();
            };
            while (true) {
                drawCams();
                std::string c2;
                if (!std::getline(std::cin, c2)) break;
                c2 = trimLine(c2);
                if (c2 == "CONFIRM") break;
                std::string u = c2;
                std::transform(u.begin(), u.end(), u.begin(),
                               [](unsigned char ch){ return std::toupper(ch); });
                std::string* slot     = nullptr;
                bool*        attnFlag = nullptr;
                std::string  slotLabel;
                if      (u == "F") { slot = &roleFront; attnFlag = &frontAttn; slotLabel = "Front";     }
                else if (u == "B") { slot = &roleRear;  attnFlag = &rearAttn;  slotLabel = "Back/Rear"; }
                else if (u == "L") { slot = &roleLeft;  attnFlag = &leftAttn;  slotLabel = "Left";      }
                else if (u == "R") { slot = &roleRight; attnFlag = &rightAttn; slotLabel = "Right";     }
                if (!slot) continue;
                std::cout << "\nRemap " << slotLabel << " camera.\n"
                          << "Type subdirectory name (blank to unassign):\n"
                          << "  <path>/ ";
                std::cout.flush();
                std::string sub;
                if (!std::getline(std::cin, sub)) continue;
                sub = trimLine(sub);
                // Strip any trailing slash the user may have typed
                while (!sub.empty() && sub.back() == '/') sub.pop_back();
                if (sub.empty()) {
                    *slot     = "";
                    *attnFlag = false;
                } else if (validateCamDir(sub)) {
                    // Release any other slot that already held this dirname
                    if (roleFront == sub) { roleFront = ""; frontAttn = false; }
                    if (roleRear  == sub) { roleRear  = ""; rearAttn  = false; }
                    if (roleLeft  == sub) { roleLeft  = ""; leftAttn  = false; }
                    if (roleRight == sub) { roleRight = ""; rightAttn = false; }
                    *slot     = sub;
                    *attnFlag = false;
                    std::cout << "  OK -- <path>/" << sub << "/ validated.\n";
                } else {
                    *slot     = sub;
                    *attnFlag = true;
                    std::cout << "  Warning: <path>/" << sub
                              << "/ not found or has no video files.\n";
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

    // ---- Build JSON ----
    json j;
    j["profile_name"] = profileName;
    j["version"]      = "1.0";

    // detection block
    {
        json jDirNames = json::array();
        for (const auto& [dir, role] : dirRoles) jDirNames.push_back(dir);
        j["detection"] = {
            {"directory_names",  jDirNames},
            {"file_extension",   primaryExt},
            {"filename_pattern", fnPattern}
        };
    }

    // cameras block — standard order then any others
    {
        json jCams;
        int priority = 1;
        const std::vector<std::string> roleOrder = {"front", "rear", "left", "right"};

        auto addCam = [&](const std::string& role, const std::string& dir) {
            json cam;
            cam["dir"]      = dir;
            cam["priority"] = priority++;
            for (const auto& c : cams) {
                if (c.dirName != dir) continue;
                if (c.probe.video.width > 0) {
                    cam["video"] = {
                        {"width",       c.probe.video.width},
                        {"height",      c.probe.video.height},
                        {"frame_rate",  c.probe.video.frameRate},
                        {"pix_fmt",     c.probe.video.pixFmt},
                        {"color_space", c.probe.video.colorSpace}
                    };
                }
                for (const auto& s : c.probe.streams) {
                    if (s.codecType != "audio") continue;
                    json aud;
                    aud["codec"] = s.codecName;
                    if (!s.sampleRate.empty())
                        try { aud["sample_rate_hz"] = std::stoi(s.sampleRate); }
                        catch (...) {}
                    if (s.channels > 0) aud["channels"] = s.channels;
                    cam["audio"] = aud;
                    break;
                }
                break;
            }
            jCams[role] = cam;
        };

        for (const auto& role : roleOrder)
            for (const auto& [dir, r] : dirRoles)
                if (r == role) { addCam(role, dir); break; }

        for (const auto& [dir, role] : dirRoles) {
            bool standard = false;
            for (const auto& r : roleOrder) if (r == role) { standard = true; break; }
            if (!standard) addCam(role, dir);
        }

        j["cameras"] = jCams;
    }

    j["timestamp"] = {{"format", tsFmt}, {"timezone", tzLower}};

    // gps block
    j["gps"] = {{"method", gpsMethod}, {"start_offset_seconds", gpsOffset}};

    // thumbnails block
    {
        json jThumb;
        jThumb["source"] = thumbSource;
        if (!thumbPattern.empty()) jThumb["pattern"] = thumbPattern;
        j["thumbnails"] = jThumb;
    }

    if (!durations.empty()) j["segment_duration_seconds"] = durations;

    // ---- Save ----
    std::string sanitized = sanitizeProfileName(profileName);
    if (sanitized.empty()) sanitized = "custom_profile";

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

    std::cout << "Profile saved: " << outPath << "\n"
              << "Note: trial scan requires the CameraProfile C++ layer"
              << " (not yet implemented).\n";
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
            std::cerr << "Unknown argument: " << arg << "\n"; printUsage(argv[0]); return 1;
        }
    }

    ConfigManager config;
    std::string ffprobePath  = config.getFfprobePath();
    std::string exiftoolPath = config.getExiftoolPath();
    std::string exiftoolOpts = config.getExiftoolOptions();

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
// SN: 00083
