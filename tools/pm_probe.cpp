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
        + " -show_entries stream=index,codec_type,codec_name,codec_tag_string,sample_rate,channels"
        + " \"" + filePath + "\" 2>/dev/null";

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
        j["submit_to"] = "https://github.com/Nutball-Labs/PathMux/issues";

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
    auto slash = filename.rfind('/');
    std::string base = (slash != std::string::npos) ? filename.substr(slash + 1) : filename;

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
    auto cameraDirs = findCameraDirs(root);
    if (cameraDirs.empty()) {
        std::cerr << "No camera directories with video files found under: " << root << "\n";
        return;
    }

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

    // Thumbnail state
    bool anySidecar = std::any_of(cams.begin(), cams.end(),
                                   [](const CamData& c){ return c.hasSidecar; });
    bool allSidecarOk = anySidecar &&
        std::all_of(cams.begin(), cams.end(),
                    [](const CamData& c){ return !c.hasSidecar || c.sidecarOk; });

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

    // ---- Step 1: Confirm camera mappings ----
    // role slots — empty string means unassigned
    std::string roleFront, roleRear, roleLeft, roleRight;
    for (const auto& c : cams) {
        std::string r = guessCameraRole(c.dirName);
        if      (r == "front") roleFront = c.dirName;
        else if (r == "rear")  roleRear  = c.dirName;
        else if (r == "left")  roleLeft  = c.dirName;
        else if (r == "right") roleRight = c.dirName;
    }

    auto drawMappings = [&]() {
        std::cout << "\n--- Step 1: Confirm Camera Mappings ---\n\n";
        auto showRole = [](const std::string& label, const std::string& dir) {
            std::cout << "  " << std::left << std::setw(6) << label << " -> "
                      << (dir.empty() ? "(unassigned)" : dir + "/") << "\n";
        };
        showRole("front", roleFront);
        showRole("rear",  roleRear);
        showRole("left",  roleLeft);
        showRole("right", roleRight);

        // Show any dirs not assigned to any role
        std::vector<std::string> unassigned;
        for (const auto& c : cams)
            if (c.dirName != roleFront && c.dirName != roleRear &&
                c.dirName != roleLeft  && c.dirName != roleRight)
                unassigned.push_back(c.dirName);
        if (!unassigned.empty()) {
            std::cout << "\n  Unassigned:";
            for (const auto& d : unassigned) std::cout << "  " << d << "/";
            std::cout << "\n";
        }

        std::cout << "\nCONFIRM to accept"
                  << "  |  [F]ront  [B]ack/rear  [L]eft  [R]ight  to remap"
                  << "\n> ";
        std::cout.flush();
    };

    while (true) {
        drawMappings();

        std::string cmd;
        if (!std::getline(std::cin, cmd)) break;
        auto lt = cmd.find_first_not_of(" \t");
        if (lt != std::string::npos) {
            auto rt = cmd.find_last_not_of(" \t");
            cmd = cmd.substr(lt, rt - lt + 1);
        } else {
            cmd = "";
        }

        if (cmd == "CONFIRM") break;

        // Map F/B/L/R to the role slot and a display label
        std::string upper = cmd;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c){ return std::toupper(c); });

        std::string* slot    = nullptr;
        std::string  slotLabel;
        if      (upper == "F") { slot = &roleFront; slotLabel = "Front";     }
        else if (upper == "B") { slot = &roleRear;  slotLabel = "Back/Rear"; }
        else if (upper == "L") { slot = &roleLeft;  slotLabel = "Left";      }
        else if (upper == "R") { slot = &roleRight; slotLabel = "Right";     }
        // Unknown input → redraw
        if (!slot) continue;

        // Show numbered dir list for this role
        std::cout << "\nAssign " << slotLabel << " camera:\n";
        for (size_t i = 0; i < cams.size(); ++i)
            std::cout << "  [" << (i + 1) << "] " << cams[i].dirName << "/\n";
        std::cout << "  [0] (unassign)\n"
                  << "Select [0-" << cams.size() << "]: ";
        std::cout.flush();

        std::string sel;
        if (!std::getline(std::cin, sel)) continue;
        int idx = -1;
        try { idx = std::stoi(sel); } catch (...) { continue; }

        if (idx == 0) {
            *slot = "";
        } else if (idx >= 1 && idx <= (int)cams.size()) {
            // Unassign this dir from any other slot first
            std::string chosen = cams[idx - 1].dirName;
            if (roleFront == chosen) roleFront = "";
            if (roleRear  == chosen) roleRear  = "";
            if (roleLeft  == chosen) roleLeft  = "";
            if (roleRight == chosen) roleRight = "";
            *slot = chosen;
        }
        // loop → redraw
    }

    // Build dirRoles from confirmed slots
    std::vector<std::pair<std::string,std::string>> dirRoles; // (dirName, role)
    if (!roleFront.empty()) dirRoles.push_back({roleFront, "front"});
    if (!roleRear.empty())  dirRoles.push_back({roleRear,  "rear"});
    if (!roleLeft.empty())  dirRoles.push_back({roleLeft,  "left"});
    if (!roleRight.empty()) dirRoles.push_back({roleRight, "right"});

    // ---- Step 2: Filename timestamp format ----
    std::cout << "\n--- Step 2: Filename Timestamp Format ---\n";
    std::cout << "Sample filename: " << fs::path(primaryFiles[0]).filename().string() << "\n";
    if (!tsFmt.empty()) std::cout << "Detected format:  " << tsFmt << "\n";
    tsFmt = promptWizard("Timestamp format",
                          tsFmt.empty() ? "YYYYMMDD_HHMMSS" : tsFmt);

    std::string fnPattern = "^" + timestampRegex(tsFmt)
                          + suffix.pattern + "\\" + primaryExt + "$";
    std::cout << "  → Pattern: " << fnPattern << "\n";
    if (!suffix.determined)
        std::cout << "  Note: suffix pattern undetermined — review manually.\n";

    // ---- Step 3: Thumbnails ----
    std::cout << "\n--- Step 3: Thumbnails ---\n";
    std::string thumbSource;
    std::string thumbPattern;
    if (anySidecar) {
        std::cout << "Sidecar .jpg files detected.\n";
        if (allSidecarOk) {
            SuffixAnalysis thumbSfx = analyzeThumbSuffix(cameraDirs, tsFmt);
            thumbPattern = "^" + timestampRegex(tsFmt)
                         + thumbSfx.pattern + "\\.jpg$";
            std::cout << "  Timestamp match confirmed — pattern: " << thumbPattern << "\n";
            thumbSource = "sidecar_jpg";
        } else {
            std::cout << "  Warning: .jpg files found but basenames don't consistently\n"
                      << "  match video files. Set thumbnails.pattern manually in the\n"
                      << "  saved profile, or run:\n"
                      << "    pm_probe --card " << root << "\n"
                      << "  and file an issue at:\n"
                      << "    https://github.com/Nutball-Labs/PathMux/issues\n";
            thumbSource = "sidecar_jpg";
        }
    } else {
        std::cout << "No sidecar .jpg files detected.\n";
        thumbSource = "generate_ffmpeg";
    }
    thumbSource = promptWizard(
        "Thumbnail source (sidecar_jpg/generate_ffmpeg/none)", thumbSource);

    // ---- Step 4: Timezone ----
    std::cout << "\n--- Step 4: Timestamp Timezone ---\n"
              << "Are file timestamps in local time or UTC?\n";
    std::string tz = promptWizard("Timezone", "local");
    std::string tzLower = tz;
    std::transform(tzLower.begin(), tzLower.end(), tzLower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (tzLower != "utc") tzLower = "local";

    // ---- Step 5: GPS lock offset ----
    std::cout << "\n--- Step 5: GPS Cold-Start Offset ---\n"
              << "Approximate seconds before GPS acquires first lock (0 if unknown):\n";
    std::string gpsOffStr = promptWizard("GPS lock offset (seconds)", "0");
    int gpsOffset = 0;
    try { gpsOffset = std::stoi(gpsOffStr); } catch (...) {}

    // ---- Step 6: Profile name ----
    std::cout << "\n--- Step 6: Profile Name ---\n";
    std::string profileName =
        promptWizard("Profile name (e.g. 'Pruveeo D90', 'Cobra 4500')");
    if (profileName.empty()) {
        std::cout << "Profile name is required. Wizard aborted.\n";
        return;
    }

    // ---- Summary ----
    std::cout << "\n--- Profile Summary ---\n"
              << "Name:      " << profileName << "\n"
              << "Cameras:\n";
    for (const auto& [dir, role] : dirRoles)
        std::cout << "  " << role << " → " << dir << "/\n";
    std::cout << "Timestamp: " << tsFmt << " (" << tzLower << ")\n"
              << "Pattern:   " << fnPattern << "\n";
    if (primaryCam) {
        std::string gpsMethod = primaryCam->probe.gps.method;
        if (gpsMethod == "LIGOGPSINFO") gpsMethod = "exiftool_ligogps";
        std::cout << "GPS:       " << gpsMethod
                  << " (lock offset " << gpsOffset << "s)\n";
    }
    std::cout << "Thumbs:    " << thumbSource;
    if (!thumbPattern.empty()) std::cout << "  pattern: " << thumbPattern;
    std::cout << "\n";
    if (!durations.empty()) {
        std::cout << "Segments: ";
        for (int d : durations) std::cout << " " << d << "s";
        std::cout << "\n";
    }

    // ---- Confirm save ----
    std::string confirm = promptWizard("\nSave profile?", "y");
    std::string confirmLower = confirm;
    std::transform(confirmLower.begin(), confirmLower.end(), confirmLower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (confirmLower != "y" && confirmLower != "yes") {
        std::cout << "Wizard cancelled. No profile saved.\n";
        return;
    }

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
    {
        std::string gpsMethod = primaryCam ? primaryCam->probe.gps.method : "none";
        if (gpsMethod == "LIGOGPSINFO") gpsMethod = "exiftool_ligogps";
        j["gps"] = {{"method", gpsMethod}, {"start_offset_seconds", gpsOffset}};
    }

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

    const char* homeEnv = std::getenv("HOME");
    if (!homeEnv) {
        std::cerr << "Error: HOME not set, cannot determine config directory.\n";
        return;
    }
    std::string profilesDir = std::string(homeEnv) + "/.config/pathmux/profiles";
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
            std::cerr << "Unknown argument: " << arg << "\n"; return 1;
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
// SN: 00082
