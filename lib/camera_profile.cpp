// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "camera_profile.hpp"
#include "../json.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace CamClops {

// ---------------------------------------------------------------------------

std::string CameraProfile::primarySlot() const {
    for (const auto& s : cameraSlots)
        if (s.isPrimary) return s.name;
    return "";
}

const CameraSlot* CameraProfile::slotByName(const std::string& slotName) const {
    for (const auto& s : cameraSlots)
        if (s.name == slotName) return &s;
    return nullptr;
}

// Returns a copy adjusted for scanning a parking subdir: slots redirected to
// subdir with tokens prefixed by parkingTokenPrefix.  parkingSubdir/roSubdir
// cleared so the returned profile cannot trigger further parking scans.
static CameraProfile parkingVariantProfile(const CameraProfile& base,
                                            const std::string& subdir) {
    if (subdir.empty()) return {};
    CameraProfile p = base;
    for (auto& slot : p.cameraSlots) {
        slot.scanSubdir = subdir;
        slot.scanSubdirCandidates.clear();
        if (!base.parkingTokenPrefix.empty() && !slot.filenameToken.empty())
            slot.filenameToken = base.parkingTokenPrefix + slot.filenameToken;
    }
    p.parkingSubdir.clear();
    p.roSubdir.clear();
    return p;
}

CameraProfile CameraProfile::parkingProfile() const {
    return parkingVariantProfile(*this, parkingSubdir);
}

CameraProfile CameraProfile::roProfile() const {
    return parkingVariantProfile(*this, roSubdir);
}

bool CameraProfile::isValid() const {
    // timestampFormat only required when timestamps are parsed from the filename.
    bool needsFmt = (timestampSource == "filename");
    if (cameraSlots.empty() || filenameRegex.empty() || (needsFmt && timestampFormat.empty()))
        return false;
    for (const auto& s : cameraSlots)
        if (s.isPrimary) return true;
    return false;
}

// ---------------------------------------------------------------------------
// loadFromFile — read a profile JSON from disk.
//
// JSON structure:
// {
//   "name": "Pruveeo D90",
//   "profile_id": "pruveeo_d90",
//   "filename_regex": "(\\d{8}_\\d{6})_[A-Za-z]\\.[tT][sS]",
//   "timestamp_format": "%Y%m%d_%H%M%S",
//   "timestamp_timezone": "utc",
//   "container_ext": ".ts",
//   "thumbnail_method": "ths_sidecar",
//   "gps_method": "exiftool_ligogps",
//   "default_layout": "2x2",
//   "cameraSlots": [
//     { "name": "front", "display": "Front", "filename_token": "_F",
//       "scan_subdir": "Front", "is_primary": true },
//     ...
//   ]
// }
// ---------------------------------------------------------------------------
CameraProfile CameraProfile::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open camera profile: " + path);
    json j = json::parse(f);

    CameraProfile p;
    p.name            = j.value("name",              "");
    p.profileId       = j.value("profile_id",        "");
    p.filenameRegex   = j.value("filename_regex",    "");
    p.timestampFormat = j.value("timestamp_format",  "");
    p.timestampSource   = j.value("timestamp_source",    "filename");
    p.timestampTimezone       = j.value("timestamp_timezone",      "utc");
    p.timestampCaptureGroup   = j.value("timestamp_capture_group", 1);
    p.tokenCaptureGroup       = j.value("token_capture_group",     2);
    p.containerExt    = j.value("container_ext",     "");
    p.thumbnailMethod = j.value("thumbnail_method", "replace_ext");
    p.gpsMethod       = j.value("gps_method",        "none");
    p.gpsExiftoolArgs    = j.value("gps_exiftool_args",    "");
    p.defaultLayout      = j.value("default_layout",      "2x2");
    p.parkingSubdir      = j.value("parking_subdir",      "");
    p.roSubdir           = j.value("ro_subdir",           "");
    p.parkingTokenPrefix = j.value("parking_token_prefix","");

    if (j.contains("camera_start_offsets") && j["camera_start_offsets"].is_object())
        for (const auto& [k, v] : j["camera_start_offsets"].items())
            if (v.is_number()) p.cameraStartOffsets[k] = v.get<double>();

    // Accept both "cameraSlots" (current) and "slots" (legacy, pre-Qt-macro-rename).
    const json& slotArr = j.contains("cameraSlots") ? j["cameraSlots"]
                        : j.value("slots", json::array());
    for (const auto& js : slotArr) {
        CameraSlot s;
        s.name          = js.value("name",           "");
        s.displayName   = js.value("display",        "");
        s.filenameToken = js.value("filename_token", "");
        s.scanSubdir    = js.value("scan_subdir",    "");
        s.isPrimary     = js.value("is_primary",     false);
        s.quadrant      = js.value("quadrant",        -1);
        if (js.contains("scan_subdir_candidates") && js["scan_subdir_candidates"].is_array())
            for (const auto& c : js["scan_subdir_candidates"])
                if (c.is_string()) s.scanSubdirCandidates.push_back(c.get<std::string>());
        if (!s.name.empty()) p.cameraSlots.push_back(s);
    }
    return p;
}

