#include "find_trips.hpp"
#include "config_manager.hpp"
#include "ui_helpers.hpp"
#include "version.hpp"
#include "video_build.hpp"
#include "trip_detection.hpp"
#include "trip_format.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include "compat.hpp"
#include <vector>

using namespace Pathmux;

// ---------------------------------------------------------------------------
// normalizeId — uppercase + remap lookalike chars
// ---------------------------------------------------------------------------
std::string FindTrips::normalizeId(const std::string& input) {
    std::string out;
    for (char c : input) {
        char u = std::toupper((unsigned char)c);
        if      (u == 'O') u = '0';
        else if (u == 'I') u = '1';
        else if (u == 'L') u = '1';
        out += u;
    }
    return out;
}

// ---------------------------------------------------------------------------
// drawManifestList — home screen table
// ---------------------------------------------------------------------------
void FindTrips::drawManifestList(const std::vector<ManifestEntry>& index) {
    std::cout << "\n";
    UI::printTitle("PathMux v" + std::string(APP_VERSION) + " -- Manifests");
    if (index.empty()) {
        UI::printLine("  (No manifests found — use [S] to scan a path)");
    } else {
        UI::printLine("  ID   Trips  First Trip            Last Trip");
        UI::printLine("  " + std::string(47, '-'));
        for (const auto& e : index) {
            std::string rowBase;
            {
                std::ostringstream rb;
                rb << "  "
                   << std::left << std::setw(4)  << e.id
                   << std::setw(7) << e.tripCount
                   << std::setw(21) << e.firstTrip
                   << e.lastTrip;
                rowBase = rb.str();
            }
            std::string rowFull = rowBase;
            if (!e.path.empty()) {
                int remaining = UI::innerWidth() - (int)rowBase.size() - 2;
                if (remaining > 6) {
                    std::string pathStr = "  " + e.path;
                    if ((int)pathStr.size() > remaining + 2)
                        pathStr = "  " + e.path.substr(0, remaining - 3) + "...";
                    rowFull += pathStr;
                }
            }
            UI::printLine(rowFull);
        }
    }
    UI::printFooter("[<ID>] Select Manifest   [S] Scan New Path   [V] Validate   [Q] Quit");
}

