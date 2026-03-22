// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef GPS_EXPORT_HPP
#define GPS_EXPORT_HPP

#include <string>
#include "json.hpp"

using json = nlohmann::json;

namespace Pathmux {

// Run ExifTool on all Front camera segments of the trip at tripIdx.
// Populates gpsTrack, gpsTrackStatus, startLat/Lon, endLat/Lon in
// root["trips"][tripIdx].  Rewrites the manifest to disk and updates its
// MD5 in the index so --validate does not flag it as externally modified.
// Returns false if ExifTool is unavailable or produced no GPS records.
bool extractGps(json& root,
                int tripIdx,
                const std::string& manifestFile,
                const std::string& exiftoolPath,
                const std::string& exiftoolOptions,
                bool verbose = false);

// Write GPX 1.1 track file.  outPath must be fully resolved before calling.
// Returns outPath on success, "" on failure.
std::string writeGpx(const json& root, int tripIdx, const std::string& outPath);

// Write KML 2.2 track file.  outPath must be fully resolved before calling.
// Returns outPath on success, "" on failure.
std::string writeKml(const json& root, int tripIdx, const std::string& outPath);

// Write GeoJSON FeatureCollection (RFC 7946).  outPath must be fully resolved.
// Coordinates are [longitude, latitude] per spec.
// Returns outPath on success, "" on failure.
std::string writeGeoJson(const json& root, int tripIdx, const std::string& outPath);

} // namespace Pathmux

#endif
// SN: 00089
