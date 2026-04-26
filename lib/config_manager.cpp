// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "config_manager.hpp"
#include "camera_profile.hpp"
#include "compat.hpp"
#include "platform.hpp"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <random>
#include <map>
#include <set>
#include <iomanip>
#include <ctime>
#include <cstdio>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Pathmux {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ConfigManager::ConfigManager() {
    configDir         = Platform::getConfigDir();
    settingsFile      = configDir + "pathmux.json";
    locationsFile     = configDir + "locations.json";
    manifestIndexFile = configDir + "manifests.json";
    staleArchiveFile  = configDir + "manifests_stale.json";

    // Derive short hostname for host-specific settings overlay.
    hostname         = getShortHostname();
    hostSettingsFile = configDir + "pathmux_" + hostname + ".json";

    loadSettings();
}

void ConfigManager::ensureConfigDir() {
    if (!fs::exists(configDir))
        fs::create_directories(configDir);
}

// ---------------------------------------------------------------------------
// Settings — pathmux.json
// ---------------------------------------------------------------------------

void ConfigManager::loadSettings() {
    settings = AppSettings{};   // start with compiled-in defaults

    std::ifstream ifs(settingsFile);
    if (!ifs.is_open()) {
        cfgState = ConfigState::FIRST_RUN;
        return;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error&) {
        std::cerr << "Warning: pathmux.json is corrupt — using defaults.\n";
        cfgState = ConfigState::FIRST_RUN;
        return;
    }

    // Empty or near-empty JSON object means first run
    if (j.empty()) {
        cfgState = ConfigState::FIRST_RUN;
        return;
    }

    settings.schemaVersion       = j.value("schemaVersion",       settings.schemaVersion);
    settings.gapThresholdSeconds = j.value("gapThresholdSeconds", settings.gapThresholdSeconds);
    settings.fuzzyWindowSeconds  = j.value("fuzzyWindowSeconds",  settings.fuzzyWindowSeconds);
    settings.exiftoolPath        = j.value("exiftoolPath",        settings.exiftoolPath);
    settings.ffmpegPath          = j.value("ffmpegPath",          settings.ffmpegPath);
    settings.defaultExportDir    = j.value("defaultExportDir",    settings.defaultExportDir);
    settings.tmpDir              = j.value("tmpDir",              settings.tmpDir);
    settings.timestampFormat     = j.value("timestampFormat",     settings.timestampFormat);
    settings.timeDisplay         = j.value("timeDisplay",         settings.timeDisplay);
    settings.useImperial         = j.value("useImperial",         settings.useImperial);
    settings.videoFormat         = j.value("videoFormat",         settings.videoFormat);
    settings.defaultAudioSource  = j.value("defaultAudioSource",  settings.defaultAudioSource);
    settings.logLevel            = j.value("logLevel",            settings.logLevel);
    settings.activeProfileId     = j.value("activeProfileId",     settings.activeProfileId);
    settings.uiScale             = j.value("uiScale",             settings.uiScale);
    settings.hudFontScale        = j.value("hudFontScale",        settings.hudFontScale);
    settings.hudLineScale        = j.value("hudLineScale",        settings.hudLineScale);
    settings.hudColor            = j.value("hudColor",            settings.hudColor);

    if (j.contains("encode") && j["encode"].is_object()) {
        const auto& e = j["encode"];
        settings.encode.preset          = e.value("preset",          settings.encode.preset);
        settings.encode.hwDevice        = e.value("hwDevice",        settings.encode.hwDevice);
        settings.encode.hwDeviceType    = e.value("hwDeviceType",    settings.encode.hwDeviceType);
        settings.encode.normEncoder     = e.value("normEncoder",     settings.encode.normEncoder);
        settings.encode.collageEncoder  = e.value("collageEncoder",  settings.encode.collageEncoder);
        settings.encode.downEncoder     = e.value("downEncoder",     settings.encode.downEncoder);
        settings.encode.pixFmt          = e.value("pixFmt",          settings.encode.pixFmt);
        settings.encode.normQuality     = e.value("normQuality",     settings.encode.normQuality);
        settings.encode.collageQuality  = e.value("collageQuality",  settings.encode.collageQuality);
        settings.encode.downQuality     = e.value("downQuality",     settings.encode.downQuality);
        settings.encode.extraNormArgs   = e.value("extraNormArgs",   settings.encode.extraNormArgs);
        settings.encode.extraCollageArgs= e.value("extraCollageArgs",settings.encode.extraCollageArgs);
        settings.encode.extraDownArgs   = e.value("extraDownArgs",   settings.encode.extraDownArgs);
    }

    if (j.contains("kml") && j["kml"].is_object()) {
        const auto& k          = j["kml"];
        settings.kml.trackAheadColor   = k.value("trackAheadColor",   settings.kml.trackAheadColor);
        settings.kml.trackBehindColor  = k.value("trackBehindColor",  settings.kml.trackBehindColor);
        settings.kml.trackLineWidth    = k.value("trackLineWidth",     settings.kml.trackLineWidth);
        settings.kml.waypointColor     = k.value("waypointColor",      settings.kml.waypointColor);
        settings.kml.startPinUrl       = k.value("startPinUrl",        settings.kml.startPinUrl);
        settings.kml.endPinUrl         = k.value("endPinUrl",          settings.kml.endPinUrl);
        settings.kml.showKnownLocations = k.value("showKnownLocations", settings.kml.showKnownLocations);
    }

    // Apply host-specific overlay — host file wins on any key it contains.
    loadHostOverlay();

    // INCOMPLETE if export dir is not set — key field for write operations
    // (host overlay may have provided it, so evaluate after overlay)
    if (settings.defaultExportDir.empty())
        cfgState = ConfigState::INCOMPLETE;
    else
        cfgState = ConfigState::VALID;

    // Open logger based on loaded log level (host overlay may have set logLevel)
    LogLevel ll = LogLevel::OFF;
    if (settings.logLevel == "normal") ll = LogLevel::NORMAL;
    else if (settings.logLevel == "debug") ll = LogLevel::DEBUG;
    Logger::instance().open(configDir, ll);
}

void ConfigManager::saveSettings() {
    json j;
    j["schemaVersion"]       = settings.schemaVersion;
    j["gapThresholdSeconds"] = settings.gapThresholdSeconds;
    j["fuzzyWindowSeconds"]  = settings.fuzzyWindowSeconds;
    j["exiftoolPath"]        = settings.exiftoolPath;
    j["ffmpegPath"]          = settings.ffmpegPath;
    j["defaultExportDir"]    = settings.defaultExportDir;
    j["tmpDir"]              = settings.tmpDir;
    j["timestampFormat"]     = settings.timestampFormat;
    j["timeDisplay"]         = settings.timeDisplay;
    j["useImperial"]         = settings.useImperial;
    j["videoFormat"]         = settings.videoFormat;
    j["defaultAudioSource"]  = settings.defaultAudioSource;
    j["logLevel"]            = settings.logLevel;
    j["activeProfileId"]     = settings.activeProfileId;

    json e;
    e["preset"]           = settings.encode.preset;
    e["hwDevice"]         = settings.encode.hwDevice;
    e["hwDeviceType"]     = settings.encode.hwDeviceType;
    e["normEncoder"]      = settings.encode.normEncoder;
    e["collageEncoder"]   = settings.encode.collageEncoder;
    e["downEncoder"]      = settings.encode.downEncoder;
    e["pixFmt"]           = settings.encode.pixFmt;
    e["normQuality"]      = settings.encode.normQuality;
    e["collageQuality"]   = settings.encode.collageQuality;
    e["downQuality"]      = settings.encode.downQuality;
    e["extraNormArgs"]    = settings.encode.extraNormArgs;
    e["extraCollageArgs"] = settings.encode.extraCollageArgs;
    e["extraDownArgs"]    = settings.encode.extraDownArgs;
    j["encode"]           = e;

    json k;
    k["trackAheadColor"]    = settings.kml.trackAheadColor;
    k["trackBehindColor"]   = settings.kml.trackBehindColor;
    k["trackLineWidth"]     = settings.kml.trackLineWidth;
    k["waypointColor"]      = settings.kml.waypointColor;
    k["startPinUrl"]        = settings.kml.startPinUrl;
    k["endPinUrl"]          = settings.kml.endPinUrl;
    k["showKnownLocations"] = settings.kml.showKnownLocations;
    j["kml"]                = k;

    std::ofstream ofs(settingsFile);
    if (!ofs.is_open()) {
        std::cerr << "Warning: Could not write " << settingsFile << "\n";
        return;
    }
    ofs << j.dump(2) << "\n";

    // Update in-memory state to reflect what was just saved
    if (settings.defaultExportDir.empty())
        cfgState = ConfigState::INCOMPLETE;
    else
        cfgState = ConfigState::VALID;
}

// ---------------------------------------------------------------------------
// Camera profile
// ---------------------------------------------------------------------------

