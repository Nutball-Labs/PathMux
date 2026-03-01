#include "gpx_export.hpp"
#include "config_manager.hpp"
#include "ui_helpers.hpp"
#include "json.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace Pathmux;

// ===========================================================================
// Static helpers
// ===========================================================================

// ---------------------------------------------------------------------------
// resolveManifestFile
// ---------------------------------------------------------------------------
std::string GpxExport::resolveManifestFile(const std::string& pathOrStem)
{
    ConfigManager config;

    if (pathOrStem.empty()) {
        std::string lp = config.getLastPath();
        if (lp.empty()) return "";
        return resolveManifestFile(lp);
    }

    // Already an absolute path to an existing .json file?
    if (pathOrStem[0] == '/' && fs::exists(pathOrStem))
        return pathOrStem;

    // Absolute filesystem source path → derive cache filename
    if (pathOrStem[0] == '/') {
        if (!config.isCached(pathOrStem)) return "";
        const char* home = getenv("HOME");
        if (!home) return "";
        std::string san = pathOrStem.substr(1);
        for (char& c : san) if (c == '/') c = '_';
        return std::string(home) + "/.config/pathmux/" + san + ".json";
    }

    // No slashes → treat as cache stem
    const char* home = getenv("HOME");
    if (!home) return "";
    std::string candidate = std::string(home) + "/.config/pathmux/" + pathOrStem + ".json";
    if (fs::exists(candidate)) return candidate;
    return "";
}

