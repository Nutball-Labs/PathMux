#ifndef CONFIG_MANAGER_HPP
#define CONFIG_MANAGER_HPP

#include <string>
#include <vector>
#include <set>
#include "trip_detection.hpp"
#include "logger.hpp"

namespace Pathmux {

// ---------------------------------------------------------------------------
// NamedLocation — a user-defined point of interest for KML overlays.
// Stored in ~/.config/pathmux/locations.json
// ---------------------------------------------------------------------------
struct NamedLocation {
    std::string name;
    double      lat          = 0.0;
    double      lon          = 0.0;
    int         radiusMetres = 50;   // proximity match radius
    std::string icon         = "pin";
};

// ---------------------------------------------------------------------------
// KmlSettings — visual styling for KML exports.
// Stored inside pathmux.json under "kml" key.
// Colors are KML AABBGGRR hex strings (AA=alpha, BB=blue, GG=green, RR=red).
// ---------------------------------------------------------------------------
struct KmlSettings {
    std::string trackAheadColor  = "ff00ff00"; // bright green — route ahead
    std::string trackBehindColor = "ff0000ff"; // red — route already traveled
    int         trackLineWidth   = 3;
    std::string waypointColor    = "ffff0000"; // blue — waypoints stand out
    std::string startPinUrl      =
        "http://maps.google.com/mapfiles/kml/paddle/wht-blank.png";
    std::string endPinUrl        =
        "http://maps.google.com/mapfiles/kml/paddle/wht-blank.png";
    bool        showKnownLocations = true;
};

// ---------------------------------------------------------------------------
// AppSettings — main application preferences.
// Persisted to ~/.config/pathmux/pathmux.json.
// Missing or corrupt file silently replaced with defaults on next save.
//
// Note: segdur is NOT a setting — determined per-trip by ffprobe.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// EncodeSettings — hardware encoder configuration.
// Supports Intel QSV, NVIDIA NVENC, AMD VAAPI, and CPU fallback.
// Presets populate these fields with known-good values; user can override.
// ---------------------------------------------------------------------------
struct EncodeSettings {
    std::string preset          = "qsv";        // qsv|nvenc|vaapi|cpu
    std::string hwDevice        = "qsv:hw";     // passed to -init_hw_device
    std::string hwDeviceType    = "qsv";        // qsv|vaapi|cuda|none
    std::string normEncoder     = "h264_qsv";   // normalization encode
    std::string collageEncoder  = "hevc_qsv";   // 4K collage encode
    std::string downEncoder     = "h264_qsv";   // 1080p downscale encode
    std::string pixFmt          = "nv12";       // pixel format before encoder
    std::string normQuality     = "24";         // -q value for norm encode
    std::string collageQuality  = "20";         // -q value for collage encode
    std::string downQuality     = "22";         // -q value for downscale
    std::string extraNormArgs   = "";           // injected after -c:v in norm
    std::string extraCollageArgs = "";          // injected after -c:v in collage
    std::string extraDownArgs    = "";          // injected after -c:v in 1080p downscale
};

// Known presets — populated by applyEncodePreset()
// qsv:   Intel Quick Sync (RocketLake, Tiger Lake, etc.)
// nvenc: NVIDIA RTX/GTX series
// vaapi: AMD / older Intel via raw VAAPI
// cpu:   libx264/libx265 software fallback

struct AppSettings {
    // Trip detection
    int  gapThresholdSeconds = 900;    // Trip gap: default 15 minutes
    int  fuzzyWindowSeconds  = 5;      // Camera fuzzy-match tolerance (±N seconds)

    // GPS extraction
    int  gpsColStartSkip     = 45;     // Seconds to skip at start of first segment

    // External tool paths ("" = search system PATH)
    std::string exiftoolPath;          // default: "exiftool"
    std::string exiftoolOptions;       // default: "-ee3"
    std::string ffmpegPath;            // default: "ffmpeg"

    // Output
    std::string defaultExportDir;      // default: "" (current directory)
    std::string tmpDir;                // default: "" (use <exportDir>/pm_tmp/)
    std::string timestampFormat  = "YYYYMMDD-HHMMSS";
    std::string timeDisplay      = "24-hour";
    bool        useImperial      = false;  // false=metric, true=imperial (dumb american mode)

    // Video build settings
    std::string videoFormat         = "mp4";  // per-camera container: mp4|mkv|mov|avi|mpg
    std::string defaultAudioSource  = "left"; // collage audio: left|right|front|rear

    // Hardware encode settings
    EncodeSettings encode;

    // Logging
    std::string logLevel = "off";     // off|normal|debug

    // KML visual settings (nested)
    KmlSettings kml;

