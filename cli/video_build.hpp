// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef VIDEO_BUILD_HPP
#define VIDEO_BUILD_HPP

#include <string>
#include <vector>
#include <array>
#include <functional>
#include "trip_detection.hpp"
#include "config_manager.hpp"

using namespace Pathmux;

// ---------------------------------------------------------------------------
// CollageSlot — one quadrant of a free-form collage.
// Any camera from any trip can fill any slot.
// Empty slots render as logo-on-dark-blue.
// ---------------------------------------------------------------------------
struct CollageSlot {
    std::string filePath;       // "" = empty (logo placeholder)
    std::string label;          // display label e.g. "2026-02-17 Front"
                                // currently unused in render — dashcam OSD
                                // already has timestamp watermark
};

// ---------------------------------------------------------------------------
// CollageOptions — settings for the free-form collage builder (mode 2).
// ---------------------------------------------------------------------------
struct CollageOptions {
    CollageSlot slots[4];       // [0]=top-left  [1]=top-right
                                // [2]=bottom-left [3]=bottom-right
    int         audioSlot       = 0;    // which slot provides audio (0-based)
    bool        build4K         = true;
    bool        build1080       = false;
    std::string outputDir;
    std::string outputFilename;         // "" = auto-generate at build time
    std::string    ffmpegPath;
    std::string    ffprobePath;
    EncodeSettings encode;
};
// Navigation action returned by configureOptions to tell run() what to do
// when the user exits the Build Options menu without building.
enum class NavAction { NONE, SWITCH_TRIP, SWITCH_MANIFEST, QUIT };

// ---------------------------------------------------------------------------
struct VideoOptions {
    // Per-camera single files
    bool buildFront  = true;
    bool buildRear   = true;
    bool buildLeft   = true;
    bool buildRight  = true;

    // Collage
    bool buildCollage4K   = true;   // 3840x2160 H.265 master
    bool buildCollage1080 = false;  // 1080p transcode of 4K master

    // Audio extract — independent of camera file and collage toggles
    bool        buildAudio           = false;
    std::string audioExtractCamera   = "left";  // camera to extract from
    std::string audioExtractFormat   = "m4a";   // m4a|mp3|aac

    // Output
    std::string outputDir;          // "" = use config defaultExportDir
    std::string tmpDir;             // "" = auto: <outputDir>/pm_tmp
    std::string containerFormat;    // "mp4", "mkv", "mov" — per-camera files
                                    // collage is always mp4/H.265

    // ffmpeg path (from config)
    std::string ffmpegPath;

    // Hardware encode settings (from config)
    EncodeSettings encode;

    // Collage audio source camera (left|right|front|rear)
    // Default "left" = driver position in LHD countries.
    std::string audioSource = "left";

    // Build control — set by GODONE to exit -V after build completes.
    bool exitAfterBuild = false;

    // Navigation action — set when user exits Build Options without building.
    NavAction navAction = NavAction::NONE;

    // Provenance — set by the caller before invoking buildTrip() or run().
    // Used to write pm_buildlog.json in the footage source path.
    std::string sourcePath;   // footage source path (e.g. /z/srcdash/ex1)
    std::string manifestId;   // two-char base36 manifest ID (e.g. "CQ")

    // Basename override — if non-empty, replaces the auto-generated
    // date_time prefix in ALL output filenames for this build session.
    // e.g. "MyTrip" → MyTrip_Front.mp4, MyTrip_Collage_4K.mp4, etc.
    // Empty = use auto-generated date_time prefix.
    std::string basenameOverride;
};

// ---------------------------------------------------------------------------
// VideoBuilder — orchestrates ffmpeg calls to assemble trip video files.
//
// Pipeline:
//   1. Per-camera: concat all .ts segments → single output file (stream copy,
//      strip GPS data track via -map 0:v:0 -map 0:a:0).
//   2. Collage 4K: sync-normalize cameras (fade-to-black on short cameras),
//      build 2x2 filter graph at native 1080p per cell → 3840x2160 H.265.
//      Audio: from opts.audioSource (default: Left camera).
//   3. Collage 1080p: transcode 4K master → 1920x1080 H.264 (stream copy audio).
//   4. Audio extract: concat selected camera audio → m4a|mp3|aac output.
//      Independent of camera file and collage outputs.
// ---------------------------------------------------------------------------
class VideoBuilder {
public:
    void run(ConfigManager& config);