// ---------------------------------------------------------------------------
// loadManifest
// ---------------------------------------------------------------------------
bool GpxExport::loadManifest(const std::string& manifestFile, json& root)
{
    if (manifestFile.empty()) {
        std::cerr << "Error: No manifest path resolved. Run a scan first.\n";
        return false;
    }
    std::ifstream ifs(manifestFile);
    if (!ifs.is_open()) {
        std::cerr << "Error: Cannot open manifest: " << manifestFile << "\n";
        return false;
    }
    try {
        ifs >> root;
    } catch (const json::parse_error& e) {
        std::cerr << "Error: JSON parse error in " << manifestFile
                  << "\n  " << e.what() << "\n";
        return false;
    }
    if (!root.contains("trips") || !root["trips"].is_array()) {
        std::cerr << "Error: No 'trips' array in manifest.\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// buildStem — derive default filename stem from segments[0].front
//   "/z/srcdash/ex6/Front/20260216_095422F.ts"  →  "20260216_095422"
// ---------------------------------------------------------------------------
std::string GpxExport::buildStem(const json& jTrip)
{
    if (!jTrip.contains("segments") || !jTrip["segments"].is_array()
            || jTrip["segments"].empty())
        return "pathmux_unknown";

    std::string front = jTrip["segments"][0].value("front", "");
    if (front.empty() || front == "-")
        return "pathmux_unknown";

    // Get filename without directory
    fs::path p(front);
    std::string stem = p.stem().string(); // e.g. "20260216_095422F"

    // Drop the trailing camera letter (always a single alpha char)
    if (!stem.empty() && std::isalpha((unsigned char)stem.back()))
        stem.pop_back();

    return stem;  // "20260216_095422"
}

// ---------------------------------------------------------------------------
// resolveOutputPath
//   stem      : "20260216_095422"
//   ext       : "gpx" or "kml"
//   dirOverride  : from --gpxpath / --kmlpath  (directory)
//   fileOverride : from --gpxfile / --kmlfile  (filename, may lack extension)
//   force     : overwrite without asking
//
// Priority:
//   fileOverride → dir/fileOverride.ext  (with .1 .2 collision avoidance)
//   dirOverride  → dir/stem.ext
//   default      → ./stem.ext
//
// When force is true, always overwrites.
// When force is false and the target exists:
//   - With fileOverride: append .1, .2, ... before extension
//   - Without fileOverride: ask Y/N
// ---------------------------------------------------------------------------
std::string GpxExport::resolveOutputPath(const std::string& stem,
                                          const std::string& ext,
                                          const std::string& dirOverride,
                                          const std::string& fileOverride,
                                          bool force)
{
    // If dirOverride itself ends with .ext, treat the whole thing as a filepath
    std::string effDir  = dirOverride.empty() ? "." : dirOverride;
    std::string effFile = fileOverride;

    if (effFile.empty() && effDir.size() > ext.size() + 1) {
        std::string tail = effDir.substr(effDir.size() - ext.size() - 1);
        // lowercase comparison
        std::string tailLow = tail;
        for (char& c : tailLow) c = std::tolower((unsigned char)c);
        if (tailLow == "." + ext) {
            // Split into dir + filename
            fs::path fp(effDir);
            effFile = fp.filename().string();
            effDir  = fp.parent_path().string();
            if (effDir.empty()) effDir = ".";
        }
    }

    std::string dir = effDir;

    if (!effFile.empty()) {
        // Explicit filename — ensure it has the right extension
        fs::path fp(effFile);
        std::string fname = fp.stem().string();
        // Build base: dir/fname.ext
        std::string base = dir + "/" + fname + "." + ext;

        if (force || !fs::exists(base))
            return base;

        // Collision avoidance: dir/fname.1.ext, .2, ...
        for (int n = 1; n <= 9999; ++n) {
            std::string candidate = dir + "/" + fname + "."
                                    + std::to_string(n) + "." + ext;
            if (!fs::exists(candidate)) {
                std::cout << "Note: " << base << " exists — writing to "
                          << candidate << "\n";
                return candidate;
            }
        }
        std::cerr << "Error: Cannot find an available filename for " << base << "\n";
        return "";
    }

    // No explicit filename — use stem
    std::string target = dir + "/" + stem + "." + ext;

    if (force || !fs::exists(target))
        return target;

    // Ask the user
    std::cout << "File exists: " << target << "\nOverwrite? [Y/N]: ";
    std::string ans; std::cin >> ans;
    if (ans == "y" || ans == "Y") return target;
    return "";  // user declined
}

// ---------------------------------------------------------------------------
// countUnscanned
// ---------------------------------------------------------------------------
int GpxExport::countUnscanned(const json& jTrips)
{
    int n = 0;
    for (const auto& t : jTrips)
        if (t.value("gpsTrackStatus", "none") != "complete") ++n;
    return n;
}

// ---------------------------------------------------------------------------
// selectTrip — show trip table, return index (0-based), -1=all, -2=cancel
// ---------------------------------------------------------------------------
int GpxExport::selectTrip(const json& jTrips, ExportMode /*mode*/)
{
    std::cout << "\n--- Select Trip ---\n";
    std::cout << std::left
              << std::setw(6)  << "ID"
              << std::setw(14) << "Date"
              << std::setw(12) << "Start"
              << std::setw(14) << "Duration"
              << std::setw(10) << "Segments"
              << "GPS\n";
    std::cout << std::string(70, '-') << "\n";

    for (size_t i = 0; i < jTrips.size(); ++i) {
        const auto& t = jTrips[i];
        int segCount = 0;
        if (t.contains("segments") && t["segments"].is_array())
            segCount = (int)t["segments"].size();
        std::string gpsStatus = t.value("gpsTrackStatus", "none");
        std::string gpsInd =
            (gpsStatus == "complete") ? "complete"  :
            (gpsStatus == "partial")  ? "partial"   :
            (t.value("startLat", 0.0) != 0.0) ? "start/end" : "none";

        std::cout << std::left
                  << std::setw(6)  << t.value("id", std::to_string(i + 1))
                  << std::setw(14) << t.value("date",       "?")
                  << std::setw(12) << t.value("start_time", "?")
                  << std::setw(14) << t.value("duration",   "?")
                  << std::setw(10) << segCount
                  << gpsInd << "\n";
    }

    std::cout << "\nEnter Trip ID, [A]ll, or [Q] to cancel: ";
    std::string sel; std::cin >> sel;

    if (sel == "q" || sel == "Q") return -2;
    if (sel == "a" || sel == "A") return -1;

    // Match by trip ID string (case-insensitive)
    std::string selUp = sel;
    for (char& c : selUp) c = std::toupper((unsigned char)c);
    for (int i = 0; i < (int)jTrips.size(); ++i) {
        std::string tid = jTrips[i].value("id", "");
        for (char& c : tid) c = std::toupper((unsigned char)c);
        if (tid == selUp) return i;
    }
    std::cout << "Invalid trip ID.\n";
    return -2;
}

// ===========================================================================
// GPS extraction
// ===========================================================================

bool GpxExport::extractGps(json& root, int tripIdx, const std::string& manifestFile,
                            const std::string& exiftoolPath,
                            const std::string& exiftoolOptions,
                            bool verbose)
{
    auto& jTrip = root["trips"][tripIdx];

    if (!jTrip.contains("segments") || !jTrip["segments"].is_array()
            || jTrip["segments"].empty()) {
        std::cerr << "Error: No segments in trip.\n";
        return false;
    }

    const auto& segs  = jTrip["segments"];
    int segCount      = (int)segs.size();

    // exiftool format: "YYYY:MM:DD HH:MM:SS lat lon altitude speed_kmh heading_deg"
    // Full command including -p and format string is in exiftoolOptions pref.
    const std::string exifCmd = exiftoolPath + " " + exiftoolOptions + " ";

    json trackArray = json::array();
    bool gotAny     = false;

    for (int si = 0; si < segCount; ++si) {
        std::string frontPath = segs[si].value("front", "-");
        if (frontPath == "-" || frontPath.empty()) continue;

        // Drop 2>/dev/null so errors are visible during extraction
        std::string cmd = exifCmd + "\"" + frontPath + "\""
                          + (verbose ? "" : " 2>/dev/null");
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            std::cerr << "\n  Error: popen failed — is exiftool installed?\n";
            return false;
        }

        char linebuf[512];
        int lineIdx = 0;

        while (fgets(linebuf, sizeof(linebuf), pipe)) {
            std::string line(linebuf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'
                                     || line.back() == ' '))
                line.pop_back();
            if (line.empty()) { ++lineIdx; continue; }

            std::istringstream iss(line);
            std::string datePart, timePart;
            double lat, lon, alt, speed, heading, accelX, accelY, accelZ;

            // Format: "YYYY:MM:DD HH:MM:SS lat lon alt speed heading accelX accelY accelZ"
            // Accelerometer outputs as 3 space-separated values, parsed as individual fields.
            // Altitude stored as-is; D90 values are negative/incorrect but useful on other cameras.
            if (!(iss >> datePart >> timePart >> lat >> lon >> alt >> speed >> heading
                      >> accelX >> accelY >> accelZ)) {
                ++lineIdx; continue;
            }

            // Skip records with zero lat/lon — GPS not yet locked
            if (lat == 0.0 && lon == 0.0) { ++lineIdx; continue; }

            json pt;
            pt["timestamp"] = datePart + " " + timePart;
            pt["lat"]       = lat;
            pt["lon"]       = lon;
            pt["alt"]       = alt;       // metres; D90 values incorrect, stored for other cameras
            pt["speed"]     = speed;     // km/h
            pt["heading"]   = heading;
            pt["accelX"]    = accelX;
            pt["accelY"]    = accelY;
            pt["accelZ"]    = accelZ;
            trackArray.push_back(pt);
            gotAny = true;
            ++lineIdx;
        }
        pclose(pipe);

        std::cout << "." << std::flush;  // one dot per segment
    }
    std::cout << "\n";

    if (!gotAny) {
        std::cerr << "\n  No GPS records parsed. Check exiftool version (13.51+ required)\n"
                  << "  and verify exiftoolOptions format string matches your camera.\n";
        return false;
    }

    jTrip["gpsTrack"]       = trackArray;
    jTrip["gpsTrackStatus"] = "complete";
    jTrip["startLat"]       = trackArray.front().value("lat", 0.0);
    jTrip["startLon"]       = trackArray.front().value("lon", 0.0);
    jTrip["endLat"]         = trackArray.back().value("lat",  0.0);
    jTrip["endLon"]         = trackArray.back().value("lon",  0.0);

    // Rewrite manifest
    std::ofstream ofs(manifestFile);
    if (!ofs.is_open()) {
        std::cerr << "Error: Cannot rewrite manifest: " << manifestFile << "\n";
        return false;
    }
    ofs << root.dump(2) << "\n";
    ofs.close();

    // Keep index md5 in sync so validation doesn't flag this as external modification
    ConfigManager config;
    config.updateManifestMd5(manifestFile);

    return true;
}