CameraProfile ConfigManager::getCameraProfile() const {
    // Check user JSON file first (allows overriding a built-in).
    std::string profileFile = configDir + "profiles/"
                            + settings.activeProfileId + ".json";
    if (fs::exists(profileFile)) {
        CameraProfile p = CameraProfile::loadFromFile(profileFile);
        if (p.isValid()) return p;
        std::cerr << "Warning: profile " << profileFile
                  << " failed validation — falling back to built-in.\n";
    }
    // Search built-ins by profile_id.
    for (const auto& p : CameraProfile::getBuiltinProfiles())
        if (p.profileId == settings.activeProfileId) return p;
    return CameraProfile::d90Default();
}

std::vector<CameraProfile> ConfigManager::loadAllProfiles() const {
    // Start with built-ins.
    auto profiles = CameraProfile::getBuiltinProfiles();
    std::set<std::string> seen;
    for (const auto& p : profiles) seen.insert(p.profileId);

    // Merge user JSON files; user file wins on profile_id conflict.
    std::string profileDir = configDir + "profiles/";
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(profileDir, ec)) {
        if (entry.path().extension() != ".json") continue;
        try {
            CameraProfile p = CameraProfile::loadFromFile(entry.path().string());
            if (!p.isValid() || p.profileId.empty()) continue;
            if (seen.count(p.profileId)) {
                // Replace built-in with user override.
                auto it = std::find_if(profiles.begin(), profiles.end(),
                    [&](const CameraProfile& b){ return b.profileId == p.profileId; });
                if (it != profiles.end()) *it = p;
            } else {
                profiles.push_back(p);
                seen.insert(p.profileId);
            }
        } catch (...) {}
    }
    return profiles;
}

// ---------------------------------------------------------------------------
// Camera profile JSON serialisation — mirrors CameraProfile::saveToFile /
// loadFromFile but operates on json objects rather than files, so the profile
// can be embedded inline inside the manifest document.
// ---------------------------------------------------------------------------

