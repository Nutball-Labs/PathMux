// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef GPX_EXPORT_HPP
#define GPX_EXPORT_HPP

#include <string>
#include "json.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// ExportMode — what action run() performs
// ---------------------------------------------------------------------------
enum class ExportMode {
    GpsOnly,    // -g  extract GPS into manifest, no file output
    Gpx,        // -G  extract GPS if needed, write GPX
    Kml         // -K  extract GPS if needed, write KML
};

// ---------------------------------------------------------------------------
// ExportOptions — collects all CLI output path overrides
// ---------------------------------------------------------------------------
struct ExportOptions {
    std::string manifestPath;   // "" = use lastPath / prompt
    std::string gpxPath;        // --gpxpath <dir>   output directory for GPX
    std::string gpxFile;        // --gpxfile <name>  explicit GPX filename
    std::string kmlPath;        // --kmlpath <dir>   output directory for KML
    std::string kmlFile;        // --kmlfile <name>  explicit KML filename
    std::string exiftoolPath;    // from config -- path to exiftool binary
    std::string exiftoolOptions; // from config -- e.g. "-ee3"
    std::string defaultExportDir; // from config -- default output directory
    bool        force = false;  // --force           overwrite without prompting
};

// ---------------------------------------------------------------------------
// GpxExport — GPS extraction, GPX and KML export
// ---------------------------------------------------------------------------
class GpxExport {
public:
    // Entry point: interactive trip selection then action based on mode.
    void run(ExportMode mode, const ExportOptions& opts);
    void runInteractive(const ExportOptions& opts);

private:
    bool m_verbose = false;   // exiftool stderr — toggled via [E] in menus

    // --------------- output path helpers ------------------------------------

    // Derive the default filename stem from segments[0].front:
    //   "/z/srcdash/ex6/Front/20260216_095422F.ts"  →  "20260216_095422"
    static std::string buildStem(const json& jTrip);

    // Resolve final output path for a single trip given stem, extension,
    // explicit dir override, explicit file override, and force flag.
    // Applies .1 .2 collision avoidance when an explicit file is given
    // and force is false.
    static std::string resolveOutputPath(const std::string& stem,
                                         const std::string& ext,
                                         const std::string& dirOverride,
                                         const std::string& fileOverride,
                                         bool force);

    // GPS extraction and file writers are free functions in lib/gps_export.hpp.
    // GpxExport calls them directly; no wrapper methods needed here.

    // --------------- shared helpers ----------------------------------------

    // Load and parse a manifest JSON file.
    // Returns false and prints an error if the file cannot be opened/parsed.
    static bool loadManifest(const std::string& manifestFile, json& root);

    // Resolve a manifest source-path or cache stem → full path to .json file.
    // Returns "" if nothing can be located.
    static std::string resolveManifestFile(const std::string& pathOrStem);

    // Display the trip selection table and return chosen trip index (0-based),
    // -1 for "all", or -2 for cancel/quit.
    static int selectTrip(const json& jTrips, ExportMode /*mode*/);

    // Count trips in jTrips that have no GPS data yet (gpsTrackStatus != "complete").
    static int countUnscanned(const json& jTrips);
};

#endif
// SN: 00089