// ===========================================================================
// XML helpers (shared by GPX and KML writers)
// ===========================================================================

static std::string xmlEscape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;
        }
    }
    return out;
}

// "2026:02:16 14:32:00"  →  "2026-02-16T14:32:00Z"
static std::string fmtIso8601(const std::string& raw)
{
    if (raw.size() < 19) return raw;
    std::string out = raw;
    out[4]  = '-';
    out[7]  = '-';
    out[10] = 'T';
    out    += 'Z';
    return out;
}

// "2026-02-16" + "09:54:22"  →  "2026-02-16T09:54:22Z"
static std::string buildIso(const std::string& date, const std::string& time)
{
    return date + "T" + time + "Z";
}

// ===========================================================================
// GPX writer
// ===========================================================================

std::string GpxExport::writeGpx(const json& root,
                                  int tripIdx,
                                  const std::string& outPath)
{
    const auto& jTrip     = root["trips"][tripIdx];
    std::string date      = jTrip.value("date",       "unknown");
    std::string startTime = jTrip.value("start_time", "00:00:00");
    std::string duration  = jTrip.value("duration",   "");

    std::vector<json> trackPoints;
    if (jTrip.contains("gpsTrack") && jTrip["gpsTrack"].is_array())
        for (const auto& pt : jTrip["gpsTrack"])
            trackPoints.push_back(pt);

    std::ofstream ofs(outPath);
    if (!ofs.is_open()) {
        std::cerr << "Error: Cannot create: " << outPath << "\n";
        return "";
    }

    ofs << R"(<?xml version="1.0" encoding="UTF-8"?>)" "\n"
        << R"(<gpx version="1.1")" "\n"
        << R"(     creator="PathMux Dashcam Explorer")" "\n"
        << R"(     xmlns="http://www.topografix.com/GPX/1/1")" "\n"
        << R"(     xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance")" "\n"
        << R"(     xsi:schemaLocation="http://www.topografix.com/GPX/1/1 )"
        <<                             R"(http://www.topografix.com/GPX/1/1/gpx.xsd">)" "\n";

    ofs << "  <metadata>\n"
        << "    <name>" << xmlEscape("PathMux Trip " + date + " " + startTime) << "</name>\n";
    if (!duration.empty())
        ofs << "    <desc>Duration: " << xmlEscape(duration) << "</desc>\n";
    ofs << "    <time>" << buildIso(date, startTime) << "</time>\n"
        << "  </metadata>\n";

    ofs << "  <trk>\n"
        << "    <name>" << xmlEscape("Trip " + date + " " + startTime) << "</name>\n"
        << "    <trkseg>\n";

    if (trackPoints.empty()) {
        ofs << "      <!-- No GPS track data. -->\n";
        double sLat = jTrip.value("startLat", 0.0);
        double sLon = jTrip.value("startLon", 0.0);
        double eLat = jTrip.value("endLat",   0.0);
        double eLon = jTrip.value("endLon",   0.0);
        ofs << std::fixed << std::setprecision(6);
        if (sLat != 0.0 || sLon != 0.0)
            ofs << "      <trkpt lat=\"" << sLat << "\" lon=\"" << sLon << "\">"
                << "<name>Trip Start</name></trkpt>\n";
        if ((eLat != 0.0 || eLon != 0.0) && (eLat != sLat || eLon != sLon))
            ofs << "      <trkpt lat=\"" << eLat << "\" lon=\"" << eLon << "\">"
                << "<name>Trip End</name></trkpt>\n";
    } else {
        ofs << std::fixed << std::setprecision(6);
        for (const auto& pt : trackPoints) {
            double lat  = pt.value("lat", 0.0);
            double lon  = pt.value("lon", 0.0);
            double alt  = pt.value("altitude", -9999.0);
            double spd  = pt.value("speed",    -1.0);
            std::string ts = pt.value("timestamp", "");

            ofs << "      <trkpt lat=\"" << lat << "\" lon=\"" << lon << "\">\n";
            if (alt > -9999.0)
                ofs << "        <ele>" << std::setprecision(1) << alt
                    << "</ele>\n" << std::setprecision(6);
            if (!ts.empty())
                ofs << "        <time>" << fmtIso8601(ts) << "</time>\n";
            if (spd >= 0.0) {
                ofs << "        <extensions><speed>"
                    << std::setprecision(2) << (spd / 3.6)
                    << "</speed>"
                    << "<!-- m/s (" << std::setprecision(1) << spd << " km/h) -->"
                    << "</extensions>\n" << std::setprecision(6);
            }
            ofs << "      </trkpt>\n";
        }
    }

    ofs << "    </trkseg>\n  </trk>\n</gpx>\n";
    return outPath;
}