// ---------------------------------------------------------------------------
void CameraProfile::saveToFile(const std::string& path) const {
    json j;
    j["name"]              = name;
    j["profile_id"]        = profileId;
    j["filename_regex"]    = filenameRegex;
    j["timestamp_format"]  = timestampFormat;
    j["timestamp_source"]   = timestampSource;
    j["timestamp_timezone"] = timestampTimezone;
    if (timestampCaptureGroup != 1) j["timestamp_capture_group"] = timestampCaptureGroup;
    if (tokenCaptureGroup     != 2) j["token_capture_group"]     = tokenCaptureGroup;
    j["container_ext"]     = containerExt;
    j["thumbnail_method"] = thumbnailMethod;
    j["gps_method"]        = gpsMethod;
    if (!gpsExiftoolArgs.empty())
        j["gps_exiftool_args"] = gpsExiftoolArgs;
    j["default_layout"]   = defaultLayout;
    if (!parkingSubdir.empty())      j["parking_subdir"]       = parkingSubdir;
    if (!roSubdir.empty())           j["ro_subdir"]            = roSubdir;
    if (!parkingTokenPrefix.empty()) j["parking_token_prefix"] = parkingTokenPrefix;

    json slotArr = json::array();
    for (const auto& s : cameraSlots) {
        json js;
        js["name"]           = s.name;
        js["display"]        = s.displayName;
        js["filename_token"] = s.filenameToken;
        js["scan_subdir"]    = s.scanSubdir;
        js["is_primary"]     = s.isPrimary;
        if (s.quadrant >= 0)
            js["quadrant"]   = s.quadrant;
        if (!s.scanSubdirCandidates.empty()) {
            json cands = json::array();
            for (const auto& c : s.scanSubdirCandidates) cands.push_back(c);
            js["scan_subdir_candidates"] = cands;
        }
        slotArr.push_back(js);
    }
    j["cameraSlots"] = slotArr;

    if (!cameraStartOffsets.empty()) {
        json offsets;
        for (const auto& [k, v] : cameraStartOffsets) offsets[k] = v;
        j["camera_start_offsets"] = offsets;
    }

    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot write camera profile: " + path);
    f << j.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
// d90Default — built-in profile for the Pruveeo D90 four-camera dashcam.
//
// Layout: subdirectory per camera (Front/, Rear/, Left/, Right/).
// Filename: YYYYMMDD_HHMMSS_X.ts where X is a single camera letter.
// scanSubdir is authoritative for camera identity on D90; filenameToken
// is carried for completeness and for flat-layout fallback if needed.
// Thumbnail: _ths.jpg sidecar (e.g. 20260225_044424_F_ths.jpg).
// GPS: LIGOGPSINFO binary stream via ExifTool 13.51+.
// ---------------------------------------------------------------------------
CameraProfile CameraProfile::d90Default() {
    CameraProfile p;
    p.name            = "Pruveeo D90";
    p.profileId       = "pruveeo_d90";
    p.filenameRegex      = R"((\d{8}_\d{6})[A-Za-z]\.[tT][sS])";
    p.timestampFormat    = "%Y%m%d_%H%M%S";
    p.timestampTimezone  = "utc";
    p.containerExt    = ".ts";
    p.thumbnailMethod  = "ths_sidecar";
    p.gpsMethod        = "exiftool_ligogps";
    p.gpsExiftoolArgs  = "-ee3 -p '$GPSDateTime $GPSLatitude# $GPSLongitude#"
                         " $GPSAltitude# $GPSSpeed# $GPSTrack# $Accelerometer'";
    p.defaultLayout    = "2x2";

    CameraSlot front;
    front.name = "front"; front.displayName = "Front";
    front.filenameToken = "_F"; front.scanSubdir = "Front";
    front.isPrimary = true; front.quadrant = 0; // TL

    CameraSlot rear;
    rear.name = "rear"; rear.displayName = "Rear";
    rear.filenameToken = "_R"; rear.scanSubdir = "Rear";
    rear.quadrant = 1; // TR

    CameraSlot left;
    left.name = "left"; left.displayName = "Left";
    left.filenameToken = "_L"; left.scanSubdir = "Left";
    left.quadrant = 3; // BR

    CameraSlot right;
    right.name = "right"; right.displayName = "Right";
    right.filenameToken = "_B"; right.scanSubdir = "Right";
    right.quadrant = 2; // BL

    p.cameraSlots = { front, rear, left, right };
    return p;
}

// ---------------------------------------------------------------------------
// cobraDefault — Cobra CCDC4500 single-camera dashcam.
// Flat layout: all files in DCIM/100_DSC/.
// Filename: YYYYMMDD_NNNN_CAM1_VID.MOV (sequential counter, not HHMMSS).
// Timestamp: DateTimeOriginal from QuickTime audio track (UTC).
// Contributed by K.S.
// ---------------------------------------------------------------------------
CameraProfile CameraProfile::cobraDefault() {
    CameraProfile p;
    p.name            = "Cobra CCDC4500";
    p.profileId       = "cobra";
    p.filenameRegex   = R"((\d{8}_\d{4})_CAM1_VID\.[mM][oO][vV])";
    p.timestampSource = "exiftool_metadata";
    p.containerExt    = ".MOV";
    p.thumbnailMethod = "none";
    p.gpsMethod       = "none";
    p.defaultLayout   = "single";

    CameraSlot front;
    front.name = "front"; front.displayName = "Front";
    front.filenameToken = ""; front.scanSubdir = "100_DSC";
    front.scanSubdirCandidates = {"DCIM/100_DSC"};
    front.isPrimary = true; front.quadrant = 0; // TL

    p.cameraSlots = { front };
    return p;
}

// ---------------------------------------------------------------------------
// cobraGpsDefault — Cobra dual-camera dashcam with GPS.
// Flat layout: CAM1 (front) and CAM2 (rear) both in DCIM/100_DSC/.
// Filename: YYYYMMDD_NNNN_CAMx_VID.MOV — camera ID in filename token.
// Timestamp: DateTimeOriginal from QuickTime audio track (UTC).
// GPS: standard gps0 atom via ExifTool (no accelerometer on this camera).
// Contributed by K.S.
// ---------------------------------------------------------------------------
CameraProfile CameraProfile::cobraGpsDefault() {
    CameraProfile p;
    p.name            = "Cobra GPS";
    p.profileId       = "cobra_gps";
    p.filenameRegex   = R"((\d{8}_\d{4})_(CAM[12])_VID\.[mM][oO][vV])";
    p.timestampSource = "exiftool_metadata";
    p.containerExt    = ".MOV";
    p.thumbnailMethod  = "none";
    p.gpsMethod        = "exiftool_gps0";
    p.gpsExiftoolArgs  = "-ee -p '$GPSDateTime $GPSLatitude# $GPSLongitude#"
                         " $GPSAltitude# $GPSSpeed# $GPSTrack#'";
    p.defaultLayout    = "side_by_side";

    CameraSlot front;
    front.name = "front"; front.displayName = "Front";
    front.filenameToken = "CAM1"; front.scanSubdir = "100_DSC";
    front.scanSubdirCandidates = {"DCIM/100_DSC"};
    front.isPrimary = true; front.quadrant = 0; // TL

    CameraSlot rear;
    rear.name = "rear"; rear.displayName = "Rear";
    rear.filenameToken = "CAM2"; rear.scanSubdir = "100_DSC";
    rear.scanSubdirCandidates = {"DCIM/100_DSC"};
    rear.quadrant = 1; // TR

    p.cameraSlots = { front, rear };
    return p;
}

// ---------------------------------------------------------------------------
// prirotteDefault — Prilotte dual-camera AVI dashcam.
// Subdirectory per camera: DCIM/DCIMA/ (front), DCIM/DCIMB/ (mid, optional),
//   DCIM/DCIMC/ (rear).  Camera letter embedded in filename: MOVA####.avi.
// Timestamp: filesystem mtime (no embedded metadata in AVI/RIFF headers).
//   mtime = segment end time; 2-minute segments.
// No GPS on this camera.
// Contributed by K.S.
// ---------------------------------------------------------------------------
CameraProfile CameraProfile::prirotteDefault() {
    CameraProfile p;
    p.name              = "Prilotte";
    p.profileId         = "prilotte";
    p.filenameRegex     = R"(MOV([ABC])(\d{4})\.avi)";
    p.timestampSource   = "mtime";
    p.timestampCaptureGroup = 2;
    p.tokenCaptureGroup     = 1;
    p.containerExt      = ".avi";
    p.thumbnailMethod   = "none";
    p.gpsMethod         = "none";
    p.defaultLayout     = "side_by_side";

    CameraSlot front;
    front.name = "front"; front.displayName = "Front";
    front.filenameToken = "A"; front.scanSubdir = "DCIM/DCIMA";
    front.isPrimary = true; front.quadrant = 0; // TL

    CameraSlot rear;
    rear.name = "rear"; rear.displayName = "Rear";
    rear.filenameToken = "C"; rear.scanSubdir = "DCIM/DCIMC";
    rear.quadrant = 1; // TR

    p.cameraSlots = { front, rear };
    return p;
}

// ---------------------------------------------------------------------------
// viofoA229UltraDefault — Viofo A229 Ultra 3-channel dashcam.
// Layout: all three cameras in a single flat directory (DCIM/Movie/).
// Filename: YYYY_MMDD_HHMMSS_NNNNNN[P?][FIR].MP4
//   F/I/R   = driving clips (Front 4K, Interior fisheye, Rear)
//   PF/PI/PR = parking-mode clips (same cameras, parking subdir)
// Timestamps in the filename are local wall-clock time.
// GPS: standard QuickTime GPS metadata, 1 record/sec, UTC timestamps.
//   Speed in km/h; no altitude; no accelerometer.
//   GPS Date/Time tag may have trailing 'Z' — parser strips it.
// No sidecar thumbnails — frame extracted by CamClops at scan time.
// Parking: motion-triggered clips in DCIM/Movie/Parking/ (writable).
//   G-sensor clips in DCIM/Movie/RO/ (firmware-locked, never delete).
//   Both use PF/PI/PR token prefix; 45s clips (15s pre + 30s post).
// Contributed by B.G.
// ---------------------------------------------------------------------------
CameraProfile CameraProfile::viofoA229UltraDefault() {
    CameraProfile p;
    p.name              = "Viofo A229 Ultra";
    p.profileId         = "viofo_a229ultra";
    // P?[FIR] matches both driving tokens (F/I/R) and parking tokens (PF/PI/PR).
    p.filenameRegex     = R"((\d{4}_\d{4}_\d{6})_\d{6}(P?[FIR])\.[Mm][Pp][4])";
    p.timestampFormat   = "%Y_%m%d_%H%M%S";
    p.timestampTimezone = "local";
    p.containerExt      = ".MP4";
    p.thumbnailMethod   = "extract_frame";
    p.gpsMethod         = "exiftool_gps0";
    p.gpsExiftoolArgs   = "-ee3 -p '$GPSDateTime $GPSLatitude# $GPSLongitude#"
                          " 0 $GPSSpeed# $GPSTrack#'";
    p.defaultLayout     = "2x2";
    p.parkingSubdir     = "DCIM/Movie/Parking";
    p.roSubdir          = "DCIM/Movie/RO";  // unconfirmed path — scanned only if present
    p.parkingTokenPrefix = "P";

    CameraSlot front;
    front.name = "front"; front.displayName = "Front";
    front.filenameToken = "F"; front.scanSubdir = "DCIM/Movie";
    front.isPrimary = true; front.quadrant = 0; // TL

    CameraSlot interior;
    interior.name = "interior"; interior.displayName = "Interior";
    interior.filenameToken = "I"; interior.scanSubdir = "DCIM/Movie";
    interior.quadrant = 1; // TR

    CameraSlot rear;
    rear.name = "rear"; rear.displayName = "Rear";
    rear.filenameToken = "R"; rear.scanSubdir = "DCIM/Movie";
    rear.quadrant = 2; // BL

    p.cameraSlots = { front, interior, rear };
    return p;
}

// ---------------------------------------------------------------------------
// getBuiltinProfiles — all factory-embedded profiles, detection-priority order.
// More specific profiles (more cameras, unique layout) listed before generic ones
// so the detector picks the best match when signatures overlap (e.g. Cobra GPS
// before plain Cobra, since the GPS model's regex also matches single-camera cards).
// ---------------------------------------------------------------------------
std::vector<CameraProfile> CameraProfile::getBuiltinProfiles() {
    return {
        d90Default(),
        viofoA229UltraDefault(),
        cobraGpsDefault(),   // before cobraDefault — more specific (2-cam regex)
        cobraDefault(),
        prirotteDefault(),
    };
}

} // namespace CamClops
// SN: 00122
