// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "gps_export.hpp"
#include "camera_profile.hpp"
#include "compat.hpp"
#include "config_manager.hpp"
#include "json.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cstdio>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace CamClops {

// ===========================================================================
// Internal helpers
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
    // Strip any existing timezone suffix before reformatting
    while (!out.empty() && !std::isdigit((unsigned char)out.back()) && out.back() != ':')
        out.pop_back();
    if (out.size() < 19) return raw;
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
// GPS extraction
// ===========================================================================

bool extractGps(json& root,
                int tripIdx,
                const std::string& manifestFile,
                const std::string& exiftoolPath,
                bool verbose,
                std::function<void(int done, int total)> progressCb)
{
    auto& jTrip = root["trips"][tripIdx];

    if (!jTrip.contains("segments") || !jTrip["segments"].is_array()
            || jTrip["segments"].empty()) {
        std::cerr << "Error: No segments in trip.\n";
        return false;
    }

    const auto& segs  = jTrip["segments"];
    int segCount      = (int)segs.size();

    // Resolve GPS extraction args and primary slot from the manifest's embedded
    // camera profile.  Fallback chain:
    //   1. Embedded profile's gps_exiftool_args field
    //   2. Built-in profile matching the embedded profile_id
    //   3. D90 hard-coded default (legacy manifests pre-dating profile embedding)
    static const std::string kD90Args =
        "-ee3 -p '$GPSDateTime $GPSLatitude# $GPSLongitude#"
        " $GPSAltitude# $GPSSpeed# $GPSTrack# $Accelerometer'";
    std::string effectiveArgs = kD90Args;
    std::string primarySlot   = "front";
    if (root.contains("camera_profile") && root["camera_profile"].is_object()) {
        const auto& cp = root["camera_profile"];
        // 1. Profile has GPS args directly embedded
        std::string profileArgs = cp.value("gps_exiftool_args", "");
        if (!profileArgs.empty()) {
            effectiveArgs = profileArgs;
        } else {
            // 2. Look up built-in by profile_id — handles manifests embedded
            //    before gps_exiftool_args was added to the profile schema
            std::string pid = cp.value("profile_id", "");
            for (const auto& bp : CameraProfile::getBuiltinProfiles()) {
                if (bp.profileId == pid && !bp.gpsExiftoolArgs.empty()) {
                    effectiveArgs = bp.gpsExiftoolArgs;
                    break;
                }
            }
        }
        // Primary slot from profile
        if (cp.contains("cameraSlots") && cp["cameraSlots"].is_array()) {
            for (const auto& s : cp["cameraSlots"])
                if (s.value("is_primary", false)) {
                    primarySlot = s.value("name", "front");
                    break;
                }
        }
    }
#ifdef _WIN32
    // cmd.exe does not treat single quotes as grouping characters, so the -p
    // format string (e.g. '-p '$GPSDateTime ...'') is split on its interior
    // spaces and reaches ExifTool as many separate broken arguments.
    // Replace single quotes with double quotes; $ is not expanded by cmd.exe.
    for (char& c : effectiveArgs)
        if (c == '\'') c = '"';
#endif
    const std::string exifCmd = exiftoolPath + " " + effectiveArgs + " ";

    // Resolve source root so relative segment paths (schema 3) become absolute.
    // Priority: path_map[os_hostname] → manifest parent directory.
    std::string sourceRoot;
    {
        std::string h = getShortHostname();
        std::transform(h.begin(), h.end(), h.begin(),
            [](unsigned char c){ return (char)std::tolower(c); });
#ifdef _WIN32
        std::string pmKey = "windows_" + h;
#elif defined(__APPLE__)
        std::string pmKey = "macos_" + h;
#else
        std::string pmKey = "linux_" + h;
#endif
        if (root.contains("path_map") && root["path_map"].is_object()) {
            const auto& pm = root["path_map"];
            if (pm.contains(pmKey) && pm[pmKey].is_string())
                sourceRoot = pm[pmKey].get<std::string>();
        }
        if (sourceRoot.empty())
            sourceRoot = fs::path(manifestFile).parent_path().string();
        while (!sourceRoot.empty() &&
               (sourceRoot.back() == '/' || sourceRoot.back() == '\\'))
            sourceRoot.pop_back();
    }

    json trackArray = json::array();
    bool gotAny     = false;
    int prePositionLockCount = 0;  // records with zero lat/lon (no position fix yet)
    int preTimeLockCount     = 0;  // records with valid position but unsynchronized clock
    int teleportCount        = 0;  // records skipped by velocity filter
    double prevLat = 0.0, prevLon = 0.0;
    time_t prevEpoch = 0;

    // Capture first-segment front path now so gpsLockSeconds can be computed after the loop.
    std::string firstSegFront;
    {
        const auto& s0 = segs[0];
        if (s0.contains("cameras") && s0["cameras"].is_object()) {
            const auto& c0 = s0["cameras"];
            if (c0.contains(primarySlot))  firstSegFront = c0[primarySlot].get<std::string>();
            else if (c0.contains("front")) firstSegFront = c0["front"].get<std::string>();
        } else {
            firstSegFront = s0.value("front", "-");
        }
        if (!firstSegFront.empty() && firstSegFront != "-"
                && !fs::path(firstSegFront).is_absolute() && !sourceRoot.empty())
            firstSegFront = sourceRoot + "/" + firstSegFront;
    }

    for (int si = 0; si < segCount; ++si) {
        std::string frontPath = "-";
        if (segs[si].contains("cameras") && segs[si]["cameras"].is_object()) {
            const auto& cams = segs[si]["cameras"];
            // Use the profile's primary slot; fall back to "front" for legacy manifests
            if (cams.contains(primarySlot))
                frontPath = cams[primarySlot].get<std::string>();
            else if (cams.contains("front"))
                frontPath = cams["front"].get<std::string>();
        } else {
            frontPath = segs[si].value("front", "-");  // legacy manifest fallback
        }

        // Resolve relative path to absolute (schema 3 manifests store relative paths).
        if (!frontPath.empty() && frontPath != "-"
                && !fs::path(frontPath).is_absolute()
                && !sourceRoot.empty())
            frontPath = sourceRoot + "/" + frontPath;

        if (frontPath == "-" || frontPath.empty()) continue;

        // verbose=true: let ExifTool stderr reach the terminal.
        // verbose=false: -q suppresses informational/tty output; 2>/dev/null
        //   catches any remaining stderr.
        std::string cmd = exifCmd
                          + (verbose ? "" : " -q")
                          + " \"" + frontPath + "\""
                          + (verbose ? "" : " " NULL_REDIRECT);
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            std::cerr << "\n  Error: popen failed — is exiftool installed?\n";
            return false;
        }

        char linebuf[512];

        while (fgets(linebuf, sizeof(linebuf), pipe)) {
            std::string line(linebuf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'
                                     || line.back() == ' '))
                line.pop_back();
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string datePart, timePart;
            double lat, lon, alt, speed, heading, accelX, accelY, accelZ;

            // Format: "YYYY:MM:DD HH:MM:SS lat lon alt speed heading [accelX accelY accelZ]"
            // Accelerometer is optional — cameras without it (e.g. Cobra GPS) omit those fields.
            // Altitude stored as-is; D90 values are negative/incorrect but useful on other cameras.
            if (!(iss >> datePart >> timePart >> lat >> lon >> alt >> speed >> heading)) {
                continue;
            }
            // Strip UTC suffix — some cameras (e.g. Cobra GPS) append 'Z' to the time token
            if (!timePart.empty() && (timePart.back() == 'Z' || timePart.back() == 'z'))
                timePart.pop_back();
            accelX = accelY = accelZ = 0.0;
            iss >> accelX >> accelY >> accelZ;  // optional; leave zero if absent

            // Skip records with unsynchronized clock (year < 2000).
            // ExifTool renders the all-zero GPS clock register as "1900:01:00";
            // other cameras/versions may emit "1970:01:01" or similar.
            // A broad threshold catches all known variants.
            int year = (datePart.size() >= 4) ? std::stoi(datePart.substr(0, 4)) : 0;
            if (year < 2000) { ++preTimeLockCount; continue; }

            // Parse epoch for velocity check.
            struct tm ptm = {};
            {
                std::istringstream tss(datePart + " " + timePart);
                tss >> std::get_time(&ptm, "%Y:%m:%d %H:%M:%S");
            }
            time_t ptEpoch = timegm(&ptm);

            // Velocity filter: skip records that jump more than 1 degree/second
            // on either axis from the previous accepted point.  1 degree ≈ 111 km,
            // so 1 deg/s ≈ 400 000 km/h — no vehicle ever triggers this on valid data.
            // Catches partial-lock sentinels (0,0 or last-known bleed) regardless of
            // the camera's specific sentinel value.
            if (prevEpoch > 0) {
                double dt = std::max(static_cast<double>(ptEpoch - prevEpoch), 1.0);
                if (std::abs(lat - prevLat) / dt > 1.0 ||
                    std::abs(lon - prevLon) / dt > 1.0) {
                    ++teleportCount;
                    continue;
                }
            }

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
            gotAny    = true;
            prevLat   = lat;
            prevLon   = lon;
            prevEpoch = ptEpoch;
        }
        pclose(pipe);

        std::cout << "." << std::flush;  // one dot per segment
        if (progressCb) progressCb(si + 1, segCount);
    }
    std::cout << "\n";

    if (!gotAny) {
        std::cerr << "\n  No GPS records returned by exiftool.\n"
                  << "  Verify the camera profile's gps_exiftool_args matches your camera.\n"
                  << "  If your camera's GPS format is not supported, contact the\n"
                  << "  ExifTool maintainer at https://exiftool.org\n";
        return false;
    }

    // Compute gpsLockSeconds: UTC epoch of first GPS record minus trip start epoch.
    // Use start_epoch already stored in the manifest — it was computed at scan time
    // from the segment filename via parseSegTimestamp() and is timezone-correct.
    // Fallback to filename re-parse only if start_epoch is absent (old manifests).
    if (!trackArray.empty()) {
        time_t fileEpoch = 0;
        if (jTrip.contains("start_epoch") && jTrip["start_epoch"].is_number_integer())
            fileEpoch = static_cast<time_t>(jTrip["start_epoch"].get<int64_t>());
        else if (!firstSegFront.empty() && firstSegFront != "-") {
            std::string baseName = fs::path(firstSegFront).filename().string();
            if (baseName.size() >= 15 && baseName[8] == '_') {
                struct tm ft = {};
                std::istringstream fss(baseName.substr(0, 15));
                fss >> std::get_time(&ft, "%Y%m%d_%H%M%S");
                ft.tm_isdst = -1;
                fileEpoch = std::mktime(&ft);
            }
        }
        if (fileEpoch > 0) {
            std::string firstGpsTs = trackArray.front().value("timestamp", "");
            if (firstGpsTs.size() >= 19) {
                struct tm gt = {};
                std::istringstream gss(firstGpsTs);
                gss >> std::get_time(&gt, "%Y:%m:%d %H:%M:%S");
                time_t gpsEpoch = timegm(&gt);
                if (gpsEpoch > fileEpoch) {
                    int lockSec = static_cast<int>(gpsEpoch - fileEpoch);
                    if (lockSec <= 300)  // sanity: GPS lock should be within 5 minutes
                        jTrip["gpsLockSeconds"] = lockSec;
                }
            }
        }
    }

    jTrip["gpsTrack"]                  = trackArray;
    jTrip["gpsTrackStatus"]            = "complete";
    jTrip["pre_position_lock_samples"] = prePositionLockCount;
    jTrip["pre_time_lock_samples"]     = preTimeLockCount;
    if (teleportCount > 0)
        jTrip["teleport_filtered_samples"] = teleportCount;
    jTrip["startLat"]                  = trackArray.front().value("lat", 0.0);
    jTrip["startLon"]                  = trackArray.front().value("lon", 0.0);
    jTrip["endLat"]                    = trackArray.back().value("lat",  0.0);
    jTrip["endLon"]                    = trackArray.back().value("lon",  0.0);

    // Rewrite manifest
    std::ofstream ofs(manifestFile);
    if (!ofs.is_open()) {
        std::cerr << "Error: Cannot rewrite manifest: " << manifestFile << "\n";
        return false;
    }
    ofs << root.dump(2) << "\n";
    ofs.close();

    // Keep index MD5 in sync so --validate does not flag this as external modification
    ConfigManager config;
    config.updateManifestMd5(manifestFile);

    return true;
}

