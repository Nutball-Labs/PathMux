#ifndef CAMERA_PROFILE_HPP
#define CAMERA_PROFILE_HPP

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
    bool        isPrimary = false;
};

// ---------------------------------------------------------------------------
// CameraProfile — complete description of a dashcam's storage organisation.
//
// filenameRegex   — ECMAScript regex matched against the bare filename (no
//                   directory part).  Capture group 1 = full timestamp
//                   string (fed to strptime via timestampFormat).  Capture
//                   group 2 (optional) = camera token; used to distinguish
//                   cameras when the layout is flat.  If group 2 is absent,
//                   camera identity is derived from scanSubdir alone.
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
//   "exiftool_ligogps" — LIGOGPSINFO binary stream via ExifTool 13.51+
//   "none"             — no GPS stream
//
// defaultLayout   — collage layout hint: "2x2", "3x3", "side_by_side",
//                   "single".  Informational for now; consumed by future
//                   Qt6 collage builder.
// ---------------------------------------------------------------------------
struct CameraProfile {
    std::string name;
    std::string profileId;

    std::vector<CameraSlot> slots;

    std::string filenameRegex;
    std::string timestampFormat;
    // "filename"          — parse group 1 of filenameRegex via timestampFormat (default)
    // "exiftool_metadata" — read DateTimeOriginal from file metadata via exiftool (UTC)
    std::string timestampSource  = "filename";
    std::string containerExt;
    std::string thumbnailMethod = "replace_ext";
    std::string gpsMethod       = "none";
    std::string defaultLayout   = "2x2";

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
    // D90 default — used when no profile has been configured.
    static CameraProfile d90Default();
};

} // namespace Pathmux

#endif
// SN: 00088