    // Internal
    int  schemaVersion       = 1;
};

// ---------------------------------------------------------------------------
// ConfigState — returned by configState() to indicate first-run status.
// ---------------------------------------------------------------------------
enum class ConfigState {
    VALID,           // pathmux.json exists and has key fields populated
    FIRST_RUN,       // pathmux.json absent or empty
    INCOMPLETE       // pathmux.json present but missing key fields
};

// ---------------------------------------------------------------------------
// ManifestEntry — one entry in ~/.config/pathmux/manifests.json.
// Tracks all known path manifests for the browser and startup validation.
// ---------------------------------------------------------------------------
struct ManifestEntry {
    std::string id;              // Two-char base36 manifest ID — never changes
    std::string path;            // Absolute source path e.g. /z/srcdash/ex9
    std::string manifestFile;    // Absolute path to pm_manifest_*.json
    std::string lastScan;        // ISO8601 timestamp of last successful scan
    int         tripCount  = 0;
    std::string firstTrip;       // startTime of first trip (for index display)
    std::string lastTrip;        // startTime of last trip  (for index display)
    std::string manifestMd5;     // md5 of manifest file — validated on load
    std::string note;            // User-set note for this manifest
};

// ---------------------------------------------------------------------------
// ConfigManager
// ---------------------------------------------------------------------------
class ConfigManager {
public:
    ConfigManager();
    void ensureConfigDir();

    // --- Config state ---
    ConfigState        configState()   const { return cfgState; }
    bool               isFirstRun()   const { return cfgState != ConfigState::VALID; }

    // --- Settings (pathmux.json) ---
    void               loadSettings();
    void               saveSettings();
    void               applySettings(const AppSettings& s)  { settings = s; }
    void               applyKmlSettings(const KmlSettings& k) { settings.kml = k; }

    int                getGapThreshold()  const { return settings.gapThresholdSeconds; }
    int                getFuzzyWindow()   const { return settings.fuzzyWindowSeconds; }
    int                getGpsSkip()       const { return settings.gpsColStartSkip; }
    std::string        getExiftoolPath()  const {
        return settings.exiftoolPath.empty() ? "exiftool" : settings.exiftoolPath;
    }
    std::string        getExiftoolOptions() const {
        return settings.exiftoolOptions.empty()
            ? "-ee3 -p '$GPSDateTime $GPSLatitude# $GPSLongitude# $GPSAltitude# $GPSSpeed# $GPSTrack# $Accelerometer'"
            : settings.exiftoolOptions;
    }
    std::string        getFfmpegPath()    const {
        return settings.ffmpegPath.empty()   ? "ffmpeg"   : settings.ffmpegPath;
    }
    std::string        getFfprobePath()   const {
        std::string p = getFfmpegPath();
        // Derive ffprobe from ffmpeg path — same directory, different binary name
        auto pos = p.rfind("ffmpeg");
        if (pos != std::string::npos) { p.replace(pos, 6, "ffprobe"); return p; }
        return "ffprobe";
    }
    std::string        getDefaultExportDir() const { return settings.defaultExportDir; }
    bool               getUseImperial()      const { return settings.useImperial; }
    std::string        getTmpDir(const std::string& outputDir = "") const {
        if (!settings.tmpDir.empty()) return settings.tmpDir;
        std::string base = outputDir.empty() ? settings.defaultExportDir : outputDir;
        if (base.empty()) base = ".";
        return base + "/pm_tmp";
    }
    std::string        getTimestampFormat()  const { return settings.timestampFormat; }
    std::string        getTimeDisplay()      const { return settings.timeDisplay; }
    std::string        getVideoFormat()        const { return settings.videoFormat; }
    std::string        getDefaultAudioSource() const { return settings.defaultAudioSource; }
    const EncodeSettings& getEncodeSettings() const { return settings.encode; }
    std::string        getLogLevel()           const { return settings.logLevel; }

    // Apply a named encode preset, populating EncodeSettings with known-good values.
    // preset: "qsv" | "nvenc" | "vaapi" | "cpu"
    void applyEncodePreset(const std::string& preset) {
        EncodeSettings& e = settings.encode;
        e.preset = preset;
        if (preset == "qsv") {
            e.hwDevice = "qsv:hw"; e.hwDeviceType = "qsv";
            e.normEncoder = "h264_qsv"; e.collageEncoder = "hevc_qsv";
            e.downEncoder = "h264_qsv"; e.pixFmt = "nv12";
            e.normQuality = "24"; e.collageQuality = "20"; e.downQuality = "22";
            e.extraNormArgs = ""; e.extraCollageArgs = "";
        } else if (preset == "nvenc") {
            e.hwDevice = "cuda"; e.hwDeviceType = "cuda";
            e.normEncoder = "h264_nvenc"; e.collageEncoder = "hevc_nvenc";
            e.downEncoder = "h264_nvenc"; e.pixFmt = "yuv420p";
            e.normQuality = "10"; e.collageQuality = "15"; e.downQuality = "22";
            // -preset p7: slowest/highest-quality NVENC preset (p1=fast .. p7=slow).
            // -tune hq:   driver-level quality tuning.
            // -cq 15:     constrained-quality VBR; NVENC CQ 15 ≈ QSV ICQ 20 perceptually.
            e.extraNormArgs = "-preset p4 -cq 10";
            e.extraCollageArgs = "-preset p7 -tune hq -cq 15";
            e.extraDownArgs = "-preset p4 -cq 18";
        } else if (preset == "vaapi") {
            e.hwDevice = "vaapi=/dev/dri/renderD128"; e.hwDeviceType = "vaapi";
            e.normEncoder = "h264_vaapi"; e.collageEncoder = "hevc_vaapi";
            e.downEncoder = "h264_vaapi"; e.pixFmt = "nv12";
            e.normQuality = "24"; e.collageQuality = "20"; e.downQuality = "22";
            e.extraNormArgs = ""; e.extraCollageArgs = "";
        } else if (preset == "cpu") {
            e.hwDevice = ""; e.hwDeviceType = "none";
            e.normEncoder = "libx264"; e.collageEncoder = "libx265";
            e.downEncoder = "libx264"; e.pixFmt = "yuv420p";
            e.normQuality = "23"; e.collageQuality = "18"; e.downQuality = "20";
            e.extraNormArgs = "-preset fast"; e.extraCollageArgs = "-preset slow";
        }
    }