// ===========================================================================
// GPX writer
// ===========================================================================

std::string writeGpx(const json& root, int tripIdx, const std::string& outPath)
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
        << R"(     creator="CamClops Dashcam Explorer")" "\n"
        << R"(     xmlns="http://www.topografix.com/GPX/1/1")" "\n"
        << R"(     xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance")" "\n"
        << R"(     xsi:schemaLocation="http://www.topografix.com/GPX/1/1 )"
        <<                             R"(http://www.topografix.com/GPX/1/1/gpx.xsd">)" "\n";

    ofs << "  <metadata>\n"
        << "    <name>" << xmlEscape("CamClops Trip " + date + " " + startTime) << "</name>\n";
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

std::string writeKml(const json& root, int tripIdx, const std::string& outPath)
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

    std::string tripName = "CamClops Trip " + date + " " + startTime;

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

        for (const auto& pt : trackPoints) {
            std::string ts = pt.value("timestamp", "");
            ofs << "        <when>" << (ts.empty() ? "" : fmtIso8601(ts)) << "</when>\n";
        }

        for (const auto& pt : trackPoints) {
            double lat = pt.value("lat",      0.0);
            double lon = pt.value("lon",      0.0);
            double alt = pt.value("altitude", 0.0);
            ofs << "        <gx:coord>" << lon << " " << lat << " " << alt << "</gx:coord>\n";
        }

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

