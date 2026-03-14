// pm_gpsinfo — standalone GPS inspection tool for PathMux suite.
// Runs exiftool on a Pruveeo D90 .ts file and reports GPS data.
//
// Usage:
//   pm_gpsinfo [options] <file.ts>
//   pm_gpsinfo --scan-all-trips [options]
//
// Options:
//   --first-lock         Report first GPS fix with non-zero lat/lon (default)
//   --all                Report all GPS records
//   --json               Output as JSON (default)
//   --csv                Output as CSV
//   --xml                Output as XML
//   --text               Human-readable report
//   --scan-all-trips     Scan first segment of every trip in all manifests
//   --exiftool PATH      Path to exiftool binary (default: exiftool)
//   --exiftool-opts O    Extraction options (default: -ee3 ...)
//

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <filesystem>

#include "compat.hpp"
#include "config_manager.hpp"
#include "platform.hpp"
#include "version.hpp"
namespace fs = std::filesystem;
using namespace Pathmux;

// ---------------------------------------------------------------------------
// Epoch helpers — mirrors pm_findgpslock logic
// ---------------------------------------------------------------------------

// "20260226_084009F.ts" → time_t (local time, via mktime)
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

// "YYYY:MM:DD HH:MM:SS" (GPS UTC, as stored in GpsRecord::timestamp) → time_t
static time_t gpsTimestampToEpoch(const std::string& ts)
{
    if (ts.size() < 19) return 0;
    struct tm t = {};
    std::istringstream ss(ts);
    ss >> std::get_time(&t, "%Y:%m:%d %H:%M:%S");
    if (ss.fail()) return 0;
    return timegm(&t);
}

// ---------------------------------------------------------------------------
// Minimal JSON output helpers (hand-rolled to avoid json.hpp in output paths)
// ---------------------------------------------------------------------------

struct GpsRecord {
    std::string timestamp;
    double lat     = 0.0;
    double lon     = 0.0;
    double speed   = -1.0;   // km/h; -1 = not in stream
    double heading = -1.0;   // degrees; -1 = not in stream
    int    index   = 0;      // 0-based record index in stream (≈ seconds from segment start)
};

// One row of output for --scan-all-trips
struct LockScanResult {
    std::string manifestId;
    std::string tripId;
    std::string firstSegFile;  // basename of first Front segment
    int         segDur  = 0;   // segment duration in seconds (from manifest)
    int         lockSec = -1;  // seconds to first GPS lock; -1 = no lock found
    bool        fileErr = false; // true if segment file could not be opened
};

// ---------------------------------------------------------------------------
// jsonEscape — minimal JSON string escaping
// ---------------------------------------------------------------------------
static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// csvEscape — wrap in quotes if contains comma, quote, or newline
// ---------------------------------------------------------------------------
static std::string csvEscape(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else          out += c;
    }
    out += "\"";
    return out;
}

// ---------------------------------------------------------------------------
// xmlEscape
// ---------------------------------------------------------------------------
static std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if      (c == '&')  out += "&amp;";
        else if (c == '<')  out += "&lt;";
        else if (c == '>')  out += "&gt;";
        else if (c == '"')  out += "&quot;";
        else if (c == '\'') out += "&apos;";
        else                out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// printUsage
// ---------------------------------------------------------------------------
static void printUsage(const char* argv0) {
    std::cerr
        << "pm_gpsinfo — GPS inspection tool (PathMux suite)\n\n"
        << "Usage:\n"
        << "  " << argv0 << " [options] <file.ts>\n"
        << "  " << argv0 << " [options] MID:TID\n"
        << "  " << argv0 << " --scan-all-trips [options]\n\n"
        << "Single-file options:\n"
        << "  --first-lock       Report first valid GPS fix (default)\n"
        << "  --all              Report all GPS records\n"
        << "  --json             JSON output (default)\n"
        << "  --text             Human-readable report\n"
        << "  --csv              CSV output\n"
        << "  --xml              XML output\n\n"
        << "Batch scan options:\n"
        << "  --scan-all-trips   Scan first segment of every trip across all\n"
        << "                     manifests; report seconds to first GPS lock\n\n"
        << "Common options:\n"
        << "  --exiftool PATH    Path to exiftool binary\n"
        << "  --exiftool-opts O  Extraction options (default: -ee3 ...)\n"
        << "  -v, --version      Show version and exit\n\n"
        << "Notes:\n"
        << "  MID:TID addresses a trip by manifest ID and trip ID (e.g. F0:4P).\n"
        << "  GPS extraction requires exiftool to be installed and configured.\n"
        << "  Speed in km/h.  Altitude not reported (D90 values unreliable).\n"
        << "  --scan-all-trips reads manifests from ~/.config/pathmux/\n";
}

