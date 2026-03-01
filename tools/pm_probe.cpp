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
#include "json.hpp"

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
        << "       " << argv0 << " --card <path> [--json]\n\n"
        << "  <file.ts>    Probe a single segment\n"
        << "  MID:TID      Probe first Front segment of a known trip\n"
        << "  --card PATH  Fingerprint a dashcam storage root\n"
        << "  --json       Machine-readable JSON output\n\n"
        << "  --card output is suitable for a GitHub issue to request\n"
        << "  support for a new camera model.\n";
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
        + " -show_entries stream=index,codec_type,codec_name,codec_tag_string"
        + " \"" + filePath + "\" 2>/dev/null";

    std::string raw = runCmd(cmd);
    if (raw.empty()) return result;

    try {
        json j = json::parse(raw);
        if (!j.contains("streams")) return result;
        for (const auto& s : j["streams"]) {
            StreamInfo si;
            si.index     = s.value("index",             0);
            si.codecType = s.value("codec_type",        "");
            si.codecName = s.value("codec_name",        "");
            si.codecTag  = s.value("codec_tag_string",  "");
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
        + " \"" + filePath + "\" 2>/dev/null";

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

    // Check for LIGOGPSINFO private stream (codec_tag "LIGO")
    for (const auto& s : streams) {
        if (s.codecType == "data" && s.codecTag.find("LIGO") != std::string::npos) {
            g.method      = "LIGOGPSINFO";
            g.streamIndex = s.index;
            g.exiftoolNote = "GPS in private LIGO stream — requires exiftool with LIGOGPSINFO support";
            break;
        }
    }

    // Try exiftool extraction to confirm and grab a sample fix
    std::string cmd = exiftoolPath + " " + exiftoolOptions
        + " \"" + filePath + "\" 2>/dev/null";
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

        // If we got data but didn't detect LIGOGPSINFO by stream tag,
        // mark it as standard GPS metadata
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
            " \"" + filePath + "\" 2>/dev/null";
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
    auto slash = p.filePath.rfind('/');
    std::string fname = (slash != std::string::npos)
                        ? p.filePath.substr(slash + 1) : p.filePath;

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

// Find candidate camera directories under root
static std::vector<std::string> findCameraDirs(const std::string& root)
{
    std::vector<std::string> dirs;
    if (!fs::is_directory(root)) return dirs;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        // Only include dirs that contain video files
        auto vids = listVideoFiles(entry.path().string());
        if (!vids.empty())
            dirs.push_back(entry.path().string());
    }
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
        + " \"" + filePath + "\" 2>/dev/null";
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

    auto cameraDirs = findCameraDirs(root);
    if (cameraDirs.empty()) {
        std::cerr << "No camera directories with video files found under: " << root << "\n";
        return;
    }

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

    // Sample filenames (up to 3) with relative paths from root
    std::vector<std::string> sampleFiles;
    for (int i = 0; i < std::min((int)primaryFiles.size(), 3); ++i) {
        std::string rel = fs::relative(primaryFiles[i], root).string();
        sampleFiles.push_back(rel);
        // Also show matching thumbnail if present
        std::string thumbPath = primaryFiles[i];
        auto dot = thumbPath.rfind('.');
        if (dot != std::string::npos) thumbPath = thumbPath.substr(0, dot) + ".jpg";
        if (fs::exists(thumbPath))
            sampleFiles.push_back(fs::relative(thumbPath, root).string());
    }

    // ---- JSON output ----
    if (jsonMode) {
        json j;
        j["root"] = root;

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
        j["submit_to"] = "https://github.com/BiloxiGeek/PathMux/issues";

        std::cout << j.dump(2) << "\n";
        return;
    }

    // ---- Text output ----
    std::cout << "--- pm_probe SD Card Fingerprint ---\n"
              << "Root:         " << root << "\n";

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
              << "https://github.com/BiloxiGeek/PathMux/issues ---\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    bool jsonMode = false;
    bool cardMode = false;
    std::string cardPath;
    std::string targetArg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { printUsage(argv[0]); return 0; }
        else if (arg == "--json")           { jsonMode = true; }
        else if (arg == "--card") {
            cardMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') cardPath = argv[++i];
        } else if (arg[0] != '-') {
            targetArg = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n"; return 1;
        }
    }

    ConfigManager config;
    std::string ffprobePath  = config.getFfprobePath();
    std::string exiftoolPath = config.getExiftoolPath();
    std::string exiftoolOpts = config.getExiftoolOptions();

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
// SN: 00080
