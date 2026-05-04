// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef TRIP_DETECTION_HPP
#define TRIP_DETECTION_HPP

#include <string>
#include <vector>
#include <map>
#include "camera_profile.hpp"

namespace Pathmux {

// One GPS fix extracted from the LIGOGPSINFO stream via ExifTool 13.51+.
// Timestamp format: "YYYY:MM:DD HH:MM:SS" (as returned by exiftool -p).
// Speed in km/h.  Altitude is present in the stream but incorrect
// (negative values on D90) — omitted.
struct GpsPoint {
    std::string timestamp;  // "2026:02:16 14:32:05"
    double lat      = 0.0;
    double lon      = 0.0;
    double altitude = -9999.0; // metres; -9999 = not available / known bad
    double speed    = -1.0;    // km/h; -1 = not available
    double heading  = -1.0;    // degrees true; -1 = not available
};

// ---------------------------------------------------------------------------
// TripSegment — one time-aligned set of video files, one per camera.
//
// cameras — maps slot name (e.g. "front", "rear") to absolute file path.
//           "-" means the camera was absent or unmatched for this segment.
// thumbs  — maps slot name to absolute .jpg sidecar path, or "" if absent.
// ---------------------------------------------------------------------------
struct TripSegment {
    std::string timestamp;
    std::map<std::string, std::string> cameras;  // slot name -> abs path
    std::map<std::string, std::string> thumbs;   // slot name -> abs path
};

// ---------------------------------------------------------------------------
// VideoProfile — pixel format and color characteristics of the source footage.
// Probed once from the first primary-camera segment during detectTrips() and
// stored in the manifest so downstream encode operations don't need to re-probe.
// ---------------------------------------------------------------------------
struct VideoProfile {
    int         width       = 1920;
    int         height      = 1080;
    std::string pixFmt      = "yuvj420p";
    std::string colorRange  = "pc";
    std::string colorSpace  = "bt709";
    std::string frameRate   = "25/1";
};

// ---------------------------------------------------------------------------
// ValidationFile — one entry in a trip's 3-file integrity sample.
// Relative path is relative to the source scan root so manifests survive
// remounts.  md5 is computed at scan time; recomputed on demand for [V].
// ---------------------------------------------------------------------------
struct ValidationFile {
    std::string relPath;   // e.g. "Front/20260225_044424_F.ts"
    std::string md5;       // hex md5 computed at scan time
};

// Per-trip camera start-offset sync data, produced by pm_sync_analyze.py.
// segmentTrims[i][cam] = seconds to trim from the start of that camera's
// segment i file before building the collage (Tier 2 sync correction).
struct CameraSync {
    std::string analyzedAt;    // ISO-8601 UTC timestamp of analysis run
    std::string syncCam;       // camera with no trim (latest-starting, reference)
    double spanFrames    = 0.0;
    double spanVariation = 0.0;
    std::vector<std::map<std::string, double>> segmentTrims;
    bool   valid         = false;
};

struct Trip {
    // Two-character base36 ID.  Assigned once on first detection of this trip;
    // never changed on rescan.  Matched across rescans by startEpoch.
    std::string id;

    std::string date;
    std::string startTime;
    std::string duration;
    int         segdur              = 0;  // nominal segment interval (seconds), derived from timestamp spacing
    int         segDetectedDuration = 0;  // trip duration from timestamp arithmetic only (pre-ffprobe)
    int         durationFFProbed    = -1; // sum of all primary-camera segment ffprobe durations; -1 = not yet computed
    std::vector<TripSegment> segments;

    // Epoch timestamp for ID-stable matching across rescans.
    time_t startEpoch = 0;

    // Video format profile — probed from first primary-camera segment during scan.
    VideoProfile videoProfile;

    // User note — free-form text attached to this trip via the -V build menu.
    std::string note;

    // Trip-level thumbnail convenience fields.
    // firstThumbs: from segments[1] (or segments[0] if only one) to avoid
    //   cold-start frames (garage door, parking lot, etc.).
    // lastThumbs:  from segments.back().
    // Key = slot name (e.g. "front").  Value = "" if camera or sidecar absent.
    std::map<std::string, std::string> firstThumbs;
    std::map<std::string, std::string> lastThumbs;

    // Three randomly-chosen source files sampled at scan time for integrity
    // checking.  Selection is deterministic (seeded on startEpoch).
    std::vector<ValidationFile> validationFiles;

    // GPS fields — populated by GPS extraction pass (ExifTool 13.51+).
    // firstLock: first GPS record with non-zero lat/lon (chip acquisition time).
    // startLat/Lon: same as firstLock lat/lon (first valid fix of the trip).
    // endLat/Lon:   last valid fix of the last segment.
    // gpsLockSeconds: seconds from segment start to first valid fix.
    //   -1 = not yet scanned; >=0 = seconds to lock (0 means immediate).
    //   Written by pm_gpsinfo --scan-all-trips; read by UI for display.
    double      firstLockLat       = 0.0;
    double      firstLockLon       = 0.0;
    std::string firstLockTimestamp;       // "2026:02:16 14:32:05"
    int         firstLockRecord    = -1;  // 0-based index into GPS stream
    int         gpsLockSeconds     = -1;  // seconds to first valid fix; -1 = not scanned
    double startLat = 0.0;
    double startLon = 0.0;
    double endLat   = 0.0;
    double endLon   = 0.0;

    // "none" | "partial" | "complete"
    std::string gpsTrackStatus = "none";

    // Generated video assets — absolute paths recorded in the manifest so files
    // can live anywhere and multiple outputs per trip are supported.
    std::vector<std::string> mapVideos;   // moving-map MP4s (map1, map2, …)
    std::vector<std::string> dashVideos;  // dashboard MP4s  (dash1, dash2, …)
    std::vector<std::string> hudVideos;   // HUD overlay WebMs (hud1, hud2, …)

    // Camera start-offset sync data from pm_sync_analyze.py --write.
    CameraSync cameraSync;

    // Full GPS track — one point per second extracted from all segments.
    // Empty until user requests GPS extraction.
    std::vector<GpsPoint> gpsTrack;
};

// ---------------------------------------------------------------------------
// camPath — safe camera file lookup from a TripSegment.
// Returns the absolute path for the named slot, or "-" if absent.
// ---------------------------------------------------------------------------
inline std::string camPath(const TripSegment& seg, const std::string& slot) {
    auto it = seg.cameras.find(slot);
    return (it != seg.cameras.end() && !it->second.empty()) ? it->second : "-";
}

// camThumb — safe thumbnail lookup from a TripSegment.  Returns path or "".
inline std::string camThumb(const TripSegment& seg, const std::string& slot) {
    auto it = seg.thumbs.find(slot);
    return (it != seg.thumbs.end()) ? it->second : "";
}

class TripDetection {
public:
    // gapThresholdSeconds: time between last segment of one trip and first
    // segment of the next before a new trip is declared.  Comes from
    // AppSettings so the user can tune it.  fuzzyWindowSeconds is the ±
    // tolerance for matching non-primary cameras to the primary anchor time.
    //
    // profile: camera layout description.  Defaults to the Pruveeo D90
    // built-in profile so existing callers compile without modification.
    std::vector<Trip> detectTrips(const std::string& path,
                                  const CameraProfile& profile    = CameraProfile::d90Default(),
                                  int gapThresholdSeconds         = 900,
                                  int fuzzyWindowSeconds          = 5,
                                  const std::string& ffprobePath  = "ffprobe",
                                  const std::string& exiftoolPath = "exiftool");
};

} // namespace Pathmux

#endif
// SN: 00109
