// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
// clops_ls — non-interactive trip lister (CamClops suite)
//
// Usage:
//   clops_ls                  List all manifests and their trips
//   clops_ls MID              List trips for one manifest
//   clops_ls MID:TID          Show details for a single trip
//   clops_ls --json           JSON output (all manifests)
//   clops_ls --json MID       JSON output for one manifest
//   clops_ls --json MID:TID   JSON output for one trip
//
// Output is to stdout.  Errors go to stderr.
// Exit 0 = success; exit 1 = unknown MID/TID or other error.

#include "camclops.hpp"
#include "format_helpers.hpp"
#include "trip_format.hpp"
#include "json.hpp"
#include "version.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;
using namespace CamClops;

// ---------------------------------------------------------------------------
// printUsage
// ---------------------------------------------------------------------------
static void printUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << " [options] [MID | MID:TID]\n\n"
        << "  (no args)          List all manifests and trips\n"
        << "  MID                List trips for one manifest\n"
        << "  MID:TID            Show details for one trip\n"
        << "  --json             JSON output (all or filtered)\n"
        << "  --format=<fmt>     Structured output: json, csv, xml\n"
        << "  --fields=<f1,f2>   Fields for csv/xml output\n"
        << "    Fields: manifest_id trip_id address date start_time start_epoch\n"
        << "            duration duration_seconds segment_count note\n"
        << "            start_lat start_lon end_lat end_lon\n"
        << "            distance_km distance_mi gps_lock_seconds gps_track_status\n"
        << "  -v, --version      Show version and exit\n\n"
        << "  Default text columns:\n"
        << "    MID  TID  date        start     duration   segs  distance  GPS\n";
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
// tripLine — format one trip as a single summary line
// ---------------------------------------------------------------------------
static std::string tripLine(const std::string& mid, const Trip& t, bool imperial)
{
    double dist = haversineKm(t.startLat, t.startLon, t.endLat, t.endLon);
    std::string distStr = (t.startLat == 0.0 && t.startLon == 0.0)
                          ? "    -   "
                          : formatDistance(dist, imperial);

    std::string gps = (t.gpsTrackStatus == "complete") ? "track"  :
                      (t.gpsTrackStatus == "partial")  ? "part"   :
                      (t.startLat != 0.0)              ? "pts"    : "-";

    // width columns: MID(3) TID(4) date(12) start(10) dur(10) segs(6) dist(9) gps(5)
    std::ostringstream oss;
    oss << std::left
        << std::setw(4)  << mid
        << std::setw(4)  << t.id
        << std::setw(12) << t.date
        << std::setw(10) << t.startTime
        << std::setw(10) << t.duration
        << std::setw(6)  << t.segments.size()
        << std::setw(10) << distStr
        << gps;
    return oss.str();
}

