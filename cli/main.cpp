#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>
#include "find_trips.hpp"
#include "trip_detection.hpp"
#include "config_manager.hpp"
#include "gpx_export.hpp"
#include "prefs.hpp"
#include "host_prefs.hpp"
#include "kml_prefs.hpp"
#include "locations.hpp"
#include "video_build.hpp"
#include "version.hpp"

namespace fs = std::filesystem;
using namespace Pathmux;

void printUsage() {
    std::cout << APP_NAME << " v" << APP_VERSION << "\n"
              << "Usage: pathmux [options]\n\n"
              << "Scan & display:\n"
              << "  -s, --scan <path>       Scan a directory for trips\n"
              << "  -T, --dump              Show all manifests and trips\n"
              << "      --fulldump          Show all manifests, trips, and segment files\n"
              << "      --jsondump          Dump everything as JSON (scriptable)\n"
              << "  -T  --format=<fmt>      Structured output modifier: json, csv, xml\n"
              << "      --fields=<f1,f2>    Fields for csv/xml (use with -T --format)\n"
              << "                          Fields: manifest_id trip_id address date\n"
              << "                            start_time start_epoch duration\n"
              << "                            duration_seconds segment_count note\n"
              << "                            start_lat start_lon end_lat end_lon\n"
              << "                            distance_km distance_mi\n"
              << "                            gps_lock_seconds gps_track_status\n"
              << "  -I, --interactive       Interactive browser\n\n"
              << "GPS & export:\n"
              << "  -G, --gps               Interactive GPS extraction and GPX/KML export\n\n"
              << "Settings:\n"
              << "  -P, --prefs             Interactive preferences editor\n"
              << "      --hostprefs         Host-specific prefs (encoder, paths, output dir)\n"
              << "      --encoderprefs      Interactive encoder/hardware preferences\n"
              << "      --kmlprefs          Interactive KML visual preferences editor\n"
              << "      --locations         Manage known locations (Home, Work, etc.)\n"
              << "      --set <key=val>     Set a preference non-interactively\n"
              << "                          Keys: gap=<seconds>  Example: --set gap=1800\n"
              << "      --show-config       Show raw configuration key/value pairs\n\n"
              << "Manifest management:\n"
              << "      --clear-cache           Interactive: pick one manifest to delete\n"
              << "      --clear-cache ALL       List all, confirm, then wipe all manifests\n"
              << "      --clear-cache ALL --force  Wipe all without prompting (scriptable)\n"
              << "      --show-stale            Show archived stale manifest entries\n"
              << "      --clear-stale           Clear the stale archive (with confirmation)\n"
              << "      --clear-stale --force   Clear the stale archive without prompting\n"
              << "      --validate              Report manifest health; exit 0=ok 1=problems\n\n"
              << "General:\n"
              << "  -v, --version           Show version information\n"
              << "  -h, --help              Show this help message\n\n";
}

