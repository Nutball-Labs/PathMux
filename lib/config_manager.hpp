// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef CONFIG_MANAGER_HPP
#define CONFIG_MANAGER_HPP

#include <string>
#include <vector>
#include <set>
#include "trip_detection.hpp"
#include "camera_profile.hpp"
#include "logger.hpp"

namespace CamClops {

// ---------------------------------------------------------------------------
// NamedLocation — a user-defined point of interest for KML overlays.
// Stored in ~/.config/camclops/locations.json
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
// Stored inside camclops.json under "kml" key.
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
// Persisted to ~/.config/camclops/camclops.json.
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
    // Default to CPU software encode — works on any machine without GPU setup.
    // Users upgrade to qsv/nvenc/vaapi via --encoderprefs or --prefs [L].
    std::string preset          = "cpu";        // qsv|nvenc|vaapi|cpu
    std::string hwDevice        = "";           // passed to -init_hw_device
    std::string hwDeviceType    = "none";       // qsv|vaapi|cuda|none
    std::string normEncoder     = "libx264";    // normalization encode
    std::string collageEncoder  = "libx265";    // 4K collage encode
    std::string downEncoder     = "libx264";    // 1080p downscale encode
    std::string pixFmt          = "yuv420p";    // pixel format before encoder
    std::string normQuality     = "23";         // -crf for libx264
    std::string collageQuality  = "18";         // -crf for libx265
    std::string downQuality     = "20";         // -crf for libx264
    std::string extraNormArgs   = "-preset fast";   // injected after -c:v in norm
    std::string extraCollageArgs = "-preset slow";  // injected after -c:v in collage
    std::string extraDownArgs    = "-preset fast";  // injected after -c:v in 1080p downscale
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

    // External tool paths ("" = search system PATH)
    std::string exiftoolPath;          // default: "exiftool"
    std::string ffmpegPath;            // default: "ffmpeg"

    // Output
    std::string defaultExportDir;      // default: "" (current directory)
    std::string tmpDir;                // default: "" (use <exportDir>/clops_tmp/)
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

    // Camera profile
    std::string activeProfileId  = "pruveeo_d90"; // matches profile JSON filename stem

    // GUI display scale (set before QApplication; requires restart to take effect)
    double uiScale = 1.0;

    // Job Queue panel placement: "docked" (bottom panel) or "window" (floating)
    std::string jobQueueMode = "docked";

    // Remote monitor HTTP port (clops_monitor.py --port)
    int monitorPort = 8647;

    // HUD overlay appearance
    double      hudFontScale = 1.0;      // multiplier applied to all HUD font sizes
    double      hudLineScale = 1.0;      // multiplier applied to all HUD line widths
    std::string hudColor     = "#00ff41"; // phosphor green; any CSS hex color

    // Internal
    int  schemaVersion       = 1;
};

// ---------------------------------------------------------------------------
// ConfigState — returned by configState() to indicate first-run status.
// ---------------------------------------------------------------------------
enum class ConfigState {
    VALID,           // camclops.json exists and has key fields populated
    FIRST_RUN,       // camclops.json absent or empty
    INCOMPLETE       // camclops.json present but missing key fields
};

// ---------------------------------------------------------------------------
// ManifestEntry — one entry in ~/.config/camclops/manifests.json.
// Tracks all known path manifests for the browser and startup validation.
// ---------------------------------------------------------------------------
struct ManifestEntry {
    std::string id;              // Two-char base36 manifest ID — never changes
    std::string path;            // Absolute source path e.g. /z/srcdash/ex9
    std::string manifestFile;    // Absolute path to clops_manifest_*.json
    std::string lastScan;        // ISO8601 timestamp of last successful scan
    int         tripCount  = 0;
    std::string firstTrip;       // startTime of first trip (for index display)
    std::string lastTrip;        // startTime of last trip  (for index display)
    std::string manifestMd5;     // md5 of manifest file — validated on load
    std::string note;            // User-set note for this manifest
    std::string nickname;        // Display label; defaults to path if empty
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

