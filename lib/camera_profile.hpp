// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef CAMERA_PROFILE_HPP
#define CAMERA_PROFILE_HPP

#include <map>
#include <string>
#include <vector>

namespace Pathmux {

// ---------------------------------------------------------------------------
// CameraSlot — describes one camera in a multi-camera dashcam setup.
//
// name          — canonical slot identifier used as the key in
//                 TripSegment::cameras (e.g. "front", "rear", "left_repeater")
// displayName   — human-readable label for UI (e.g. "Front", "Left Repeater")
// filenameToken — substring in the filename that uniquely identifies this
//                 camera (e.g. "_F", "-front", "_CAM1_").  Empty = camera
//                 is identified by scanSubdir alone.
// scanSubdir    — subdirectory under the source root to search for this
//                 camera's files (e.g. "Front", "Rear").  Empty = search
//                 source root (flat layout, as in TeslaCam / Cobra).
// isPrimary     — true for the anchor camera used to establish trip
//                 boundaries.  Exactly one slot per profile should be primary.
// ---------------------------------------------------------------------------
struct CameraSlot {
    std::string name;
    std::string displayName;
    std::string filenameToken;
    std::string scanSubdir;
    // Fallback subdirs tried in order if scanSubdir exists but yields no matching files.
    // Handles cameras that ship cards with different directory layouts (e.g. Cobra GPS:
    // some cards use 100_DSC/, others wrap it under DCIM/100_DSC/).
    std::vector<std::string> scanSubdirCandidates;
    bool        isPrimary = false;
    // Collage quadrant position: 0=TL 1=TR 2=BL 3=BR; -1=auto (fill in slot order)
    int         quadrant  = -1;
};

// ---------------------------------------------------------------------------
// CameraProfile — complete description of a dashcam's storage organisation.
//
// filenameRegex         — ECMAScript regex matched against the bare filename
//                         (no directory part).  By default group 1 = full
//                         timestamp string; group 2 (optional) = camera token.
//                         For prefix-token layouts (token precedes timestamp)
//                         the groups are reversed; see timestampCaptureGroup /
//                         tokenCaptureGroup.  If no token group is captured,
//                         camera identity is derived from scanSubdir alone.
//
// timestampFormat — strptime format applied to group 1.
//                   D90 example: "%Y%m%d_%H%M%S"
//                   Tesla example: "%Y-%m-%d_%H-%M-%S"
//
// containerExt    — video file extension, case-insensitive match at scan
//                   time (e.g. ".ts", ".mp4", ".MOV").
//
// thumbnailMethod — how to find the sidecar thumbnail for a video file:
//   "replace_ext"  — swap video extension with ".jpg"
//   "ths_sidecar"  — replace extension with "_ths.jpg" (Pruveeo D90)
//   "none"         — no thumbnails
//
// gpsMethod       — GPS data source:
//   "exiftool_ligogps" — LIGOGPSINFO binary stream via ExifTool 13.51+ (Pruveeo D90)
//   "exiftool_gps0"    — standard GPS0 atom in 3GP/MOV via ExifTool (Cobra GPS, etc.)
//   "none"             — no GPS stream
//
// timestampSource — how to obtain each segment's epoch timestamp:
//   "filename"          — parse group 1 of filenameRegex via timestampFormat (default)
//   "exiftool_metadata" — read DateTimeOriginal from file metadata via exiftool (UTC)
//   "mtime"             — use filesystem last-write-time (segment end time)
//
// defaultLayout   — collage layout hint: "2x2", "3x3", "side_by_side",
//                   "single".  Informational for now; consumed by future
//                   Qt6 collage builder.
// ---------------------------------------------------------------------------
struct CameraProfile {
    std::string name;
    std::string profileId;

    std::vector<CameraSlot> cameraSlots;

    std::string filenameRegex;
    std::string timestampFormat;
    std::string timestampSource   = "filename";
    // "utc"   — filename timestamps are in UTC; use timegm() for epoch conversion
    // "local" — filename timestamps are in local wall-clock time; use mktime()
    std::string timestampTimezone = "utc";
    // Capture group indices in filenameRegex.  Default: group 1 = timestamp,
    // group 2 = camera token.  Prefix-token layouts reverse these (1 = token,
    // 2 = timestamp); set both fields accordingly in those profiles.
    int         timestampCaptureGroup = 1;
    int         tokenCaptureGroup     = 2;
    std::string containerExt;
    std::string thumbnailMethod = "replace_ext";
    std::string gpsMethod       = "none";
    // Full exiftool option string for GPS extraction (everything except the
    // binary path and the file path appended at the end).  Camera-specific —
    // must not be a global application setting.  Empty = use the legacy
    // Falls back to D90 built-in default when absent (legacy manifests).
    std::string gpsExiftoolArgs;
    std::string defaultLayout   = "2x2";

    // Per-camera recording start offset relative to the primary camera, in seconds.
    // Positive = this camera started recording before the primary (is ahead in time);
    // trim this many seconds from the head of this camera's stream to align it.
    // Populated by measureCameraOffsets() via GPS lock clapperboard on cold-start trips.
    // Stored in the profile so it applies to all trips from the same hardware unit.
    // Key = slot name (e.g. "rear"), primary slot is omitted (its offset is zero by def).
    std::map<std::string, double> cameraStartOffsets;

    // Returns the name of the primary slot, or "" if none designated.
    std::string primarySlot() const;

    // Returns pointer to slot by name, or nullptr if not found.
    const CameraSlot* slotByName(const std::string& slotName) const;

    // Returns true if the profile has the minimum fields needed to scan:
    // at least one slot, exactly one primary, a regex, and a timestamp format.
    bool isValid() const;

    // --- Persistence ---
    static CameraProfile loadFromFile(const std::string& path);
    void saveToFile(const std::string& path) const;

    // --- Built-in profiles ---
    static CameraProfile d90Default();
    static CameraProfile cobraDefault();
    static CameraProfile cobraGpsDefault();
    static CameraProfile prirotteDefault();

    // All built-in profiles in detection-priority order.
    static std::vector<CameraProfile> getBuiltinProfiles();
};

} // namespace Pathmux

#endif
// SN: 00109
