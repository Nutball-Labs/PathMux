#include "config_manager.hpp"
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
    settings.gpsColStartSkip     = j.value("gpsColStartSkip",     settings.gpsColStartSkip);
    settings.exiftoolPath        = j.value("exiftoolPath",        settings.exiftoolPath);
    settings.exiftoolOptions     = j.value("exiftoolOptions",     settings.exiftoolOptions);
    settings.ffmpegPath          = j.value("ffmpegPath",          settings.ffmpegPath);
    settings.defaultExportDir    = j.value("defaultExportDir",    settings.defaultExportDir);
    settings.tmpDir              = j.value("tmpDir",              settings.tmpDir);
    settings.timestampFormat     = j.value("timestampFormat",     settings.timestampFormat);
    settings.timeDisplay         = j.value("timeDisplay",         settings.timeDisplay);
    settings.useImperial         = j.value("useImperial",         settings.useImperial);
    settings.videoFormat         = j.value("videoFormat",         settings.videoFormat);
    settings.defaultAudioSource  = j.value("defaultAudioSource",  settings.defaultAudioSource);
    settings.logLevel            = j.value("logLevel",            settings.logLevel);

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

    // INCOMPLETE if export dir is not set — key field for write operations
    if (settings.defaultExportDir.empty())
        cfgState = ConfigState::INCOMPLETE;
    else
        cfgState = ConfigState::VALID;

    // Open logger based on loaded log level
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
    j["gpsColStartSkip"]     = settings.gpsColStartSkip;
    j["exiftoolPath"]        = settings.exiftoolPath;
    j["exiftoolOptions"]     = settings.exiftoolOptions;
    j["ffmpegPath"]          = settings.ffmpegPath;
    j["defaultExportDir"]    = settings.defaultExportDir;
    j["tmpDir"]              = settings.tmpDir;
    j["timestampFormat"]     = settings.timestampFormat;
    j["timeDisplay"]         = settings.timeDisplay;
    j["useImperial"]         = settings.useImperial;
    j["videoFormat"]         = settings.videoFormat;
    j["defaultAudioSource"]  = settings.defaultAudioSource;
    j["logLevel"]            = settings.logLevel;

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
              << std::setw(28) << "gpsColStartSkip"
              << settings.gpsColStartSkip << "s\n"
              << std::setw(28) << "exiftoolPath"
              << (settings.exiftoolPath.empty() ? "exiftool (system)" : settings.exiftoolPath) << "\n"
              << std::setw(28) << "exiftoolOptions"
              << (settings.exiftoolOptions.empty()
                  ? "-ee3 -p '$GPSDateTime ...' (default)" : settings.exiftoolOptions) << "\n"
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
    for (const auto& e : index)
        if (e.path == sourcePath) return e.id;
    // New path — assign an ID and save a minimal entry
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
    // Use system md5sum command — available on all Linux distros
    std::string cmd = "md5sum " + std::string("'") + filePath + "' 2>/dev/null";
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
        key += "|" + trip.segments[0].front;
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
        for (const std::string& f : {seg.front, seg.rear, seg.left, seg.right}) {
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
        if (!e.path.empty()) index.push_back(e);
    }
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
            for (int i = (int)missing.size() - 1; i >= 0; --i)
                index.erase(index.begin() + missing[i]);
            saveManifestIndex(index);
            std::cout << "  Note: " << missing.size()
                      << " stale index entr" << (missing.size() == 1 ? "y" : "ies")
                      << " removed (manifest file not found).\n";
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
                                   const std::vector<Trip>& trips) {
    std::string manifestFile = getManifestFilePath(path);

    // Load existing manifest to preserve note and trip IDs
    auto existingTrips = loadTripCache(path);
    std::set<std::string> usedIds;
    for (const auto& t : existingTrips) usedIds.insert(t.id);

    // Preserve existing manifest-level note
    std::string existingNote;
    {
        std::ifstream nifs(manifestFile);
        if (nifs.is_open()) {
            try {
                json existing; nifs >> existing;
                existingNote = existing.value("note", "");
            } catch (...) {}
        }
    }

    // Build identity hash → id map from existing trips
    std::map<std::string, std::string> hashToId;
    for (const auto& t : existingTrips)
        if (!t.id.empty())
            hashToId[tripIdentityHash(t)] = t.id;

    json root;
    root["source_path"]   = path;
    root["schema_version"] = 2;
    root["note"]          = existingNote;
    root["trips"]         = json::array();

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
            jSeg["front"]     = seg.front;
            jSeg["rear"]      = seg.rear;
            jSeg["left"]      = seg.left;
            jSeg["right"]     = seg.right;
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

    // Accept either a full source path or a direct manifest file path
    std::string fullFile;
    if (path.find("pm_manifest_") != std::string::npos && fs::exists(path)) {
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
                seg.front     = jSeg.value("front",     "-");
                seg.rear      = jSeg.value("rear", jSeg.value("back", "-"));
                seg.left      = jSeg.value("left",      "-");
                seg.right     = jSeg.value("right",     "-");
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
            std::cin >> answer;
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
            std::cin >> choice;
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

} // namespace Pathmux

// SN: 00079
