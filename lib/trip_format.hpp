// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef TRIP_FORMAT_HPP
#define TRIP_FORMAT_HPP

// ---------------------------------------------------------------------------
// trip_format.hpp — structured output helpers (CSV, XML) for trip data.
// Header-only: shared by pathmux (find_trips.cpp) and pm_ls.
//
// Available field names:
//   manifest_id, trip_id, address, date, start_time, start_epoch,
//   duration, duration_seconds, segment_count, note,
//   start_lat, start_lon, end_lat, end_lon,
//   distance_km, distance_mi, gps_lock_seconds, gps_track_status
// ---------------------------------------------------------------------------

#include "trip_detection.hpp"
#include "config_manager.hpp"
#include "format_helpers.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace Pathmux {

// Default fields emitted when --fields is not specified.
inline std::vector<std::string> defaultTripFields() {
    return {
        "manifest_id", "trip_id", "date", "start_time", "duration",
        "segment_count", "start_lat", "start_lon", "end_lat", "end_lon"
    };
}

// Get the value of one field for a trip as a plain string.
// Returns "" for unknown fields or fields with no data.
inline std::string tripFieldVal(const std::string& field,
                                 const ManifestEntry& me,
                                 const Trip& t)
{
    if (field == "manifest_id")      return me.id;
    if (field == "trip_id")          return t.id;
    if (field == "address")          return me.id + ":" + t.id;
    if (field == "date")             return t.date;
    if (field == "start_time")       return t.startTime;
    if (field == "start_epoch")      return std::to_string(t.startEpoch);
    if (field == "duration")         return t.duration;
    if (field == "duration_seconds") return std::to_string(t.segDetectedDuration);
    if (field == "segment_count")    return std::to_string((int)t.segments.size());
    if (field == "note")             return t.note;
    if (field == "start_lat")        return (t.startLat != 0.0)
                                         ? std::to_string(t.startLat) : "";
    if (field == "start_lon")        return (t.startLon != 0.0)
                                         ? std::to_string(t.startLon) : "";
    if (field == "end_lat")          return (t.endLat != 0.0)
                                         ? std::to_string(t.endLat) : "";
    if (field == "end_lon")          return (t.endLon != 0.0)
                                         ? std::to_string(t.endLon) : "";
    if (field == "distance_km" || field == "distance_mi") {
        if (t.startLat == 0.0 && t.startLon == 0.0) return "";
        if (t.endLat   == 0.0 && t.endLon   == 0.0) return "";
        double km = haversineKm(t.startLat, t.startLon, t.endLat, t.endLon);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        oss << (field == "distance_mi" ? km * 0.621371 : km);
        return oss.str();
    }
    if (field == "gps_lock_seconds") return (t.gpsLockSeconds >= 0)
                                         ? std::to_string(t.gpsLockSeconds) : "";
    if (field == "gps_track_status") return t.gpsTrackStatus;
    return "";
}

// CSV-quote a field value: wraps in quotes if it contains comma, quote, or newline.
inline std::string csvQuote(const std::string& v) {
    if (v.find_first_of(",\"\n") == std::string::npos) return v;
    std::string escaped;
    for (char c : v)
        if (c == '"') escaped += "\"\""; else escaped += c;
    return "\"" + escaped + "\"";
}

// Write CSV output for the given manifests+trips to stdout.
// cols: field list; if empty, defaultTripFields() is used.
inline void writeTripsCSV(const std::vector<ManifestEntry>& index,
                           ConfigManager& config,
                           const std::vector<std::string>& cols,
                           const std::string& filterMid = "",
                           const std::string& filterTid = "")
{
    const std::vector<std::string>& fields = cols.empty() ? defaultTripFields() : cols;

    // Header
    for (int i = 0; i < (int)fields.size(); ++i) {
        if (i) std::cout << ",";
        std::cout << fields[i];
    }
    std::cout << "\n";

    for (const auto& me : index) {
        if (!filterMid.empty() && me.id != filterMid) continue;
        auto trips = config.loadTripCache(me.path);
        for (const auto& t : trips) {
            if (!filterTid.empty() && t.id != filterTid) continue;
            for (int i = 0; i < (int)fields.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << csvQuote(tripFieldVal(fields[i], me, t));
            }
            std::cout << "\n";
        }
    }
}

// Write XML output for the given manifests+trips to stdout.
// cols: field list; if empty, defaultTripFields() is used.
inline void writeTripsXML(const std::vector<ManifestEntry>& index,
                           ConfigManager& config,
                           const std::vector<std::string>& cols,
                           const std::string& version,
                           const std::string& filterMid = "",
                           const std::string& filterTid = "")
{
    const std::vector<std::string>& fields = cols.empty() ? defaultTripFields() : cols;

    std::cout << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              << "<pathmux version=\"" << version << "\">\n";

    for (const auto& me : index) {
        if (!filterMid.empty() && me.id != filterMid) continue;
        auto trips = config.loadTripCache(me.path);

        bool anyTrips = false;
        for (const auto& t : trips)
            if (filterTid.empty() || t.id == filterTid) { anyTrips = true; break; }
        if (!anyTrips) continue;

        std::cout << "  <manifest id=\"" << me.id
                  << "\" path=\"" << me.path << "\">\n";
        for (const auto& t : trips) {
            if (!filterTid.empty() && t.id != filterTid) continue;
            std::cout << "    <trip>\n";
            for (const auto& f : fields) {
                std::string v = tripFieldVal(f, me, t);
                if (!v.empty())
                    std::cout << "      <" << f << ">" << v << "</" << f << ">\n";
            }
            std::cout << "    </trip>\n";
        }
        std::cout << "  </manifest>\n";
    }

    std::cout << "</pathmux>\n";
}

} // namespace Pathmux

#endif
// SN: 00089
