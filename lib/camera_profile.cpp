#include "camera_profile.hpp"
#include "../json.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace Pathmux {

// ---------------------------------------------------------------------------

std::string CameraProfile::primarySlot() const {
    for (const auto& s : slots)
        if (s.isPrimary) return s.name;
    return "";
}

const CameraSlot* CameraProfile::slotByName(const std::string& slotName) const {
    for (const auto& s : slots)
        if (s.name == slotName) return &s;
    return nullptr;
}

bool CameraProfile::isValid() const {
    // timestampFormat is required only when timestamps come from filenames.
    bool needsFmt = (timestampSource != "exiftool_metadata");
    if (slots.empty() || filenameRegex.empty() || (needsFmt && timestampFormat.empty()))
        return false;
    for (const auto& s : slots)
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
//   "container_ext": ".ts",
//   "thumbnail_method": "ths_sidecar",
//   "gps_method": "exiftool_ligogps",
//   "default_layout": "2x2",
//   "slots": [
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
    p.timestampSource = j.value("timestamp_source",  "filename");
    p.containerExt    = j.value("container_ext",     "");
    p.thumbnailMethod = j.value("thumbnail_method", "replace_ext");
    p.gpsMethod       = j.value("gps_method",       "none");
    p.defaultLayout   = j.value("default_layout",   "2x2");

    for (const auto& js : j.value("slots", json::array())) {
        CameraSlot s;
        s.name          = js.value("name",           "");
        s.displayName   = js.value("display",        "");
        s.filenameToken = js.value("filename_token", "");
        s.scanSubdir    = js.value("scan_subdir",    "");
        s.isPrimary     = js.value("is_primary",     false);
        if (!s.name.empty()) p.slots.push_back(s);
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
    j["timestamp_source"]  = timestampSource;
    j["container_ext"]     = containerExt;
    j["thumbnail_method"] = thumbnailMethod;
    j["gps_method"]       = gpsMethod;
    j["default_layout"]   = defaultLayout;

    json slotArr = json::array();
    for (const auto& s : slots) {
        json js;
        js["name"]           = s.name;
        js["display"]        = s.displayName;
        js["filename_token"] = s.filenameToken;
        js["scan_subdir"]    = s.scanSubdir;
        js["is_primary"]     = s.isPrimary;
        slotArr.push_back(js);
    }
    j["slots"] = slotArr;

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
    p.filenameRegex   = R"((\d{8}_\d{6})_[A-Za-z]\.[tT][sS])";
    p.timestampFormat = "%Y%m%d_%H%M%S";
    p.containerExt    = ".ts";
    p.thumbnailMethod = "ths_sidecar";
    p.gpsMethod       = "exiftool_ligogps";
    p.defaultLayout   = "2x2";

    CameraSlot front;
    front.name = "front"; front.displayName = "Front";
    front.filenameToken = "_F"; front.scanSubdir = "Front"; front.isPrimary = true;

    CameraSlot rear;
    rear.name = "rear"; rear.displayName = "Rear";
    rear.filenameToken = "_R"; rear.scanSubdir = "Rear";

    CameraSlot left;
    left.name = "left"; left.displayName = "Left";
    left.filenameToken = "_L"; left.scanSubdir = "Left";

    CameraSlot right;
    right.name = "right"; right.displayName = "Right";
    right.filenameToken = "_B"; right.scanSubdir = "Right";

    p.slots = { front, rear, left, right };
    return p;
}

} // namespace Pathmux
// SN: 00088