    // Optional progress callback — set by Qt layer to route updates to a
    // progress bar widget instead of the terminal.
    // Args: stage label (e.g. "collage:4K"), percent complete (0–100), ETA seconds.
    // Null (default) = draw to terminal.
    std::function<void(const std::string& label, int pct, int etaSecs)> progressCallback;

    // Called from find_trips interactive browser.
    VideoOptions configureOptions(ConfigManager& config, Trip& trip);
    void buildTrip(Trip& trip, const VideoOptions& opts);

private:
    bool buildCameraFile(const Trip& trip,
                         const std::string& camera,
                         const std::vector<std::string>& segments,
                         const VideoOptions& opts);

    bool buildCollage4K(const Trip& trip,
                        const VideoOptions& opts);

    bool buildCollage1080(const std::string& source4K,
                          const std::string& outputPath,
                          const VideoOptions& opts);

    // Direct 1080p collage from source segments (no 4K master required).
    // Each cell is 960x540; output is 1920x1080.
    bool buildCollage1080Direct(const Trip& trip, const VideoOptions& opts);

    // Extract audio track from a camera's segments to m4a/mp3/aac.
    bool buildAudioFile(const Trip& trip,
                        const std::vector<std::string>& segments,
                        const VideoOptions& opts);

    // Get duration of a video file in seconds via ffprobe.
    double getFileDuration(const std::string& file,
                           const std::string& ffprobePath);

    // Mode 2: interactive free-form collage from existing files.
    void runCollageFromFiles(ConfigManager& config);

    // Build a padded input for collage — appends fade-to-black + logo hold
    // to reach targetDuration. Returns path to padded temp file.
    // If filePath is empty, generates a full-duration logo placeholder.
    std::string buildPaddedInput(const std::string& filePath,
                                  double fileDuration,
                                  double targetDuration,
                                  int slotIndex,
                                  const std::string& ffmpegPath,
                                  const std::string& logoPath,
                                  const std::string& outDir);

    // Build the 2x2 xstack collage from four pre-padded input files.
    bool buildCollageFromSlots(const CollageOptions& opts,
                               const std::string& outputPath,
                               const std::array<std::string,4>& inputs);

    // Rename tracking — populated when an output path is changed to avoid
    // overwriting an existing file. Reported once at end of run().
    std::vector<std::pair<std::string,std::string>> renamedFiles;

    // --- Helpers ---
    // Write a ffmpeg concat list file to a temp path, return the path.
    std::string writeConcatList(const std::vector<std::string>& files,
                                const std::string& tmpPath);

    // Get frame count of a video file via ffprobe.
    int getFrameCount(const std::string& file, const std::string& ffprobePath);

    // Resolve output directory: opts.outputDir → config default → ".".
    std::string resolveOutputDir(const VideoOptions& opts);

    // Build output filename: <date>_<startTime>_<camera>.<ext>
    // If basenameOverride is non-empty it replaces the date_time prefix.
    std::string makeOutputName(const Trip& trip,
                               const std::string& label,
                               const std::string& ext,
                               const std::string& basenameOverride = "");

    // Run an ffmpeg command, stream output to terminal.
    // Returns true if ffmpeg exits 0.
    bool runFfmpeg(const std::string& cmd);

    // Run an ffmpeg command with a live progress bar.
    // label            — stage name shown in bar / passed to progressCallback
    // totalDurationSecs — expected output duration; used for % and ETA.
    // Falls back to runFfmpeg() if duration is unknown, debug mode is active,
    // or the platform doesn't support named pipes.
    bool runFfmpegWithProgress(const std::string& cmd,
                               const std::string& label,
                               int totalDurationSecs);

    // Interactive trip picker — shows lastPath trips, offers manifest switch.
    // Returns selected trip index or -1 to abort.
    int pickTrip(const std::vector<Trip>& trips,
                 ConfigManager& config,
                 std::vector<Trip>& tripsOut,
                 std::string& manifestIdOut);

    // Run trip validation against source files, print results inline.
    void validateTrip(const Trip& trip, const std::string& sourcePath);
};

#endif
// SN: 00089
