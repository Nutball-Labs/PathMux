// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "gps_export.hpp"
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

namespace Pathmux {

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
    int prePositionLockCount = 0;  // records with zero lat/lon (no position fix yet)
    int preTimeLockCount     = 0;  // records with valid position but unsynchronized clock

    for (int si = 0; si < segCount; ++si) {
        std::string frontPath = segs[si].value("front", "-");
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

            // Skip records with zero lat/lon — GPS position not yet locked
            if (lat == 0.0 && lon == 0.0) { ++prePositionLockCount; ++lineIdx; continue; }

            // Skip records with unsynchronized clock (year < 2000).
            // ExifTool renders the all-zero GPS clock register as "1900:01:00";
            // other cameras/versions may emit "1970:01:01" or similar.
            // A broad threshold catches all known variants.
            int year = (datePart.size() >= 4) ? std::stoi(datePart.substr(0, 4)) : 0;
            if (year < 2000) { ++preTimeLockCount; ++lineIdx; continue; }

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
        std::cerr << "\n  No GPS records returned by exiftool.\n"
                  << "  Verify the exiftoolOptions format string matches your camera.\n"
                  << "  If your camera's GPS format is not supported, contact the\n"
                  << "  ExifTool maintainer at https://exiftool.org\n";
        return false;
    }

    jTrip["gpsTrack"]                  = trackArray;
    jTrip["gpsTrackStatus"]            = "complete";
    jTrip["pre_position_lock_samples"] = prePositionLockCount;
    jTrip["pre_time_lock_samples"]     = preTimeLockCount;
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

} // namespace Pathmux
// SN: 00089