    void               setGapThreshold(int seconds);
    void               showSettings()     const;
    const AppSettings& getSettings()      const { return settings; }
    const KmlSettings& getKmlSettings()   const { return settings.kml; }

    // --- Known locations (locations.json) ---
    std::vector<NamedLocation> loadLocations();
    void                       saveLocations(const std::vector<NamedLocation>& locs);

    // --- Trip manifest cache ---
    // Returns absolute path to pm_manifest_<sanitized>.json at source root.
    // Falls back to ~/.config/pathmux/ if source path is not writable.
    std::string    getManifestFilePath(const std::string& sourcePath);

    bool               isCached(const std::string& path);
    void               saveTripCache(const std::string& path,
                                     const std::vector<Trip>& trips);
    std::vector<Trip>  loadTripCache(const std::string& path);
    void               clearCache(const std::string& path, bool force = false);

    // --- Master manifest index (~/.config/pathmux/manifests.json) ---
    std::vector<ManifestEntry> loadManifestIndex();
    std::string                getManifestIdForPath(const std::string& path);
    void                       updateManifestMd5(const std::string& manifestFile);
    void                       saveManifestIndex(const std::vector<ManifestEntry>& index);
    void                       updateManifestIndex(const std::string& path,
                                                   const std::vector<Trip>& trips);
    // Save a note for a manifest without triggering a rescan.
    // Updates both the index entry and the note field in the manifest file.
    void                       saveManifestNote(const std::string& path,
                                                const std::string& note);

    // Startup validation — checks each index entry's manifest file exists
    // and its md5 matches.  Prompts user on any failure.
    // Returns false if user chose Quit.
    bool validateManifestIndex();
    bool validateManifestIndexReport();  // non-interactive: print status, return false if any fail

    // --- Stale manifest archive (manifests_stale.json) ---
    // Entries pruned from the live index are appended here for troubleshooting.
    // Never read during normal operations; write-only from the runtime perspective.
    void               showStale();
    void               clearStale(bool force = false);

    // --- Last-used path ---
    void               setLastPath(const std::string& path);
    std::string        getLastPath();

    // --- Manifest listing ---
    void               listCachedManifests();
    std::vector<std::string> getAllCachedPaths();

    // Compute md5 of a file, returns hex string or "" on failure.
    // Public so find_trips can use it for inline [V] validate display.
    static std::string fileMd5(const std::string& filePath);

private:
    std::string  configDir;
    std::string  settingsFile;      // ~/.config/pathmux/pathmux.json
    std::string  locationsFile;     // ~/.config/pathmux/locations.json
    std::string  manifestIndexFile; // ~/.config/pathmux/manifests.json
    std::string  staleArchiveFile;  // ~/.config/pathmux/manifests_stale.json
    AppSettings  settings;
    ConfigState  cfgState = ConfigState::FIRST_RUN;

    // Sanitize a source path to a filename stem:
    //   /z/srcdash/ex9  →  z_srcdash_ex9
    std::string  sanitizePath(const std::string& path);

    // Ensure sourcePath has a manifest ID; create minimal index entry if new.
    // Returns the 2-char ID. Write operations only — has index side effects.
    std::string  ensureManifestId(const std::string& sourcePath);

    // Read-only manifest file path lookup (no index side effects).
    // Returns "" if not in index. Handles old-style filename migration.
    std::string  lookupManifestFilePath(const std::string& sourcePath);

    // Generate a unique 2-char base36 ID not already in the given set.
    // Manifest IDs prefer alpha first char; trip IDs prefer digit first char.
    std::string  generateId(const std::set<std::string>& existing,
                             bool preferAlphaFirst);

    // Normalize user-typed ID input: uppercase, O→0, I→1, L→1
    static std::string normalizeId(const std::string& input);

    // Compute trip identity hash from metadata (no file I/O).
    static std::string tripIdentityHash(const Trip& trip);

    // Select 3 validation files deterministically from trip's file pool.
    static std::vector<ValidationFile> selectValidationFiles(
        const Trip& trip, const std::string& sourcePath);
};

} // namespace Pathmux

#endif
// SN: 00084