// ===========================================================================
// KML writer
// ===========================================================================

std::string GpxExport::writeKml(const json& root,
                                  int tripIdx,
                                  const std::string& outPath)
{
    const auto& jTrip     = root["trips"][tripIdx];
    std::string date      = jTrip.value("date",       "unknown");
    std::string startTime = jTrip.value("start_time", "00:00:00");
    std::string duration  = jTrip.value("duration",   "");

    std::vector<json> trackPoints;
    if (jTrip.contains("gpsTrack") && jTrip["gpsTrack"].is_array())
        for (const auto& pt : jTrip["gpsTrack"])
            trackPoints.push_back(pt);

    std::ofstream ofs(outPath);
    if (!ofs.is_open()) {
        std::cerr << "Error: Cannot create: " << outPath << "\n";
        return "";
    }

    std::string tripName = "PathMux Trip " + date + " " + startTime;

    ofs << R"(<?xml version="1.0" encoding="UTF-8"?>)" "\n"
        << R"(<kml xmlns="http://www.opengis.net/kml/2.2")" "\n"
        << R"(     xmlns:gx="http://www.google.com/kml/ext/2.2">)" "\n"
        << "  <Document>\n"
        << "    <name>" << xmlEscape(tripName) << "</name>\n";

    if (!duration.empty())
        ofs << "    <description>Duration: " << xmlEscape(duration) << "</description>\n";

    // Track line style
    ofs << "    <Style id=\"trackLine\">\n"
        << "      <LineStyle>\n"
        << "        <color>ff0000ff</color>\n"  // AABBGGRR — solid red
        << "        <width>3</width>\n"
        << "      </LineStyle>\n"
        << "    </Style>\n"
        << "    <Style id=\"startPin\">\n"
        << "      <IconStyle><Icon>"
        << "<href>http://maps.google.com/mapfiles/kml/paddle/grn-circle.png</href>"
        << "</Icon></IconStyle>\n"
        << "    </Style>\n"
        << "    <Style id=\"endPin\">\n"
        << "      <IconStyle><Icon>"
        << "<href>http://maps.google.com/mapfiles/kml/paddle/red-circle.png</href>"
        << "</Icon></IconStyle>\n"
        << "    </Style>\n";

    ofs << std::fixed << std::setprecision(6);

    if (trackPoints.empty()) {
        // Emit start/end placemarks only if we have them
        double sLat = jTrip.value("startLat", 0.0);
        double sLon = jTrip.value("startLon", 0.0);
        double eLat = jTrip.value("endLat",   0.0);
        double eLon = jTrip.value("endLon",   0.0);

        if (sLat != 0.0 || sLon != 0.0)
            ofs << "    <Placemark><styleUrl>#startPin</styleUrl>\n"
                << "      <name>Trip Start</name>\n"
                << "      <Point><coordinates>" << sLon << "," << sLat
                << ",0</coordinates></Point>\n"
                << "    </Placemark>\n";
        if ((eLat != 0.0 || eLon != 0.0) && (eLat != sLat || eLon != sLon))
            ofs << "    <Placemark><styleUrl>#endPin</styleUrl>\n"
                << "      <name>Trip End</name>\n"
                << "      <Point><coordinates>" << eLon << "," << eLat
                << ",0</coordinates></Point>\n"
                << "    </Placemark>\n";
    } else {
        // Start placemark
        double sLat = trackPoints.front().value("lat", 0.0);
        double sLon = trackPoints.front().value("lon", 0.0);
        double eLat = trackPoints.back().value("lat",  0.0);
        double eLon = trackPoints.back().value("lon",  0.0);

        ofs << "    <Placemark><styleUrl>#startPin</styleUrl>\n"
            << "      <name>Trip Start</name>\n"
            << "      <TimeStamp><when>" << buildIso(date, startTime) << "</when></TimeStamp>\n"
            << "      <Point><coordinates>" << sLon << "," << sLat
            << ",0</coordinates></Point>\n"
            << "    </Placemark>\n";

        ofs << "    <Placemark><styleUrl>#endPin</styleUrl>\n"
            << "      <name>Trip End</name>\n"
            << "      <Point><coordinates>" << eLon << "," << eLat
            << ",0</coordinates></Point>\n"
            << "    </Placemark>\n";

        // gx:Track — Google Earth extension for time-stamped track with data
        ofs << "    <Placemark>\n"
            << "      <name>" << xmlEscape(tripName) << "</name>\n"
            << "      <styleUrl>#trackLine</styleUrl>\n"
            << "      <gx:Track>\n"
            << "        <altitudeMode>clampToGround</altitudeMode>\n";

        // <when> elements (ISO 8601 timestamps)
        for (const auto& pt : trackPoints) {
            std::string ts = pt.value("timestamp", "");
            ofs << "        <when>" << (ts.empty() ? "" : fmtIso8601(ts)) << "</when>\n";
        }

        // <gx:coord> elements: lon lat alt
        for (const auto& pt : trackPoints) {
            double lat = pt.value("lat",      0.0);
            double lon = pt.value("lon",      0.0);
            double alt = pt.value("altitude", 0.0);  // emit 0 if unknown; clamped anyway
            ofs << "        <gx:coord>" << lon << " " << lat << " " << alt << "</gx:coord>\n";
        }

        // Extended data: speed and heading per point
        ofs << "        <ExtendedData>\n"
            << "          <SchemaData>\n"
            << "            <gx:SimpleArrayData name=\"speed_kmh\">\n";
        for (const auto& pt : trackPoints)
            ofs << "              <gx:value>" << pt.value("speed", 0.0) << "</gx:value>\n";
        ofs << "            </gx:SimpleArrayData>\n"
            << "            <gx:SimpleArrayData name=\"heading\">\n";
        for (const auto& pt : trackPoints)
            ofs << "              <gx:value>" << pt.value("heading", 0.0) << "</gx:value>\n";
        ofs << "            </gx:SimpleArrayData>\n"
            << "          </SchemaData>\n"
            << "        </ExtendedData>\n"
            << "      </gx:Track>\n"
            << "    </Placemark>\n";
    }

    ofs << "  </Document>\n</kml>\n";
    return outPath;
}