int main(int argc, char* argv[]) {
    ConfigManager    config;
    TripDetection    detector;
    FindTrips        finder;
    GpxExport        exporter;
    PrefsEditor        prefsEditor;
    EncoderPrefsEditor encoderPrefsEditor;
    HostPrefsEditor    hostPrefsEditor;
    KmlPrefsEditor     kmlPrefsEditor;
    LocationsEditor  locationsEditor;

    if (argc < 2) { printUsage(); return 1; }

    // --- Short-circuit for flags that need no manifest state ---
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")    { printUsage(); return 0; }
        if (arg == "-v" || arg == "--version") {
            std::cout << APP_NAME << " v" << APP_VERSION << "\n"; return 0;
        }
    }

    // --- Config state warning — non-blocking, fires on every invocation ---
    if (config.configState() == ConfigState::FIRST_RUN) {
        std::cerr << "\n[!] PathMux: No configuration found. Running with defaults.\n"
                  << "   Use --prefs to configure before writing any files.\n\n";
    } else if (config.configState() == ConfigState::INCOMPLETE) {
        std::cerr << "\n[!] PathMux: Configuration incomplete (export directory not set).\n"
                  << "   Use --prefs to complete setup before writing any files.\n\n";
    }

    // --- Non-interactive read-only flags: bypass validation prompt ---
    {
        bool isReadOnly = false;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-t" || a == "-T" || a == "--dump" || a == "--validate"
                || a == "--jsondump")
                isReadOnly = true;
        }
        if (isReadOnly) {
            // Still load config but skip interactive validation
        } else {
            if (!config.validateManifestIndex()) return 0;
        }
    }

    std::string   scanPath;
    bool doScan          = false;
    bool doFullTree      = false;
    bool doManifestList  = false;
    bool doValidate      = false;
    bool doFullDump      = false;
    bool doJsonDump      = false;
    bool doInteractive   = false;
    bool doExport        = false;
    bool doPrefs         = false;
    bool doEncoderPrefs  = false;
    bool doKmlPrefs      = false;
    bool doLocations     = false;
    bool doHostPrefs     = false;
    bool doSet           = false;
    std::string formatArg;
    std::vector<std::string> fieldsArg;

    ExportOptions exportOpts;
    std::string   setKV;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--clear-cache") {
            std::string ccArg;
            bool ccForce = false;
            if (i + 1 < argc && std::string(argv[i + 1]) != "--force"
                              && argv[i + 1][0] != '-') {
                ccArg = argv[++i];
            }
            // --force may appear anywhere after --clear-cache
            for (int j = i + 1; j < argc; ++j) {
                if (std::string(argv[j]) == "--force") { ccForce = true; break; }
            }
            config.clearCache(ccArg, ccForce);
            return 0;
        }
        if (arg == "--show-stale") { config.showStale();  return 0; }
        if (arg == "--clear-stale") {
            bool csForce = false;
            for (int j = i + 1; j < argc; ++j) {
                if (std::string(argv[j]) == "--force") { csForce = true; break; }
            }
            config.clearStale(csForce);
            return 0;
        }
        if (arg == "--show-config") { config.showSettings(); return 0; }

        if (arg == "-I" || arg == "--interactive") { doInteractive = true; continue; }
        if (arg == "-T" || arg == "--dump")        { doFullTree    = true; continue; }
        if (arg == "-t")                            { doManifestList = true; continue; }
        if (arg == "--validate")                    { doValidate    = true; continue; }
        if (arg == "--fulldump")                   { doFullDump    = true; continue; }
        if (arg == "--jsondump")                   { doJsonDump    = true; continue; }

        if (arg == "-s" || arg == "--scan") {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                scanPath = argv[++i];
                doScan   = true;
            } else {
                std::cerr << "Error: -s/--scan requires a path argument.\n";
                return 1;
            }
            continue;
        }

        // ---- Export flags ----
        if (arg == "-G" || arg == "--gps") { doExport = true; continue; }


        // ---- Settings flags ----
        if (arg == "-P" || arg == "--prefs")  { doPrefs        = true; continue; }
        if (arg == "--hostprefs")             { doHostPrefs    = true; continue; }
        if (arg == "--encoderprefs")          { doEncoderPrefs = true; continue; }
        if (arg == "--kmlprefs")              { doKmlPrefs     = true; continue; }
        if (arg == "--locations")             { doLocations    = true; continue; }

        if (arg.find("--format=") == 0) {
            formatArg = arg.substr(9);
            continue;
        }
        if (arg.find("--fields=") == 0) {
            std::string fstr = arg.substr(9);
            std::istringstream ss(fstr);
            std::string tok;
            while (std::getline(ss, tok, ','))
                if (!tok.empty()) fieldsArg.push_back(tok);
            continue;
        }

        if (arg == "--set") {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                setKV = argv[++i]; doSet = true;
            } else {
                std::cerr << "Error: --set requires key=value argument.\n"; return 1;
            }
            continue;
        }

        std::cerr << "Warning: unknown option '" << arg << "'\n";
    }

    // ---- Settings modes ----
    if (doPrefs)       { prefsEditor.run(config);        return 0; }
    if (doHostPrefs)   { hostPrefsEditor.run(config);   return 0; }
    if (doEncoderPrefs){ encoderPrefsEditor.run(config); return 0; }
    if (doKmlPrefs)    { kmlPrefsEditor.run(config);     return 0; }
    if (doLocations)   { locationsEditor.run(config);    return 0; }
    if (doSet) {
        auto eq = setKV.find('=');
        if (eq == std::string::npos) {
            std::cerr << "Error: --set expects key=value (e.g. gap=900)\n"; return 1;
        }
        std::string key = setKV.substr(0, eq);
        std::string val = setKV.substr(eq + 1);
        if (key == "gap") {
            try {
                config.setGapThreshold(std::stoi(val));
                std::cout << "Gap threshold set to " << val << "s.\n";
            } catch (...) {
                std::cerr << "Error: '" << val << "' is not an integer.\n"; return 1;
            }
        } else {
            std::cerr << "Error: unknown key '" << key << "'. Valid keys: gap\n"; return 1;
        }
        return 0;
    }

    // ---- Export mode ----
    if (doExport) {
        if (doScan) exportOpts.manifestPath = scanPath;
        exportOpts.exiftoolPath     = config.getExiftoolPath();
        exportOpts.exiftoolOptions  = config.getExiftoolOptions();
        exportOpts.defaultExportDir = config.getDefaultExportDir();
        exporter.runInteractive(exportOpts);
        return 0;
    }

    // ---- Scan (non-interactive) ----
    if (doScan) {
        if (!std::filesystem::exists(scanPath)) {
            std::cerr << "Error: path does not exist: " << scanPath << "\n"; return 1;
        }

        // Warn if a manifest already exists for this path
        std::string existingMid = config.getManifestIdForPath(scanPath);
        if (!existingMid.empty()) {
            std::cout << "\n  Warning: existing manifest [" << existingMid << "] found for:\n"
                      << "    " << scanPath << "\n\n"
                      << "  [1]  Continue and overwrite\n"
                      << "  [2]  Delete existing manifest and rescan (fresh start)\n"
                      << "  [3]  Quit\n\n"
                      << "  Choice: ";
            std::string choice;
            std::getline(std::cin, choice);
            if (choice != "1" && choice != "2") {
                std::cout << "  Scan cancelled.\n";
                return 0;
            }
            if (choice == "2") {
                config.clearCache(scanPath, false);
                std::cout << "  Existing manifest deleted.\n";
            }
        }

        std::cout << "Scanning: " << scanPath
                  << "  (gap=" << config.getGapThreshold() << "s)\n";
        auto trips = detector.detectTrips(scanPath,
                                          config.getGapThreshold(),
                                          config.getFuzzyWindow(),
                                          config.getFfprobePath(),
                                          config.getExiftoolPath(),
                                              config.getExiftoolOptions());
        if (!trips.empty()) {
            config.saveTripCache(scanPath, trips);
            auto saved = config.loadTripCache(scanPath);
            std::string mid = config.getManifestIdForPath(scanPath);
            finder.showTripSummary(saved, scanPath, config.getUseImperial(), mid);
        } else {
            std::cout << "No trips found.\n";
        }
        return 0;
    }

    // ---- Validate ----
    if (doValidate) {
        std::cout << APP_NAME << " v" << APP_VERSION << " — Manifest Validation\n\n";
        bool ok = config.validateManifestIndexReport();
        std::cout << "\n" << (ok ? "  All manifests OK.\n" : "  One or more manifests have problems.\n");
        return ok ? 0 : 1;
    }

    // ---- Full tree dump ----
    if (doFullTree) {
        if (!formatArg.empty())
            finder.formatDump(config, formatArg, fieldsArg);
        else
            finder.showAllTrees(config);
        return 0;
    }
    if (doManifestList) {
        finder.showManifestList(config);
        return 0;
    }

    // ---- Full dump with files ----
    if (doFullDump) {
        finder.showFullDump(config);
        return 0;
    }

    // ---- JSON dump ----
    if (doJsonDump) {
        finder.jsonDump(config);
        return 0;
    }

    // ---- Interactive browser ----
    if (doInteractive) {
        finder.runInteractive(config);
        return 0;
    }

    // No mode selected
    printUsage();
    return 0;
}
// SN: 00087