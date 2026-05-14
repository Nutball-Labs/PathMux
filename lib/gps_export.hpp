// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef GPS_EXPORT_HPP
#define GPS_EXPORT_HPP

#include <functional>
#include <map>
#include <string>
#include "camera_profile.hpp"
#include "json.hpp"

using json = nlohmann::json;

namespace CamClops {

// Run ExifTool on all Front camera segments of the trip at tripIdx.
// Populates gpsTrack, gpsTrackStatus, startLat/Lon, endLat/Lon in
// root["trips"][tripIdx].  Rewrites the manifest to disk and updates its
// MD5 in the index so --validate does not flag it as externally modified.
// Returns false if ExifTool is unavailable or produced no GPS records.
//
// progressCb: optional callback called after each segment completes.
//   Args: (segmentsDone, segmentsTotal).  Null = no callback.
bool extractGps(json& root,
                int tripIdx,
                const std::string& manifestFile,
                const std::string& exiftoolPath,
                bool verbose = false,
                std::function<void(int done, int total)> progressCb = nullptr);

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

// Measure per-camera recording start offsets using the GPS lock clapperboard —
// the first frame where each camera's GPS data transitions from invalid (0,0) to
// a real fix.  Because there is one GPS receiver, the lock happens at one wall-clock
// moment; different frame positions across cameras at that moment reveal the
// inter-camera start offset.
//
// camPaths:   slot name → absolute path to the first segment file for that camera.
// startEpoch: trip start epoch (from segment filename timestamp).
// profile:    updated in-place with measured offsets in cameraStartOffsets.
//             Primary slot is the reference; other slots get positive values when
//             they started recording before the primary (and must be trimmed).
//
// Only cold-start trips (GPS locks during recording) provide a usable clapperboard.
// Returns true if at least one non-zero offset was measured and stored.
// TODO: upgrade to sub-second precision via ffprobe data-stream packet PTS once
//       the D90's LIGOGPSINFO packet timing is characterised.
bool measureCameraOffsets(const std::map<std::string, std::string>& camPaths,
                          time_t startEpoch,
                          const std::string& exiftoolPath,
                          CameraProfile& profile);

} // namespace CamClops

#endif
// SN: 00109