// ---------------------------------------------------------------------------
// tripDetail — multi-line detail block for a single trip
// ---------------------------------------------------------------------------
static void printTripDetail(const std::string& mid,
                             const ManifestEntry& entry,
                             const Trip& t,
                             bool imperial)
{
    double dist = haversineKm(t.startLat, t.startLon, t.endLat, t.endLon);

    std::cout << "Trip:       " << mid << ":" << t.id << "\n"
              << "Date:       " << t.date << "\n"
              << "Start:      " << t.startTime << "\n"
              << "Duration:   " << t.duration;
    if (t.durationFFProbed >= 0) {
        int h = t.durationFFProbed / 3600;
        int m = (t.durationFFProbed % 3600) / 60;
        int s = t.durationFFProbed % 60;
        std::cout << "  (ffprobe: ";
        if (h) std::cout << h << "h ";
        if (h || m) std::cout << m << "m ";
        std::cout << s << "s)";
    }
    std::cout << "\n"
              << "Segments:   " << t.segments.size() << "\n"
              << "Seg dur:    " << t.segdur << "s\n"
              << "Manifest:   " << entry.manifestFile << "\n"
              << "Source:     " << entry.path << "\n";

    if (t.startLat != 0.0 || t.startLon != 0.0) {
        std::cout << std::fixed << std::setprecision(6)
                  << "Start pos:  " << t.startLat << ", " << t.startLon << "\n"
                  << "End pos:    " << t.endLat   << ", " << t.endLon   << "\n"
                  << "Distance:   " << formatDistance(dist, imperial) << "\n";
    }

    std::cout << "GPS:        " << t.gpsTrackStatus;
    if (t.gpsLockSeconds >= 0)
        std::cout << "  (lock: " << t.gpsLockSeconds << "s)";
    std::cout << "\n";

    if (!t.note.empty())
        std::cout << "Note:       " << t.note << "\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    bool jsonMode = false;
    std::string formatArg;
    std::vector<std::string> fieldsArg;
    std::string filter;   // MID or MID:TID

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { printUsage(argv[0]); return 0; }
        else if (arg == "-v" || arg == "--version") {
            std::cout << APP_NAME << " clops_ls v" << APP_VERSION << "\n"; return 0;
        }
        else if (arg == "--json")              { jsonMode = true; }
        else if (arg.find("--format=") == 0)   { formatArg = arg.substr(9); }
        else if (arg.find("--fields=") == 0) {
            std::istringstream ss(arg.substr(9));
            std::string tok;
            while (std::getline(ss, tok, ','))
                if (!tok.empty()) fieldsArg.push_back(tok);
        }
        else if (arg[0] != '-')                { filter = arg; }
        else { std::cerr << "Unknown option: " << arg << "\n"; printUsage(argv[0]); return 1; }
    }

    // --format=json is an alias for --json
    if (formatArg == "json") jsonMode = true;

    // Determine if filter is MID or MID:TID
    bool filterHasColon = (filter.find(':') != std::string::npos);
    std::string filterMid, filterTid;
    if (!filter.empty()) {
        if (filterHasColon) {
            auto colon = filter.find(':');
            filterMid = normalizeId(filter.substr(0, colon));
            filterTid = normalizeId(filter.substr(colon + 1));
        } else {
            filterMid = normalizeId(filter);
        }
    }

    ConfigManager config;
    auto index = config.loadManifestIndex();

    if (index.empty()) {
        std::cerr << "No cached manifests. Run: camclops -s <path>\n";
        return 1;
    }

    // Validate filter MID exists
    if (!filterMid.empty()) {
        bool found = false;
        for (const auto& e : index)
            if (normalizeId(e.id) == filterMid) { found = true; break; }
        if (!found) {
            std::cerr << "Error: manifest '" << filterMid << "' not found.\n";
            return 1;
        }
    }

    bool imperial = config.getUseImperial();

    // -------------------------------------------------------------------------
    // Structured format output (csv / xml)
    // -------------------------------------------------------------------------
    if (!formatArg.empty() && formatArg != "json") {
        if (formatArg == "csv") {
            writeTripsCSV(index, config, fieldsArg, filterMid, filterTid);
        } else if (formatArg == "xml") {
            writeTripsXML(index, config, fieldsArg, APP_VERSION, filterMid, filterTid);
        } else {
            std::cerr << "Unknown format '" << formatArg
                      << "'. Valid: json, csv, xml\n";
            return 1;
        }
        return 0;
    }

    // -------------------------------------------------------------------------
    // JSON output
    // -------------------------------------------------------------------------
    if (jsonMode) {
        json out = json::array();

        for (const auto& entry : index) {
            if (!filterMid.empty() && normalizeId(entry.id) != filterMid)
                continue;

            auto trips = config.loadTripCache(entry.manifestFile);
            json jManifest = {
                {"id",           entry.id},
                {"path",         entry.path},
                {"manifestFile", entry.manifestFile},
                {"trips",        json::array()}
            };

            for (const auto& t : trips) {
                if (!filterTid.empty() && normalizeId(t.id) != filterTid)
                    continue;

                double dist = haversineKm(t.startLat, t.startLon, t.endLat, t.endLon);
                json jt = {
                    {"id",             t.id},
                    {"date",           t.date},
                    {"start_time",     t.startTime},
                    {"duration",       t.duration},
                    {"segment_count",  (int)t.segments.size()},
                    {"segdur",         t.segdur},
                    {"distance_km",    (t.startLat == 0.0 && t.startLon == 0.0) ? -1.0 : dist},
                    {"start_lat",      t.startLat},
                    {"start_lon",      t.startLon},
                    {"end_lat",        t.endLat},
                    {"end_lon",        t.endLon},
                    {"gps_status",     t.gpsTrackStatus},
                    {"gps_lock_sec",   t.gpsLockSeconds},
                    {"duration_ffprobed", t.durationFFProbed},
                    {"note",           t.note}
                };
                jManifest["trips"].push_back(jt);
            }

            // Single-trip mode: emit just the trip object, not wrapped
            if (!filterTid.empty() && jManifest["trips"].size() == 1) {
                std::cout << jManifest["trips"][0].dump(2) << "\n";
                return 0;
            }

            out.push_back(jManifest);
        }

        std::cout << out.dump(2) << "\n";
        return 0;
    }

    // -------------------------------------------------------------------------
    // Text output
    // -------------------------------------------------------------------------

    // Single trip detail
    if (filterHasColon && !filterTid.empty()) {
        for (const auto& entry : index) {
            if (normalizeId(entry.id) != filterMid) continue;
            auto trips = config.loadTripCache(entry.manifestFile);
            for (const auto& t : trips) {
                if (normalizeId(t.id) == filterTid) {
                    printTripDetail(entry.id, entry, t, imperial);
                    return 0;
                }
            }
            std::cerr << "Error: trip '" << filterTid << "' not found in manifest '"
                      << filterMid << "'.\n";
            return 1;
        }
    }

    // Summary header
    std::cout << std::left
              << std::setw(4)  << "MID"
              << std::setw(4)  << "TID"
              << std::setw(12) << "Date"
              << std::setw(10) << "Start"
              << std::setw(10) << "Duration"
              << std::setw(6)  << "Segs"
              << std::setw(10) << "Distance"
              << "GPS\n"
              << std::string(64, '-') << "\n";

    int totalTrips = 0;

    for (const auto& entry : index) {
        if (!filterMid.empty() && normalizeId(entry.id) != filterMid)
            continue;

        auto trips = config.loadTripCache(entry.manifestFile);
        if (trips.empty()) continue;

        // Print manifest header line when showing all manifests
        if (filterMid.empty())
            std::cout << "[" << entry.id << "]  " << entry.path << "\n";

        for (const auto& t : trips) {
            std::cout << tripLine(entry.id, t, imperial) << "\n";
            ++totalTrips;
        }

        if (filterMid.empty())
            std::cout << "\n";
    }

    if (totalTrips == 0)
        std::cout << "(no trips found)\n";

    return 0;
}
// SN: 00112
