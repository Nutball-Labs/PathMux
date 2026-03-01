// pm_gpsexport — non-interactive GPS track exporter
//
// Usage:
//   pm_gpsexport MID:TID --gpx  [path]
//   pm_gpsexport MID:TID --kml  [path]
//   pm_gpsexport MID:TID --geojson [path]
//   pm_gpsexport MID:TID --gpx out.gpx --kml out.kml   (multiple formats)
//   pm_gpsexport MID:TID --gpx /dir/   (auto-name: YYYYMMDD_HHMMSS.gpx)
//   pm_gpsexport MID:TID --force       (overwrite existing files)
//   pm_gpsexport MID:TID --verbose     (show exiftool stderr during extraction)
//
// MID:TID addresses a trip by manifest ID and trip ID (e.g. F0:4P).
// At least one format flag (--gpx, --kml, --geojson) is required.
// If GPS has not yet been extracted, pm_gpsexport runs ExifTool extraction
// first and stores the track in the manifest before writing output files.
// Progress dots go to stdout; exported file paths go to stdout on success.

#include "pathmux.hpp"
#include "gps_export.hpp"
#include "json.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace Pathmux;

// ---------------------------------------------------------------------------
// printUsage
// ---------------------------------------------------------------------------
static void printUsage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << " MID:TID [--gpx PATH] [--kml PATH] [--geojson PATH]\n"
        << "               [--force] [--verbose]\n\n"
        << "  MID:TID   Trip address, e.g. F0:4P\n"
        << "  --gpx     Write GPX 1.1 track to PATH\n"
        << "  --kml     Write KML 2.2 track to PATH\n"
        << "  --geojson Write GeoJSON FeatureCollection to PATH\n"
        << "  --force   Overwrite existing output files without prompting\n"
        << "  --verbose Show exiftool stderr during GPS extraction\n\n"
        << "  PATH may be a directory (auto-named) or a full file path.\n"
        << "  Omit PATH to write to the current directory with an auto-generated name.\n\n"
        << "  Multiple format flags may be combined in a single invocation.\n"
        << "  GPS extraction runs at most once per trip regardless of output count.\n";
}

// ---------------------------------------------------------------------------
// normalizeId — uppercase, collapse visually ambiguous chars
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
// Resolve MID:TID → manifestFile + tripIdx in the loaded JSON.
// Returns false and prints an error on failure.
// ---------------------------------------------------------------------------
static bool resolveMidTid(const std::string& midtid,
                           std::string& outManifestFile,
                           int&         outTripIdx)
{
    auto colon = midtid.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == midtid.size()) {
        std::cerr << "Error: '" << midtid << "' is not a valid MID:TID address.\n";
        return false;
    }

    std::string mid = normalizeId(midtid.substr(0, colon));
    std::string tid = normalizeId(midtid.substr(colon + 1));

    ConfigManager config;
    auto index = config.loadManifestIndex();

    for (const auto& entry : index) {
        if (normalizeId(entry.id) != mid) continue;

        outManifestFile = entry.manifestFile;
        if (outManifestFile.empty() || !fs::exists(outManifestFile)) {
            std::cerr << "Error: manifest file not found for '" << mid << "'.\n";
            return false;
        }

        // Load raw JSON to find trip index
        std::ifstream ifs(outManifestFile);
        if (!ifs.is_open()) {
            std::cerr << "Error: Cannot open manifest: " << outManifestFile << "\n";
            return false;
        }
        json root;
        try { ifs >> root; }
        catch (const std::exception& e) {
            std::cerr << "Error: Cannot parse manifest: " << e.what() << "\n";
            return false;
        }

        if (!root.contains("trips") || !root["trips"].is_array()) {
            std::cerr << "Error: manifest '" << mid << "' has no trips.\n";
            return false;
        }

        const auto& trips = root["trips"];
        for (int i = 0; i < (int)trips.size(); ++i) {
            if (normalizeId(trips[i].value("id", "")) == tid) {
                outTripIdx = i;
                return true;
            }
        }

        std::cerr << "Error: trip '" << tid << "' not found in manifest '" << mid << "'.\n";
        return false;
    }

    std::cerr << "Error: manifest '" << mid << "' not found.\n";
    return false;
}

// ---------------------------------------------------------------------------
// Derive output filename stem from first Front segment of the trip:
//   "/path/Front/20260226_084009F.ts"  →  "20260226_084009"
// ---------------------------------------------------------------------------
static std::string buildStem(const json& jTrip)
{
    if (!jTrip.contains("segments") || !jTrip["segments"].is_array()
            || jTrip["segments"].empty())
        return "pm_trip";

    std::string front = jTrip["segments"][0].value("front", "");
    if (front.empty()) return "pm_trip";

    auto slash = front.rfind('/');
    std::string fname = (slash != std::string::npos) ? front.substr(slash + 1) : front;
    auto dot = fname.rfind('.');
    if (dot != std::string::npos) fname = fname.substr(0, dot);
    // Strip trailing channel letter (F, R, L, N)
    if (!fname.empty() && std::isalpha(static_cast<unsigned char>(fname.back())))
        fname.pop_back();
    return fname.empty() ? "pm_trip" : fname;
}