// ---------------------------------------------------------------------------
// runInteractive — -I unified browser entry point
// ---------------------------------------------------------------------------
void FindTrips::runInteractive(ConfigManager& config) {
    TripDetection detector;

    while (true) {
        auto index = config.loadManifestIndex();
        drawManifestList(index);

        std::string input = UI::readCommand();

        // Single-char commands on raw input; normalizeId only for ID lookup
        std::string raw = input;
        for (char& c : raw) c = std::toupper((unsigned char)c);

        if (raw == "Q") return;

        if (raw == "S") {
            std::cout << "  Enter path to scan: ";
            std::string path;
            std::getline(std::cin >> std::ws, path);
            if (path.empty()) continue;
            if (!std::filesystem::exists(path)) {
                std::cout << "  Path does not exist: " << path << "\n";
                continue;
            }
            std::cout << "  Scanning: " << path
                      << "  (gap=" << config.getGapThreshold() << "s)\n";
            auto trips = detector.detectTrips(path,
                                              config.getGapThreshold(),
                                              config.getFuzzyWindow(),
                                              config.getFfprobePath(),
                                              config.getExiftoolPath(),
                                              config.getExiftoolOptions());
            if (!trips.empty()) {
                config.saveTripCache(path, trips);
                std::cout << "  Scan complete — " << trips.size() << " trip(s) found.\n";
            } else {
                std::cout << "  No trips found in " << path << "\n";
            }
            continue;
        }

        if (raw == "V") {
            std::cout << "  Validate [<ID>] or [ALL]: ";
            std::string vid;
            std::getline(std::cin >> std::ws, vid);
            std::string vraw = vid;
            for (char& c : vraw) c = std::toupper((unsigned char)c);
            std::string vup = normalizeId(vid);
            if (vraw == "ALL") {
                for (const auto& e : index) {
                    bool exists = std::filesystem::exists(e.manifestFile);
                    std::string currentMd5 = exists
                        ? ConfigManager::fileMd5(e.manifestFile) : "";
                    std::cout << "  [" << e.id << "] " << e.path << "  ";
                    if (!exists)
                        std::cout << "MISSING\n";
                    else if (currentMd5 != e.manifestMd5)
                        std::cout << "MODIFIED\n";
                    else
                        std::cout << "OK\n";
                }
            } else {
                const ManifestEntry* found = nullptr;
                for (const auto& e : index)
                    if (normalizeId(e.id) == vup) { found = &e; break; }
                if (!found) {
                    std::cout << "  Unknown manifest ID: " << vup << "\n";
                } else {
                    bool exists = std::filesystem::exists(found->manifestFile);
                    std::string currentMd5 = exists
                        ? ConfigManager::fileMd5(found->manifestFile) : "";
                    std::cout << "  [" << found->id << "] " << found->path << "  ";
                    if (!exists)        std::cout << "MISSING\n";
                    else if (currentMd5 != found->manifestMd5) std::cout << "MODIFIED\n";
                    else                std::cout << "OK\n";
                }
            }
            continue;
        }

        // Try as manifest ID — normalize both sides
        std::string up = normalizeId(input);
        const ManifestEntry* selected = nullptr;
        for (const auto& e : index)
            if (normalizeId(e.id) == up) { selected = &e; break; }

        if (selected) {
            if (!runManifestMenu(config, *selected)) return; // user hit Q
        } else {
            std::cout << "  Unknown option or manifest ID: " << input << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// runManifestMenu — trip list for one manifest, pick trip to build
// ---------------------------------------------------------------------------
bool FindTrips::runManifestMenu(ConfigManager& config,
                                 const ManifestEntry& entry) {
    VideoBuilder videoBuilder;

    while (true) {
        // Reload entry from index to get current note
        auto index = config.loadManifestIndex();
        const ManifestEntry* currentEntry = &entry;
        for (const auto& e : index)
            if (e.path == entry.path) { currentEntry = &e; break; }

        auto trips = config.loadTripCache(entry.path);

        std::cout << "\n";
        UI::printTitle("PathMux v" + std::string(APP_VERSION)
                       + " -- Manifest " + entry.id
                       + "  " + entry.path);
        {
            std::string nd = currentEntry->note.empty() ? "(none)"
                : currentEntry->note.substr(0, 48)
                  + (currentEntry->note.size() > 48 ? "..." : "");
            UI::printLine("  Note: " + nd);
        }
        UI::printDivider();

        if (trips.empty()) {
            UI::printLine("  (No trips in this manifest)");
        } else {
            bool imperial = config.getUseImperial();
            UI::printLine("  ID   Date         Start     Segs  Duration      Crow's dist");
            UI::printLine("  " + std::string(65, '-'));
            for (const auto& t : trips) {
                std::string crowStr;
                if (t.startLat != 0.0 && t.endLat != 0.0) {
                    double km = haversineKm(t.startLat, t.startLon, t.endLat, t.endLon);
                    crowStr = formatDistance(km, imperial);
                }
                std::string rowBase;
                {
                    std::ostringstream rb;
                    rb << "  "
                       << std::left << std::setw(5)  << t.id
                       << std::setw(13) << t.date
                       << std::setw(10) << t.startTime
                       << std::right << std::setw(4) << t.segments.size()
                       << "  " << std::left << std::setw(14) << t.duration
                       << crowStr;
                    rowBase = rb.str();
                }
                std::string rowFull = rowBase;
                if (!t.note.empty()) {
                    int remaining = UI::innerWidth() - (int)rowBase.size() - 2;
                    if (remaining > 6) {
                        std::string noteStr = "  " + t.note;
                        if ((int)noteStr.size() > remaining + 2)
                            noteStr = "  " + t.note.substr(0, remaining - 3) + "...";
                        rowFull += noteStr;
                    }
                }
                UI::printLine(rowFull);
            }
        }
        UI::printFooter("[<TripID>] Select Trip   [N] Set Note   [S] Re-scan   [L] Manifest List   [Q] Quit");

        std::string input = UI::readCommand();

        // Single-char commands checked on raw (uppercased) input before
        // normalization — L, S, Q are commands, not ID characters.
        std::string raw = input;
        for (char& c : raw) c = std::toupper((unsigned char)c);

        if (raw == "Q") return false;
        if (raw == "L") return true;  // back to manifest list

        if (raw == "N") {
            std::string cur = currentEntry->note;
            if (cur.empty()) {
                std::cout << "  Set note (Enter to cancel):\n  > ";
            } else {
                std::cout << "  Current note:\n  " << cur << "\n"
                          << "  Set note (Enter to keep, space+Enter to clear):\n  > ";
            }
            std::string noteInput;
            std::getline(std::cin, noteInput);
            if (!noteInput.empty() && noteInput.find_first_not_of(' ') == std::string::npos) {
                config.saveManifestNote(entry.path, "");
                std::cout << "  Note cleared.\n";
            } else if (!noteInput.empty()) {
                config.saveManifestNote(entry.path, noteInput);
                std::cout << "  Note saved.\n";
            }
            continue;
        }

        if (raw == "S") {
            std::cout << "  Re-scanning " << entry.path << " ...\n";
            TripDetection detector;
            auto newTrips = detector.detectTrips(entry.path,
                                                  config.getGapThreshold(),
                                                  config.getFuzzyWindow(),
                                                  config.getFfprobePath(),
                                                  config.getExiftoolPath(),
                                              config.getExiftoolOptions());
            if (!newTrips.empty()) {
                config.saveTripCache(entry.path, newTrips);
                std::cout << "  Scan complete — " << newTrips.size()
                          << " trip(s) found.\n";
            } else {
                std::cout << "  No trips found.\n";
            }
            continue;
        }

        // Try as trip ID — normalize both sides
        std::string up = normalizeId(input);
        Trip* selected = nullptr;
        for (auto& t : trips)
            if (normalizeId(t.id) == up) { selected = &t; break; }

        if (selected) {
            std::cout << "\nSelected: " << selected->id
                      << "  " << selected->date
                      << " " << selected->startTime
                      << "  (" << selected->segments.size() << " segments)\n";
            if (!selected->note.empty())
                std::cout << "  Note: " << selected->note << "\n";

            VideoOptions opts = videoBuilder.configureOptions(config, *selected);
            opts.sourcePath = entry.path;
            opts.manifestId = config.getManifestIdForPath(entry.path);

            // Persist note edits and any durationFFProbed computed during options menu
            config.saveTripCache(entry.path, trips);

            if (opts.navAction == NavAction::QUIT)           return false;
            if (opts.navAction == NavAction::SWITCH_MANIFEST) return true;
            if (opts.navAction == NavAction::SWITCH_TRIP)    continue;

            // Check something was selected
            bool anyWork = opts.buildFront || opts.buildRear || opts.buildLeft ||
                           opts.buildRight || opts.buildCollage4K ||
                           opts.buildCollage1080 || opts.buildAudio;
            if (!anyWork) continue;

            // Ensure output dir
            std::string outDir = opts.outputDir.empty() ? "." : opts.outputDir;
            std::error_code ec;
            if (!std::filesystem::exists(outDir))
                std::filesystem::create_directories(outDir, ec);
            opts.outputDir = outDir;

            videoBuilder.buildTrip(*selected, opts);

            if (opts.exitAfterBuild) return false;
        } else {
            std::cout << "  Unknown trip ID: " << input << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// showTripSummary — non-interactive -s output
// ---------------------------------------------------------------------------
void FindTrips::showTripSummary(const std::vector<Trip>& trips,
                                 const std::string& currentPath,
                                 bool imperial,
                                 const std::string& manifestId) {
    std::string label = manifestId.empty() ? "" : " [" + manifestId + "]";
    std::cout << "\n--- Manifest" << label << " summary for: " << currentPath << " ---\n";
    std::cout << std::left
              << std::setw(6)  << "ID"
              << std::setw(15) << "Date"
              << std::setw(15) << "Start"
              << std::setw(15) << "Duration"
              << "Crow's dist\n";
    std::cout << std::string(70, '-') << "\n";
    for (const auto& t : trips) {
        std::string crowStr;
        if (t.startLat != 0.0 && t.endLat != 0.0) {
            double km = haversineKm(t.startLat, t.startLon, t.endLat, t.endLon);
            crowStr = formatDistance(km, imperial);
        }
        std::cout << std::left
                  << std::setw(6)  << t.id
                  << std::setw(15) << t.date
                  << std::setw(15) << t.startTime
                  << std::setw(15) << t.duration
                  << crowStr << "\n";
    }
}

// ---------------------------------------------------------------------------
// showTripDetails — single trip detail view
// ---------------------------------------------------------------------------
void FindTrips::showTripDetails(const Trip& trip, bool imperial) {
    std::cout << "\n--- Trip " << trip.id << "  "
              << trip.date << " " << trip.startTime << " ---\n";
    if (!trip.note.empty())
        std::cout << "Note: " << trip.note << "\n";
    std::cout << "Duration:  " << trip.duration << "\n";
    if (trip.startLat != 0.0 && trip.endLat != 0.0) {
        double km = haversineKm(trip.startLat, trip.startLon, trip.endLat, trip.endLon);
        std::cout << "Crow's dist: " << formatDistance(km, imperial) << "\n";
    }
    std::cout << std::left << std::setw(20) << "Timestamp"
              << "[ F  B  L  R ]\n";
    std::cout << std::string(45, '-') << "\n";
    for (const auto& seg : trip.segments) {
        std::cout << std::left << std::setw(20) << seg.timestamp << "[ ";
        std::cout << (seg.front != "-" ? "F  " : "   ");
        std::cout << (seg.rear  != "-" ? "B  " : "   ");
        std::cout << (seg.left  != "-" ? "L  " : "   ");
        std::cout << (seg.right != "-" ? "R " : "  ");
        std::cout << "]\n";
    }
}

// ---------------------------------------------------------------------------
// showAllTrees — -T / --dump: manifests → trips, no file listing
// ---------------------------------------------------------------------------
void FindTrips::showAllTrees(ConfigManager& config) {
    auto index = config.loadManifestIndex();
    if (index.empty()) { std::cout << "No manifests found.\n"; return; }
    bool imperial = config.getUseImperial();
    for (const auto& entry : index) {
        std::cout << "\nManifest " << entry.id
                  << "  " << entry.path
                  << "  (" << entry.tripCount << " trip"
                  << (entry.tripCount != 1 ? "s" : "") << ")"
                  << (entry.note.empty() ? "" : "  [" + entry.note.substr(0,30)
                      + (entry.note.size() > 30 ? "..." : "") + "]")
                  << "\n";
        std::cout << std::string(70, '-') << "\n";

        auto trips = config.loadTripCache(entry.path);
        if (trips.empty()) { std::cout << "  (no trips)\n"; continue; }

        std::cout << "  " << std::left
                  << std::setw(5)  << "ID"
                  << std::setw(13) << "Date"
                  << std::setw(10) << "Start"
                  << std::setw(6)  << "Segs"
                  << std::setw(15) << "Duration"
                  << "Crow's dist\n";
        std::cout << "  " << std::string(60, '-') << "\n";
        for (const auto& t : trips) {
            std::string crowStr;
            if (t.startLat != 0.0 && t.endLat != 0.0) {
                double km = haversineKm(t.startLat, t.startLon, t.endLat, t.endLon);
                crowStr = formatDistance(km, imperial);
            }
            std::cout << "  " << std::left
                      << std::setw(5)  << t.id
                      << std::setw(13) << t.date
                      << std::setw(10) << t.startTime
                      << std::setw(6)  << t.segments.size()
                      << std::setw(15) << t.duration
                      << crowStr;
            if (!t.note.empty())
                std::cout << "  [" << t.note.substr(0, 30)
                          << (t.note.size() > 30 ? "..." : "") << "]";
            std::cout << "\n";
        }
    }
    std::cout << "\n";
}

// ---------------------------------------------------------------------------
// showManifestList — -t: compact one-line-per-manifest summary
// ---------------------------------------------------------------------------
void FindTrips::showManifestList(ConfigManager& config) {
    auto index = config.loadManifestIndex();
    if (index.empty()) { std::cout << "No manifests found.\n"; return; }
    std::cout << std::left
              << std::setw(5)  << "ID"
              << std::setw(12) << "Trips"
              << std::setw(22) << "Last trip"
              << "Path\n";
    std::cout << std::string(70, '-') << "\n";
    for (const auto& e : index) {
        std::string tripCol = std::to_string(e.tripCount) + " trip"
                              + (e.tripCount != 1 ? "s" : "");
        std::cout << std::left
                  << std::setw(5)  << e.id
                  << std::setw(12) << tripCol
                  << std::setw(22) << e.lastTrip
                  << e.path;
        if (!e.note.empty())
            std::cout << "  [" << e.note.substr(0, 30)
                      << (e.note.size() > 30 ? "..." : "") << "]";
        std::cout << "\n";
    }
}

// ---------------------------------------------------------------------------
// showFullDump — --fulldump: manifests → trips → segment files
// ---------------------------------------------------------------------------
void FindTrips::showFullDump(ConfigManager& config) {
    auto index = config.loadManifestIndex();
    if (index.empty()) {
        std::cout << "No manifests found.\n";
        return;
    }
    for (const auto& entry : index) {
        std::cout << "\nManifest " << entry.id
                  << "  " << entry.path
                  << "  (" << entry.tripCount << " trip"
                  << (entry.tripCount != 1 ? "s" : "") << ")"
                  << (entry.note.empty() ? "" : "  [" + entry.note.substr(0,30)
                      + (entry.note.size() > 30 ? "..." : "") + "]")
                  << "\n";
        std::cout << std::string(70, '=') << "\n";

        auto trips = config.loadTripCache(entry.path);
        for (const auto& t : trips) {
            std::cout << "  Trip " << t.id
                      << "  " << t.date << " " << t.startTime
                      << "  " << t.duration
                      << "  (" << t.segments.size() << " segments)\n";
            if (!t.note.empty())
                std::cout << "  Note: " << t.note << "\n";
            std::cout << "  " << std::string(60, '-') << "\n";

            for (const auto& seg : t.segments) {
                // Front filename only (not full path)
                std::string frontName = pathBasename(seg.front);
                if (frontName == "-" || frontName.empty())
                    frontName = "(no front)";

                // Camera presence flags
                std::string flags = "[";
                flags += (seg.front != "-" && !seg.front.empty()) ? 'F' : '-';
                flags += (seg.rear  != "-" && !seg.rear.empty())  ? 'B' : '-';
                flags += (seg.left  != "-" && !seg.left.empty())  ? 'L' : '-';
                flags += (seg.right != "-" && !seg.right.empty()) ? 'R' : '-';
                flags += ']';

                std::cout << "    " << seg.timestamp
                          << "  " << std::left << std::setw(32) << frontName
                          << "  " << flags << "\n";
            }
            std::cout << "\n";
        }
        std::cout << std::string(70, '=') << "\n";
    }
}
// ---------------------------------------------------------------------------
// jsonDump — --jsondump: single JSON document of all manifests/trips/segments.
// Segment paths are relative to the manifest's source path for portability.
// GPS track included only if non-empty.
// ---------------------------------------------------------------------------
void FindTrips::jsonDump(ConfigManager& config) {
    using json = nlohmann::json;

    auto index = config.loadManifestIndex();

    json root;
    root["pathmux_version"] = APP_VERSION;
    root["generated"] = []() -> std::string {
        auto t = std::time(nullptr);
        std::tm tmBuf{};
        localtime_r(&t, &tmBuf);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmBuf);
        return std::string(buf);
    }();
    root["manifests"] = json::array();

    for (const auto& entry : index) {
        json jm;
        jm["id"]          = entry.id;
        jm["path"]        = entry.path;
        jm["last_scan"]   = entry.lastScan;
        jm["trip_count"]  = entry.tripCount;
        jm["note"]        = entry.note;

        jm["trips"] = json::array();
        auto trips = config.loadTripCache(entry.path);

        for (const auto& t : trips) {
            json jt;
            jt["id"]            = t.id;
            jt["date"]          = t.date;
            jt["start_time"]    = t.startTime;
            jt["start_epoch"]   = t.startEpoch;
            jt["duration"]      = t.duration;
            jt["segment_count"] = (int)t.segments.size();
            jt["note"]          = t.note;

            if (t.firstLockLat != 0.0 || t.firstLockLon != 0.0) {
                jt["first_lock_lat"]       = t.firstLockLat;
                jt["first_lock_lon"]       = t.firstLockLon;
                jt["first_lock_timestamp"] = t.firstLockTimestamp;
                jt["first_lock_record"]    = t.firstLockRecord;
            }

            if (t.startLat != 0.0 || t.startLon != 0.0) {
                jt["start_lat"] = t.startLat;
                jt["start_lon"] = t.startLon;
            }
            if (t.endLat != 0.0 || t.endLon != 0.0) {
                jt["end_lat"] = t.endLat;
                jt["end_lon"] = t.endLon;
            }

            // Segments — paths relative to manifest source path
            jt["segments"] = json::array();
            for (const auto& seg : t.segments) {
                json js;
                js["timestamp"] = seg.timestamp;

                // Strip source path prefix to make paths relative
                auto rel = [&](const std::string& f) -> std::string {
                    if (f == "-" || f.empty()) return "";
                    if (f.find(entry.path) == 0) {
                        std::string r = f.substr(entry.path.size());
                        if (!r.empty() && r[0] == '/') r.erase(0, 1);
                        return r;
                    }
                    return f;
                };

                std::string fr = rel(seg.front);
                std::string br = rel(seg.rear);
                std::string lr = rel(seg.left);
                std::string rr = rel(seg.right);

                if (!fr.empty()) js["front"] = fr;
                if (!br.empty()) js["rear"]  = br;
                if (!lr.empty()) js["left"]  = lr;
                if (!rr.empty()) js["right"] = rr;

                jt["segments"].push_back(js);
            }

            // GPS track — omit if empty
            if (!t.gpsTrack.empty()) {
                json jgps = json::array();
                for (const auto& pt : t.gpsTrack) {
                    json jp;
                    jp["timestamp"] = pt.timestamp;
                    jp["lat"]       = pt.lat;
                    jp["lon"]       = pt.lon;
                    if (pt.speed   >= 0.0) jp["speed"]   = pt.speed;
                    if (pt.heading >= 0.0) jp["heading"] = pt.heading;
                    jgps.push_back(jp);
                }
                jt["gps_track"] = jgps;
            }

            jm["trips"].push_back(jt);
        }

        root["manifests"].push_back(jm);
    }

    std::cout << root.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
// formatDump — --format=[json|csv|xml] [--fields=f1,f2,...]
//
// Outputs all trips in the requested format, one record per trip.
// json: delegates to jsonDump (--fields ignored).
// csv/xml: flat per-trip output with selectable fields.
//
// Available field names:
//   manifest_id, trip_id, address, date, start_time, start_epoch,
//   duration, duration_seconds, segment_count, note,
//   start_lat, start_lon, end_lat, end_lon,
//   distance_km, distance_mi, gps_lock_seconds, gps_track_status
// ---------------------------------------------------------------------------
void FindTrips::formatDump(ConfigManager& config,
                            const std::string& format,
                            const std::vector<std::string>& fields) {
    if (format == "json") { jsonDump(config); return; }

    auto index = config.loadManifestIndex();

    if (format == "csv") {
        writeTripsCSV(index, config, fields);
    } else if (format == "xml") {
        writeTripsXML(index, config, fields, APP_VERSION);
    } else {
        std::cerr << "Unknown format '" << format
                  << "'. Valid: json, csv, xml\n";
    }
}

// SN: 00087