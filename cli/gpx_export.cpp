// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "gpx_export.hpp"
#include "config_manager.hpp"
#include "gps_export.hpp"
#include "ui_helpers.hpp"
#include "platform.hpp"
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
using namespace CamClops;

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
    if (fs::path(pathOrStem).is_absolute() && fs::exists(pathOrStem))
        return pathOrStem;

    // Absolute filesystem source path → derive cache filename
    if (fs::path(pathOrStem).is_absolute()) {
        if (!config.isCached(pathOrStem)) return "";
        std::string san = pathOrStem.substr(1);
        for (char& c : san) if (c == '/' || c == '\\' || c == ':') c = '_';
        return (fs::path(CamClops::Platform::getConfigDir()) / (san + ".json")).string();
    }

    // No slashes → treat as cache stem
    std::string candidate = (fs::path(CamClops::Platform::getConfigDir()) / (pathOrStem + ".json")).string();
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
        return "camclops_unknown";

    // Support both new ("cameras" object) and old (flat named fields) formats.
    std::string front;
    const auto& seg0 = jTrip["segments"][0];
    if (seg0.contains("cameras") && seg0["cameras"].is_object())
        front = seg0["cameras"].value("front", "");
    else
        front = seg0.value("front", "");
    if (front.empty() || front == "-")
        return "camclops_unknown";

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
            if (!CamClops::extractGps(root, idx, manifestFile, opts.exiftoolPath, m_verbose)) {
                std::cout << "\nGPS extraction failed for trip " << tId << ".\n"
                          << "Verify exiftool is installed and the options match your camera format.\n"
                          << "If GPS is unsupported for your camera, contact https://exiftool.org\n";
                continue;
            }
            std::cout << "GPS extracted.\n";
        } else if (needsGps && mode == ExportMode::GpsOnly) {
            int segCount = root["trips"][idx].contains("segments")
                           ? (int)root["trips"][idx]["segments"].size() : 0;
            std::cout << "Extracting GPS for trip " << tId
                      << " (" << segCount << " segments)";
            if (!CamClops::extractGps(root, idx, manifestFile, opts.exiftoolPath, m_verbose)) {
                std::cout << "\nGPS extraction failed for trip " << tId << ".\n"
                          << "Verify exiftool is installed and the options match your camera format.\n"
                          << "If GPS is unsupported for your camera, contact https://exiftool.org\n";
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
                ? CamClops::writeKml(root, idx, outPath)
                : CamClops::writeGpx(root, idx, outPath);

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
// SN: 00104

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
                        std::string tf = sourcePath + "/.clops_write_test";
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
                    if (!CamClops::extractGps(root, i, manifestFile,
                                    actionOpts.exiftoolPath,
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

                std::string result = (ch == 'K') ? CamClops::writeKml(root, i, outPath)
                                   : (ch == 'J') ? CamClops::writeGeoJson(root, i, outPath)
                                   :               CamClops::writeGpx(root, i, outPath);
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