// ---------------------------------------------------------------------------
// runExiftool — parse GPS stream from file, return all records.
// Uses extended format to get speed and heading if available.
// ---------------------------------------------------------------------------
static std::vector<GpsRecord> runExiftool(const std::string& filePath,
                                           const std::string& exiftoolPath,
                                           const std::string& exiftoolOpts)
{
    std::vector<GpsRecord> records;

    std::string cmd = exiftoolPath + " " + exiftoolOpts +
        " \"" + filePath + "\" " NULL_REDIRECT;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "Error: Could not run exiftool. "
                  << "Is it installed and in PATH?\n";
        return records;
    }

    char linebuf[512];
    int idx = 0;

    while (fgets(linebuf, sizeof(linebuf), pipe)) {
        std::string line(linebuf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'
                                  || line.back() == ' '))
            line.pop_back();
        if (line.empty()) { ++idx; continue; }

        std::istringstream iss(line);
        std::string datePart, timePart;
        double lat, lon, speed = -1.0, heading = -1.0;

        if (!(iss >> datePart >> timePart >> lat >> lon)) {
            ++idx; continue;
        }
        // Speed and heading are optional — don't fail if absent
        iss >> speed >> heading;

        GpsRecord r;
        r.timestamp = datePart + " " + timePart;
        r.lat       = lat;
        r.lon       = lon;
        r.speed     = speed;
        r.heading   = heading;
        r.index     = idx;
        records.push_back(r);
        ++idx;
    }
    pclose(pipe);
    return records;
}

// ---------------------------------------------------------------------------
// Output helpers — single-file modes
// ---------------------------------------------------------------------------