    // --- Settings (camclops.json) ---
    void               loadSettings();
    void               saveSettings();
    void               applySettings(const AppSettings& s)  { settings = s; }
    void               applyKmlSettings(const KmlSettings& k) { settings.kml = k; }

    int                getGapThreshold()  const { return settings.gapThresholdSeconds; }
    int                getFuzzyWindow()   const { return settings.fuzzyWindowSeconds; }
    std::string        getExiftoolPath()  const {
        return settings.exiftoolPath.empty() ? "exiftool" : settings.exiftoolPath;
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
        return base + "/clops_tmp";
    }
    std::string        getTimestampFormat()  const { return settings.timestampFormat; }
    std::string        getTimeDisplay()      const { return settings.timeDisplay; }
    std::string        getVideoFormat()        const { return settings.videoFormat; }
    std::string        getDefaultAudioSource() const { return settings.defaultAudioSource; }
    const EncodeSettings& getEncodeSettings() const { return settings.encode; }
    std::string        getLogLevel()           const { return settings.logLevel; }
    std::string        getActiveProfileId()    const { return settings.activeProfileId; }
    double             getUiScale()            const { return settings.uiScale; }
    double             getHudFontScale()       const { return settings.hudFontScale; }
    double             getHudLineScale()       const { return settings.hudLineScale; }
    std::string        getHudColor()           const { return settings.hudColor; }

    // Load the active camera profile from disk.
    // Returns d90Default() if the profile file is absent or invalid.
    CameraProfile      getCameraProfile()      const;

    // All known profiles: built-ins merged with any user JSON files in
    // ~/.config/camclops/profiles/.  User files win on profile_id conflict.
    std::vector<CameraProfile> loadAllProfiles() const;

    // Read the profile_id stored in an existing manifest for sourcePath.
    // Returns "" if no manifest exists or it contains no profile_id.
    std::string getManifestProfileId(const std::string& sourcePath) const;

    // Return the full camera profile embedded in the manifest for sourcePath.
    // If the manifest has a "camera_profile" object, deserialise and return it.
    // Falls back to profile_id lookup, then getCameraProfile() if neither present.
    CameraProfile getManifestProfile(const std::string& sourcePath) const;