// ===========================================================================
// GeoJSON writer (RFC 7946 FeatureCollection)
// ===========================================================================

std::string GpxExport::writeGeoJson(const json& root,
                                     int tripIdx,
                                     const std::string& outPath)
{
    const auto& jTrip     = root["trips"][tripIdx];
    std::string date      = jTrip.value("date",       "unknown");
    std::string startTime = jTrip.value("start_time", "00:00:00");
    std::string duration  = jTrip.value("duration",   "");
    std::string tripId    = jTrip.value("id",         "");
    int segCount = jTrip.contains("segments") && jTrip["segments"].is_array()
                   ? (int)jTrip["segments"].size() : 0;

    // Coordinates: [longitude, latitude] per RFC 7946 (lon first)
    json coords = json::array();
    if (jTrip.contains("gpsTrack") && jTrip["gpsTrack"].is_array()) {
        for (const auto& pt : jTrip["gpsTrack"])
            coords.push_back({ pt.value("lon", 0.0), pt.value("lat", 0.0) });
    } else {
        // Fallback: start/end points only
        double sLat = jTrip.value("startLat", 0.0), sLon = jTrip.value("startLon", 0.0);
        double eLat = jTrip.value("endLat",   0.0), eLon = jTrip.value("endLon",   0.0);
        if (sLat != 0.0 || sLon != 0.0) coords.push_back({sLon, sLat});
        if ((eLat != 0.0 || eLon != 0.0) && (eLat != sLat || eLon != sLon))
            coords.push_back({eLon, eLat});
    }

    json doc = {
        {"type", "FeatureCollection"},
        {"features", json::array({{
            {"type", "Feature"},
            {"geometry", {
                {"type", "LineString"},
                {"coordinates", coords}
            }},
            {"properties", {
                {"creator",       "PathMux Dashcam Explorer"},
                {"trip_id",       tripId},
                {"date",          date},
                {"start_time",    startTime},
                {"duration",      duration},
                {"segment_count", segCount},
                {"point_count",   (int)coords.size()}
            }}
        }})}
    };

    std::ofstream ofs(outPath);
    if (!ofs.is_open()) {
        std::cerr << "Error: Cannot create: " << outPath << "\n";
        return "";
    }
    ofs << doc.dump(2) << "\n";
    return outPath;
}

// ===========================================================================
// run — main entry point
// ===========================================================================

