#ifndef TRIP_DETECTION_HPP
#define TRIP_DETECTION_HPP

#include <string>
#include <vector>

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

struct TripSegment {
    std::string timestamp;
    std::string front;
    std::string rear;
    std::string left;
    std::string right;
};

// ---------------------------------------------------------------------------
// VideoProfile — pixel format and color characteristics of the source footage.
// Probed once from the first Front segment during detectTrips() and stored in
// the manifest so downstream encode operations don't need to re-probe.
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
    std::string relPath;   // e.g. "Front/20260225_044424F.ts"
    std::string md5;       // hex md5 computed at scan time
};

struct Trip {
    // Two-character base36 ID.  Assigned once on first detection of this trip;
    // never changed on rescan.  Matched across rescans by startEpoch.
    std::string id;

    std::string date;
    std::string startTime;
    std::string duration;
    int         segdur   = 0;   // segment duration in seconds (ffprobe first seg)
    std::vector<TripSegment> segments;

    // Epoch timestamp for ID-stable matching across rescans.
    time_t startEpoch = 0;

    // Video format profile — probed from first Front segment during scan.
    VideoProfile videoProfile;

    // User note — free-form text attached to this trip via the -V build menu.
    std::string note;

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
    std::string firstLockTimestamp;       // \"2026:02:16 14:32:05\"
    int         firstLockRecord    = -1;  // 0-based index into GPS stream
    int         gpsLockSeconds     = -1;  // seconds to first valid fix; -1 = not scanned
    double startLat = 0.0;
    double startLon = 0.0;
    double endLat   = 0.0;
    double endLon   = 0.0;

    // "none" | "partial" | "complete"
    std::string gpsTrackStatus = "none";

    // Full GPS track — one point per second extracted from all segments.
    // Empty until user requests GPS extraction.
    std::vector<GpsPoint> gpsTrack;
};

class TripDetection {
public:
    // gapThresholdSeconds: time between last segment of one trip and first
    // segment of the next before a new trip is declared.  Comes from
    // AppSettings so the user can tune it.  fuzzyWindowSeconds is the ±
    // tolerance for matching non-front cameras to the front anchor time.
    std::vector<Trip> detectTrips(const std::string& path,
                                  int gapThresholdSeconds = 900,
                                  int fuzzyWindowSeconds  = 5,
                                  const std::string& ffprobePath      = "ffprobe",
                                  const std::string& exiftoolPath     = "exiftool",
                                  const std::string& exiftoolOptions  = "-ee3");
};

} // namespace Pathmux

#endif
// SN: 00071