std::string writeGeoJson(const json& root, int tripIdx, const std::string& outPath)
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
                {"creator",       "CamClops Dashcam Explorer"},
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

// ---------------------------------------------------------------------------
// measureCameraOffsets
// ---------------------------------------------------------------------------
bool measureCameraOffsets(const std::map<std::string, std::string>& camPaths,
                          time_t startEpoch,
                          const std::string& exiftoolPath,
                          CameraProfile& profile)
{
    const std::string primary = profile.primarySlot().empty()
                              ? "front" : profile.primarySlot();

    // Build a minimal exiftool command that extracts only GPSDateTime and position.
    // $SampleTime would give sub-second PTS but is not reliable for LIGOGPSINFO
    // binary streams in MPEG-TS — fall back to counting records (1 Hz) for now.
    // TODO: upgrade to ffprobe data-stream packet PTS for sub-frame precision.
    const std::string exifArgs =
        " -ee3 -p \"$GPSDateTime $GPSLatitude# $GPSLongitude#\"";

    // Compute gpsLockSecs for one camera file: run exiftool, count records until
    // first with non-zero lat/lon.  Returns -1 if GPS never locks in the file.
    auto probeOneCam = [&](const std::string& path) -> int {
        if (path.empty() || path == "-") return -1;
        std::string cmd = exiftoolPath + exifArgs
                        + " -q \"" + path + "\" " NULL_REDIRECT;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return -1;

        char buf[256];
        int  record   = 0;
        int  lockSecs = -1;
        while (fgets(buf, sizeof(buf), pipe)) {
            std::string line(buf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            if (line.empty()) continue;

            std::istringstream ss(line);
            std::string gpsTs;
            double lat = 0.0, lon = 0.0;
            if (!(ss >> gpsTs >> lat >> lon)) { ++record; continue; }
            if (lat == 0.0 && lon == 0.0)    { ++record; continue; }

            // First valid fix: compute wall-clock offset from segment start.
            if (gpsTs.size() >= 19) {
                struct tm gt = {};
                std::istringstream tss(gpsTs);
                tss >> std::get_time(&gt, "%Y:%m:%d %H:%M:%S");
                time_t gpsEpoch = timegm(&gt);
                int diff = static_cast<int>(gpsEpoch - startEpoch);
                if (diff >= 0 && diff <= 300) lockSecs = diff;
            }
            if (lockSecs < 0) lockSecs = record;  // fallback: record index ≈ seconds
            break;
        }
        pclose(pipe);
        return lockSecs;
    };

    // Probe primary camera first to establish reference lock time.
    auto it = camPaths.find(primary);
    if (it == camPaths.end()) return false;
    int primaryLock = probeOneCam(it->second);
    if (primaryLock < 0) return false;  // not a cold-start trip for primary

    bool anyMeasured = false;
    for (const auto& slot : profile.cameraSlots) {
        if (slot.name == primary) continue;
        auto jt = camPaths.find(slot.name);
        if (jt == camPaths.end() || jt->second.empty() || jt->second == "-") continue;

        int camLock = probeOneCam(jt->second);
        if (camLock < 0) continue;

        // Positive offset = this camera locked at an earlier record = started sooner.
        int offsetSecs = primaryLock - camLock;
        if (offsetSecs != 0) {
            profile.cameraStartOffsets[slot.name] = static_cast<double>(offsetSecs);
            anyMeasured = true;
        }
    }
    return anyMeasured;
}

} // namespace CamClops
// SN: 00122