void GpxExport::run(ExportMode mode, const ExportOptions& opts)
{
    ConfigManager config;

    // ---- Step 1: Resolve manifest ----
    std::string manifestFile = resolveManifestFile(opts.manifestPath);

    if (manifestFile.empty()) {
        auto mfList = config.loadManifestIndex();
        if (mfList.empty()) {
            std::cout << "No cached manifests found. Run a scan first.\n";
            return;
        }
        if (mfList.size() == 1) {
            manifestFile = mfList[0].manifestFile;
        } else {
            std::cout << "\n--- Select Manifest ---\n";
            for (const auto& e : mfList)
                std::cout << "  [" << e.id << "]  " << e.path << "\n";
            std::cout << "  [Q]  Cancel\n\nManifest ID: ";
            std::string input; std::cin >> input;
            if (input == "q" || input == "Q") return;
            std::string inputUp = input;
            for (char& c : inputUp) c = std::toupper((unsigned char)c);
            int sel = -1;
            for (int i = 0; i < (int)mfList.size(); ++i) {
                std::string mid = mfList[i].id;
                for (char& c : mid) c = std::toupper((unsigned char)c);
                if (mid == inputUp) { sel = i; break; }
            }
            if (sel < 0) { std::cout << "Invalid manifest ID.\n"; return; }
            manifestFile = mfList[sel].manifestFile;
        }
        if (manifestFile.empty()) {
            std::cout << "Could not locate manifest file.\n"; return;
        }
    }

    // ---- Step 2: Load manifest ----
    json root;
    if (!loadManifest(manifestFile, root)) return;
    const json& jTrips = root["trips"];
    if (jTrips.empty()) { std::cout << "No trips in manifest.\n"; return; }

    // ---- Step 3: Trip selection ----
    int tripSel = selectTrip(jTrips, mode);
    if (tripSel == -2) return;  // cancelled

    bool doAll = (tripSel == -1);

    // Warn if --gpxfile / --kmlfile supplied with "all"
    const std::string& fileOverride =
        (mode == ExportMode::Kml) ? opts.kmlFile : opts.gpxFile;
    if (doAll && !fileOverride.empty()) {
        std::cout << "Note: --"
                  << (mode == ExportMode::Kml ? "kmlfile" : "gpxfile")
                  << " ignored when 'all' is selected — using auto-generated filenames.\n";
    }

    // Build list of trip indices to process
    std::vector<int> indices;
    if (doAll) {
        for (int i = 0; i < (int)jTrips.size(); ++i) {
            // GPS-only: only unscanned trips
            // GPX/KML: all trips (extract if needed)
            if (mode == ExportMode::GpsOnly) {
                if (jTrips[i].value("gpsTrackStatus", "none") != "complete")
                    indices.push_back(i);
            } else {
                indices.push_back(i);
            }
        }
        if (indices.empty()) {
            std::cout << "All trips already have GPS data extracted.\n";
            return;
        }
    } else {
        indices.push_back(tripSel);
    }

    // ---- Step 4: Warn if bulk extraction needed ----
    int needExtract = 0;
    for (int idx : indices)
        if (jTrips[idx].value("gpsTrackStatus", "none") != "complete") ++needExtract;

    if (needExtract > 0) {
        std::cout << needExtract << " trip" << (needExtract > 1 ? "s" : "")
                  << " require GPS extraction.";
        if (needExtract > 2)
            std::cout << " For long drives this may take several minutes per trip.";
        std::cout << "\nContinue? [Y/N]: ";
        std::string ans; std::cin >> ans;
        if (ans != "y" && ans != "Y") return;
    }

    // ---- Step 5: Process each trip ----
    int exported = 0;
    for (int idx : indices) {
        // Reload root from disk between trips (extractGps rewrites it each time)
        if (exported > 0) {
            if (!loadManifest(manifestFile, root)) return;
        }

        const std::string& dirOverride =
            (mode == ExportMode::Kml) ? opts.kmlPath : opts.gpxPath;
        const std::string& ext =
            (mode == ExportMode::Kml) ? "kml" : "gpx";
        // File override only valid for single-trip; ignored for "all"
        const std::string effFileOverride =
            doAll ? "" : fileOverride;

        // Extract GPS if needed
        std::string tId = root["trips"][idx].value("id", "?");
        bool needsGps = (root["trips"][idx].value("gpsTrackStatus", "none") != "complete");
        if (needsGps && mode != ExportMode::GpsOnly) {
            int segCount = root["trips"][idx].contains("segments")
                           ? (int)root["trips"][idx]["segments"].size() : 0;
            std::cout << "Extracting GPS for trip " << tId
                      << " (" << segCount << " segments)";
            if (!extractGps(root, idx, manifestFile, opts.exiftoolPath, opts.exiftoolOptions, m_verbose)) {
                std::cout << "\nGPS extraction failed for trip " << tId << ".\n"
                          << "Make sure ExifTool 13.51+ is installed (EPEL 13.10 does NOT work).\n"
                          << "  exiftool -ver\n";
                continue;
            }
            std::cout << "GPS extracted.\n";
        } else if (needsGps && mode == ExportMode::GpsOnly) {
            int segCount = root["trips"][idx].contains("segments")
                           ? (int)root["trips"][idx]["segments"].size() : 0;
            std::cout << "Extracting GPS for trip " << tId
                      << " (" << segCount << " segments)";
            if (!extractGps(root, idx, manifestFile, opts.exiftoolPath, opts.exiftoolOptions, m_verbose)) {
                std::cout << "\nGPS extraction failed for trip " << tId << ".\n"
                          << "Make sure ExifTool 13.51+ is installed (EPEL 13.10 does NOT work).\n"
                          << "  exiftool -ver\n";
                continue;
            }
            std::cout << "GPS saved to manifest.\n";
            ++exported;
            continue;  // GPS-only mode: no file output
        } else if (mode == ExportMode::GpsOnly) {
            std::cout << "Trip " << tId << " already has GPS data — skipping.\n";
            continue;
        }

        // Resolve output path
        std::string stem    = buildStem(root["trips"][idx]);
        std::string outPath = resolveOutputPath(stem, ext, dirOverride,
                                                effFileOverride, opts.force);
        if (outPath.empty()) {
            std::cout << "Skipping trip " << tId << ".\n";
            continue;
        }

        // Write file
        std::string result =
            (mode == ExportMode::Kml)
                ? writeKml(root, idx, outPath)
                : writeGpx(root, idx, outPath);

        if (result.empty()) {
            std::cout << "Export failed for trip " << tId << ".\n";
            continue;
        }

        int ptCount = root["trips"][idx].contains("gpsTrack")
                      ? (int)root["trips"][idx]["gpsTrack"].size() : 0;
        std::cout << (mode == ExportMode::Kml ? "KML" : "GPX")
                  << " written: " << result
                  << "  (" << ptCount << " track points)\n";
        ++exported;
    }

    if (doAll)
        std::cout << exported << " file" << (exported != 1 ? "s" : "") << " written.\n";
}
// SN: 00075

// ===========================================================================
// runInteractive — interactive GPS menu entry point
// Outer loop: manifest selection.  Inner loop: trip picker then action menu.
// ===========================================================================

