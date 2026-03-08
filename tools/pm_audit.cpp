// pm_audit — footage integrity checker (PathMux suite)
//
// Usage:
//   pm_audit                 Audit all manifests
//   pm_audit MID             Audit one manifest
//   pm_audit MID:TID         Audit one trip
//   pm_audit --deep          Also run ffprobe on every segment
//   pm_audit --json          Machine-readable output
//
// Checks that the source footage files referenced in each trip's validation
// sample are still present and unmodified.  Does not write to any manifest.
// Exit 0 = all OK; exit 1 = one or more problems found.

#include "pathmux.hpp"
#include "compat.hpp"
#include "json.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace Pathmux;

// ---------------------------------------------------------------------------
// Trip audit result
// ---------------------------------------------------------------------------
enum class TripStatus { Ok, Missing, Modified, NoValidation, DeepFail };

struct TripResult {
    std::string mid;
    std::string tid;
    std::string date;
    std::string startTime;
    int         segCount    = 0;
    TripStatus  status      = TripStatus::Ok;
    std::string detail;     // which file caused the problem
};

static std::string statusStr(TripStatus s)
{
    switch (s) {
        case TripStatus::Ok:           return "OK";
        case TripStatus::Missing:      return "MISSING";
        case TripStatus::Modified:     return "MODIFIED";
        case TripStatus::NoValidation: return "NO_VALIDATION";
        case TripStatus::DeepFail:     return "CORRUPT";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// printUsage
// ---------------------------------------------------------------------------
static void printUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << " [--deep] [--json] [MID | MID:TID]\n\n"
        << "  (no args)    Audit all manifests\n"
        << "  MID          Audit one manifest\n"
        << "  MID:TID      Audit one trip\n"
        << "  --deep       Also run ffprobe on every segment (slow)\n"
        << "  --json       Machine-readable JSON output\n\n"
        << "  Exit 0 = all OK.  Exit 1 = one or more problems found.\n";
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
// auditTrip — check validation files; return TripResult
// ---------------------------------------------------------------------------
static TripResult auditTrip(const std::string& mid,
                             const Trip& trip,
                             const std::string& sourcePath)
{
    TripResult r;
    r.mid       = mid;
    r.tid       = trip.id;
    r.date      = trip.date;
    r.startTime = trip.startTime;
    r.segCount  = (int)trip.segments.size();

    if (trip.validationFiles.empty()) {
        r.status = TripStatus::NoValidation;
        return r;
    }

    for (const auto& vf : trip.validationFiles) {
        std::string absPath = sourcePath + "/" + vf.relPath;

        if (!fs::exists(absPath)) {
            r.status = TripStatus::Missing;
            r.detail = vf.relPath;
            return r;
        }

        if (!vf.md5.empty()) {
            std::string computed = ConfigManager::fileMd5(absPath);
            if (computed != vf.md5) {
                r.status = TripStatus::Modified;
                r.detail = vf.relPath;
                return r;
            }
        }
    }

    r.status = TripStatus::Ok;
    return r;
}

// ---------------------------------------------------------------------------
// deepAuditTrip — run ffprobe on every segment; updates result if any fail
// ---------------------------------------------------------------------------
static void deepAuditTrip(TripResult& r,
                           const Trip& trip,
                           const std::string& ffprobePath)
{
    // Only run deep check if basic validation passed
    if (r.status != TripStatus::Ok) return;

    for (const auto& seg : trip.segments) {
        // Check front camera — the primary stream
        if (seg.front.empty() || seg.front == "-") continue;
        if (!fs::exists(seg.front)) {
            r.status = TripStatus::Missing;
            r.detail = seg.front;
            return;
        }

        // ffprobe: check that the file has a readable video stream with duration > 0
        std::string cmd = ffprobePath
            + " -v error -select_streams v:0"
            + " -show_entries stream=duration"
            + " -of csv=p=0 \"" + seg.front + "\" 2>/dev/null";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) continue;

        char buf[64] = {};
        bool gotOutput = (fgets(buf, sizeof(buf), pipe) != nullptr);
        pclose(pipe);

        double dur = gotOutput ? std::atof(buf) : 0.0;
        if (!gotOutput || dur <= 0.0) {
            r.status = TripStatus::DeepFail;
            // Extract basename for display
            r.detail = pathBasename(seg.front);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    bool deepMode = false;
    bool jsonMode = false;
    std::string filter;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { printUsage(argv[0]); return 0; }
        else if (arg == "--deep")           { deepMode = true; }
        else if (arg == "--json")           { jsonMode = true; }
        else if (arg[0] != '-')             { filter = arg; }
        else { std::cerr << "Unknown argument: " << arg << "\n"; return 1; }
    }

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
        std::cerr << "No cached manifests. Run: pathmux -s <path>\n";
        return 1;
    }

    if (!filterMid.empty()) {
        bool found = false;
        for (const auto& e : index)
            if (normalizeId(e.id) == filterMid) { found = true; break; }
        if (!found) {
            std::cerr << "Error: manifest '" << filterMid << "' not found.\n";
            return 1;
        }
    }

    std::string ffprobePath = config.getFfprobePath();

    // --deep warning for large collections
    if (deepMode && filter.empty()) {
        int totalTrips = 0;
        for (const auto& e : index) {
            auto trips = config.loadTripCache(e.manifestFile);
            totalTrips += (int)trips.size();
        }
        if (totalTrips > 5 && !jsonMode) {
            std::cerr << "Warning: --deep will run ffprobe on every segment of "
                      << totalTrips << " trips.  This may take several minutes.\n"
                      << "Continue? [Y/N]: ";
            std::string ans;
            std::getline(std::cin >> std::ws, ans);
            if (ans != "y" && ans != "Y") return 0;
        }
    }

    // ---- Collect results ----
    struct ManifestResults {
        ManifestEntry           entry;
        std::vector<TripResult> trips;
    };
    std::vector<ManifestResults> allResults;

    for (const auto& entry : index) {
        if (!filterMid.empty() && normalizeId(entry.id) != filterMid)
            continue;

        auto trips = config.loadTripCache(entry.manifestFile);
        ManifestResults mr;
        mr.entry = entry;

        for (const auto& trip : trips) {
            if (!filterTid.empty() && normalizeId(trip.id) != filterTid)
                continue;

            TripResult r = auditTrip(entry.id, trip, entry.path);
            if (deepMode)
                deepAuditTrip(r, trip, ffprobePath);

            mr.trips.push_back(r);
        }

        if (!mr.trips.empty())
            allResults.push_back(mr);
    }

    // ---- Count problems ----
    int totalTrips = 0, problemTrips = 0;
    for (const auto& mr : allResults) {
        for (const auto& r : mr.trips) {
            ++totalTrips;
            if (r.status != TripStatus::Ok) ++problemTrips;
        }
    }

    // -------------------------------------------------------------------------
    // JSON output
    // -------------------------------------------------------------------------
    if (jsonMode) {
        json out = json::array();
        for (const auto& mr : allResults) {
            json jm = {
                {"id",           mr.entry.id},
                {"path",         mr.entry.path},
                {"manifestFile", mr.entry.manifestFile},
                {"trips",        json::array()}
            };
            for (const auto& r : mr.trips) {
                json jt = {
                    {"mid",        r.mid},
                    {"tid",        r.tid},
                    {"date",       r.date},
                    {"start_time", r.startTime},
                    {"seg_count",  r.segCount},
                    {"status",     statusStr(r.status)},
                    {"detail",     r.detail}
                };
                jm["trips"].push_back(jt);
            }
            out.push_back(jm);
        }

        json summary = {
            {"total_trips",   totalTrips},
            {"problem_trips", problemTrips},
            {"all_ok",        problemTrips == 0},
            {"manifests",     out}
        };
        std::cout << summary.dump(2) << "\n";
        return problemTrips > 0 ? 1 : 0;
    }

    // -------------------------------------------------------------------------
    // Text output
    // -------------------------------------------------------------------------
    for (const auto& mr : allResults) {
        std::cout << "[" << mr.entry.id << "]  " << mr.entry.path << "\n";

        for (const auto& r : mr.trips) {
            std::string stat = statusStr(r.status);
            std::cout << "  " << std::left
                      << std::setw(4)  << r.mid
                      << std::setw(4)  << r.tid
                      << std::setw(12) << stat
                      << std::setw(12) << r.date
                      << std::setw(10) << r.startTime
                      << std::setw(5)  << r.segCount << " segs";
            if (!r.detail.empty())
                std::cout << "  (" << r.detail << ")";
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    if (totalTrips == 0) {
        std::cout << "(no trips found)\n";
        return 0;
    }

    if (problemTrips == 0) {
        std::cout << "All " << totalTrips << " trip"
                  << (totalTrips != 1 ? "s" : "") << " OK.\n";
    } else {
        std::cout << problemTrips << " of " << totalTrips << " trip"
                  << (totalTrips != 1 ? "s" : "") << " have problems.\n";
    }

    return problemTrips > 0 ? 1 : 0;
}
// SN: 00083