    // --- Host-specific settings (camclops_<hostname>.json) ---
    // Host file overlays base prefs for this machine only.
    // Fields: encode.*, ffmpegPath, exiftoolPath,
    //         defaultExportDir, tmpDir, logLevel.
    std::string        getHostname()        const { return hostname; }
    std::string        getHostSettingsFile() const { return hostSettingsFile; }
    void               saveHostSettings();    // write host subset to host file
    void               reloadHostSettings(); // re-apply host overlay from disk (picks up --encoderprefs changes without restart)

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
            // -low_power 0: disable low-power mode, which is unsupported on many
            // Intel iGPUs (Optiplex, NUC, etc.) and causes initialisation failure.
            e.extraNormArgs   = "-low_power 0";
            e.extraCollageArgs = "-low_power 0";
            e.extraDownArgs   = "-low_power 0";
        } else if (preset == "nvenc") {
            e.hwDevice = "cuda"; e.hwDeviceType = "cuda";
            e.normEncoder = "h264_nvenc"; e.collageEncoder = "hevc_nvenc";
            e.downEncoder = "h264_nvenc"; e.pixFmt = "yuv420p";
            e.normQuality = "10"; e.collageQuality = "24"; e.downQuality = "24";
            // -preset p7: slowest/highest-quality NVENC preset (p1=fast .. p7=slow).
            // -tune hq:   driver-level quality tuning.
            // -rc constqp -qp 24: constant quantizer mode — no hidden VBR bitrate cap.
            //   CQ/VBR mode silently capped output at ~20 Mbps regardless of quality
            //   setting, rendering the CQ value meaningless. constqp QP 24 produces
            //   ~44 Mbps for 4K H.265 dashcam collage — clean Roku Ultra direct play,
            //   visually indistinguishable from QP 20 (68 Mbps) on 85" 4K display.
            e.extraNormArgs   = "-preset p4 -rc constqp -qp 24";
            e.extraCollageArgs = "-preset p7 -tune hq -rc constqp -qp 24";
            e.extraDownArgs   = "-preset p4 -rc constqp -qp 24";
        } else if (preset == "vaapi") {
            e.hwDevice = "vaapi=/dev/dri/renderD128"; e.hwDeviceType = "vaapi";
            e.normEncoder = "h264_vaapi"; e.collageEncoder = "hevc_vaapi";
            e.downEncoder = "h264_vaapi"; e.pixFmt = "nv12";
            e.normQuality = "24"; e.collageQuality = "20"; e.downQuality = "22";
            e.extraNormArgs = ""; e.extraCollageArgs = ""; e.extraDownArgs = "";
        } else if (preset == "cpu") {
            e.hwDevice = ""; e.hwDeviceType = "none";
            e.normEncoder = "libx264"; e.collageEncoder = "libx265";
            e.downEncoder = "libx264"; e.pixFmt = "yuv420p";
            e.normQuality = "23"; e.collageQuality = "18"; e.downQuality = "20";
            e.extraNormArgs = "-preset fast"; e.extraCollageArgs = "-preset slow";
            e.extraDownArgs = "-preset fast";
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
    // Returns absolute path to clops_manifest_<sanitized>.json at source root.
    // Falls back to ~/.config/camclops/ if source path is not writable.
    std::string    getManifestFilePath(const std::string& sourcePath);

    bool               isCached(const std::string& path);
    // profile: the camera profile used for this scan.  When non-empty it is
    // embedded in the manifest as "camera_profile" so downstream operations
    // never need to consult the global settings file.  Pass CameraProfile{}
    // (the default) for non-scan writes (GPS update, note save, etc.) — the
    // existing embedded profile is preserved.
    void               saveTripCache(const std::string& path,
                                     const std::vector<Trip>& trips,
                                     const CameraProfile& profile = CameraProfile{});
    std::vector<Trip>  loadTripCache(const std::string& path);
    void               clearCache(const std::string& path, bool force = false);

    // --- Master manifest index (~/.config/camclops/manifests.json) ---
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
    void                       setManifestNickname(const std::string& id,
                                                   const std::string& nickname);

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

    // Read-only manifest file path lookup (no index side effects).
    // Returns "" if not in index. Handles old-style filename migration.
    std::string  lookupManifestFilePath(const std::string& sourcePath);

private:
    std::string  configDir;
    std::string  settingsFile;      // ~/.config/camclops/camclops.json
    std::string  hostname;          // short hostname, no domain
    std::string  hostSettingsFile;  // ~/.config/camclops/camclops_<hostname>.json
    std::string  locationsFile;     // ~/.config/camclops/locations.json
    std::string  manifestIndexFile; // ~/.config/camclops/manifests.json
    std::string  staleArchiveFile;  // ~/.config/camclops/manifests_stale.json
    AppSettings  settings;
    ConfigState  cfgState = ConfigState::FIRST_RUN;

    // Sanitize a source path to a filename stem:
    //   /z/srcdash/ex9  →  z_srcdash_ex9
    std::string  sanitizePath(const std::string& path);

    // Ensure sourcePath has a manifest ID; create minimal index entry if new.
    // Returns the 2-char ID. Write operations only — has index side effects.
    std::string  ensureManifestId(const std::string& sourcePath);

    // Generate a unique 2-char base36 ID not already in the given set.
    // Manifest IDs prefer alpha first char; trip IDs prefer digit first char.
    std::string  generateId(const std::set<std::string>& existing,
                             bool preferAlphaFirst);

    // Apply host overlay from camclops_<hostname>.json over already-loaded base settings.
    void         loadHostOverlay();

    // Normalize user-typed ID input: uppercase, O→0, I→1, L→1
    static std::string normalizeId(const std::string& input);

    // Compute trip identity hash from metadata (no file I/O).
    static std::string tripIdentityHash(const Trip& trip);

    // Select 3 validation files deterministically from trip's file pool.
    static std::vector<ValidationFile> selectValidationFiles(
        const Trip& trip, const std::string& sourcePath);
};

} // namespace CamClops

#endif
// SN: 00115
