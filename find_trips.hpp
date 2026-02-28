#ifndef FIND_TRIPS_HPP
#define FIND_TRIPS_HPP

#include <string>
#include <vector>
#include "trip_detection.hpp"
#include "config_manager.hpp"
#include "json.hpp"

class FindTrips {
public:
    // -I unified interactive browser.  Starts at manifest list home screen.
    // Returns when user quits.
    void runInteractive(ConfigManager& config);

    // -T / --dump: all manifests and trips, no file listing.
    void showAllTrees(ConfigManager& config);

    // -t: compact one-line-per-manifest list, sorted by lastTrip descending.
    void showManifestList(ConfigManager& config);

    // --fulldump: all manifests, trips, and segment file paths.
    void showFullDump(ConfigManager& config);

    // --jsondump: full manifest+trip+segment data as a single JSON document.
    // Segment paths are relative to each manifest's source path.
    void jsonDump(ConfigManager& config);

    // -s scan result summary (non-interactive).
    void showTripSummary(const std::vector<Trip>& trips,
                         const std::string& currentPath,
                         bool imperial,
                         const std::string& manifestId = "");

    // Detail view for a single trip (used within interactive browser).
    void showTripDetails(const Trip& trip, bool imperial);

private:
    // Draw the manifest list home screen.
    void drawManifestList(const std::vector<ManifestEntry>& index);

    // Manifest-level menu — list trips for one manifest, pick one to build.
    // Returns false if user chose [Q] quit entirely.
    bool runManifestMenu(ConfigManager& config, const ManifestEntry& entry);

    // Normalize user-typed manifest/trip ID input.
    static std::string normalizeId(const std::string& input);
};

#endif
// SN: 00069