static void outputJson(const std::vector<GpsRecord>& recs,
                       const std::string& filePath,
                       bool firstLockOnly)
{
    std::cout << "{\n";
    std::cout << "  \"file\": \"" << jsonEscape(filePath) << "\",\n";
    std::cout << "  \"total_records\": " << recs.size() << ",\n";

    // Find first lock
    int firstLockIdx = -1;
    for (int i = 0; i < (int)recs.size(); ++i) {
        if (recs[i].lat != 0.0 && recs[i].lon != 0.0) {
            firstLockIdx = i;
            break;
        }
    }

    std::cout << "  \"first_lock_record\": "
              << firstLockIdx << ",\n";

    if (firstLockIdx >= 0) {
        const auto& r = recs[firstLockIdx];
        std::cout << "  \"first_lock\": {\n";
        std::cout << "    \"timestamp_utc\": \"" << jsonEscape(r.timestamp) << "\",\n";
        std::cout << "    \"record_index\": " << r.index << ",\n";
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "    \"lat\": " << r.lat << ",\n";
        std::cout << "    \"lon\": " << r.lon << "\n";
        std::cout << "  }";
    } else {
        std::cout << "  \"first_lock\": null";
    }

    if (!firstLockOnly) {
        std::cout << ",\n  \"records\": [\n";
        for (int i = 0; i < (int)recs.size(); ++i) {
            const auto& r = recs[i];
            std::cout << "    {\n";
            std::cout << "      \"index\": " << r.index << ",\n";
            std::cout << "      \"timestamp_utc\": \"" << jsonEscape(r.timestamp) << "\",\n";
            std::cout << std::fixed << std::setprecision(6);
            std::cout << "      \"lat\": " << r.lat << ",\n";
            std::cout << "      \"lon\": " << r.lon;
            if (r.speed   >= 0.0) std::cout << ",\n      \"speed_kmh\": "  << std::fixed << std::setprecision(1) << r.speed;
            if (r.heading >= 0.0) std::cout << ",\n      \"heading\": "    << std::fixed << std::setprecision(1) << r.heading;
            std::cout << "\n    }";
            if (i + 1 < (int)recs.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "  ]";
    }

    std::cout << "\n}\n";
}

static void outputCsv(const std::vector<GpsRecord>& recs,
                      const std::string& filePath,
                      bool firstLockOnly)
{
    std::cout << "file,record_index,timestamp_utc,lat,lon,speed_kmh,heading\n";

    auto printRow = [&](const GpsRecord& r) {
        std::cout << csvEscape(filePath) << ","
                  << r.index << ","
                  << csvEscape(r.timestamp) << ","
                  << std::fixed << std::setprecision(6) << r.lat << ","
                  << r.lon << ","
                  << (r.speed   >= 0.0 ? std::to_string(r.speed)   : "") << ","
                  << (r.heading >= 0.0 ? std::to_string(r.heading)  : "")
                  << "\n";
    };

    if (firstLockOnly) {
        for (const auto& r : recs) {
            if (r.lat != 0.0 && r.lon != 0.0) { printRow(r); break; }
        }
    } else {
        for (const auto& r : recs) printRow(r);
    }
}

static void outputXml(const std::vector<GpsRecord>& recs,
                      const std::string& filePath,
                      bool firstLockOnly)
{
    std::cout << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    std::cout << "<pm_gpsinfo>\n";
    std::cout << "  <file>" << xmlEscape(filePath) << "</file>\n";
    std::cout << "  <total_records>" << recs.size() << "</total_records>\n";

    // Find first lock
    int firstLockIdx = -1;
    for (int i = 0; i < (int)recs.size(); ++i) {
        if (recs[i].lat != 0.0 && recs[i].lon != 0.0) {
            firstLockIdx = i; break;
        }
    }

    if (firstLockIdx >= 0) {
        const auto& r = recs[firstLockIdx];
        std::cout << "  <first_lock>\n";
        std::cout << "    <record_index>" << r.index << "</record_index>\n";
        std::cout << "    <timestamp_utc>" << xmlEscape(r.timestamp) << "</timestamp_utc>\n";
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "    <lat>" << r.lat << "</lat>\n";
        std::cout << "    <lon>" << r.lon << "</lon>\n";
        std::cout << "  </first_lock>\n";
    } else {
        std::cout << "  <first_lock/>\n";
    }

    if (!firstLockOnly) {
        std::cout << "  <records>\n";
        for (const auto& r : recs) {
            std::cout << "    <record>\n";
            std::cout << "      <index>" << r.index << "</index>\n";
            std::cout << "      <timestamp_utc>" << xmlEscape(r.timestamp) << "</timestamp_utc>\n";
            std::cout << std::fixed << std::setprecision(6);
            std::cout << "      <lat>" << r.lat << "</lat>\n";
            std::cout << "      <lon>" << r.lon << "</lon>\n";
            if (r.speed   >= 0.0) std::cout << "      <speed_kmh>" << std::fixed << std::setprecision(1) << r.speed   << "</speed_kmh>\n";
            if (r.heading >= 0.0) std::cout << "      <heading>"   << std::fixed << std::setprecision(1) << r.heading << "</heading>\n";
            std::cout << "    </record>\n";
        }
        std::cout << "  </records>\n";
    }

    std::cout << "</pm_gpsinfo>\n";
}

// ---------------------------------------------------------------------------
// outputText — human-readable report
// ---------------------------------------------------------------------------
static void outputText(const std::vector<GpsRecord>& recs,
                       const std::string& filePath,
                       bool firstLockOnly)
{
    std::cout << "File:           " << filePath << "  (filename = local time)\n";
    std::cout << "Total records:  " << recs.size() << "\n";

    int validCount   = 0;
    int firstLockIdx = -1;
    for (int i = 0; i < (int)recs.size(); ++i) {
        if (recs[i].lat != 0.0 && recs[i].lon != 0.0) {
            if (firstLockIdx < 0) firstLockIdx = i;
            ++validCount;
        }
    }

    std::cout << "Valid fixes:    " << validCount;
    if (!recs.empty())
        std::cout << "  (" << (validCount * 100 / (int)recs.size()) << "% coverage)";
    std::cout << "\n";

    if (firstLockIdx >= 0) {
        const auto& r = recs[firstLockIdx];
        std::cout << "First lock:     record " << r.index
                  << "  (" << r.index << "s after segment start)\n";
        std::cout << "  Timestamp:    " << r.timestamp << "  (UTC)\n";
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "  Position:     " << r.lat << ", " << r.lon << "\n";
        if (r.speed >= 0.0)
            std::cout << std::fixed << std::setprecision(1)
                      << "  Speed:        " << r.speed << " km/h  ("
                      << (r.speed * 0.621371) << " mph)\n";
        if (r.heading >= 0.0)
            std::cout << std::fixed << std::setprecision(1)
                      << "  Heading:      " << r.heading << "\xc2\xb0\n";
    } else {
        std::cout << "First lock:     none (no valid GPS fixes found)\n";
    }

    if (!firstLockOnly && !recs.empty()) {
        int lastValidIdx = -1;
        for (int i = (int)recs.size() - 1; i >= 0; --i) {
            if (recs[i].lat != 0.0 && recs[i].lon != 0.0) {
                lastValidIdx = i; break;
            }
        }
        if (lastValidIdx >= 0 && lastValidIdx != firstLockIdx) {
            const auto& r = recs[lastValidIdx];
            std::cout << "Last fix:       record " << r.index << "\n";
            std::cout << "  Timestamp:    " << r.timestamp << "  (UTC)\n";
            std::cout << std::fixed << std::setprecision(6);
            std::cout << "  Position:     " << r.lat << ", " << r.lon << "\n";
        }

        // Report dropouts (sequences of zero-coord records > 5s)
        int dropoutCount = 0;
        int dropoutStart = -1;
        for (int i = 0; i < (int)recs.size(); ++i) {
            bool zero = (recs[i].lat == 0.0 && recs[i].lon == 0.0);
            if (zero  && dropoutStart < 0) dropoutStart = i;
            if (!zero && dropoutStart >= 0) {
                if ((i - dropoutStart) > 5) ++dropoutCount;
                dropoutStart = -1;
            }
        }
        if (dropoutStart >= 0 && ((int)recs.size() - dropoutStart) > 5)
            ++dropoutCount;

        if (dropoutCount > 0)
            std::cout << "GPS dropouts:   " << dropoutCount << " gap(s) > 5s\n";
        else if (validCount > 0)
            std::cout << "GPS dropouts:   none\n";
    }
}

// ---------------------------------------------------------------------------
// scanAllTrips — batch mode: find all manifests, scan first segment per trip.
// Loads manifests via ConfigManager, writes gpsLockSeconds back for any trip
// where a GPS lock is found.  Returns one LockScanResult per trip found.
// ---------------------------------------------------------------------------
static std::vector<LockScanResult> scanAllTrips(
    const std::string& exiftoolPath,
    const std::string& exiftoolOpts)
{
    std::vector<LockScanResult> results;

    ConfigManager config;
    auto index = config.loadManifestIndex();

    if (index.empty()) {
        std::cerr << "Error: No manifests found in "
                  << Platform::getConfigDir() << "manifests.json\n"
                  << "  Has pathmux been run at least once to build manifests?\n";
        return results;
    }

    for (const auto& entry : index) {
        auto trips = config.loadTripCache(entry.manifestFile);
        if (trips.empty()) continue;

        bool anyChanged = false;

        for (auto& trip : trips) {
            if (trip.segments.empty()) continue;

            const std::string& front = trip.segments[0].front;
            if (front == "-" || front.empty()) continue;

            // front is an absolute path in the manifest; extract basename for display.
            std::string baseName = pathBasename(front);

            LockScanResult r;
            r.manifestId   = entry.id;
            r.tripId       = trip.id;
            r.firstSegFile = baseName;
            r.segDur       = trip.segdur;

            // Verify file is accessible before handing to exiftool
            {
                std::ifstream fcheck(front);
                if (!fcheck.good()) {
                    r.fileErr = true;
                    results.push_back(r);
                    continue;
                }
            }

            std::cerr << "  Scanning " << entry.id << ":" << trip.id
                      << "  " << baseName << " ...\n";

            auto records = runExiftool(front, exiftoolPath, exiftoolOpts);
            for (const auto& rec : records) {
                if (rec.lat != 0.0 && rec.lon != 0.0) {
                    // Compute true lock offset via epoch arithmetic:
                    // GPS timestamp is UTC; filename is local time.
                    time_t fileEpoch = filenameToEpoch(
                                           fs::path(front).filename().string());
                    time_t gpsEpoch  = gpsTimestampToEpoch(rec.timestamp);
                    r.lockSec = (fileEpoch > 0 && gpsEpoch > 0)
                                ? static_cast<int>(gpsEpoch - fileEpoch)
                                : rec.index;
                    break;
                }
            }

            // Write lock time back into the Trip if found.
            // Only marks changed if the stored value differs.
            if (r.lockSec >= 0 && trip.gpsLockSeconds != r.lockSec) {
                trip.gpsLockSeconds = r.lockSec;
                anyChanged = true;
            }

            results.push_back(r);
        }

        // Persist updated trips for this manifest if any lock times changed.
        if (anyChanged)
            config.saveTripCache(entry.path, trips);
    }

    return results;
}

// ---------------------------------------------------------------------------
// outputLockTable — tabular output for --scan-all-trips
// ---------------------------------------------------------------------------
static void outputLockTable(const std::vector<LockScanResult>& results) {
    if (results.empty()) {
        std::cout << "No trips found in any manifest.\n";
        return;
    }

    // Fixed column widths; filename column expands to fit longest name
    const int W_MID  = 3;
    const int W_TID  = 3;
    const int W_DUR  = 6;
    const int W_LOCK = 6;

    int W_FILE = 20;
    for (const auto& r : results)
        W_FILE = std::max(W_FILE, (int)r.firstSegFile.size());

    // Right-pad or left-pad a string to width w
    auto rpad = [](const std::string& s, int w) -> std::string {
        if ((int)s.size() >= w) return s.substr(0, w);
        return s + std::string(w - s.size(), ' ');
    };
    auto lpad = [](const std::string& s, int w) -> std::string {
        if ((int)s.size() >= w) return s.substr(0, w);
        return std::string(w - s.size(), ' ') + s;
    };

    // Header
    std::cout << rpad("MID", W_MID) << "  "
              << rpad("TID", W_TID) << "  "
              << lpad("SegDur", W_DUR) << "  "
              << lpad("LockAt", W_LOCK) << "  "
              << "First Segment\n";

    // Divider
    std::cout << std::string(W_MID,  '-') << "  "
              << std::string(W_TID,  '-') << "  "
              << std::string(W_DUR,  '-') << "  "
              << std::string(W_LOCK, '-') << "  "
              << std::string(W_FILE, '-') << "\n";

    for (const auto& r : results) {
        std::string durStr  = std::to_string(r.segDur) + "s";
        std::string lockStr = r.fileErr    ? "!file"
                            : r.lockSec < 0 ? "none"
                            : std::to_string(r.lockSec) + "s";
        std::cout << rpad(r.manifestId,   W_MID) << "  "
                  << rpad(r.tripId,       W_TID) << "  "
                  << lpad(durStr,         W_DUR) << "  "
                  << lpad(lockStr,        W_LOCK) << "  "
                  << r.firstSegFile << "\n";
    }
}

// ---------------------------------------------------------------------------
// resolveMidTid — look up MID:TID in manifests, return path to first Front
// segment of that trip.  Returns "" and prints an error on failure.
// ---------------------------------------------------------------------------
static std::string resolveMidTid(const std::string& midtid)
{
    auto colon = midtid.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == midtid.size()) {
        std::cerr << "Error: '" << midtid << "' is not a valid MID:TID address.\n";
        return "";
    }

    // Normalize: uppercase, O→0, I→1, L→1  (mirrors ConfigManager::normalizeId)
    auto normalize = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            char u = std::toupper((unsigned char)c);
            if      (u == 'O') u = '0';
            else if (u == 'I') u = '1';
            else if (u == 'L') u = '1';
            out += u;
        }
        return out;
    };

    std::string mid = normalize(midtid.substr(0, colon));
    std::string tid = normalize(midtid.substr(colon + 1));

    ConfigManager config;
    auto index = config.loadManifestIndex();

    for (const auto& entry : index) {
        if (normalize(entry.id) != mid) continue;

        auto trips = config.loadTripCache(entry.manifestFile);
        for (const auto& trip : trips) {
            if (normalize(trip.id) != tid) continue;

            if (trip.segments.empty()) {
                std::cerr << "Error: " << midtid << " has no segments.\n";
                return "";
            }
            const std::string& front = trip.segments[0].front;
            if (front == "-" || front.empty()) {
                std::cerr << "Error: " << midtid << " has no Front segment.\n";
                return "";
            }
            return front;
        }
        std::cerr << "Error: trip '" << tid << "' not found in manifest '" << mid << "'.\n";
        return "";
    }
    std::cerr << "Error: manifest '" << mid << "' not found.\n";
    return "";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    if (argc < 2) { printUsage(argv[0]); return 1; }

    std::string filePath;
    std::string midTid;       // set when positional arg looks like MID:TID
    std::string exiftoolPath = "exiftool";
    std::string exiftoolOpts = "-ee3 -p '$GPSDateTime $GPSLatitude# $GPSLongitude# $GPSSpeed# $GPSTrack#'";
    bool firstLockOnly   = true;   // default mode
    bool scanAllTripsMode = false;
    std::string format   = "json"; // default format

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { printUsage(argv[0]); return 0; }
        else if (arg == "-v" || arg == "--version") {
            std::cout << APP_NAME << " pm_gpsinfo v" << APP_VERSION << "\n"; return 0;
        }
        else if (arg == "--first-lock")     { firstLockOnly = true; }
        else if (arg == "--all")            { firstLockOnly = false; }
        else if (arg == "--json")           { format = "json"; }
        else if (arg == "--text")           { format = "text"; }
        else if (arg == "--csv")            { format = "csv";  }
        else if (arg == "--xml")            { format = "xml";  }
        else if (arg == "--scan-all-trips") { scanAllTripsMode = true; }
        else if (arg == "--exiftool") {
            if (i + 1 < argc) exiftoolPath = argv[++i];
            else { std::cerr << "Error: --exiftool requires a path.\n"; return 1; }
        }
        else if (arg == "--exiftool-opts") {
            if (i + 1 < argc) exiftoolOpts = argv[++i];
            else { std::cerr << "Error: --exiftool-opts requires a value.\n"; return 1; }
        }
        else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]); return 1;
        }
        else {
            // Detect MID:TID addressing: contains ':' but no '/' (not a path)
            if (arg.find(':') != std::string::npos && arg.find('/') == std::string::npos) {
                midTid = arg;
            } else {
                if (!filePath.empty()) {
                    std::cerr << "Error: multiple file paths given.\n";
                    return 1;
                }
                filePath = arg;
            }
        }
    }

    // --scan-all-trips: batch mode — no file or MID:TID argument accepted
    if (scanAllTripsMode) {
        if (!filePath.empty() || !midTid.empty()) {
            std::cerr << "Error: --scan-all-trips does not accept a file or MID:TID argument.\n";
            return 1;
        }
        std::cerr << "Scanning all manifests for GPS lock times...\n";
        auto results = scanAllTrips(exiftoolPath, exiftoolOpts);
        std::cerr << "\n";
        outputLockTable(results);
        return 0;
    }

    // Resolve MID:TID to an absolute file path if that form was given
    if (!midTid.empty()) {
        filePath = resolveMidTid(midTid);
        if (filePath.empty()) return 1;
    }

    // Single-file mode
    if (filePath.empty()) {
        std::cerr << "Error: no input file specified.\n";
        printUsage(argv[0]); return 1;
    }

    auto records = runExiftool(filePath, exiftoolPath, exiftoolOpts);

    if (records.empty()) {
        std::cerr << "Warning: no GPS records found in " << filePath << "\n"
                  << "  Verify exiftool is installed and options match your camera.\n"
                  << "  If your camera's GPS format is unsupported, contact https://exiftool.org\n";
        // Still output empty result in requested format
    }

    if      (format == "csv")  outputCsv(records, filePath, firstLockOnly);
    else if (format == "xml")  outputXml(records, filePath, firstLockOnly);
    else if (format == "text") outputText(records, filePath, firstLockOnly);
    else                       outputJson(records, filePath, firstLockOnly);

    return 0;
}

// SN: 00086