// ---------------------------------------------------------------------------
// Resolve output path for one format.
// If pathSpec ends with '/' or is an existing directory → auto-name in it.
// If pathSpec is empty → auto-name in current directory.
// Otherwise use pathSpec as the full output path.
// Returns "" if target exists and force is false.
// ---------------------------------------------------------------------------
static std::string resolveOut(const std::string& pathSpec,
                               const std::string& stem,
                               const std::string& ext,
                               bool force)
{
    std::string resolved;

    if (pathSpec.empty() || pathSpec.back() == '/' || fs::is_directory(pathSpec)) {
        std::string dir = pathSpec.empty() ? "." : pathSpec;
        // Trim trailing slash for path construction
        while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
        resolved = dir + "/" + stem + "." + ext;
    } else {
        resolved = pathSpec;
        // Append extension if not already present
        if (resolved.size() < ext.size() + 1 ||
            resolved.substr(resolved.size() - ext.size() - 1) != "." + ext)
            resolved += "." + ext;
    }

    if (fs::exists(resolved) && !force) {
        std::cerr << "Error: output file already exists (use --force to overwrite): "
                  << resolved << "\n";
        return "";
    }

    return resolved;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    if (argc < 2) { printUsage(argv[0]); return 1; }

    std::string midTid;
    std::string gpxPath, kmlPath, geojsonPath;
    bool doGpx = false, doKml = false, doGeoJson = false;
    bool force = false, verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")     { printUsage(argv[0]); return 0; }
        else if (arg == "--force")              { force   = true; }
        else if (arg == "--verbose")            { verbose = true; }
        else if (arg == "--gpx") {
            doGpx = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') gpxPath = argv[++i];
        } else if (arg == "--kml") {
            doKml = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') kmlPath = argv[++i];
        } else if (arg == "--geojson") {
            doGeoJson = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') geojsonPath = argv[++i];
        } else if (arg.find(':') != std::string::npos && arg[0] != '-') {
            midTid = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (midTid.empty()) {
        std::cerr << "Error: MID:TID argument required.\n";
        printUsage(argv[0]);
        return 1;
    }
    if (!doGpx && !doKml && !doGeoJson) {
        std::cerr << "Error: at least one format flag required (--gpx, --kml, --geojson).\n";
        printUsage(argv[0]);
        return 1;
    }

    // ---- Resolve MID:TID ----
    std::string manifestFile;
    int tripIdx = -1;
    if (!resolveMidTid(midTid, manifestFile, tripIdx)) return 1;

    // ---- Load manifest JSON ----
    std::ifstream ifs(manifestFile);
    if (!ifs.is_open()) {
        std::cerr << "Error: Cannot open manifest: " << manifestFile << "\n";
        return 1;
    }
    json root;
    try { ifs >> root; }
    catch (const std::exception& e) {
        std::cerr << "Error: Cannot parse manifest: " << e.what() << "\n";
        return 1;
    }

    // ---- Derive stem for auto-naming ----
    const json& jTrip = root["trips"][tripIdx];
    std::string stem  = buildStem(jTrip);

    // ---- Resolve all output paths before doing any work ----
    std::string resolvedGpx, resolvedKml, resolvedGeoJson;
    if (doGpx) {
        resolvedGpx = resolveOut(gpxPath, stem, "gpx", force);
        if (resolvedGpx.empty()) return 1;
    }
    if (doKml) {
        resolvedKml = resolveOut(kmlPath, stem, "kml", force);
        if (resolvedKml.empty()) return 1;
    }
    if (doGeoJson) {
        resolvedGeoJson = resolveOut(geojsonPath, stem, "geojson", force);
        if (resolvedGeoJson.empty()) return 1;
    }

    // ---- Extract GPS if needed ----
    bool needsGps = (jTrip.value("gpsTrackStatus", "none") != "complete");
    if (needsGps) {
        ConfigManager config;
        std::string etPath = config.getExiftoolPath();
        std::string etOpts = config.getExiftoolOptions();

        int segCount = jTrip.contains("segments") && jTrip["segments"].is_array()
                       ? (int)jTrip["segments"].size() : 0;
        std::cerr << "Extracting GPS for " << midTid
                  << " (" << segCount << " segment" << (segCount != 1 ? "s" : "") << ")";

        if (!Pathmux::extractGps(root, tripIdx, manifestFile, etPath, etOpts, verbose)) {
            std::cerr << "GPS extraction failed.\n";
            return 1;
        }
        std::cerr << "GPS extracted.\n";
    }

    // ---- Write output files ----
    int exitCode = 0;

    if (doGpx) {
        std::string out = Pathmux::writeGpx(root, tripIdx, resolvedGpx);
        if (out.empty()) { exitCode = 1; }
        else { std::cout << out << "\n"; }
    }
    if (doKml) {
        std::string out = Pathmux::writeKml(root, tripIdx, resolvedKml);
        if (out.empty()) { exitCode = 1; }
        else { std::cout << out << "\n"; }
    }
    if (doGeoJson) {
        std::string out = Pathmux::writeGeoJson(root, tripIdx, resolvedGeoJson);
        if (out.empty()) { exitCode = 1; }
        else { std::cout << out << "\n"; }
    }

    return exitCode;
}
// SN: 00080