static json profileToJson(const CameraProfile& p) {
    json j;
    j["name"]               = p.name;
    j["profile_id"]         = p.profileId;
    j["filename_regex"]     = p.filenameRegex;
    j["timestamp_format"]   = p.timestampFormat;
    j["timestamp_source"]   = p.timestampSource;
    j["timestamp_timezone"] = p.timestampTimezone;
    if (p.timestampCaptureGroup != 1) j["timestamp_capture_group"] = p.timestampCaptureGroup;
    if (p.tokenCaptureGroup     != 2) j["token_capture_group"]     = p.tokenCaptureGroup;
    j["container_ext"]      = p.containerExt;
    j["thumbnail_method"]   = p.thumbnailMethod;
    j["gps_method"]          = p.gpsMethod;
    if (!p.gpsExiftoolArgs.empty())
        j["gps_exiftool_args"] = p.gpsExiftoolArgs;
    j["default_layout"]     = p.defaultLayout;
    json slotArr = json::array();
    for (const auto& s : p.cameraSlots) {
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
    return j;
}

static CameraProfile profileFromJson(const json& j) {
    CameraProfile p;
    p.name                  = j.value("name",                    "");
    p.profileId             = j.value("profile_id",              "");
    p.filenameRegex         = j.value("filename_regex",          "");
    p.timestampFormat       = j.value("timestamp_format",        "");
    p.timestampSource       = j.value("timestamp_source",        "filename");
    p.timestampTimezone     = j.value("timestamp_timezone",      "utc");
    p.timestampCaptureGroup = j.value("timestamp_capture_group", 1);
    p.tokenCaptureGroup     = j.value("token_capture_group",     2);
    p.containerExt          = j.value("container_ext",           "");
    p.thumbnailMethod       = j.value("thumbnail_method",        "replace_ext");
    p.gpsMethod             = j.value("gps_method",              "none");
    p.gpsExiftoolArgs       = j.value("gps_exiftool_args",       "");
    p.defaultLayout         = j.value("default_layout",          "2x2");
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

std::string ConfigManager::getManifestProfileId(const std::string& sourcePath) const {
    std::string mf = const_cast<ConfigManager*>(this)->lookupManifestFilePath(sourcePath);
    if (mf.empty()) return "";
    std::ifstream f(mf);
    if (!f.is_open()) return "";
    try {
        json j; f >> j;
        return j.value("profile_id", "");
    } catch (...) { return ""; }
}

CameraProfile ConfigManager::getManifestProfile(const std::string& sourcePath) const {
    std::string mf = const_cast<ConfigManager*>(this)->lookupManifestFilePath(sourcePath);
    if (mf.empty()) return getCameraProfile();
    std::ifstream f(mf);
    if (!f.is_open()) return getCameraProfile();
    json j;
    try { f >> j; } catch (...) { return getCameraProfile(); }

    // Prefer the full embedded profile snapshot.
    if (j.contains("camera_profile") && j["camera_profile"].is_object()) {
        CameraProfile p = profileFromJson(j["camera_profile"]);
        if (p.isValid()) return p;
    }

    // Fall back to profile_id string — look up user file then built-ins.
    std::string pid = j.value("profile_id", "");
    if (!pid.empty()) {
        std::string profileFile = configDir + "profiles/" + pid + ".json";
        if (fs::exists(profileFile)) {
            try {
                CameraProfile p = CameraProfile::loadFromFile(profileFile);
                if (p.isValid()) return p;
            } catch (...) {}
        }
        for (const auto& p : CameraProfile::getBuiltinProfiles())
            if (p.profileId == pid) return p;
    }

    return getCameraProfile();
}

// ---------------------------------------------------------------------------
// Host overlay — pathmux_<hostname>.json
// ---------------------------------------------------------------------------

void ConfigManager::loadHostOverlay() {
    std::ifstream ifs(hostSettingsFile);
    if (!ifs.is_open()) return;   // no host file — fine, use base settings

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error&) {
        std::cerr << "Warning: " << hostSettingsFile
                  << " is corrupt — host overlay skipped.\n";
        return;
    }
    if (j.empty()) return;

    // Only override fields that are actually present in the host file.
    if (j.contains("exiftoolPath"))    settings.exiftoolPath    = j["exiftoolPath"];
    if (j.contains("ffmpegPath"))      settings.ffmpegPath      = j["ffmpegPath"];
    if (j.contains("defaultExportDir")) settings.defaultExportDir = j["defaultExportDir"];
    if (j.contains("tmpDir"))          settings.tmpDir          = j["tmpDir"];
    if (j.contains("logLevel"))        settings.logLevel        = j["logLevel"];
    if (j.contains("uiScale"))         settings.uiScale         = j["uiScale"];
    if (j.contains("hudFontScale"))    settings.hudFontScale    = j["hudFontScale"];
    if (j.contains("hudLineScale"))    settings.hudLineScale    = j["hudLineScale"];
    if (j.contains("hudColor"))        settings.hudColor        = j["hudColor"];

    if (j.contains("encode") && j["encode"].is_object()) {
        const auto& e = j["encode"];
        settings.encode.preset          = e.value("preset",           settings.encode.preset);
        settings.encode.hwDevice        = e.value("hwDevice",         settings.encode.hwDevice);
        settings.encode.hwDeviceType    = e.value("hwDeviceType",     settings.encode.hwDeviceType);
        settings.encode.normEncoder     = e.value("normEncoder",      settings.encode.normEncoder);
        settings.encode.collageEncoder  = e.value("collageEncoder",   settings.encode.collageEncoder);
        settings.encode.downEncoder     = e.value("downEncoder",      settings.encode.downEncoder);
        settings.encode.pixFmt          = e.value("pixFmt",           settings.encode.pixFmt);
        settings.encode.normQuality     = e.value("normQuality",      settings.encode.normQuality);
        settings.encode.collageQuality  = e.value("collageQuality",   settings.encode.collageQuality);
        settings.encode.downQuality     = e.value("downQuality",      settings.encode.downQuality);
        settings.encode.extraNormArgs   = e.value("extraNormArgs",    settings.encode.extraNormArgs);
        settings.encode.extraCollageArgs= e.value("extraCollageArgs", settings.encode.extraCollageArgs);
        settings.encode.extraDownArgs   = e.value("extraDownArgs",    settings.encode.extraDownArgs);
    }
}

void ConfigManager::saveHostSettings() {
    json j;
    j["exiftoolPath"]     = settings.exiftoolPath;
    j["ffmpegPath"]       = settings.ffmpegPath;
    j["defaultExportDir"] = settings.defaultExportDir;
    j["tmpDir"]           = settings.tmpDir;
    j["logLevel"]         = settings.logLevel;
    j["uiScale"]          = settings.uiScale;
    j["hudFontScale"]     = settings.hudFontScale;
    j["hudLineScale"]     = settings.hudLineScale;
    j["hudColor"]         = settings.hudColor;

    json e;
    e["preset"]            = settings.encode.preset;
    e["hwDevice"]          = settings.encode.hwDevice;
    e["hwDeviceType"]      = settings.encode.hwDeviceType;
    e["normEncoder"]       = settings.encode.normEncoder;
    e["collageEncoder"]    = settings.encode.collageEncoder;
    e["downEncoder"]       = settings.encode.downEncoder;
    e["pixFmt"]            = settings.encode.pixFmt;
    e["normQuality"]       = settings.encode.normQuality;
    e["collageQuality"]    = settings.encode.collageQuality;
    e["downQuality"]       = settings.encode.downQuality;
    e["extraNormArgs"]     = settings.encode.extraNormArgs;
    e["extraCollageArgs"]  = settings.encode.extraCollageArgs;
    e["extraDownArgs"]     = settings.encode.extraDownArgs;
    j["encode"]            = e;

    ensureConfigDir();
    std::ofstream ofs(hostSettingsFile);
    if (!ofs.is_open()) {
        std::cerr << "Warning: Could not write " << hostSettingsFile << "\n";
        return;
    }
    ofs << j.dump(2) << "\n";
}

void ConfigManager::reloadHostSettings() {
    loadHostOverlay();
}

void ConfigManager::setGapThreshold(int seconds) {
    if (seconds < 30) {
        std::cerr << "Warning: Gap threshold below 30s is impractical; clamping to 30.\n";
        seconds = 30;
    }
    settings.gapThresholdSeconds = seconds;
    saveSettings();
}

void ConfigManager::showSettings() const {
    auto fmtTime = [](int s) -> std::string {
        if (s < 60) return std::to_string(s) + "s";
        if (s < 3600) {
            int m = s / 60, r = s % 60;
            return std::to_string(m) + "m" + (r ? " " + std::to_string(r) + "s" : "");
        }
        int h = s / 3600, m = (s % 3600) / 60;
        return std::to_string(h) + "h" + (m ? " " + std::to_string(m) + "m" : "");
    };

    std::cout << "\n--- PathMux Settings (" << settingsFile << ") ---\n";
    std::cout << std::left
              << std::setw(28) << "gapThresholdSeconds"
              << settings.gapThresholdSeconds
              << "  (" << fmtTime(settings.gapThresholdSeconds) << ")\n"
              << std::setw(28) << "fuzzyWindowSeconds"
              << settings.fuzzyWindowSeconds
              << "  (±" << settings.fuzzyWindowSeconds << "s)\n"
              << std::setw(28) << "exiftoolPath"
              << (settings.exiftoolPath.empty() ? "exiftool (system)" : settings.exiftoolPath) << "\n"
              << std::setw(28) << "ffmpegPath"
              << (settings.ffmpegPath.empty()   ? "ffmpeg (system)"   : settings.ffmpegPath)   << "\n"
              << std::setw(28) << "defaultExportDir"
              << (settings.defaultExportDir.empty() ? "(current directory)" : settings.defaultExportDir) << "\n"
              << std::setw(28) << "timestampFormat"
              << settings.timestampFormat << "\n"
              << std::setw(28) << "timeDisplay"
              << settings.timeDisplay << "\n"
              << std::setw(28) << "videoFormat"
              << settings.videoFormat << "\n"
              << std::setw(28) << "defaultAudioSource"
              << settings.defaultAudioSource << "\n"
              << std::setw(28) << "schemaVersion"
              << settings.schemaVersion << "\n";
}

// ---------------------------------------------------------------------------
// Known Locations — locations.json
// ---------------------------------------------------------------------------

std::vector<NamedLocation> ConfigManager::loadLocations() {
    std::vector<NamedLocation> locs;
    std::ifstream ifs(locationsFile);
    if (!ifs.is_open()) return locs;

    json j;
    try { ifs >> j; } catch (...) {
        std::cerr << "Warning: locations.json is corrupt — ignoring.\n";
        return locs;
    }

    if (!j.is_array()) return locs;
    for (const auto& jl : j) {
        NamedLocation loc;
        loc.name         = jl.value("name",         "");
        loc.lat          = jl.value("lat",           0.0);
        loc.lon          = jl.value("lon",           0.0);
        loc.radiusMetres = jl.value("radiusMetres",  50);
        loc.icon         = jl.value("icon",          "pin");
        if (!loc.name.empty()) locs.push_back(loc);
    }
    return locs;
}

void ConfigManager::saveLocations(const std::vector<NamedLocation>& locs) {
    json j = json::array();
    for (const auto& loc : locs) {
        json jl;
        jl["name"]         = loc.name;
        jl["lat"]          = loc.lat;
        jl["lon"]          = loc.lon;
        jl["radiusMetres"] = loc.radiusMetres;
        jl["icon"]         = loc.icon;
        j.push_back(jl);
    }
    std::ofstream ofs(locationsFile);
    if (!ofs.is_open()) {
        std::cerr << "Warning: Could not write " << locationsFile << "\n";
        return;
    }
    ofs << j.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
// ID generation and normalization
// ---------------------------------------------------------------------------

// Safe base36 alphabet — I, O, L excluded from generation to eliminate
// lookalike ambiguity.  Normalizer maps user input I→1, O→0, L→1.
static const std::string B36_DIGITS = "0123456789ABCDEFGHJKMNPQRSTUVWXYZ";  // no I O L
static const std::string B36_ALPHA  = "ABCDEFGHJKMNPQRSTUVWXYZ";            // no I O L
static const std::string B36_NUM    = "0123456789";

std::string ConfigManager::normalizeId(const std::string& input) {
    std::string out;
    for (char c : input) {
        char u = std::toupper((unsigned char)c);
        if      (u == 'O') u = '0';
        else if (u == 'I') u = '1';
        else if (u == 'L') u = '1';
        out += u;
    }
    return out;
}

std::string ConfigManager::generateId(const std::set<std::string>& existing,
                                       bool preferAlphaFirst) {
    // Try up to 200 random candidates before falling back to sequential scan
    const std::string& firstPool = preferAlphaFirst ? B36_ALPHA : B36_NUM;
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<> d0(0, (int)firstPool.size() - 1);
    std::uniform_int_distribution<> d1(0, (int)B36_DIGITS.size() - 1);

    for (int attempt = 0; attempt < 200; ++attempt) {
        std::string id;
        id += firstPool[d0(rng)];
        id += B36_DIGITS[d1(rng)];
        if (!existing.count(id)) return id;
    }
    // Exhaustive fallback (shouldn't happen at these scales)
    for (char c1 : firstPool)
        for (char c2 : B36_DIGITS) {
            std::string id; id += c1; id += c2;
            if (!existing.count(id)) return id;
        }
    return "??"; // should never reach here
}

// ---------------------------------------------------------------------------
// Path portability helpers (file-scope statics)
// ---------------------------------------------------------------------------

static std::string normalizeSep(const std::string& p) {
    std::string s = p;
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

static bool isAbsolutePath(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/') return true;
    if (p.size() >= 3 && std::isalpha((unsigned char)p[0]) &&
        p[1] == ':' && (p[2] == '/' || p[2] == '\\')) return true;
    return false;
}

// Strip sourceRoot prefix from absPath and normalize separators to /.
// Returns normalized absPath unchanged if it is not under sourceRoot.
static std::string makeRelPath(const std::string& absPath,
                               const std::string& sourceRoot) {
    std::string p = normalizeSep(absPath);
    std::string r = normalizeSep(sourceRoot);
    while (!r.empty() && r.back() == '/') r.pop_back();
    if (!r.empty() && p.size() > r.size() &&
        p.rfind(r, 0) == 0 && p[r.size()] == '/')
        return p.substr(r.size() + 1);
    return p;
}

// Resolve path against localRoot.  If path is already absolute, first try
// substituting any other host root from pathMap so cross-platform
// absolute paths translate correctly (e.g. Linux path read on Windows).
static std::string resolvePathWithMap(const std::string& p,
                                      const std::string& localRoot,
                                      const json& pathMap) {
    if (!isAbsolutePath(p))
        return localRoot.empty() ? p : (localRoot + "/" + p);

    std::string np = normalizeSep(p);
    std::string nr = normalizeSep(localRoot);
    while (!nr.empty() && nr.back() == '/') nr.pop_back();

    // Already under our root — use as-is.
    if (!nr.empty() && np.rfind(nr + "/", 0) == 0) return p;

    // Try substituting another host's root from path_map.
    for (const auto& [k, v] : pathMap.items()) {
        if (!v.is_string()) continue;
        std::string other = normalizeSep(v.get<std::string>());
        while (!other.empty() && other.back() == '/') other.pop_back();
        if (!other.empty() && np.rfind(other + "/", 0) == 0) {
            std::string rel = np.substr(other.size() + 1);
            return nr.empty() ? rel : (nr + "/" + rel);
        }
    }

    return p; // can't translate — return as-is
}

// Build the path_map key for the current machine: "os_hostname".
static std::string pathMapKey(const std::string& shortHostname) {
#ifdef _WIN32
    const std::string os = "windows";
#elif defined(__APPLE__)
    const std::string os = "macos";
#else
    const std::string os = "linux";
#endif
    std::string h = shortHostname;
    std::transform(h.begin(), h.end(), h.begin(),
        [](char c){ return (char)std::tolower((unsigned char)c); });
    return os + "_" + h;
}

// ---------------------------------------------------------------------------
// Path sanitization
// ---------------------------------------------------------------------------

std::string ConfigManager::sanitizePath(const std::string& path) {
    std::string s = path;
    // Strip trailing slash
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    // Strip leading slash
    if (!s.empty() && s[0] == '/') s.erase(0, 1);
    // Replace remaining slashes with underscores
    std::replace(s.begin(), s.end(), '/', '_');
    return s;
}

// ---------------------------------------------------------------------------
// Manifest file path
// ---------------------------------------------------------------------------

std::string ConfigManager::ensureManifestId(const std::string& sourcePath) {
    auto index = loadManifestIndex();

    // 1. Already in our index — done.
    for (const auto& e : index)
        if (e.path == sourcePath) return e.id;

    // 2. Scan directory for existing pm_manifest_XX.json files.
    //    Adopt the best one rather than minting a new ID, so all platforms
    //    converge on one manifest file per footage directory.
    std::error_code ec;
    if (fs::exists(sourcePath, ec)) {
        struct Candidate {
            std::string id, file;
            int tripCount = 0;
            int priority  = 0; // 2=has our key, 1=new-format other host, 0=old format
        };
        std::vector<Candidate> candidates;
        std::string key = pathMapKey(hostname);

        // IDs already used for other paths — must not steal them.
        std::set<std::string> indexedIds;
        for (const auto& e : index) indexedIds.insert(normalizeId(e.id));

        for (const auto& de : fs::directory_iterator(sourcePath, ec)) {
            std::string fname = de.path().filename().string();
            // Must match "pm_manifest_XX.json" exactly (19 chars, 2-char base36 ID)
            if (fname.size() != 19) continue;
            if (fname.rfind("pm_manifest_", 0) != 0) continue;
            if (fname.substr(fname.size() - 5) != ".json") continue;
            std::string id = fname.substr(12, 2);
            if (!std::isalnum((unsigned char)id[0]) ||
                !std::isalnum((unsigned char)id[1])) continue;
            if (indexedIds.count(normalizeId(id))) continue; // ID belongs to another path

            Candidate c;
            c.id   = id;
            c.file = de.path().string();
            try {
                std::ifstream ifs(c.file);
                if (ifs.is_open()) {
                    json j; ifs >> j;
                    if (j.contains("trips") && j["trips"].is_array())
                        c.tripCount = (int)j["trips"].size();
                    if (j.contains("path_map") && j["path_map"].is_object()) {
                        c.priority = j["path_map"].contains(key) ? 2 : 1;
                    }
                }
            } catch (...) {}
            candidates.push_back(c);
        }

        if (!candidates.empty()) {
            std::stable_sort(candidates.begin(), candidates.end(),
                [](const Candidate& a, const Candidate& b) {
                    return a.priority != b.priority ? a.priority > b.priority
                                                    : a.tripCount > b.tripCount;
                });
            const auto& w = candidates[0];
            ManifestEntry newEntry;
            newEntry.id           = w.id;
            newEntry.path         = sourcePath;
            newEntry.manifestFile = w.file;
            index.push_back(newEntry);
            saveManifestIndex(index);
            return w.id;
        }
    }

    // 3. Nothing adoptable — mint a fresh ID.
    std::set<std::string> existing;
    for (const auto& e : index) existing.insert(e.id);
    std::string id = generateId(existing, true);
    ManifestEntry newEntry;
    newEntry.id   = id;
    newEntry.path = sourcePath;
    index.push_back(newEntry);
    saveManifestIndex(index);
    return id;
}

std::string ConfigManager::lookupManifestFilePath(const std::string& sourcePath) {
    std::string id = getManifestIdForPath(sourcePath);
    if (id.empty()) return "";

    std::string filename  = "pm_manifest_" + id + ".json";
    std::string colocated = sourcePath + "/" + filename;
    if (fs::exists(colocated)) return colocated;

    std::string inConfig = configDir + filename;
    if (fs::exists(inConfig)) return inConfig;

    // Migration: look for old-style sanitized-path file and rename it
    std::string oldName = "pm_manifest_" + sanitizePath(sourcePath) + ".json";
    std::vector<std::string> candidates = {sourcePath + "/" + oldName,
                                           configDir + oldName};
    for (const std::string& loc : candidates) {
        if (fs::exists(loc)) {
            std::string newLoc = colocated;
            std::error_code ec;
            fs::rename(loc, newLoc, ec);
            if (ec) { newLoc = inConfig; fs::rename(loc, newLoc, ec); }
            if (!ec) {
                auto idx = loadManifestIndex();
                for (auto& e : idx)
                    if (e.path == sourcePath) { e.manifestFile = newLoc; break; }
                saveManifestIndex(idx);
                return newLoc;
            }
        }
    }
    return "";
}

std::string ConfigManager::getManifestFilePath(const std::string& sourcePath) {
    std::string id       = ensureManifestId(sourcePath);
    std::string filename = "pm_manifest_" + id + ".json";
    std::string preferred = sourcePath + "/" + filename;

    std::error_code ec;
    if (fs::exists(sourcePath, ec)) {
        std::string testFile = sourcePath + "/.pm_write_test";
        std::ofstream test(testFile);
        if (test.is_open()) {
            test.close();
            fs::remove(testFile, ec);
            return preferred;
        }
    }
    std::cerr << "  Warning: " << sourcePath << " is not writable.\n"
              << "  Manifest will be stored in " << configDir << "\n";
    return configDir + filename;
}

// ---------------------------------------------------------------------------
// MD5 computation
// ---------------------------------------------------------------------------

std::string ConfigManager::fileMd5(const std::string& filePath) {
#ifdef _WIN32
    // certutil -hashfile output (3 non-empty lines):
    //   line 0: "MD5 hash of <file>:"
    //   line 1: the hash (uppercase, possibly space-separated bytes)
    //   line 2: "CertUtil: -hashfile command completed successfully."
    std::string cmd = "certutil -hashfile \"" + filePath + "\" MD5 2>NUL";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    std::vector<std::string> lines;
    char buf[256] = {};
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    pclose(pipe);
    if (lines.size() >= 2) {
        std::string hash = lines[1];
        // remove any spaces certutil inserts between byte pairs
        hash.erase(std::remove(hash.begin(), hash.end(), ' '), hash.end());
        // normalize to lowercase to match md5sum output format
        for (char& c : hash) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return hash;
    }
    return "";
#else
    // Use system md5sum command — available on all Linux/macOS distros
    std::string cmd = "md5sum '" + filePath + "' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[64] = {};
    if (fgets(buf, sizeof(buf), pipe)) {
        pclose(pipe);
        std::string result(buf);
        // md5sum output: "<hash>  <filename>"
        auto pos = result.find(' ');
        if (pos != std::string::npos) return result.substr(0, pos);
    } else {
        pclose(pipe);
    }
    return "";
#endif
}

// ---------------------------------------------------------------------------
// Trip identity hash — metadata only, no file I/O
// Used to match trips across rescans for ID preservation.
// ---------------------------------------------------------------------------

std::string ConfigManager::tripIdentityHash(const Trip& trip) {
    // Hash: startEpoch + segment count + first segment filename
    std::string key = std::to_string(trip.startEpoch)
                    + "|" + std::to_string(trip.segments.size());
    if (!trip.segments.empty())
        key += "|" + camPath(trip.segments[0], "front");
    // Simple djb2 hash, returned as 8-char hex
    uint32_t hash = 5381;
    for (char c : key) hash = ((hash << 5) + hash) + (unsigned char)c;
    char buf[16];
    snprintf(buf, sizeof(buf), "%08x", hash);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Validation file selection — deterministic, seeded on startEpoch
// Picks 3 files from the flattened pool of all .ts and .jpg in the trip.
// ---------------------------------------------------------------------------

std::vector<ValidationFile> ConfigManager::selectValidationFiles(
    const Trip& trip, const std::string& sourcePath)
{
    // Build flat list of relative paths for all non-placeholder files
    std::vector<std::string> pool;
    for (const auto& seg : trip.segments) {
        for (const auto& [camName, f] : seg.cameras) {
            if (f != "-" && !f.empty()) {
                // Make relative to sourcePath
                std::string rel = f;
                if (rel.find(sourcePath) == 0)
                    rel = rel.substr(sourcePath.size());
                if (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
                pool.push_back(rel);
            }
        }
    }
    if (pool.empty()) return {};

    // Deterministic shuffle seeded on startEpoch
    std::mt19937 rng((uint32_t)trip.startEpoch);
    std::shuffle(pool.begin(), pool.end(), rng);

    std::vector<ValidationFile> result;
    int count = std::min(3, (int)pool.size());
    for (int i = 0; i < count; ++i) {
        ValidationFile vf;
        vf.relPath = pool[i];
        vf.md5     = fileMd5(sourcePath + "/" + pool[i]);
        result.push_back(vf);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Master manifest index — manifests.json
// ---------------------------------------------------------------------------

std::vector<ManifestEntry> ConfigManager::loadManifestIndex() {
    std::vector<ManifestEntry> index;
    std::string idxFile = configDir + "manifests.json";
    std::ifstream ifs(idxFile);
    if (!ifs.is_open()) return index;

    json j;
    try { ifs >> j; } catch (...) {
        std::cerr << "Warning: manifests.json is corrupt — rebuilding.\n";
        return index;
    }

    if (!j.contains("manifests") || !j["manifests"].is_array()) return index;
    bool needsRewrite = false;
    for (const auto& jm : j["manifests"]) {
        ManifestEntry e;
        e.id           = jm.value("id",           "");
        e.path         = jm.value("path",         "");
        e.manifestFile = jm.value("manifest_file","");
        e.lastScan     = jm.value("last_scan",    "");
        e.tripCount    = jm.value("trip_count",   0);
        e.firstTrip    = jm.value("first_trip",   "");
        e.lastTrip     = jm.value("last_trip",    "");
        e.manifestMd5  = jm.value("manifest_md5", "");
        e.note         = jm.value("note",         "");
        e.nickname     = jm.value("nickname",     e.path);  // default to path
        if (e.path.empty()) continue;
        // Discard bogus entries where path is a manifest file, not a source directory.
        // These are created when a manifest file path is mistakenly passed as a source path.
        if (e.path.find("pm_manifest_") != std::string::npos) {
            std::cerr << "Note: removing malformed index entry with file path as source: "
                      << e.path << "\n";
            // Clean up the orphaned manifest file if it exists in the config dir
            if (!e.manifestFile.empty()) {
                std::error_code ec;
                fs::remove(e.manifestFile, ec);
            }
            needsRewrite = true;
            continue;
        }
        index.push_back(e);
    }
    if (needsRewrite) saveManifestIndex(index);
    // Sort by lastTrip descending — most recently used manifest first
    std::sort(index.begin(), index.end(), [](const ManifestEntry& a, const ManifestEntry& b) {
        return a.lastTrip > b.lastTrip;
    });
    return index;
}

void ConfigManager::saveManifestIndex(const std::vector<ManifestEntry>& index) {
    std::string idxFile = configDir + "manifests.json";
    json root;
    root["manifests"] = json::array();
    for (const auto& e : index) {
        json jm;
        jm["id"]            = e.id;
        jm["path"]          = e.path;
        jm["manifest_file"] = e.manifestFile;
        jm["last_scan"]     = e.lastScan;
        jm["trip_count"]    = e.tripCount;
        jm["first_trip"]    = e.firstTrip;
        jm["last_trip"]     = e.lastTrip;
        jm["manifest_md5"]  = e.manifestMd5;
        jm["note"]          = e.note;
        jm["nickname"]      = e.nickname.empty() ? e.path : e.nickname;
        root["manifests"].push_back(jm);
    }
    std::ofstream ofs(idxFile);
    if (!ofs.is_open()) {
        std::cerr << "Warning: Could not write manifests.json\n";
        return;
    }
    ofs << root.dump(2) << "\n";
}

void ConfigManager::saveManifestNote(const std::string& path,
                                      const std::string& note) {
    if (path.find("pm_manifest_") != std::string::npos) {
        std::cerr << "Bug: saveManifestNote called with manifest file path: '"
                  << path << "' — ignoring.\n";
        return;
    }
    std::string manifestFile = getManifestFilePath(path);

    // Read existing manifest, update note, rewrite
    json root;
    std::ifstream ifs(manifestFile);
    if (ifs.is_open()) {
        try { ifs >> root; } catch (...) {}
        ifs.close();
    }
    root["note"] = note;

    std::ofstream ofs(manifestFile);
    if (!ofs.is_open()) {
        std::cerr << "Warning: Could not write manifest: " << manifestFile << "\n";
        return;
    }
    ofs << root.dump(2) << "\n";
    ofs.close();

    // Update note and recompute md5 in the index
    auto index = loadManifestIndex();
    for (auto& e : index) {
        if (e.path == path) {
            e.note        = note;
            e.manifestMd5 = fileMd5(manifestFile);
            break;
        }
    }
    saveManifestIndex(index);
}

void ConfigManager::setManifestNickname(const std::string& id,
                                        const std::string& nickname) {
    auto index = loadManifestIndex();
    for (auto& e : index) {
        if (normalizeId(e.id) == normalizeId(id)) {
            e.nickname = nickname;
            break;
        }
    }
    saveManifestIndex(index);
}

void ConfigManager::updateManifestIndex(const std::string& path,
                                         const std::vector<Trip>& trips) {
    auto index = loadManifestIndex();

    // Entry is guaranteed to exist — ensureManifestId() was called via
    // getManifestFilePath() earlier in saveTripCache().
    ManifestEntry* entry = nullptr;
    for (auto& e : index) {
        if (e.path == path) { entry = &e; break; }
    }
    if (!entry) {
        std::cerr << "Warning: updateManifestIndex: no index entry for " << path << "\n";
        return;
    }

    std::string manifestFile = getManifestFilePath(path);

    // Update dynamic fields
    entry->manifestFile = manifestFile;
    entry->lastScan     = []() -> std::string {
        auto t = std::time(nullptr);
        std::tm tmBuf{};
        localtime_r(&t, &tmBuf);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmBuf);
        return std::string(buf);
    }();
    entry->tripCount = (int)trips.size();
    entry->firstTrip = trips.empty() ? "" : trips.front().date + " " + trips.front().startTime;
    entry->lastTrip  = trips.empty() ? "" : trips.back().date  + " " + trips.back().startTime;
    entry->manifestMd5 = fileMd5(manifestFile);

    saveManifestIndex(index);

    // Also update lastpath
    setLastPath(path);
}

// ---------------------------------------------------------------------------
// Startup manifest index validation
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// nowIso8601 — current local time as ISO 8601 string (YYYY-MM-DDTHH:MM:SS).
// ---------------------------------------------------------------------------
static std::string nowIso8601() {
    time_t t = std::time(nullptr);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char buf[25];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return std::string(buf);
}

bool ConfigManager::validateManifestIndexReport() {
    auto index = loadManifestIndex();
    if (index.empty()) {
        std::cout << "  No manifests in index.\n";
        return true;
    }

    bool allOk = true;
    for (const auto& e : index) {
        std::string f    = lookupManifestFilePath(e.path);
        bool fileExists  = !f.empty();
        bool md5ok       = true;
        if (fileExists && !e.manifestMd5.empty())
            md5ok = (fileMd5(f) == e.manifestMd5);

        std::string status;
        if (!fileExists)  status = "MISSING";
        else if (!md5ok)  status = "MODIFIED";
        else              status = "ok";

        std::cout << "  [" << e.id << "]  "
                  << std::left << std::setw(8) << status
                  << "  " << e.path << "\n";
        if (status != "ok") allOk = false;
    }
    return allOk;
}

bool ConfigManager::validateManifestIndex() {
    auto index = loadManifestIndex();
    if (index.empty()) return true;

    // Pass 1: resolve actual file path for each entry (handles ID-based migration).
    // Entries whose manifest file is missing are silently pruned — a missing file
    // just means "not yet scanned"; no user action required.
    {
        std::vector<int> missing;
        for (int i = 0; i < (int)index.size(); ++i) {
            std::string f = lookupManifestFilePath(index[i].path);
            if (f.empty())
                missing.push_back(i);
            else
                index[i].manifestFile = f; // update to resolved (possibly migrated) path
        }
        if (!missing.empty()) {
            // Append pruned entries to the stale archive before removing them.
            json stale = json::array();
            {
                std::ifstream sfs(staleArchiveFile);
                if (sfs.is_open()) {
                    try { sfs >> stale; } catch (...) { stale = json::array(); }
                }
            }
            std::string pruneTs = nowIso8601();
            for (int i : missing) {
                const auto& e = index[i];
                json entry;
                entry["id"]           = e.id;
                entry["path"]         = e.path;
                entry["manifestFile"] = e.manifestFile;
                entry["lastScan"]     = e.lastScan;
                entry["tripCount"]    = e.tripCount;
                entry["note"]         = e.note;
                entry["pruned"]       = pruneTs;
                stale.push_back(entry);
            }
            {
                std::ofstream sofs(staleArchiveFile);
                if (sofs.is_open()) sofs << stale.dump(2) << "\n";
            }
            for (int i = (int)missing.size() - 1; i >= 0; --i)
                index.erase(index.begin() + missing[i]);
            saveManifestIndex(index);
            std::cout << "  Note: " << missing.size()
                      << " stale index entr" << (missing.size() == 1 ? "y" : "ies")
                      << " archived (use --show-stale to review).\n";
        }
    }
    if (index.empty()) return true;

    // Pass 2: check md5 for entries whose file exists.  Prompt only on mismatch
    // (file exists but was modified outside PathMux — worth flagging).
    std::vector<int> modified;
    for (int i = 0; i < (int)index.size(); ++i) {
        const auto& e = index[i];
        if (!e.manifestMd5.empty() && fileMd5(e.manifestFile) != e.manifestMd5)
            modified.push_back(i);
    }
    if (modified.empty()) return true;

    // Process in reverse so erase-by-index stays valid
    for (int k = (int)modified.size() - 1; k >= 0; --k) {
        int i = modified[k];
        const auto& e = index[i];
        std::cout << "\n  Warning: Manifest modified outside PathMux\n"
                  << "    Path: " << e.path << "\n"
                  << "    File: " << e.manifestFile << "\n"
                  << "\n  [I]  Ignore for this session\n"
                     "  [X]  Remove from manifest index\n"
                     "  [Q]  Quit\n"
                     "\nChoice: ";
        std::string choice;
        std::getline(std::cin >> std::ws, choice);
        std::transform(choice.begin(), choice.end(), choice.begin(), ::toupper);
        if (choice == "Q") return false;
        if (choice == "X") {
            index.erase(index.begin() + i);
            saveManifestIndex(index);
            std::cout << "  Removed from index.\n";
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// isCached
// ---------------------------------------------------------------------------

bool ConfigManager::isCached(const std::string& path) {
    if (path.empty()) return false;
    std::string f = lookupManifestFilePath(path);
    return !f.empty();
}

// ---------------------------------------------------------------------------
// Last-used path
// ---------------------------------------------------------------------------

void ConfigManager::setLastPath(const std::string& path) {
    std::ofstream ofs(configDir + "lastpath");
    if (ofs.is_open()) ofs << path;
}

std::string ConfigManager::getLastPath() {
    std::ifstream ifs(configDir + "lastpath");
    std::string p;
    if (std::getline(ifs, p)) return p;
    return "";
}

// ---------------------------------------------------------------------------
// Manifest listing
// ---------------------------------------------------------------------------

void ConfigManager::listCachedManifests() {
    auto index = loadManifestIndex();
    std::cout << "\n--- Cached Manifests ---\n";
    if (index.empty()) { std::cout << "  (No manifests found)\n"; return; }
    std::cout << std::left
              << std::setw(4)  << "ID"
              << std::setw(12) << "Trips"
              << std::setw(22) << "First Trip"
              << std::setw(22) << "Last Trip"
              << "Path\n";
    std::cout << std::string(70, '-') << "\n";
    for (const auto& e : index) {
        std::cout << std::left
                  << std::setw(4)  << e.id
                  << std::setw(12) << e.tripCount
                  << std::setw(22) << e.firstTrip
                  << std::setw(22) << e.lastTrip
                  << e.path << "\n";
    }
}

std::string ConfigManager::getManifestIdForPath(const std::string& path) {
    auto index = loadManifestIndex();
    for (const auto& e : index)
        if (e.path == path) return e.id;
    return "";
}

void ConfigManager::updateManifestMd5(const std::string& manifestFile) {
    auto index = loadManifestIndex();
    for (auto& e : index) {
        if (e.manifestFile == manifestFile) {
            e.manifestMd5 = fileMd5(manifestFile);
            saveManifestIndex(index);
            return;
        }
    }
}

std::vector<std::string> ConfigManager::getAllCachedPaths() {
    auto index = loadManifestIndex();
    std::vector<std::string> paths;
    for (const auto& e : index) paths.push_back(e.path);
    return paths;
}

// ---------------------------------------------------------------------------
// saveTripCache
// ---------------------------------------------------------------------------

void ConfigManager::saveTripCache(const std::string& path,
                                   const std::vector<Trip>& trips,
                                   const CameraProfile& profile) {
    // Guard: reject manifest file paths passed as source directories.
    // A manifest file path (e.g. /foo/pm_manifest_AB.json) must never be
    // used as a source path — doing so creates a bogus duplicate index entry.
    if (path.find("pm_manifest_") != std::string::npos) {
        std::cerr << "Bug: saveTripCache called with manifest file path: '"
                  << path << "' — ignoring to prevent duplicate index entry.\n";
        return;
    }
    std::string manifestFile = getManifestFilePath(path);

    // Load existing manifest to preserve note and trip IDs
    auto existingTrips = loadTripCache(path);
    std::set<std::string> usedIds;
    for (const auto& t : existingTrips) usedIds.insert(t.id);

    // Preserve existing manifest-level note, profile_id, camera_profile, and path_map.
    std::string existingNote;
    std::string existingProfileId;
    json existingPathMap        = json::object();
    json existingCameraProfile  = json::object();
    {
        std::ifstream nifs(manifestFile);
        if (nifs.is_open()) {
            try {
                json existing; nifs >> existing;
                existingNote      = existing.value("note",       "");
                existingProfileId = existing.value("profile_id", "");
                if (existing.contains("path_map") && existing["path_map"].is_object())
                    existingPathMap = existing["path_map"];
                if (existing.contains("camera_profile") && existing["camera_profile"].is_object())
                    existingCameraProfile = existing["camera_profile"];
            } catch (...) {}
        }
    }
    // Register this machine in path_map.
    existingPathMap[pathMapKey(hostname)] = path;

    // Build identity hash → id map from existing trips
    std::map<std::string, std::string> hashToId;
    for (const auto& t : existingTrips)
        if (!t.id.empty())
            hashToId[tripIdentityHash(t)] = t.id;

    // profile_id / camera_profile:
    //   If a profile was provided (scan-time call), embed the full snapshot.
    //   Otherwise preserve whatever was already in the manifest.
    std::string newProfileId;
    json        newCameraProfile;
    if (!profile.profileId.empty()) {
        newProfileId      = profile.profileId;
        newCameraProfile  = profileToJson(profile);
    } else {
        newProfileId = settings.activeProfileId.empty() ? existingProfileId
                                                        : settings.activeProfileId;
        newCameraProfile = existingCameraProfile;
    }

    json root;
    root["source_path"]    = path;
    root["profile_id"]     = newProfileId;
    if (!newCameraProfile.empty())
        root["camera_profile"] = newCameraProfile;
    root["schema_version"] = 3;
    root["path_map"]       = existingPathMap;
    root["note"]           = existingNote;
    root["trips"]          = json::array();

    std::vector<Trip> mutableTrips = trips;
    for (auto& trip : mutableTrips) {
        // Assign or preserve trip ID
        if (trip.id.empty()) {
            std::string hash = tripIdentityHash(trip);
            auto it = hashToId.find(hash);
            if (it != hashToId.end()) {
                trip.id = it->second;
            } else {
                trip.id = generateId(usedIds, false); // digit-first for trips
                usedIds.insert(trip.id);
            }
        }

        // Compute validation files if not already present
        if (trip.validationFiles.empty())
            trip.validationFiles = selectValidationFiles(trip, path);

        json jTrip;
        jTrip["id"]         = trip.id;
        jTrip["date"]       = trip.date;
        jTrip["start_time"] = trip.startTime;
        jTrip["start_epoch"]= trip.startEpoch;
        jTrip["duration"]   = trip.duration;
        jTrip["segdur"]     = trip.segdur;
        {
            json dur = json::object();
            dur["segDetectedDur"] = trip.segDetectedDuration;
            if (trip.durationFFProbed >= 0)
                dur["durationFFProbed"] = trip.durationFFProbed;
            jTrip["durations"] = dur;
        }
        jTrip["note"]       = trip.note;

        if (trip.firstLockLat != 0.0 || trip.firstLockLon != 0.0) {
            jTrip["firstLockLat"]       = trip.firstLockLat;
            jTrip["firstLockLon"]       = trip.firstLockLon;
            jTrip["firstLockTimestamp"] = trip.firstLockTimestamp;
            jTrip["firstLockRecord"]    = trip.firstLockRecord;
        }
        if (trip.gpsLockSeconds >= 0)
            jTrip["gpsLockSeconds"] = trip.gpsLockSeconds;

        if (trip.startLat != 0.0 || trip.startLon != 0.0) {
            jTrip["startLat"] = trip.startLat;
            jTrip["startLon"] = trip.startLon;
        }
        if (trip.endLat != 0.0 || trip.endLon != 0.0) {
            jTrip["endLat"] = trip.endLat;
            jTrip["endLon"] = trip.endLon;
        }
        jTrip["gpsTrackStatus"] = trip.gpsTrackStatus;

        if (!trip.mapVideos.empty()) {
            json arr = json::array();
            for (const auto& p : trip.mapVideos) arr.push_back(p);
            jTrip["mapVideos"] = arr;
        }
        if (!trip.dashVideos.empty()) {
            json arr = json::array();
            for (const auto& p : trip.dashVideos) arr.push_back(p);
            jTrip["dashVideos"] = arr;
        }
        if (!trip.hudVideos.empty()) {
            json arr = json::array();
            for (const auto& p : trip.hudVideos) arr.push_back(p);
            jTrip["hudVideos"] = arr;
        }

        {
            json jFirst, jLast;
            for (const auto& [k, v] : trip.firstThumbs) if (!v.empty()) jFirst[k] = makeRelPath(v, path);
            for (const auto& [k, v] : trip.lastThumbs)  if (!v.empty()) jLast[k]  = makeRelPath(v, path);
            if (!jFirst.empty()) jTrip["firstThumbs"] = jFirst;
            if (!jLast.empty())  jTrip["lastThumbs"]  = jLast;
        }

        {
            json vp;
            vp["width"]       = trip.videoProfile.width;
            vp["height"]      = trip.videoProfile.height;
            vp["pixFmt"]      = trip.videoProfile.pixFmt;
            vp["colorRange"]  = trip.videoProfile.colorRange;
            vp["colorSpace"]  = trip.videoProfile.colorSpace;
            vp["frameRate"]   = trip.videoProfile.frameRate;
            jTrip["videoProfile"] = vp;
        }

        // Validation files
        json jVal = json::array();
        for (const auto& vf : trip.validationFiles) {
            json jvf;
            jvf["file"] = vf.relPath;
            jvf["md5"]  = vf.md5;
            jVal.push_back(jvf);
        }
        jTrip["validation"] = jVal;

        json jTrack = json::array();
        for (const auto& pt : trip.gpsTrack) {
            json jPt;
            jPt["timestamp"] = pt.timestamp;
            jPt["lat"]       = pt.lat;
            jPt["lon"]       = pt.lon;
            jPt["altitude"]  = pt.altitude;
            if (pt.speed   >= 0.0) jPt["speed"]   = pt.speed;
            if (pt.heading >= 0.0) jPt["heading"]  = pt.heading;
            jTrack.push_back(jPt);
        }
        jTrip["gpsTrack"] = jTrack;

        jTrip["segments"] = json::array();
        for (const auto& seg : trip.segments) {
            json jSeg;
            jSeg["timestamp"] = seg.timestamp;
            json jCams, jThumbs;
            for (const auto& [k, v] : seg.cameras) jCams[k] = makeRelPath(v, path);
            for (const auto& [k, v] : seg.thumbs)  if (!v.empty()) jThumbs[k] = makeRelPath(v, path);
            jSeg["cameras"] = jCams;
            if (!jThumbs.empty()) jSeg["thumbs"] = jThumbs;
            jTrip["segments"].push_back(jSeg);
        }
        root["trips"].push_back(jTrip);
    }

    std::ofstream ofs(manifestFile);
    if (!ofs.is_open()) {
        std::cerr << "Warning: Could not write manifest: " << manifestFile << "\n";
        return;
    }
    ofs << root.dump(2) << "\n";
    ofs.close();

    // Update master index (recomputes manifest md5 after write)
    updateManifestIndex(path, mutableTrips);
}

// ---------------------------------------------------------------------------
// loadTripCache
// ---------------------------------------------------------------------------

std::vector<Trip> ConfigManager::loadTripCache(const std::string& path) {
    std::vector<Trip> trips;

    // Accept either a full source path or a direct manifest file path.
    bool isDirectManifest = (path.find("pm_manifest_") != std::string::npos && fs::exists(path));
    std::string fullFile;
    if (isDirectManifest) {
        fullFile = path;
    } else {
        fullFile = lookupManifestFilePath(path);
        if (fullFile.empty()) return trips;
    }

    std::ifstream ifs(fullFile);
    if (!ifs.is_open()) return trips;

    json root;
    try {
        ifs >> root;
    } catch (const json::parse_error& e) {
        std::cerr << "Warning: Failed to parse manifest: " << fullFile
                  << "\n  " << e.what() << "\n";
        return trips;
    }

    if (!root.contains("trips") || !root["trips"].is_array()) return trips;

    // Determine local source root for path resolution.
    // Priority: path_map[our key] → caller's source path → manifest parent dir → source_path field.
    json pathMap = json::object();
    if (root.contains("path_map") && root["path_map"].is_object())
        pathMap = root["path_map"];

    std::string key = pathMapKey(hostname);
    std::string localRoot;
    if (pathMap.contains(key) && pathMap[key].is_string()) {
        localRoot = pathMap[key].get<std::string>();
    } else if (!isDirectManifest) {
        localRoot = path;
    } else {
        std::error_code ec;
        fs::path parent = fs::path(fullFile).parent_path();
        std::string parentStr = fs::canonical(parent, ec).string();
        localRoot = (!ec && parentStr.find(configDir) == std::string::npos)
                    ? parentStr : root.value("source_path", "");
    }
    while (!localRoot.empty() && (localRoot.back() == '/' || localRoot.back() == '\\'))
        localRoot.pop_back();

    // Register this machine in path_map if not already present, then save back.
    if (!pathMap.contains(key) && !localRoot.empty()) {
        pathMap[key] = localRoot;
        root["path_map"] = pathMap;
        std::ofstream regofs(fullFile);
        if (regofs.is_open()) {
            regofs << root.dump(2) << "\n";
            regofs.close();
            updateManifestMd5(fullFile);
        }
    }

    for (const auto& jTrip : root["trips"]) {
        Trip trip;
        trip.id        = jTrip.value("id",          "");
        trip.date      = jTrip.value("date",         "");
        trip.startTime = jTrip.value("start_time",   "");
        trip.startEpoch= jTrip.value("start_epoch",  (time_t)0);
        trip.duration  = jTrip.value("duration",     "");
        trip.segdur    = jTrip.value("segdur",        0);
        if (jTrip.contains("durations") && jTrip["durations"].is_object()) {
            const auto& dur = jTrip["durations"];
            trip.segDetectedDuration = dur.value("segDetectedDur",    0);
            trip.durationFFProbed    = dur.value("durationFFProbed",  -1);
        }
        trip.note      = jTrip.value("note",         "");

        trip.firstLockLat       = jTrip.value("firstLockLat",       0.0);
        trip.firstLockLon       = jTrip.value("firstLockLon",       0.0);
        trip.firstLockTimestamp = jTrip.value("firstLockTimestamp", "");
        trip.firstLockRecord    = jTrip.value("firstLockRecord",    -1);
        trip.gpsLockSeconds     = jTrip.value("gpsLockSeconds",     -1);

        trip.startLat       = jTrip.value("startLat",       0.0);
        trip.startLon       = jTrip.value("startLon",       0.0);
        trip.endLat         = jTrip.value("endLat",         0.0);
        trip.endLon         = jTrip.value("endLon",         0.0);
        trip.gpsTrackStatus = jTrip.value("gpsTrackStatus", "none");

        if (jTrip.contains("mapVideos") && jTrip["mapVideos"].is_array())
            for (const auto& v : jTrip["mapVideos"]) trip.mapVideos.push_back(v.get<std::string>());
        if (jTrip.contains("dashVideos") && jTrip["dashVideos"].is_array())
            for (const auto& v : jTrip["dashVideos"]) trip.dashVideos.push_back(v.get<std::string>());
        if (jTrip.contains("hudVideos") && jTrip["hudVideos"].is_array())
            for (const auto& v : jTrip["hudVideos"]) trip.hudVideos.push_back(v.get<std::string>());

        // Trip thumbnails — new format uses firstThumbs/lastThumbs maps.
        // Migrate old per-camera named fields transparently on read.
        if (jTrip.contains("firstThumbs") && jTrip["firstThumbs"].is_object()) {
            for (const auto& [k, v] : jTrip["firstThumbs"].items())
                trip.firstThumbs[k] = resolvePathWithMap(v.get<std::string>(), localRoot, pathMap);
            for (const auto& [k, v] : jTrip["lastThumbs"].items())
                trip.lastThumbs[k] = resolvePathWithMap(v.get<std::string>(), localRoot, pathMap);
        } else {
            // Old format — migrate D90 named fields into maps.
            auto gt = [&](const std::string& fkey) { return jTrip.value(fkey, std::string("")); };
            auto rv = [&](const std::string& p) { return resolvePathWithMap(p, localRoot, pathMap); };
            trip.firstThumbs["front"] = rv(gt("firstFrontThumb"));
            trip.firstThumbs["rear"]  = rv(gt("firstRearThumb"));
            trip.firstThumbs["left"]  = rv(gt("firstLeftThumb"));
            trip.firstThumbs["right"] = rv(gt("firstRightThumb"));
            trip.lastThumbs["front"]  = rv(gt("lastFrontThumb"));
            trip.lastThumbs["rear"]   = rv(gt("lastRearThumb"));
            trip.lastThumbs["left"]   = rv(gt("lastLeftThumb"));
            trip.lastThumbs["right"]  = rv(gt("lastRightThumb"));
        }

        if (jTrip.contains("videoProfile") && jTrip["videoProfile"].is_object()) {
            const auto& vp       = jTrip["videoProfile"];
            trip.videoProfile.width      = vp.value("width",      1920);
            trip.videoProfile.height     = vp.value("height",     1080);
            trip.videoProfile.pixFmt     = vp.value("pixFmt",     "yuvj420p");
            trip.videoProfile.colorRange = vp.value("colorRange", "pc");
            trip.videoProfile.colorSpace = vp.value("colorSpace", "bt709");
            trip.videoProfile.frameRate  = vp.value("frameRate",  "25/1");
        }

        if (jTrip.contains("validation") && jTrip["validation"].is_array()) {
            for (const auto& jvf : jTrip["validation"]) {
                ValidationFile vf;
                vf.relPath = jvf.value("file", "");
                vf.md5     = jvf.value("md5",  "");
                if (!vf.relPath.empty()) trip.validationFiles.push_back(vf);
            }
        }

        if (jTrip.contains("gpsTrack") && jTrip["gpsTrack"].is_array()) {
            for (const auto& jPt : jTrip["gpsTrack"]) {
                GpsPoint pt;
                pt.timestamp = jPt.value("timestamp", "");
                pt.lat       = jPt.value("lat",        0.0);
                pt.lon       = jPt.value("lon",        0.0);
                pt.altitude  = jPt.value("altitude",  -9999.0);
                pt.speed     = jPt.value("speed",     -1.0);
                pt.heading   = jPt.value("heading",   -1.0);
                trip.gpsTrack.push_back(pt);
            }
        }

        if (jTrip.contains("segments") && jTrip["segments"].is_array()) {
            for (const auto& jSeg : jTrip["segments"]) {
                TripSegment seg;
                seg.timestamp = jSeg.value("timestamp", "");
                // Segment cameras — new format uses "cameras" object.
                // Migrate old D90 named fields transparently on read.
                auto rp = [&](const std::string& p) {
                    return resolvePathWithMap(p, localRoot, pathMap);
                };
                if (jSeg.contains("cameras") && jSeg["cameras"].is_object()) {
                    for (const auto& [k, v] : jSeg["cameras"].items())
                        seg.cameras[k] = rp(v.get<std::string>());
                    if (jSeg.contains("thumbs") && jSeg["thumbs"].is_object())
                        for (const auto& [k, v] : jSeg["thumbs"].items())
                            seg.thumbs[k] = rp(v.get<std::string>());
                } else {
                    // Old format — migrate D90 named fields.
                    seg.cameras["front"] = rp(jSeg.value("front",      "-"));
                    seg.cameras["rear"]  = rp(jSeg.value("rear",
                                           jSeg.value("back",          "-")));
                    seg.cameras["left"]  = rp(jSeg.value("left",       "-"));
                    seg.cameras["right"] = rp(jSeg.value("right",      "-"));
                    seg.thumbs["front"]  = rp(jSeg.value("frontThumb", ""));
                    seg.thumbs["rear"]   = rp(jSeg.value("rearThumb",  ""));
                    seg.thumbs["left"]   = rp(jSeg.value("leftThumb",  ""));
                    seg.thumbs["right"]  = rp(jSeg.value("rightThumb", ""));
                }
                trip.segments.push_back(seg);
            }
        }
        trips.push_back(trip);
    }

    return trips;
}

// ---------------------------------------------------------------------------
// clearCache
// ---------------------------------------------------------------------------

void ConfigManager::clearCache(const std::string& path, bool force) {
    std::error_code ec;

    if (path == "ALL") {
        auto index = loadManifestIndex();
        if (index.empty()) { std::cout << "No cached manifests found.\n"; return; }

        std::cout << "\n--- Cached Manifests ---\n";
        for (const auto& e : index)
            std::cout << "  [" << e.id << "]  " << e.path << "\n";
        std::cout << "\n";

        if (!force) {
            std::cout << "Wipe ALL cached manifests? [y/N]: ";
            std::string answer;
            std::getline(std::cin >> std::ws, answer);
            if (answer != "y" && answer != "Y") { std::cout << "Aborted.\n"; return; }
        }
        for (const auto& e : index) fs::remove(e.manifestFile, ec);
        // Wipe index and lastpath
        fs::remove(configDir + "manifests.json", ec);
        fs::remove(configDir + "lastpath", ec);
        std::cout << "All cached manifests cleared.\n";

    } else if (path.empty()) {
        // Interactive single-delete loop
        while (true) {
            auto index = loadManifestIndex();
            if (index.empty()) { std::cout << "No cached manifests remaining.\n"; return; }

            std::cout << "\n--- Select Manifest to Delete ---\n";
            for (const auto& e : index)
                std::cout << "  [" << e.id << "]  " << e.path << "\n";
            std::cout << "  [Q] Done\n\nManifest ID: ";
            std::string choice;
            std::getline(std::cin >> std::ws, choice);
            if (choice == "q" || choice == "Q") return;
            std::string choiceUp = choice;
            for (char& c : choiceUp) c = std::toupper((unsigned char)c);
            int idx = -1;
            for (int i = 0; i < (int)index.size(); ++i) {
                std::string mid = index[i].id;
                for (char& c : mid) c = std::toupper((unsigned char)c);
                if (mid == choiceUp) { idx = i; break; }
            }
            if (idx >= 0) {
                fs::remove(index[idx].manifestFile, ec);
                    if (index[idx].path == getLastPath())
                        fs::remove(configDir + "lastpath", ec);
                    std::cout << "Deleted: " << index[idx].path << "\n";
                    index.erase(index.begin() + idx);
                    saveManifestIndex(index);
            } else {
                std::cout << "Invalid manifest ID.\n";
            }
        }

    } else {
        // Single named path
        auto index = loadManifestIndex();
        for (int i = 0; i < (int)index.size(); ++i) {
            if (index[i].path == path) {
                fs::remove(index[i].manifestFile, ec);
                if (path == getLastPath())
                    fs::remove(configDir + "lastpath", ec);
                index.erase(index.begin() + i);
                saveManifestIndex(index);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// showStale — display contents of the stale manifest archive.
// ---------------------------------------------------------------------------

void ConfigManager::showStale() {
    std::ifstream ifs(staleArchiveFile);
    if (!ifs.is_open()) {
        std::cout << "  No stale entries on record.\n";
        return;
    }
    json j;
    try { ifs >> j; } catch (...) { j = json::array(); }
    if (!j.is_array() || j.empty()) {
        std::cout << "  No stale entries on record.\n";
        return;
    }

    std::cout << "\n  Stale Manifest Archive  ("
              << j.size() << " " << (j.size() == 1 ? "entry" : "entries") << ")\n\n"
              << "  " << std::left
              << std::setw(6)  << "ID"
              << std::setw(22) << "Pruned"
              << std::setw(5)  << "Trips"
              << "Path\n"
              << "  " << std::string(72, '-') << "\n";
    for (const auto& e : j) {
        std::string id     = e.value("id",        "??");
        std::string path   = e.value("path",      "");
        std::string pruned = e.value("pruned",    "");
        int         trips  = e.value("tripCount", 0);
        std::cout << "  [" << std::left << std::setw(3) << id << "]  "
                  << std::setw(22) << pruned
                  << std::setw(5)  << trips
                  << path << "\n";
    }
    std::cout << "\n  Use --clear-stale to wipe the archive.\n\n";
}

// ---------------------------------------------------------------------------
// clearStale — wipe the stale manifest archive, with optional confirmation.
// ---------------------------------------------------------------------------

void ConfigManager::clearStale(bool force) {
    std::ifstream ifs(staleArchiveFile);
    if (!ifs.is_open()) {
        std::cout << "  No stale archive to clear.\n";
        return;
    }
    json j;
    try { ifs >> j; } catch (...) { j = json::array(); }
    ifs.close();
    if (!j.is_array() || j.empty()) {
        std::cout << "  Stale archive is already empty.\n";
        return;
    }

    std::cout << "  " << j.size() << " stale "
              << (j.size() == 1 ? "entry" : "entries") << " on record.\n\n";
    if (!force) {
        std::cout << "  Clear stale archive? [y/N]: ";
        std::string answer;
        std::getline(std::cin >> std::ws, answer);
        if (answer != "y" && answer != "Y") {
            std::cout << "  Aborted.\n";
            return;
        }
    }
    std::error_code ec;
    fs::remove(staleArchiveFile, ec);
    std::cout << "  Stale archive cleared.\n";
}

} // namespace Pathmux

// SN: 00104