void GpxExport::runInteractive(const ExportOptions& opts)
{
    ConfigManager config;

    // ---- Outer loop: manifest selection ----
    while (true) {
        std::vector<ManifestEntry> mfList = config.loadManifestIndex();
        if (mfList.empty()) {
            std::cout << "No cached manifests found. Run a scan first.\n";
            return;
        }

        std::string manifestFile;

        if (mfList.size() == 1) {
            manifestFile = mfList[0].manifestFile;
        } else {
            UI::printTitle("GPS — Select Manifest");
            for (const auto& e : mfList)
                UI::printLine("[" + e.id + "]  " + e.path);
            UI::printLine("[Q]  Cancel");
            UI::printDivider();
            UI::printBottom();

            std::string input = UI::readCommand();
            if (input == "q" || input == "Q") return;

            // Match by manifest ID (case-insensitive)
            std::string inputUp = input;
            for (char& c : inputUp) c = std::toupper((unsigned char)c);
            int sel = -1;
            for (int i = 0; i < (int)mfList.size(); ++i) {
                std::string mid = mfList[i].id;
                for (char& c : mid) c = std::toupper((unsigned char)c);
                if (mid == inputUp) { sel = i; break; }
            }
            if (sel < 0) {
                std::cout << "Invalid selection.\n"; continue;
            }
            manifestFile = mfList[sel].manifestFile;
        }

        if (manifestFile.empty() || !fs::exists(manifestFile)) {
            std::cout << "Could not locate manifest file.\n"; continue;
        }

        // Find manifest ID for headers
        std::string mid;
        for (const auto& e : mfList)
            if (e.manifestFile == manifestFile) { mid = e.id; break; }

        // ---- Inner loop: trip picker then action menu ----
        while (true) {
            json root;
            if (!loadManifest(manifestFile, root)) break;

            const json trips = root.value("trips", json::array());
            if (trips.empty()) { std::cout << "No trips in manifest.\n"; break; }

            std::string sourcePath = root.value("source_path", manifestFile);

            // ---- Trip table ----
            UI::printTitleWithStatus("GPS — [" + mid + "]  " + sourcePath,
                                     m_verbose ? "Errors: ON " : "Errors: off");
            std::cout << "  " << std::left
                      << std::setw(6)  << "ID"
                      << std::setw(14) << "Date"
                      << std::setw(10) << "Start"
                      << std::setw(6)  << "Segs"
                      << "GPS Status\n";
            std::cout << "  " << std::string(UI::innerWidth() - 2, '-') << "\n";

            for (const auto& t : trips) {
                int segs = t.contains("segments") && t["segments"].is_array()
                           ? (int)t["segments"].size() : 0;
                std::string gs = t.value("gpsTrackStatus", "none");
                std::string gpsInd =
                    (gs == "complete")                 ? "complete"  :
                    (gs == "partial")                  ? "partial"   :
                    (t.value("startLat", 0.0) != 0.0) ? "start/end" : "none";
                std::cout << "  " << std::left
                          << std::setw(6)  << t.value("id", "?")
                          << std::setw(14) << t.value("date",       "?")
                          << std::setw(10) << t.value("start_time", "?")
                          << std::setw(6)  << segs
                          << gpsInd << "\n";
            }
            UI::printDivider();
            UI::printLine("[<TripID>]  Select trip   [A]ll   [E]  Toggle errors   [M]  Switch manifest   [Q]  Quit");
            UI::printDivider();
            UI::printBottom();

            std::string tripCmd = UI::readCommand();
            if (tripCmd.empty()) continue;
            if (tripCmd == "q" || tripCmd == "Q") return;
            if (tripCmd == "m" || tripCmd == "M") break;
            if (tripCmd == "e" || tripCmd == "E") { m_verbose = !m_verbose; continue; }

            // Resolve trip selection
            bool doAll = (tripCmd == "a" || tripCmd == "A");
            int tripIdx = -1;
            if (!doAll) {
                std::string selUp = tripCmd;
                for (char& c : selUp) c = std::toupper((unsigned char)c);
                for (int i = 0; i < (int)trips.size(); ++i) {
                    std::string tid = trips[i].value("id", "");
                    for (char& c : tid) c = std::toupper((unsigned char)c);
                    if (tid == selUp) { tripIdx = i; break; }
                }
                if (tripIdx == -1) { std::cout << "  Invalid trip ID.\n"; continue; }
            }

            std::string tripLabel = doAll ? "ALL" : trips[tripIdx].value("id", "?");

            // ---- Action menu ----
            UI::printTitleWithStatus("GPS — Trip " + tripLabel,
                                     m_verbose ? "Errors: ON " : "Errors: off");
            UI::printLine("[G]  Extract GPS into manifest");
            UI::printLine("[X]  Export GPX track file");
            UI::printLine("[K]  Export KML track file");
            UI::printLine("[J]  Export GeoJSON track file");
            UI::printLine("[Q]  Back");
            UI::printDivider();
            UI::printBottom();

            std::string actCmd = UI::readCommand();
            if (actCmd.empty()) continue;
            char ch = std::toupper((unsigned char)actCmd[0]);
            if (ch == 'Q') continue;
            if (ch != 'G' && ch != 'X' && ch != 'K' && ch != 'J') continue;

            // 'J' (GeoJSON) uses same GPS extraction path as 'X' (GPX)
            ExportMode mode =
                (ch == 'G')             ? ExportMode::GpsOnly :
                (ch == 'X' || ch == 'J') ? ExportMode::Gpx : ExportMode::Kml;

            // Reload fresh before processing
            if (!loadManifest(manifestFile, root)) break;

            // Output dir prompt for GPX / KML / GeoJSON
            ExportOptions actionOpts = opts;
            actionOpts.manifestPath = manifestFile;
            std::string geojsonPath;

            if (ch == 'X' || ch == 'K' || ch == 'J') {
                std::string& pathOpt =
                    (ch == 'X') ? actionOpts.gpxPath :
                    (ch == 'K') ? actionOpts.kmlPath  : geojsonPath;
                if (pathOpt.empty()) {
                    // Default: footage source directory; fall back to global pref
                    // if source dir is not writable.
                    bool srcWritable = false;
                    {
                        std::string tf = sourcePath + "/.pm_write_test";
                        std::ofstream tst(tf);
                        if (tst.is_open()) {
                            tst.close();
                            std::error_code ec;
                            fs::remove(tf, ec);
                            srcWritable = true;
                        }
                    }
                    std::string defDir;
                    if (srcWritable) {
                        defDir = sourcePath;
                    } else {
                        std::string globalDef = opts.defaultExportDir.empty()
                                                ? "(not set)" : opts.defaultExportDir;
                        std::cout << "\n  Note: " << sourcePath << " is not writable.\n"
                                  << "  [1] Use global default (" << globalDef << ")\n"
                                  << "  [2] Enter a path\n"
                                  << "  [3] Back\n"
                                  << "  Choice: ";
                        std::string choice;
                        std::getline(std::cin, choice);
                        if (choice == "3" || choice.empty()) continue;
                        defDir = (choice == "1")
                                 ? (opts.defaultExportDir.empty() ? "." : opts.defaultExportDir)
                                 : "";
                    }
                    pathOpt = UI::promptLine("Output directory or file", defDir);
                    if (pathOpt.empty()) continue;  // bare Enter on empty default = back
                }
            }

            // Build work list
            std::vector<int> indices;
            if (doAll) {
                for (int i = 0; i < (int)root["trips"].size(); ++i) {
                    if (mode == ExportMode::GpsOnly) {
                        if (root["trips"][i].value("gpsTrackStatus","none") != "complete")
                            indices.push_back(i);
                    } else {
                        indices.push_back(i);
                    }
                }
                if (indices.empty()) { std::cout << "  All trips already have GPS data.\n"; continue; }
            } else {
                indices.push_back(tripIdx);
            }

            // Bulk extraction warning
            int needExtract = 0;
            for (int i : indices)
                if (root["trips"][i].value("gpsTrackStatus","none") != "complete") ++needExtract;

            if (needExtract > 0) {
                std::cout << "  " << needExtract << " trip"
                          << (needExtract > 1 ? "s" : "") << " require GPS extraction";
                if (needExtract > 2) std::cout << " (may take several minutes per trip)";
                std::cout << ".\n  Continue? [Y/N]: ";
                std::string ans;
                std::getline(std::cin, ans);
                if (ans != "y" && ans != "Y") continue;
            }

            // Process
            int exported = 0;
            for (int i : indices) {
                if (exported > 0)
                    if (!loadManifest(manifestFile, root)) break;

                std::string tId = root["trips"][i].value("id", std::to_string(i + 1));
                bool needsGps = (root["trips"][i].value("gpsTrackStatus","none") != "complete");

                if (needsGps || mode == ExportMode::GpsOnly) {
                    if (!needsGps) {
                        std::cout << "  Trip " << tId << ": already extracted — skipped.\n";
                        ++exported; continue;
                    }
                    int segCount = root["trips"][i].contains("segments")
                                   ? (int)root["trips"][i]["segments"].size() : 0;
                    std::cout << "  Extracting GPS for trip " << tId
                              << " (" << segCount << " segments)...";
                    std::cout.flush();
                    if (!extractGps(root, i, manifestFile,
                                    actionOpts.exiftoolPath, actionOpts.exiftoolOptions,
                                    m_verbose)) {
                        // extractGps already printed the reason to stderr
                        std::cout << "\n  GPS extraction failed for trip " << tId << ".\n";
                        continue;
                    }
                    std::cout << " done.\n";
                    if (mode == ExportMode::GpsOnly) {
                        std::cout << "  Trip " << tId << ": GPS saved to manifest.\n";
                        ++exported; continue;
                    }
                }

                // GPX / KML / GeoJSON export
                std::string ext  = (ch == 'K') ? "kml" : (ch == 'J') ? "geojson" : "gpx";
                std::string stem = buildStem(root["trips"][i]);
                std::string dirO = (ch == 'K') ? actionOpts.kmlPath
                                 : (ch == 'J') ? geojsonPath : actionOpts.gpxPath;
                std::string filO = doAll ? ""
                                 : (ch == 'K') ? actionOpts.kmlFile
                                 : (ch == 'J') ? "" : actionOpts.gpxFile;

                std::string outPath = resolveOutputPath(stem, ext, dirO, filO, actionOpts.force);
                if (outPath.empty()) { std::cout << "  Skipping trip " << tId << ".\n"; continue; }

                std::string result = (ch == 'K') ? writeKml(root, i, outPath)
                                   : (ch == 'J') ? writeGeoJson(root, i, outPath)
                                   :               writeGpx(root, i, outPath);
                if (result.empty()) { std::cout << "  Export failed for trip " << tId << ".\n"; continue; }

                int ptCount = root["trips"][i].contains("gpsTrack")
                              ? (int)root["trips"][i]["gpsTrack"].size() : 0;
                std::string fmt = (ch == 'K') ? "KML" : (ch == 'J') ? "GeoJSON" : "GPX";
                std::cout << "  " << fmt << " written: " << result
                          << "  (" << ptCount << " track points)\n";
                ++exported;
            }

            if (doAll)
                std::cout << "  " << exported << " file" << (exported != 1 ? "s" : "") << " written.\n";

            UI::waitEnter();
        }

        // Only one manifest — nothing to switch to, exit rather than loop
        if (mfList.size() == 1) return;
    }
}
