#include "video_build.hpp"
#include "logger.hpp"
#include "compat.hpp"
#ifndef _WIN32
#  include <sys/wait.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <fcntl.h>
#endif
#include <functional>
#include "ui_helpers.hpp"
#include "version.hpp"
#include "json.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <array>

namespace fs = std::filesystem;
using namespace Pathmux;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// appendBuildLog — append one entry to pm_buildlog.json in sourcePath.
// Records the build configuration, output location, trip duration, and the
// full segment manifest for provenance.  New entries are appended; the file
// grows over time and is never truncated by PathMux.
// ---------------------------------------------------------------------------
static void appendBuildLog(const Trip& trip, const VideoOptions& opts, int outputDuration) {
    if (opts.sourcePath.empty()) return;

    std::string logFile = opts.sourcePath + "/pm_buildlog.json";

    // Use ordered_json so keys appear in insertion order, not alphabetically.
    using ojson = nlohmann::ordered_json;

    // Load existing log (parse as regular json, will be re-serialised ordered below)
    ojson log = ojson::array();
    {
        std::ifstream ifs(logFile);
        if (ifs.is_open()) {
            try { ifs >> log; } catch (...) { log = ojson::array(); }
        }
    }

    // Build timestamp: YYYYMMDD_HHMMSS
    auto now = std::time(nullptr);
    std::tm tmBuf{};
    localtime_r(&now, &tmBuf);
    char tsBuf[20];
    std::strftime(tsBuf, sizeof(tsBuf), "%Y%m%d_%H%M%S", &tmBuf);

    // output_filename_stem: date+time prefix, same stripping as makeOutputName
    std::string date = trip.date;
    std::string time = trip.startTime;
    date.erase(std::remove(date.begin(), date.end(), '-'), date.end());
    time.erase(std::remove(time.begin(), time.end(), ':'), time.end());
    if (time.size() > 6) time = time.substr(0, 6);

    // Basename helper
    auto bn = [](const std::string& p) { return pathBasename(p); };

    // Build entry — keys in logical reading order:
    //   1. Build identity    2. Output location + duration
    //   3. Trip note         4. Per-camera file flags + container format
    //   5. Collage options   6. Audio extract options
    //   7. Source segments
    ojson entry;

    // --- Build identity ---
    entry["timestamp"]            = std::string(tsBuf);
    entry["manifest_id"]          = opts.manifestId;
    entry["trip_id"]              = trip.id;

    // --- Output ---
    entry["output_path"]          = opts.outputDir.empty() ? "." : opts.outputDir;
    entry["output_filename_stem"] = opts.basenameOverride.empty()
                                    ? (date + "_" + time) : opts.basenameOverride;
    entry["output_duration"]      = outputDuration;

    // --- Trip note ---
    entry["note"]                 = trip.note;

    // --- Per-camera files ---
    entry["front"]                = opts.buildFront;
    entry["rear"]                 = opts.buildRear;
    entry["left"]                 = opts.buildLeft;
    entry["right"]                = opts.buildRight;
    entry["camera_format"]        = opts.containerFormat.empty()
                                    ? "mp4" : opts.containerFormat;

    // --- Collage ---
    entry["collage_4k"]           = opts.buildCollage4K;
    entry["collage_1080p"]        = opts.buildCollage1080;
    entry["collage_audio"]        = opts.buildCollage4K
                                    ? ojson(opts.audioSource) : ojson(nullptr);

    // --- Audio extract ---
    entry["audio"]                = opts.buildAudio;
    entry["audio_format"]         = opts.audioExtractFormat.empty()
                                    ? "m4a" : opts.audioExtractFormat;

    // --- Segments: split into "video" and "audio" sub-objects so it is
    // unambiguous which camera supplied which type of output.
    // "video" = cameras consumed for per-camera files and/or collage.
    // "audio" = the single camera from which the audio track was extracted;
    //           only present when an audio extract build was run.
    bool anyCollage  = opts.buildCollage4K || opts.buildCollage1080;
    bool videoFront  = opts.buildFront  || anyCollage;
    bool videoRear   = opts.buildRear   || anyCollage;
    bool videoLeft   = opts.buildLeft   || anyCollage;
    bool videoRight  = opts.buildRight  || anyCollage;

    ojson segs = ojson::object();
    segs["source_path"] = opts.sourcePath;

    // Video sub-object — omit entirely if no video outputs were built
    if (videoFront || videoRear || videoLeft || videoRight) {
        ojson frontSegs = ojson::array(), rearSegs  = ojson::array();
        ojson leftSegs  = ojson::array(), rightSegs = ojson::array();
        for (const auto& seg : trip.segments) {
            if (videoFront && seg.front != "-" && !seg.front.empty()) frontSegs.push_back(bn(seg.front));
            if (videoRear  && seg.rear  != "-" && !seg.rear.empty())  rearSegs.push_back(bn(seg.rear));
            if (videoLeft  && seg.left  != "-" && !seg.left.empty())  leftSegs.push_back(bn(seg.left));
            if (videoRight && seg.right != "-" && !seg.right.empty()) rightSegs.push_back(bn(seg.right));
        }
        ojson vid = ojson::object();
        if (!frontSegs.empty()) vid["front"] = frontSegs;
        if (!rearSegs.empty())  vid["rear"]  = rearSegs;
        if (!leftSegs.empty())  vid["left"]  = leftSegs;
        if (!rightSegs.empty()) vid["right"] = rightSegs;
        if (!vid.empty()) segs["video"] = vid;
    }

    // Audio sub-object — only present when an audio extract was built
    if (opts.buildAudio) {
        const std::string& ac = opts.audioExtractCamera;
        ojson camSegs = ojson::array();
        for (const auto& seg : trip.segments) {
            std::string f;
            if      (ac == "front") f = seg.front;
            else if (ac == "rear")  f = seg.rear;
            else if (ac == "right") f = seg.right;
            else                    f = seg.left;
            if (f != "-" && !f.empty()) camSegs.push_back(bn(f));
        }
        if (!camSegs.empty()) {
            ojson aud = ojson::object();
            aud[ac] = camSegs;
            segs["audio"] = aud;
        }
    }

    entry["segments"] = segs;

    log.push_back(entry);

    std::ofstream ofs(logFile);
    if (!ofs.is_open()) {
        std::cerr << "Warning: Could not write buildlog: " << logFile << "\n";
        return;
    }
    ofs << log.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool VideoBuilder::runFfmpeg(const std::string& cmd) {
    // Insert -loglevel/-stats after binary name for clean terminal output
    std::string fullCmd = cmd;
    auto pos = fullCmd.find(' ');
    if (pos != std::string::npos)
        fullCmd.insert(pos, " -loglevel warning -stats");

    LOG_NORMAL("ffmpeg: " + fullCmd);

    int ret = 0;
    if (Logger::instance().isDebug()) {
        // Capture full output in debug mode
        std::string captureCmd = fullCmd + " 2>&1";
        FILE* pipe = popen(captureCmd.c_str(), "r");
        if (!pipe) { LOG_NORMAL("ffmpeg: popen failed"); return false; }
        std::string output;
        char buf[1024];
        while (fgets(buf, sizeof(buf), pipe)) {
            output += buf;
            std::cout << buf;  // still show progress on terminal
        }
        ret = WEXITSTATUS(pclose(pipe));
        LOG_CMD(fullCmd, ret, output);
    } else {
        std::cout << "\n>> " << fullCmd << "\n\n";
        ret = WEXITSTATUS(std::system(fullCmd.c_str()));
        LOG_CMD(fullCmd, ret, "");
    }
    return ret == 0;
}

// ---------------------------------------------------------------------------
// ffprobeFromFfmpeg — derive ffprobe path from ffmpeg path.
// Replaces trailing "ffmpeg" with "ffprobe".  If the path doesn't end in
// "ffmpeg" (e.g. bare "ffmpeg" command), falls back to "ffprobe".
// ---------------------------------------------------------------------------
static std::string ffprobeFromFfmpeg(const std::string& ffmpegPath) {
    const std::string needle = "ffmpeg";
    if (ffmpegPath.size() >= needle.size() &&
        ffmpegPath.substr(ffmpegPath.size() - needle.size()) == needle)
        return ffmpegPath.substr(0, ffmpegPath.size() - needle.size()) + "ffprobe";
    return "ffprobe";
}

// ---------------------------------------------------------------------------
// drawProgressLine — overwrite current terminal line with a progress bar.
// Format:  <label padded to 16>  [====>        ] NNN%  ETA: M:SS
// Bar width is computed dynamically from terminal width (min 20 cols).
// Called repeatedly with \r; caller prints \n when done.
// ---------------------------------------------------------------------------
static void drawProgressLine(const std::string& label,
                              int64_t doneUs, int64_t totalUs, double speed) {
    double pct    = (totalUs > 0) ? std::min(1.0, (double)doneUs / (double)totalUs) : 0.0;
    int    pctInt = (int)(pct * 100);

    int etaSecs = 0;
    if (speed > 0.001 && pct < 0.999 && pct > 0.0) {
        double etaD = (double)(totalUs - doneUs) / (speed * 1000000.0);
        if (etaD >= 0.0 && etaD < 86400.0) etaSecs = (int)etaD;
    }

    const int W = std::max(20, Pathmux::Platform::getTerminalWidth() - 42);
    int filled = (int)(pct * W);
    std::string bar;
    bar.reserve(W);
    for (int i = 0; i < W; ++i) {
        if      (i < filled)              bar += '=';
        else if (i == filled && pctInt < 100) bar += '>';
        else                              bar += ' ';
    }

    char etaBuf[16];
    if (pctInt >= 100)
        std::snprintf(etaBuf, sizeof(etaBuf), "Done");
    else
        std::snprintf(etaBuf, sizeof(etaBuf), "%d:%02d", etaSecs / 60, etaSecs % 60);

    // Pad label to 16 chars
    std::string lbl = label;
    if (lbl.size() < 16) lbl.append(16 - lbl.size(), ' ');
    else if (lbl.size() > 16) lbl = lbl.substr(0, 16);

    std::cout << "\r  " << lbl << " [" << bar << "] "
              << std::setw(3) << pctInt << "%  ETA: " << etaBuf
              << "   " << std::flush;
}

// ---------------------------------------------------------------------------
// runFfmpegWithProgress — run ffmpeg and display a live progress bar.
//
// Uses a POSIX named pipe and ffmpeg's -progress option to get machine-
// readable key=value progress updates.  Parses out_time_us= and speed= to
// compute percentage and ETA.
//
// label         — stage name shown in the bar (e.g. "concat:Front",
//                 "collage:4K").  Also passed to progressCallback if set.
// totalDurationSecs — known output duration; used for % and ETA.
//
// If progressCallback is set on the VideoBuilder instance, it is called
// instead of drawing to the terminal — this is the Qt hook.
//
// Falls back to runFfmpeg() if:
//   - totalDurationSecs <= 0 (duration unknown)
//   - debug logging is active (user wants full ffmpeg output)
//   - platform is Windows (_WIN32)
//   - named pipe creation fails
// ---------------------------------------------------------------------------
bool VideoBuilder::runFfmpegWithProgress(const std::string& cmd,
                                          const std::string& label,
                                          int totalDurationSecs) {
#ifdef _WIN32
    return runFfmpeg(cmd);
#else
    if (totalDurationSecs <= 0 || Logger::instance().isDebug())
        return runFfmpeg(cmd);

    // Create a temporary named pipe for ffmpeg's -progress output
    char pipePath[64];
    std::snprintf(pipePath, sizeof(pipePath), "/tmp/pm_progress_XXXXXX");
    {
        int fd = mkstemp(pipePath);
        if (fd < 0) return runFfmpeg(cmd);
        close(fd);
        unlink(pipePath);
        if (mkfifo(pipePath, 0600) != 0) return runFfmpeg(cmd);
    }

    // Build command: suppress ffmpeg log output, direct progress to pipe
    std::string fullCmd = cmd;
    auto pos = fullCmd.find(' ');
    if (pos != std::string::npos)
        fullCmd.insert(pos, " -loglevel quiet");
    fullCmd += " -progress \"";
    fullCmd += pipePath;
    fullCmd += "\"";

    LOG_NORMAL("ffmpeg: " + fullCmd);

    pid_t pid = fork();
    if (pid < 0) {
        unlink(pipePath);
        return runFfmpeg(cmd);
    }

    if (pid == 0) {
        // Child: exec ffmpeg via shell
        execl("/bin/sh", "sh", "-c", fullCmd.c_str(), (char*)nullptr);
        _exit(127);
    }

    // Parent: open named pipe for reading immediately (O_NONBLOCK = no
    // blocking even if ffmpeg hasn't opened the write end yet)
    int pipeFd = open(pipePath, O_RDONLY | O_NONBLOCK);
    if (pipeFd < 0) {
        int st; waitpid(pid, &st, 0);
        unlink(pipePath);
        return WEXITSTATUS(st) == 0;
    }

    int64_t totalUs   = (int64_t)totalDurationSecs * 1000000LL;
    int64_t outTimeUs = 0;
    double  speed     = 1.0;
    bool    childDone = false;
    int     childSt   = 0;
    std::string lineBuf;

    while (!childDone) {
        char tmp[4096];
        ssize_t n = read(pipeFd, tmp, sizeof(tmp) - 1);

        if (n > 0) {
            tmp[n] = '\0';
            lineBuf += tmp;
            // Process all complete key=value lines
            size_t p = 0, q;
            while ((q = lineBuf.find('\n', p)) != std::string::npos) {
                std::string line = lineBuf.substr(p, q - p);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                p = q + 1;

                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);

                if (key == "out_time_us") {
                    try { outTimeUs = std::stoll(val); } catch (...) {}
                } else if (key == "speed") {
                    if (!val.empty() && val.back() == 'x') val.pop_back();
                    try { speed = std::stod(val); } catch (...) {}
                } else if (key == "progress" && val == "end") {
                    childDone = true;
                }

                // Redraw after each parsed line
                if (progressCallback)
                    progressCallback(label,
                                     (int)(std::min(1.0, (double)outTimeUs / totalUs) * 100),
                                     speed > 0.001 ? (int)((totalUs - outTimeUs) / (speed * 1000000.0)) : 0);
                else
                    drawProgressLine(label, outTimeUs, totalUs, speed);
            }
            lineBuf = lineBuf.substr(p);

        } else if (n == 0) {
            // POSIX: read() on a FIFO returns 0 when no writer has the pipe
            // open yet — NOT necessarily true EOF.  Treat the same as EAGAIN:
            // check whether the child has exited; if not, keep waiting.
            int wret = waitpid(pid, &childSt, WNOHANG);
            if (wret == pid) childDone = true;
            else             usleep(50000);  // 50 ms — no writer connected yet
        } else {
            // EAGAIN — writer connected but no new data; check child status
            int wret = waitpid(pid, &childSt, WNOHANG);
            if (wret == pid) childDone = true;
            else             usleep(50000);  // 50 ms
        }
    }

    // Final: show 100% then newline
    if (!progressCallback)
        drawProgressLine(label, totalUs, totalUs, speed);
    else
        progressCallback(label, 100, 0);
    std::cout << "\n";

    // Wait for child to fully exit if not already reaped
    waitpid(pid, &childSt, 0);
    close(pipeFd);
    unlink(pipePath);

    LOG_CMD(fullCmd, WEXITSTATUS(childSt), "");
    return WEXITSTATUS(childSt) == 0;
#endif
}

std::string VideoBuilder::resolveOutputDir(const VideoOptions& opts) {
    if (!opts.outputDir.empty())       return opts.outputDir;
    if (!opts.ffmpegPath.empty()) {
        // ffmpegPath is a tool path, not a directory — handled in caller
    }
    return ".";
}

// ---------------------------------------------------------------------------
// sanitizeBasename — whitelist-filter a user-supplied basename override.
// Keeps alphanumeric, dash, underscore, dot, space.
// Trims leading/trailing spaces; rejects leading dot; caps at 64 chars.
// Returns "" if the result is empty or otherwise unusable.
// ---------------------------------------------------------------------------
static std::string sanitizeBasename(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == ' ')
            out += c;
    }
    // Trim leading/trailing spaces
    size_t s = out.find_first_not_of(' ');
    if (s == std::string::npos) return "";
    size_t e = out.find_last_not_of(' ');
    out = out.substr(s, e - s + 1);
    // Reject leading dot — hidden-file / relative-path escape
    if (!out.empty() && out[0] == '.') return "";
    // Cap length
    if (out.size() > 64) out = out.substr(0, 64);
    return out;
}

std::string VideoBuilder::makeOutputName(const Trip& trip,
                                          const std::string& label,
                                          const std::string& ext,
                                          const std::string& basenameOverride) {
    std::string base;
    if (!basenameOverride.empty()) {
        base = basenameOverride;
    } else {
        // Sanitize date and time: "2026-02-16" → "20260216", "09:54:22" → "095422"
        std::string date = trip.date;
        std::string time = trip.startTime;
        date.erase(std::remove(date.begin(), date.end(), '-'), date.end());
        time.erase(std::remove(time.begin(), time.end(), ':'), time.end());
        if (time.size() > 6) time = time.substr(0, 6);
        base = date + "_" + time;
    }
    return base + "_" + label + "." + ext;
}

std::string VideoBuilder::writeConcatList(const std::vector<std::string>& files,
                                           const std::string& tmpPath) {
    std::ofstream ofs(tmpPath);
    if (!ofs.is_open()) {
        std::cerr << "Error: Could not write concat list to " << tmpPath << "\n";
        return "";
    }
    for (const auto& f : files) {
        // ffmpeg concat demuxer requires escaped single quotes
        std::string escaped = f;
        // Replace ' with '\'' for shell safety
        size_t pos = 0;
        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "'\\''");
            pos += 4;
        }
        ofs << "file '" << escaped << "'\n";
    }
    return tmpPath;
}

int VideoBuilder::getFrameCount(const std::string& file,
                                 const std::string& ffprobePath) {
    std::string cmd = ffprobePath +
        " -v error -select_streams v:0 -count_packets"
        " -show_entries stream=nb_read_packets"
        " -of csv=p=0 \"" + file + "\" " NULL_REDIRECT;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return -1;
    int count = -1;
    fscanf(pipe, "%d", &count);
    pclose(pipe);
    return count;
}

// ---------------------------------------------------------------------------
// buildCameraFile — concat all segments for one camera into a single file.
// Uses ffmpeg concat demuxer with stream copy.
// Maps only video stream 0 and audio stream 0 — strips GPS/data tracks.
// ---------------------------------------------------------------------------
bool VideoBuilder::buildCameraFile(const Trip& trip,
                                    const std::string& camera,
                                    const std::vector<std::string>& segments,
                                    const VideoOptions& opts) {
    if (segments.empty()) {
        std::cout << "  Skipping " << camera << " — no segments found.\n";
        return true;
    }

    std::string outDir  = opts.outputDir.empty() ? "." : opts.outputDir;
    std::string proposed = (fs::path(outDir) /
                            makeOutputName(trip, camera, opts.containerFormat,
                                           opts.basenameOverride)).string();
    std::string outFile  = UI::confirmOutputPath(proposed);
    if (outFile != proposed) renamedFiles.push_back({proposed, outFile});
    std::string listFile = (fs::path(outDir) / ("pm_tmp_concat_" + camera + ".txt")).string();

    if (writeConcatList(segments, listFile).empty()) return false;

    // -map 0:v:0 -map 0:a:0  — video track 0 and audio track 0 only.
    // This drops the LIGOGPSINFO data stream from the .ts container.
    // -c copy — stream copy, no re-encode, fast and lossless.
    // -movflags +faststart — moves moov atom to front for streaming (mp4).
    std::string flags = (opts.containerFormat == "mp4")
                      ? " -movflags +faststart" : "";

    std::ostringstream cmd;
    cmd << opts.ffmpegPath
        << " -y -f concat -safe 0 -i \"" << listFile << "\""
        << " -map 0:v:0 -map 0:a:0"
        << " -c copy"
        << flags
        << " \"" << outFile << "\"";

    int totalSecs = (trip.durationFFProbed > 0) ? trip.durationFFProbed
                                                 : trip.segDetectedDuration;
    bool ok = runFfmpegWithProgress(cmd.str(), "concat:" + camera, totalSecs);

    // Clean up temp file
    fs::remove(listFile);

    if (!ok) std::cerr << "  ffmpeg failed for camera: " << camera << "\n";
    return ok;
}

// ---------------------------------------------------------------------------
// buildCollage4K — sync-normalize 4 cameras and build 2x2 grid at native res.
//
// Each camera cell is 1920x1080. Grid is 3840x2160 (true 4K).
// Sync normalization: per-segment, pad the shorter cameras with fade-to-black
// to match the longest camera's frame count.
// Audio: Left camera only.
// Codec: libx265, CRF 18 (visually lossless).
// ---------------------------------------------------------------------------
bool VideoBuilder::buildCollage4K(const Trip& trip,
                                   const VideoOptions& opts) {
    if (trip.segments.empty()) {
        std::cerr << "Error: Trip has no segments.\n";
        return false;
    }

    std::string outDir    = opts.outputDir.empty() ? "." : opts.outputDir;
    std::string tmpDir    = opts.tmpDir.empty() ? (outDir + "/pm_tmp") : opts.tmpDir;
    std::string proposed  = (fs::path(outDir) /
                             makeOutputName(trip, "Collage_4K", "mp4",
                                            opts.basenameOverride)).string();
    std::string outFile   = UI::confirmOutputPath(proposed);
    if (outFile != proposed) renamedFiles.push_back({proposed, outFile});

    // -----------------------------------------------------------------------
    // Build concat lists directly from source segments.
    // Single encode pass: source → filter_complex → collage output.
    // No intermediate re-encode; full original quality fed to the encoder.
    // Missing camera segments ("-") are skipped per-camera.  A camera absent
    // from all segments gets a lavfi black+silent input so the grid stays
    // filled; -shortest stops the encode when the real streams end.
    // -----------------------------------------------------------------------
    std::vector<std::string> srcFront, srcRear, srcLeft, srcRight;
    for (const auto& seg : trip.segments) {
        if (!seg.front.empty() && seg.front != "-") srcFront.push_back(seg.front);
        if (!seg.rear.empty()  && seg.rear  != "-") srcRear.push_back(seg.rear);
        if (!seg.left.empty()  && seg.left  != "-") srcLeft.push_back(seg.left);
        if (!seg.right.empty() && seg.right != "-") srcRight.push_back(seg.right);
    }

    if (srcFront.empty()) {
        std::cerr << "Error: No front-camera segments found for collage.\n";
        return false;
    }

    // Ensure tmp directory exists for concat list files
    {
        std::error_code ec;
        fs::create_directories(tmpDir, ec);
        if (ec) {
            std::cerr << "Error: Could not create tmp directory: "
                      << tmpDir << "\n  " << ec.message() << "\n";
            return false;
        }
    }

    // Write concat lists for cameras that have segments
    auto makeList = [&](const std::vector<std::string>& segs,
                        const std::string& name) -> std::string {
        if (segs.empty()) return "";
        return writeConcatList(segs,
            (fs::path(tmpDir) / ("pm_tmp_col_" + name + ".txt")).string());
    };

    std::string listF = makeList(srcFront, "front");
    std::string listR = makeList(srcRear,  "rear");
    std::string listL = makeList(srcLeft,  "left");
    std::string listG = makeList(srcRight, "right");

    if (listF.empty()) {
        std::cerr << "Error: Failed to write front concat list.\n";
        return false;
    }

    // -----------------------------------------------------------------------
    // Build 2x2 collage via ffmpeg filter graph.
    // Layout (top-down perspective — matches intuitive driver viewpoint):
    //   [Front] [Rear ]     top row
    //   [Right] [Left ]     bottom row — rear-facing side cams mirrored
    // Audio: from defaultAudioSource preference (default: Left camera).
    // -----------------------------------------------------------------------
    // input indices: 0=front 1=rear 2=left 3=right
    // xstack order:  front(v0) top-left, rear(v1) top-right,
    //                right(v3) bottom-left, left(v2) bottom-right

    // Resolve audio input index from preference
    std::string audioSrc = opts.audioSource;
    int audioIdx = 2; // default: left camera
    if      (audioSrc == "right") audioIdx = 3;
    else if (audioSrc == "front") audioIdx = 0;
    else if (audioSrc == "rear")  audioIdx = 1;

    std::ostringstream cmd;
    cmd << opts.ffmpegPath << " -y";
    if (!opts.encode.hwDevice.empty())
        cmd << " -init_hw_device " << opts.encode.hwDeviceType
            << "=" << opts.encode.hwDeviceType << ":" << opts.encode.hwDevice;

    // Input 0: front (always present)
    cmd << " -f concat -safe 0 -i \"" << listF << "\"";
    // Inputs 1-3: use source concat list if available, lavfi black otherwise
    auto addInput = [&](const std::string& list) {
        if (!list.empty())
            cmd << " -f concat -safe 0 -i \"" << list << "\"";
        else
            cmd << " -f lavfi -i \"color=c=black:s=1920x1080:r=25,format="
                << opts.encode.pixFmt << "\"";
    };
    addInput(listR);
    addInput(listL);
    addInput(listG);

    cmd << " -filter_complex \""
        <<   "[0:v]scale=1920:1080,format=" << opts.encode.pixFmt << "[v0];"
        <<   "[1:v]scale=1920:1080,format=" << opts.encode.pixFmt << "[v1];"
        <<   "[2:v]scale=1920:1080,format=" << opts.encode.pixFmt << "[v2];"
        <<   "[3:v]scale=1920:1080,format=" << opts.encode.pixFmt << "[v3];"
        <<   "[v0][v1][v3][v2]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0[vout]\""
        << " -map \"[vout]\""
        << " -map " << audioIdx << ":a:0"
        << " -shortest"
        << " -c:v " << opts.encode.collageEncoder;
    // QSV/VAAPI/CPU: -q sets global_quality (ICQ) or constant QP.
    // NVENC: quality is controlled via -cq in extraCollageArgs; -q is redundant and wrong.
    if (opts.encode.collageEncoder.find("nvenc") == std::string::npos)
        cmd << " -q " << opts.encode.collageQuality;
    if (!opts.encode.extraCollageArgs.empty())
        cmd << " " << opts.encode.extraCollageArgs;
    cmd << " -c:a aac -b:a 96k"
        << " -movflags +faststart"
        << " \"" << outFile << "\"";

    int totalSecs = (trip.durationFFProbed > 0) ? trip.durationFFProbed
                                                 : trip.segDetectedDuration;
    bool ok = runFfmpegWithProgress(cmd.str(), "collage:4K", totalSecs);

    if (ok) {
        // Clean up concat lists
        if (!listF.empty()) fs::remove(listF);
        if (!listR.empty()) fs::remove(listR);
        if (!listL.empty()) fs::remove(listL);
        if (!listG.empty()) fs::remove(listG);
        // Remove tmp dir if auto-created and now empty
        if (opts.tmpDir.empty()) {
            std::error_code ec;
            fs::remove(tmpDir, ec);
        }
    } else {
        std::cerr << "  ffmpeg failed building 4K collage.\n";
        std::cerr << "  Temp files preserved in: " << tmpDir << "\n";
        std::cerr << "  Remove manually once issue is resolved.\n";
    }
    return ok;
}

// ---------------------------------------------------------------------------
// buildCollage1080 — transcode 4K master to 1080p.
// Stream copy audio — no re-encode needed.
// ---------------------------------------------------------------------------
bool VideoBuilder::buildCollage1080(const std::string& source4K,
                                     const std::string& outputPath,
                                     const VideoOptions& opts) {
    std::ostringstream cmd;
    cmd << opts.ffmpegPath << " -y";
    if (!opts.encode.hwDevice.empty())
        cmd << " -init_hw_device " << opts.encode.hwDeviceType
            << "=" << opts.encode.hwDeviceType << ":" << opts.encode.hwDevice;
    // CPU scale first, then upload to GPU for encode.
    // scale_cuda/scale_qsv/scale_vaapi are not universally compiled into ffmpeg
    // builds — CPU scale is trivial for a single 4K→1080p pass and always works.
    std::string downVf = opts.encode.hwDevice.empty()
        ? "scale=1920:1080"
        : "scale=1920:1080,format=" + opts.encode.pixFmt + ",hwupload=extra_hw_frames=64";
    cmd << " -i \"" << source4K << "\""
        << " -vf \"" << downVf << "\""
        << " -c:v " << opts.encode.downEncoder;
    // NVENC: quality via -cq in extraDownArgs; -q is wrong flag for nvenc.
    if (opts.encode.downEncoder.find("nvenc") == std::string::npos)
        cmd << " -q " << opts.encode.downQuality;
    if (!opts.encode.extraDownArgs.empty())
        cmd << " " << opts.encode.extraDownArgs;
    cmd << " -c:a copy"
        << " -movflags +faststart"
        << " \"" << outputPath << "\"";

    double srcDur = getFileDuration(source4K, ffprobeFromFfmpeg(opts.ffmpegPath));
    bool ok = runFfmpegWithProgress(cmd.str(), "collage:1080p", (int)srcDur);
    if (!ok) std::cerr << "  ffmpeg failed building 1080p collage.\n";
    return ok;
}

// ---------------------------------------------------------------------------
// buildCollage1080Direct — build 1080p collage directly from source segments.
// Used when 4K master is not requested. Each camera cell is 960x540;
// output is 1920x1080. Same encoder settings as the 4K collage path.
// ---------------------------------------------------------------------------
bool VideoBuilder::buildCollage1080Direct(const Trip& trip,
                                           const VideoOptions& opts) {
    if (trip.segments.empty()) {
        std::cerr << "Error: Trip has no segments.\n";
        return false;
    }

    std::string outDir   = opts.outputDir.empty() ? "." : opts.outputDir;
    std::string tmpDir   = opts.tmpDir.empty() ? (outDir + "/pm_tmp") : opts.tmpDir;
    std::string proposed = (fs::path(outDir) /
                            makeOutputName(trip, "Collage_1080p", "mp4",
                                           opts.basenameOverride)).string();
    std::string outFile  = UI::confirmOutputPath(proposed);
    if (outFile != proposed) renamedFiles.push_back({proposed, outFile});

    std::vector<std::string> srcFront, srcRear, srcLeft, srcRight;
    for (const auto& seg : trip.segments) {
        if (!seg.front.empty() && seg.front != "-") srcFront.push_back(seg.front);
        if (!seg.rear.empty()  && seg.rear  != "-") srcRear.push_back(seg.rear);
        if (!seg.left.empty()  && seg.left  != "-") srcLeft.push_back(seg.left);
        if (!seg.right.empty() && seg.right != "-") srcRight.push_back(seg.right);
    }

    if (srcFront.empty()) {
        std::cerr << "Error: No front-camera segments found for collage.\n";
        return false;
    }

    {
        std::error_code ec;
        fs::create_directories(tmpDir, ec);
        if (ec) {
            std::cerr << "Error: Could not create tmp directory: "
                      << tmpDir << "\n  " << ec.message() << "\n";
            return false;
        }
    }

    auto makeList = [&](const std::vector<std::string>& segs,
                        const std::string& name) -> std::string {
        if (segs.empty()) return "";
        return writeConcatList(segs,
            (fs::path(tmpDir) / ("pm_tmp_col1080_" + name + ".txt")).string());
    };

    std::string listF = makeList(srcFront, "front");
    std::string listR = makeList(srcRear,  "rear");
    std::string listL = makeList(srcLeft,  "left");
    std::string listG = makeList(srcRight, "right");

    if (listF.empty()) {
        std::cerr << "Error: Failed to write front concat list.\n";
        return false;
    }

    std::string audioSrc = opts.audioSource;
    int audioIdx = 2;
    if      (audioSrc == "right") audioIdx = 3;
    else if (audioSrc == "front") audioIdx = 0;
    else if (audioSrc == "rear")  audioIdx = 1;

    std::ostringstream cmd;
    cmd << opts.ffmpegPath << " -y";
    if (!opts.encode.hwDevice.empty())
        cmd << " -init_hw_device " << opts.encode.hwDeviceType
            << "=" << opts.encode.hwDeviceType << ":" << opts.encode.hwDevice;

    cmd << " -f concat -safe 0 -i \"" << listF << "\"";
    auto addInput = [&](const std::string& list) {
        if (!list.empty())
            cmd << " -f concat -safe 0 -i \"" << list << "\"";
        else
            cmd << " -f lavfi -i \"color=c=black:s=960x540:r=25,format="
                << opts.encode.pixFmt << "\"";
    };
    addInput(listR);
    addInput(listL);
    addInput(listG);

    // Each cell 960x540; 2x2 grid = 1920x1080
    cmd << " -filter_complex \""
        <<   "[0:v]scale=960:540,format=" << opts.encode.pixFmt << "[v0];"
        <<   "[1:v]scale=960:540,format=" << opts.encode.pixFmt << "[v1];"
        <<   "[2:v]scale=960:540,format=" << opts.encode.pixFmt << "[v2];"
        <<   "[3:v]scale=960:540,format=" << opts.encode.pixFmt << "[v3];"
        <<   "[v0][v1][v3][v2]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0[vout]\""
        << " -map \"[vout]\""
        << " -map " << audioIdx << ":a:0"
        << " -shortest"
        << " -c:v " << opts.encode.collageEncoder;
    if (opts.encode.collageEncoder.find("nvenc") == std::string::npos)
        cmd << " -q " << opts.encode.collageQuality;
    if (!opts.encode.extraCollageArgs.empty())
        cmd << " " << opts.encode.extraCollageArgs;
    cmd << " -c:a aac -b:a 96k"
        << " -movflags +faststart"
        << " \"" << outFile << "\"";

    int totalSecs = (trip.durationFFProbed > 0) ? trip.durationFFProbed
                                                 : trip.segDetectedDuration;
    bool ok = runFfmpegWithProgress(cmd.str(), "collage:1080p", totalSecs);

    if (ok) {
        if (!listF.empty()) fs::remove(listF);
        if (!listR.empty()) fs::remove(listR);
        if (!listL.empty()) fs::remove(listL);
        if (!listG.empty()) fs::remove(listG);
        if (opts.tmpDir.empty()) {
            std::error_code ec;
            fs::remove(tmpDir, ec);
        }
    } else {
        std::cerr << "  ffmpeg failed building 1080p collage (direct).\n";
        std::cerr << "  Temp files preserved in: " << tmpDir << "\n";
        std::cerr << "  Remove manually once issue is resolved.\n";
    }
    return ok;
}

// ---------------------------------------------------------------------------
// buildAudioFile — extract audio track from a camera's segments.
// Concatenates all segments, strips video, outputs m4a/mp3/aac.
// ---------------------------------------------------------------------------
bool VideoBuilder::buildAudioFile(const Trip& trip,
                                   const std::vector<std::string>& segments,
                                   const VideoOptions& opts) {
    if (segments.empty()) {
        std::cerr << "  No segments for audio extract camera '"
                  << opts.audioExtractCamera << "'.\n";
        return false;
    }

    std::string outDir   = opts.outputDir.empty() ? "." : opts.outputDir;
    std::string listPath = (fs::path(outDir) / "pm_tmp_audio_concat.txt").string();
    std::string concatList = writeConcatList(segments, listPath);
    if (concatList.empty()) return false;

    // Determine codec and container from format choice
    std::string codec, ext;
    const std::string& fmt = opts.audioExtractFormat;
    if (fmt == "mp3") {
        codec = "-c:a libmp3lame -q:a 2";
        ext   = "mp3";
    } else if (fmt == "aac") {
        codec = "-c:a copy";            // remux — AAC is already the source codec
        ext   = "aac";
    } else {
        // m4a default — AAC in MPEG-4 container, lossless remux
        codec = "-c:a copy";
        ext   = "m4a";
    }

    // Camera label capitalised for filename
    std::string camLabel = opts.audioExtractCamera;
    if (!camLabel.empty()) camLabel[0] = std::toupper((unsigned char)camLabel[0]);

    std::string proposed = (fs::path(opts.outputDir.empty() ? "." : opts.outputDir)
                           / makeOutputName(trip, "Audio_" + camLabel, ext,
                                            opts.basenameOverride)).string();
    std::string outFile  = UI::confirmOutputPath(proposed);
    if (outFile != proposed) renamedFiles.push_back({proposed, outFile});

    std::ostringstream cmd;
    cmd << opts.ffmpegPath
        << " -y"
        << " -f concat -safe 0 -i \"" << concatList << "\""
        << " -vn"               // no video
        << " " << codec
        << " \"" << outFile << "\"";

    std::cout << "\nExtracting audio (" << fmt << "): " << outFile << "\n";
    bool ok = runFfmpeg(cmd.str());
    fs::remove(listPath);
    if (ok) std::cout << "  Done: " << outFile << "\n";
    else    std::cerr << "  ffmpeg failed extracting audio.\n";
    return ok;
}

// ---------------------------------------------------------------------------
// getFileDuration — returns duration of a video file in seconds via ffprobe.
// ---------------------------------------------------------------------------
double VideoBuilder::getFileDuration(const std::string& file,
                                      const std::string& ffprobePath) {
    std::string cmd = ffprobePath +
        " -v error -show_entries format=duration"
        " -of csv=p=0 \"" + file + "\" " NULL_REDIRECT;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return 0.0;
    double dur = 0.0;
    if (fscanf(pipe, "%lf", &dur) != 1) dur = 0.0;
    pclose(pipe);
    return dur;
}

// ---------------------------------------------------------------------------
// buildPaddedInput — pad a slot input to targetDuration.
//
// If filePath is non-empty:
//   - Play file normally
//   - Fade video to black over 0.5s at end
//   - Hold logo on dark blue for remainder
//
// If filePath is empty (slot unassigned):
//   - Generate full-duration logo-on-dark-blue
//
// Output written to <outDir>/pm_tmp_slot_<slotIndex>.ts
// Returns output path, or "" on failure.
// ---------------------------------------------------------------------------
std::string VideoBuilder::buildPaddedInput(const std::string& filePath,
                                            double fileDuration,
                                            double targetDuration,
                                            int slotIndex,
                                            const std::string& ffmpegPath,
                                            const std::string& logoPath,
                                            const std::string& outDir) {
    std::string sid    = std::to_string(slotIndex);
    std::string outPath = (fs::path(outDir) / ("pm_tmp_slot_" + sid + ".ts")).string();
    std::ostringstream cmd;

    // Dark blue background color for logo placeholder
    const std::string bgColor = "0x00008B";
    const std::string resolution = "1920x1080";

    if (filePath.empty()) {
        // ---------------------------------------------------------------
        // Empty slot — full-duration logo on dark blue
        // ---------------------------------------------------------------
        cmd << ffmpegPath
            << " -y"
            << " -f lavfi -i \"color=c=" << bgColor
            <<   ":size=" << resolution << ":rate=30:duration=" << targetDuration << "\""
            << " -i \"" << logoPath << "\""
            << " -filter_complex \""
            <<   "[0:v][1:v]overlay=(W-w)/2:(H-h)/2:enable='between(t,0," << targetDuration << ")'[vout]\""
            << " -map \"[vout]\""
            << " -f mpegts -c:v libx264 -crf 20 -preset fast"
            << " \"" << outPath << "\"";
    } else {
        double padDuration = targetDuration - fileDuration;

        if (padDuration <= 0.01) {
            // ---------------------------------------------------------------
            // Already at target duration — just copy to ts container
            // ---------------------------------------------------------------
            cmd << ffmpegPath
                << " -y -i \"" << filePath << "\""
                << " -f mpegts -c:v libx264 -crf 20 -preset fast -c:a aac"
                << " \"" << outPath << "\"";
        } else {
            // ---------------------------------------------------------------
            // Needs padding:
            //   Part 1: original file with fade-out on last 0.5s of video
            //   Part 2: logo on dark blue for padDuration seconds
            // Build as two temp files then concat.
            // ---------------------------------------------------------------
            double fadeStart = fileDuration - 0.5;
            if (fadeStart < 0.0) fadeStart = 0.0;

            std::string part1 = (fs::path(outDir) / ("pm_tmp_slot_" + sid + "_p1.ts")).string();
            std::string part2 = (fs::path(outDir) / ("pm_tmp_slot_" + sid + "_p2.ts")).string();
            std::string concatList = (fs::path(outDir) / ("pm_tmp_slot_" + sid + "_cat.txt")).string();

            // Part 1: original + fade to black
            std::ostringstream cmd1;
            cmd1 << ffmpegPath
                 << " -y -i \"" << filePath << "\""
                 << " -vf \"fade=t=out:st=" << std::fixed << std::setprecision(3)
                 <<   fadeStart << ":d=0.5\""
                 << " -f mpegts -c:v libx264 -crf 20 -preset fast -c:a aac"
                 << " \"" << part1 << "\"";
            runFfmpeg(cmd1.str());

            // Part 2: logo on dark blue for padDuration
            std::ostringstream cmd2;
            cmd2 << ffmpegPath
                 << " -y"
                 << " -f lavfi -i \"color=c=" << bgColor
                 <<   ":size=" << resolution << ":rate=30:duration="
                 << std::fixed << std::setprecision(3) << padDuration << "\""
                 << " -i \"" << logoPath << "\""
                 << " -filter_complex \""
                 <<   "[0:v][1:v]overlay=(W-w)/2:(H-h)/2[vout]\""
                 << " -map \"[vout]\""
                 << " -f mpegts -c:v libx264 -crf 20 -preset fast"
                 << " -f lavfi -i anullsrc=r=48000:cl=stereo"  // silent audio for part2
                 << " -shortest"
                 << " \"" << part2 << "\"";
            runFfmpeg(cmd2.str());

            // Concat part1 + part2
            {
                std::ofstream ofs(concatList);
                ofs << "file '" << part1 << "'\n";
                ofs << "file '" << part2 << "'\n";
            }
            cmd << ffmpegPath
                << " -y -f concat -safe 0 -i \"" << concatList << "\""
                << " -c copy"
                << " \"" << outPath << "\"";
        }
    }

    bool ok = runFfmpeg(cmd.str());

    // Clean up part files if they exist
    for (const std::string& tmp : {
        (fs::path(outDir) / ("pm_tmp_slot_" + sid + "_p1.ts")).string(),
        (fs::path(outDir) / ("pm_tmp_slot_" + sid + "_p2.ts")).string(),
        (fs::path(outDir) / ("pm_tmp_slot_" + sid + "_cat.txt")).string()
    }) {
        std::error_code ec;
        fs::remove(tmp, ec);
    }

    return ok ? outPath : "";
}

// ---------------------------------------------------------------------------
// buildCollageFromSlots — xstack four pre-prepared inputs into a collage.
// inputs[0-3] are paths to padded .ts files (all same duration).
// ---------------------------------------------------------------------------
bool VideoBuilder::buildCollageFromSlots(const CollageOptions& opts,
                                          const std::string& outputPath,
                                          const std::array<std::string,4>& inputs) {
    std::ostringstream cmd;
    cmd << opts.ffmpegPath
        << " -y";

    for (int i = 0; i < 4; ++i)
        cmd << " -i \"" << inputs[i] << "\"";

    cmd << " -filter_complex \""
        <<   "[0:v]scale=1920:1080[v0];"
        <<   "[1:v]scale=1920:1080[v1];"
        <<   "[2:v]scale=1920:1080[v2];"
        <<   "[3:v]scale=1920:1080[v3];"
        <<   "[v0][v1][v2][v3]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0[vout]\""
        << " -map \"[vout]\""
        << " -map " << opts.audioSlot << ":a:0"
        << " -c:v libx265 -crf 18 -preset slow"
        << " -c:a aac -b:a 192k"
        << " -movflags +faststart"
        << " \"" << outputPath << "\"";

    std::cout << "\nBuilding collage: " << outputPath << "\n";
    bool ok = runFfmpeg(cmd.str());
    if (ok) std::cout << "  Done: " << outputPath << "\n";
    else    std::cerr << "  ffmpeg failed building collage.\n";
    return ok;
}

// ---------------------------------------------------------------------------
// runCollageFromFiles — mode 2 entry point.
// User assigns up to 4 video files to quadrant slots, then builds a
// synchronized collage. Clips shorter than the longest fade to black
// then hold the logo until the collage ends.
// ---------------------------------------------------------------------------
void VideoBuilder::runCollageFromFiles(ConfigManager& config) {
    CollageOptions opts;
    opts.ffmpegPath  = config.getFfmpegPath();
    opts.ffprobePath = config.getFfmpegPath();   // ffprobe lives alongside ffmpeg
    // Replace trailing "ffmpeg" with "ffprobe" if explicit path given
    if (opts.ffprobePath.size() >= 6) {
        auto& p = opts.ffprobePath;
        if (p.substr(p.size() - 6) == "ffmpeg")
            p = p.substr(0, p.size() - 6) + "ffprobe";
    }
    opts.outputDir = config.getDefaultExportDir();

    // Logo path — baked asset will replace this once xxd integration lands
    std::string logoPath = "images/PM_logo2.png";
    if (!fs::exists(logoPath)) {
        // Try relative to binary location — fallback to simple colored box
        logoPath = "";
    }

    auto slotLabel = [](const CollageSlot& s) -> std::string {
        if (s.filePath.empty()) return "(empty - logo placeholder)";
        return fs::path(s.filePath).filename().string();
    };

    while (true) {
        std::cout << "\n";
        UI::printTitle("PathMux v" + std::string(APP_VERSION)
                       + " -- Collage from Files");
        UI::printLine();
        UI::printLine("  Slots:  (top-left  top-right  bottom-left  bottom-right)");
        for (int i = 0; i < 4; ++i) {
            std::string row = "  [" + std::to_string(i + 1) + "]  "
                            + slotLabel(opts.slots[i]);
            UI::printLine(row);
        }
        UI::printLine();
        UI::printLine("  [A]  Audio slot         " + std::to_string(opts.audioSlot + 1));
        UI::printLine("  [F]  4K master (H.265)  " + std::string(opts.build4K   ? "yes" : "no"));
        UI::printLine("  [G]  1080p copy (H.264) " + std::string(opts.build1080 ? "yes" : "no"));
        UI::printLine("  [N]  Output filename    "
                      + (opts.outputFilename.empty() ? "(auto-generated)" : opts.outputFilename));
        UI::printLine("  [O]  Output directory   "
                      + (opts.outputDir.empty() ? "(current directory)" : opts.outputDir));
        UI::printLine();
        UI::printFooter("[GO] Build   [OFF] Clear All Slots   [Q] Quit");

        std::string sel = UI::readCommand();

        // Normalise to upper
        std::string up = sel;
        std::transform(up.begin(), up.end(), up.begin(), ::toupper);

        if (up == "Q") return;

        if (up == "OFF") {
            for (auto& s : opts.slots) { s.filePath.clear(); s.label.clear(); }
            std::cout << "  All slots cleared.\n";
            continue;
        }

        if (up == "GO") {
            // Validate — need at least one filled slot and at least one output
            bool anySlot = false;
            for (const auto& s : opts.slots)
                if (!s.filePath.empty()) { anySlot = true; break; }
            if (!anySlot) {
                std::cout << "  No files assigned — set at least one slot first.\n";
                continue;
            }
            if (!opts.build4K && !opts.build1080) {
                std::cout << "  No output selected — enable 4K or 1080p.\n";
                continue;
            }
            // Validate audio slot has a file
            if (opts.slots[opts.audioSlot].filePath.empty()) {
                std::cout << "  Audio slot " << (opts.audioSlot + 1)
                          << " is empty — choose a filled slot for [A] audio.\n";
                continue;
            }
            break;  // proceed to build
        }

        if (up == "A") {
            // Cycle audio slot to next filled slot
            for (int tries = 0; tries < 4; ++tries) {
                opts.audioSlot = (opts.audioSlot + 1) % 4;
                if (!opts.slots[opts.audioSlot].filePath.empty()) break;
            }
            continue;
        }
        if (up == "F") { opts.build4K    = !opts.build4K;    continue; }
        if (up == "G") { opts.build1080  = !opts.build1080;  continue; }
        if (up == "N") {
            std::string cur = opts.outputFilename.empty()
                            ? "Collage_4K.mp4" : opts.outputFilename;
            opts.outputFilename = UI::promptString("Output filename", cur);
            // Strip .mp4 extension if present — will be re-appended as needed
            if (opts.outputFilename.size() > 4 &&
                opts.outputFilename.substr(opts.outputFilename.size() - 4) == ".mp4")
                opts.outputFilename = opts.outputFilename.substr(0, opts.outputFilename.size() - 4);
            continue;
        }
        if (up == "O") {
            opts.outputDir = UI::promptString("Output directory",
                             opts.outputDir.empty() ? "." : opts.outputDir);
            if (!fs::exists(opts.outputDir))
                std::cout << "  Warning: directory does not exist yet.\n";
            continue;
        }

        // Slot selection: 1-4
        if (up.size() == 1 && up[0] >= '1' && up[0] <= '4') {
            int idx = up[0] - '1';
            std::cout << "  Enter file path (or leave blank to clear slot): ";
            std::string path;
            std::getline(std::cin, path);
            // Trim whitespace
            path.erase(0, path.find_first_not_of(" \t"));
            path.erase(path.find_last_not_of(" \t") + 1);

            if (path.empty()) {
                opts.slots[idx].filePath.clear();
                opts.slots[idx].label.clear();
                std::cout << "  Slot " << (idx + 1) << " cleared.\n";
            } else if (!fs::exists(path)) {
                std::cout << "  File not found: " << path << "\n";
            } else {
                opts.slots[idx].filePath = path;
                opts.slots[idx].label    = fs::path(path).filename().string();
                // If audio slot is empty, auto-assign to this slot
                if (opts.slots[opts.audioSlot].filePath.empty())
                    opts.audioSlot = idx;
                std::cout << "  Slot " << (idx + 1) << " set to "
                          << opts.slots[idx].label << "\n";
            }
            continue;
        }

        std::cout << "  Invalid option.\n";
    }

    // -----------------------------------------------------------------------
    // Build
    // -----------------------------------------------------------------------
    std::string outDir = opts.outputDir.empty() ? "." : opts.outputDir;
    if (!fs::exists(outDir)) {
        std::error_code ec;
        fs::create_directories(outDir, ec);
        if (ec) {
            std::cerr << "Error: Could not create output directory: "
                      << outDir << "\n  " << ec.message() << "\n";
            return;
        }
    }
    opts.outputDir = outDir;

    // Get durations of all filled slots
    double maxDur = 0.0;
    double durations[4] = {0.0, 0.0, 0.0, 0.0};
    for (int i = 0; i < 4; ++i) {
        if (!opts.slots[i].filePath.empty()) {
            durations[i] = getFileDuration(opts.slots[i].filePath,
                                           opts.ffprobePath);
            std::cout << "  Slot " << (i + 1) << ": "
                      << opts.slots[i].label << "  "
                      << std::fixed << std::setprecision(1)
                      << durations[i] << "s\n";
            if (durations[i] > maxDur) maxDur = durations[i];
        }
    }

    if (maxDur <= 0.0) {
        std::cerr << "Error: Could not determine duration of any slot.\n";
        return;
    }

    std::cout << "\nTarget duration: " << std::fixed << std::setprecision(1)
              << maxDur << "s\n";
    std::cout << "Preparing slots...\n";

    // Pad all slots to maxDur
    std::array<std::string, 4> paddedInputs;
    for (int i = 0; i < 4; ++i) {
        std::cout << "  Preparing slot " << (i + 1) << "...\n";
        paddedInputs[i] = buildPaddedInput(
            opts.slots[i].filePath,
            durations[i],
            maxDur,
            i,
            opts.ffmpegPath,
            logoPath,
            outDir
        );
        if (paddedInputs[i].empty()) {
            std::cerr << "Error: Failed to prepare slot " << (i + 1) << ".\n";
            // Clean up any already-prepared slots
            for (int j = 0; j < i; ++j) {
                std::error_code ec;
                fs::remove(paddedInputs[j], ec);
            }
            return;
        }
    }

    // Generate output filename — user override or auto timestamp
    auto now = std::time(nullptr);
    std::tm tmBuf{};
    localtime_r(&now, &tmBuf);
    char tsbuf[32];
    std::strftime(tsbuf, sizeof(tsbuf), "%Y%m%d_%H%M%S", &tmBuf);
    std::string ts(tsbuf);

    std::string baseName = opts.outputFilename.empty()
                         ? (ts + "_Collage") : opts.outputFilename;

    std::string proposed4K    = (fs::path(outDir) / (baseName + "_4K.mp4")).string();
    std::string proposed1080  = (fs::path(outDir) / (baseName + "_1080p.mp4")).string();
    std::string collage4KPath   = UI::confirmOutputPath(proposed4K);
    std::string collage1080Path = UI::confirmOutputPath(proposed1080);
    if (collage4KPath   != proposed4K)   renamedFiles.push_back({proposed4K,   collage4KPath});
    if (collage1080Path != proposed1080) renamedFiles.push_back({proposed1080, collage1080Path});

    // Show resolved output paths before the encode starts
    std::cout << "\n  Output:\n";
    if (opts.build4K)   std::cout << "    " << collage4KPath   << "\n";
    if (opts.build1080) std::cout << "    " << collage1080Path << "\n";
    std::cout << "\n";

    std::string collage4KBuilt;
    if (opts.build4K) {
        buildCollageFromSlots(opts, collage4KPath, paddedInputs);
        collage4KBuilt = collage4KPath;
    }

    if (opts.build1080) {
        if (collage4KBuilt.empty()) {
            // Need 4K as intermediate — not a user deliverable
            std::string tmp4K = (fs::path(outDir) / "pm_tmp_collage4k.mp4").string();
            buildCollageFromSlots(opts, tmp4K, paddedInputs);
            VideoOptions vopts;
            vopts.ffmpegPath = opts.ffmpegPath;
            buildCollage1080(tmp4K, collage1080Path, vopts);
            std::error_code ec;
            fs::remove(tmp4K, ec);
        } else {
            VideoOptions vopts;
            vopts.ffmpegPath = opts.ffmpegPath;
            buildCollage1080(collage4KBuilt, collage1080Path, vopts);
        }
    }

    // Clean up padded slot temp files
    for (int i = 0; i < 4; ++i) {
        std::error_code ec;
        fs::remove(paddedInputs[i], ec);
    }

    std::cout << "\nBuild complete.\n";

    if (!renamedFiles.empty()) {
        std::cout << "\n  NOTE: The following files were renamed to avoid overwriting existing files:\n";
        for (const auto& [orig, renamed] : renamedFiles) {
            std::cout << "    " << fs::path(orig).filename().string()
                      << "  ->  " << fs::path(renamed).filename().string() << "\n";
            LOG_NORMAL("Renamed: " + orig + " -> " + renamed);
        }
        renamedFiles.clear();
    }
}
// Returns selected trip index, or -1 to abort.
// ---------------------------------------------------------------------------
int VideoBuilder::pickTrip(const std::vector<Trip>& trips,
                            ConfigManager& config,
                            std::vector<Trip>& tripsOut,
                            std::string& manifestIdOut) {
    tripsOut = trips;

    while (true) {
        std::cout << "\n";
        UI::printTitle("PathMux v" + std::string(APP_VERSION) + " -- Select Trip to Build");
        UI::printLine();

        if (tripsOut.empty()) {
            UI::printLine("  (No trips in this manifest)");
        } else {
            UI::printLine("ID   Date         Start     Segments  Duration");
            UI::printLine("---  -----------  --------  --------  --------");
            for (int i = 0; i < (int)tripsOut.size(); ++i) {
                const auto& t = tripsOut[i];
                std::ostringstream row;
                row << std::right << std::setw(3) << (i + 1) << "  "
                    << std::left  << std::setw(11) << t.date << "  "
                    << std::setw(8) << t.startTime << "  "
                    << std::right << std::setw(6) << t.segments.size() << "    "
                    << t.duration
                    << (t.note.empty() ? "" : "  [note]");
                UI::printLine(row.str());
            }
        }

        UI::printLine();
        UI::printFooter("[M] Switch Manifest   [Q] Quit");

        std::string sel = UI::readCommand();

        char ch = std::toupper((unsigned char)sel[0]);
        if (ch == 'Q') return -1;

        if (ch == 'M') {
            // Show manifest list
            std::vector<std::string> manifests = config.getAllCachedPaths();
            if (manifests.empty()) {
                std::cout << "  No other manifests cached.\n";
                continue;
            }
            std::cout << "\n";
            UI::printTitle("Select Manifest");
            UI::printLine();
            for (int i = 0; i < (int)manifests.size(); ++i) {
                UI::printLine(std::to_string(i + 1) + "  " + manifests[i]);
            }
            UI::printFooter("[Q] Cancel");
            std::cout << "\nManifest ID or [Q]: ";
            std::string msel;
            std::getline(std::cin >> std::ws, msel);
            if (msel == "q" || msel == "Q") continue;
            int midx = -1;
            try { midx = std::stoi(msel) - 1; } catch (...) {}
            if (midx < 0 || midx >= (int)manifests.size()) {
                std::cout << "  Invalid selection.\n"; continue;
            }
            manifestIdOut = manifests[midx];
            tripsOut = config.loadTripCache(manifestIdOut);
            continue;
        }

        // Numeric selection
        int id = -1;
        try { id = std::stoi(sel) - 1; } catch (...) {}
        if (id < 0 || id >= (int)tripsOut.size()) {
            std::cout << "  Invalid trip ID.\n"; continue;
        }
        return id;
    }
}

// ---------------------------------------------------------------------------
// configureOptions — interactive build options menu.
// ---------------------------------------------------------------------------
VideoOptions VideoBuilder::configureOptions(ConfigManager& config, Trip& trip) {
    VideoOptions opts;
    opts.ffmpegPath          = config.getFfmpegPath();
    opts.containerFormat     = config.getVideoFormat();
    opts.outputDir           = config.getDefaultExportDir();
    opts.tmpDir              = config.getTmpDir(opts.outputDir);
    opts.encode              = config.getEncodeSettings();
    opts.audioSource         = config.getDefaultAudioSource();
    opts.audioExtractCamera  = config.getDefaultAudioSource();  // independent default

    // Helper: is a given camera name the collage audio source?
    auto isAudioSrc = [&](const std::string& cam) {
        return opts.audioSource == cam;
    };

    // Helper: normalise input to uppercase for multi-char commands
    auto toUpper = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return s;
    };

    while (true) {
        auto yn = [](bool v) -> std::string { return v ? "yes" : "no"; };

        std::cout << "\n";
        UI::printTitle("PathMux v" + std::string(APP_VERSION) + " -- Build Options");
        UI::warnMissingTools(config.getFfmpegPath(), config.getExiftoolPath());
        UI::printLine("  Trip " + trip.id
                      + "   " + trip.date + "  " + trip.startTime
                      + "   " + trip.duration
                      + "  (" + std::to_string(trip.segments.size()) + " segs)");
        if (trip.durationFFProbed >= 0) {
            std::string cd = std::to_string(trip.durationFFProbed / 60) + "m "
                           + std::to_string(trip.durationFFProbed % 60) + "s";
            UI::printLine("  Computed duration:           " + cd);
        }
        {
            std::string noteDisplay = trip.note.empty() ? "(none)"
                : trip.note.substr(0, 48) + (trip.note.size() > 48 ? "..." : "");
            UI::printLine("  Note: " + noteDisplay);
        }
        UI::printDivider();
        UI::printLine();
        UI::printLine("  Per-camera files:");
        UI::printLine("  [A]  Front camera       " + yn(opts.buildFront));
        UI::printLine("  [B]  Rear camera        " + yn(opts.buildRear));
        UI::printLine("  [C]  Left camera        " + yn(opts.buildLeft));
        UI::printLine("  [D]  Right camera       " + yn(opts.buildRight));
        UI::printLine("  [E]  Container format   " + opts.containerFormat);
        UI::printLine();
        UI::printLine("  Collage:  (camera toggles above affect per-camera files");
        UI::printLine("            only; collage uses raw segments directly)");
        UI::printLine("  [F]  4K master (H.265)  " + yn(opts.buildCollage4K));
        UI::printLine("  [G]  1080p copy (H.264) " + yn(opts.buildCollage1080));
        UI::printLine("  [H]  Collage audio      " + opts.audioSource);
        UI::printLine();
        UI::printLine("  Audio extract:");
        UI::printLine("  [S]  Extract audio      " + yn(opts.buildAudio));
        UI::printLine("  [K]  Audio camera       " + opts.audioExtractCamera);
        UI::printLine("  [U]  Audio format       " + opts.audioExtractFormat);
        UI::printLine();
        // Compute auto basename for display
        {
            std::string d = trip.date, t = trip.startTime;
            d.erase(std::remove(d.begin(), d.end(), '-'), d.end());
            t.erase(std::remove(t.begin(), t.end(), ':'), t.end());
            if (t.size() > 6) t = t.substr(0, 6);
            std::string autoBase = d + "_" + t;
            UI::printLine("  [N]  Set Note");
            UI::printLine("  [O]  Output directory  "
                          + (opts.outputDir.empty() ? "(current directory)" : opts.outputDir));
            UI::printLine("  [P]  Basename         "
                          + (opts.basenameOverride.empty()
                             ? "<auto: " + autoBase + ">"
                             : opts.basenameOverride));
        }
        UI::printLine("  [T]  Compute duration  (ffprobes all Front segments - may be slow)");
        UI::printLine("  [OFF] Deselect All");
        UI::printLine();
        UI::printLine("  [V]  Validate Trip Against Source");
        UI::printLine();
        UI::printFooter("[GO] Build And Continue   [GODONE] Build And Exit   [X] Switch Trip   [M] Switch Manifest   [Q] Quit");

        std::string sel = UI::readCommand();

        std::string up = toUpper(sel);

        // --- Multi-character commands ---
        if (up == "Q") {
            opts.navAction = NavAction::QUIT;
            return opts;
        }
        if (up == "X") {
            opts.navAction = NavAction::SWITCH_TRIP;
            return opts;
        }
        if (up == "M") {
            opts.navAction = NavAction::SWITCH_MANIFEST;
            return opts;
        }
        if (up == "GO" || up == "GODONE") {
            // Validate: at least one output selected
            bool anyWork = opts.buildFront || opts.buildRear || opts.buildLeft  ||
                           opts.buildRight || opts.buildCollage4K ||
                           opts.buildCollage1080 || opts.buildAudio;
            if (!anyWork) {
                std::cout << "  Nothing selected — enable at least one output first.\n";
                continue;
            }
            opts.exitAfterBuild = (up == "GODONE");
            return opts;
        }
        if (up == "OFF") {
            opts.buildFront = opts.buildRear  = opts.buildLeft   =
            opts.buildRight = opts.buildCollage4K = opts.buildCollage1080 =
            opts.buildAudio = false;
            std::cout << "  All outputs deselected.\n";            continue;
        }

        // Single-character commands
        if (up.size() != 1) { std::cout << "  Invalid option.\n"; continue; }
        char ch = up[0];

        // --- Per-camera toggles with audio-source guard ---
        if (ch == 'A') {
            if (opts.buildFront && isAudioSrc("front")) {
                std::cout << "  Cannot disable Front — it is the collage audio source.\n"
                          << "  Change [H] Collage audio first.\n";
            } else { opts.buildFront = !opts.buildFront; }
            continue;
        }
        if (ch == 'B') {
            if (opts.buildRear && isAudioSrc("rear")) {
                std::cout << "  Cannot disable Rear — it is the collage audio source.\n"
                          << "  Change [H] Collage audio first.\n";
            } else { opts.buildRear = !opts.buildRear; }
            continue;
        }
        if (ch == 'C') {
            if (opts.buildLeft && isAudioSrc("left")) {
                std::cout << "  Cannot disable Left — it is the collage audio source.\n"
                          << "  Change [H] Collage audio first.\n";
            } else { opts.buildLeft = !opts.buildLeft; }
            continue;
        }
        if (ch == 'D') {
            if (opts.buildRight && isAudioSrc("right")) {
                std::cout << "  Cannot disable Right — it is the collage audio source.\n"
                          << "  Change [H] Collage audio first.\n";
            } else { opts.buildRight = !opts.buildRight; }
            continue;
        }
        if (ch == 'E') {
            static const std::vector<std::string> fmts =
                {"mp4","mkv","mov","avi","mpg"};
            int cur = 0;
            for (int i = 0; i < (int)fmts.size(); ++i)
                if (fmts[i] == opts.containerFormat) { cur = i; break; }
            opts.containerFormat = fmts[UI::promptChoice("Container format", fmts, cur)];
            continue;
        }
        if (ch == 'F') { opts.buildCollage4K   = !opts.buildCollage4K;   continue; }
        if (ch == 'G') { opts.buildCollage1080 = !opts.buildCollage1080;  continue; }
        if (ch == 'H') {
            static const std::vector<std::string> audioSrcs =
                {"left","right","front","rear"};
            int cur = 0;
            for (int i = 0; i < (int)audioSrcs.size(); ++i)
                if (audioSrcs[i] == opts.audioSource) { cur = i; break; }
            opts.audioSource = audioSrcs[UI::promptChoice("Collage audio source",
                                                           audioSrcs, cur)];
            continue;
        }
        if (ch == 'S') { opts.buildAudio = !opts.buildAudio; continue; }
        if (ch == 'K') {
            static const std::vector<std::string> cams =
                {"left","right","front","rear"};
            int cur = 0;
            for (int i = 0; i < (int)cams.size(); ++i)
                if (cams[i] == opts.audioExtractCamera) { cur = i; break; }
            opts.audioExtractCamera = cams[UI::promptChoice("Audio extract camera",
                                                             cams, cur)];
            continue;
        }
        if (ch == 'T') {
            // Compute duration by ffprobing all Front segments.
            std::cout << "  Probing " << trip.segments.size() << " segment(s)...\n";
            std::string ffprobe = config.getFfprobePath();
            int total = 0;
            for (const auto& seg : trip.segments) {
                if (seg.front != "-" && !seg.front.empty()) {
                    double d = getFileDuration(seg.front, ffprobe);
                    if (d > 0.0) total += static_cast<int>(d);
                }
            }
            if (total > 0) {
                trip.durationFFProbed = total;
                std::cout << "  Computed duration: "
                          << total / 60 << "m " << total % 60 << "s\n";
            } else {
                std::cout << "  Could not probe segments (ffprobe unavailable or no Front files).\n";
            }
            continue;
        }
        if (ch == 'U') {
            static const std::vector<std::string> afmts = {"m4a","mp3","aac"};
            int cur = 0;
            for (int i = 0; i < (int)afmts.size(); ++i)
                if (afmts[i] == opts.audioExtractFormat) { cur = i; break; }
            opts.audioExtractFormat = afmts[UI::promptChoice("Audio format", afmts, cur)];
            continue;
        }
        if (ch == 'N') {
            if (trip.note.empty()) {
                std::cout << "  Set note (Enter to cancel):\n  > ";
            } else {
                std::cout << "  Current note:\n  " << trip.note << "\n"
                          << "  Set note (Enter to keep, space+Enter to clear):\n  > ";
            }
            std::string input;
            std::getline(std::cin, input);
            if (!input.empty() && input.find_first_not_of(' ') == std::string::npos) {
                trip.note.clear();
                std::cout << "  Note cleared.\n";
            } else if (!input.empty()) {
                trip.note = input;
                std::cout << "  Note saved.\n";
            }
            // else empty — keep existing, no message
            continue;
        }
        if (ch == 'V') {
            validateTrip(trip, "");
            continue;
        }
        if (ch == 'O') {
            opts.outputDir = UI::promptString("Output directory",
                             opts.outputDir.empty() ? "." : opts.outputDir);
            if (!fs::exists(opts.outputDir))
                std::cout << "  Warning: directory does not exist yet.\n";
            continue;
        }
        if (ch == 'P') {
            std::string d = trip.date, t = trip.startTime;
            d.erase(std::remove(d.begin(), d.end(), '-'), d.end());
            t.erase(std::remove(t.begin(), t.end(), ':'), t.end());
            if (t.size() > 6) t = t.substr(0, 6);
            std::string autoBase = d + "_" + t;
            if (opts.basenameOverride.empty()) {
                std::cout << "  Auto basename: " << autoBase << "\n"
                          << "  Enter override (Enter to keep auto):\n  > ";
            } else {
                std::cout << "  Current override: " << opts.basenameOverride << "\n"
                          << "  Auto basename: " << autoBase << "\n"
                          << "  Enter override (Enter to keep, space+Enter to clear):\n  > ";
            }
            std::string input;
            std::getline(std::cin, input);
            if (input.empty()) {
                // keep existing — no message
            } else if (input.find_first_not_of(' ') == std::string::npos) {
                opts.basenameOverride.clear();
                std::cout << "  Basename cleared — using auto.\n";
            } else {
                std::string cleaned = sanitizeBasename(input);
                if (cleaned.empty()) {
                    std::cout << "  Invalid basename — use alphanumeric, dash, underscore, dot, space only.\n";
                } else {
                    if (cleaned != input)
                        std::cout << "  Sanitized to: " << cleaned << "\n";
                    opts.basenameOverride = cleaned;
                    std::cout << "  Basename override set: " << cleaned << "\n";
                }
            }
            continue;
        }
        std::cout << "  Invalid option.\n";
    }
}

// ---------------------------------------------------------------------------
// validateTrip — check trip's 3 validation files against stored md5s.
// sourcePath is the scan root; relative paths in validationFiles are resolved
// against it.  Results printed inline; non-blocking.
// ---------------------------------------------------------------------------
void VideoBuilder::validateTrip(const Trip& trip, const std::string& sourcePath) {
    if (trip.validationFiles.empty()) {
        std::cout << "  No validation data stored for this trip.\n"
                     "  Re-scan the source path to generate validation checksums.\n";
        return;
    }

    // Resolve source path — prefer trip's own source over outputDir
    // The source path is stored in the manifest; we derive it from the
    // first segment's absolute path by stripping the camera subdir.
    std::string root = sourcePath;
    if (!trip.segments.empty()) {
        const std::string& front = trip.segments[0].front;
        if (front != "-" && !front.empty()) {
            // Strip last two path components (camera/filename)
            auto fsRoot = fs::path(front).parent_path().parent_path();
            if (!fsRoot.empty())
                root = fsRoot.string();
        }
    }

    std::cout << "  Validating trip " << trip.id
              << "  (" << trip.date << " " << trip.startTime << ")\n";
    std::cout << "  Source root: " << root << "\n\n";

    int pass = 0, fail = 0, missing = 0;
    for (const auto& vf : trip.validationFiles) {
        std::string fullPath = root + "/" + vf.relPath;
        if (!std::filesystem::exists(fullPath)) {
            std::cout << "  MISSING:  " << vf.relPath << "\n";
            ++missing;
            continue;
        }
        // Recompute md5
#ifdef _WIN32
        std::string cmd = "certutil -hashfile \"" + fullPath + "\" MD5 2>NUL";
#else
        std::string cmd = "md5sum '" + fullPath + "' 2>/dev/null";
#endif
        FILE* pipe = popen(cmd.c_str(), "r");
        std::string currentMd5;
        if (pipe) {
#ifdef _WIN32
            // certutil output: line 0 = header, line 1 = hash, line 2 = success msg
            std::vector<std::string> lines;
            char buf[256] = {};
            while (fgets(buf, sizeof(buf), pipe)) {
                std::string line(buf);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
                    line.pop_back();
                if (!line.empty()) lines.push_back(line);
            }
            if (lines.size() >= 2) {
                currentMd5 = lines[1];
                currentMd5.erase(std::remove(currentMd5.begin(), currentMd5.end(), ' '), currentMd5.end());
                for (char& c : currentMd5) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
#else
            char buf[64] = {};
            if (fgets(buf, sizeof(buf), pipe)) {
                std::string s(buf);
                auto pos = s.find(' ');
                if (pos != std::string::npos) currentMd5 = s.substr(0, pos);
            }
#endif
            pclose(pipe);
        }
        if (currentMd5 == vf.md5) {
            std::cout << "  OK:       " << vf.relPath << "\n";
            ++pass;
        } else {
            std::cout << "  MODIFIED: " << vf.relPath << "\n"
                      << "            stored:  " << vf.md5 << "\n"
                      << "            current: " << currentMd5 << "\n";
            ++fail;
        }
    }

    std::cout << "\n  Result: " << pass << " ok";
    if (missing) std::cout << "  " << missing << " missing";
    if (fail)    std::cout << "  " << fail    << " modified";
    std::cout << "\n";
    if (missing || fail)
        std::cout << "  Consider rescanning the source path to update the manifest.\n";
}

// ---------------------------------------------------------------------------
// buildTrip — public entry point for find_trips interactive browser.
// Dispatches build steps from a fully-configured VideoOptions.
// ---------------------------------------------------------------------------
void VideoBuilder::buildTrip(Trip& trip, const VideoOptions& opts) {
    namespace fs = std::filesystem;

    auto collectSegments = [&](const std::string TripSegment::* field)
            -> std::vector<std::string> {
        std::vector<std::string> files;
        for (const auto& seg : trip.segments) {
            const std::string& f = seg.*field;
            if (f != "-" && !f.empty()) files.push_back(f);
        }
        return files;
    };

    std::vector<std::pair<std::string,std::string>> renamedFiles;
    std::string outDir = opts.outputDir.empty() ? "." : opts.outputDir;

    if (opts.buildFront)
        buildCameraFile(trip, "Front", collectSegments(&TripSegment::front), opts);
    if (opts.buildRear)
        buildCameraFile(trip, "Rear",  collectSegments(&TripSegment::rear),  opts);
    if (opts.buildLeft)
        buildCameraFile(trip, "Left",  collectSegments(&TripSegment::left),  opts);
    if (opts.buildRight)
        buildCameraFile(trip, "Right", collectSegments(&TripSegment::right), opts);

    std::string collage4KPath;
    if (opts.buildCollage4K) {
        collage4KPath = (fs::path(outDir) /
                         makeOutputName(trip, "Collage_4K", "mp4",
                                        opts.basenameOverride)).string();
        buildCollage4K(trip, opts);
    }
    if (opts.buildCollage1080) {
        if (!collage4KPath.empty()) {
            std::string proposed1080 = (fs::path(outDir) /
                                        makeOutputName(trip, "Collage_1080p", "mp4",
                                                       opts.basenameOverride)).string();
            std::string out1080 = UI::confirmOutputPath(proposed1080);
            buildCollage1080(collage4KPath, out1080, opts);
        } else {
            buildCollage1080Direct(trip, opts);
        }
    }
    if (opts.buildAudio) {
        std::vector<std::string> audioSegs;
        const std::string& ac = opts.audioExtractCamera;
        for (const auto& seg : trip.segments) {
            std::string f;
            if      (ac == "front") f = seg.front;
            else if (ac == "rear")  f = seg.rear;
            else if (ac == "right") f = seg.right;
            else                    f = seg.left;
            if (f != "-" && !f.empty()) audioSegs.push_back(f);
        }
        buildAudioFile(trip, audioSegs, opts);
    }

    std::cout << "\nBuild complete.\n";

    // Append build provenance to pm_buildlog.json in the footage source path.
    int outDur = (trip.durationFFProbed >= 0)
                 ? trip.durationFFProbed : trip.segDetectedDuration;
    appendBuildLog(trip, opts, outDur);
}

// ---------------------------------------------------------------------------
// run — entry point from main.cpp (-V flag).
// ---------------------------------------------------------------------------
void VideoBuilder::run(ConfigManager& config) {
    // -----------------------------------------------------------------------
    // Mode selection
    // -----------------------------------------------------------------------
    int mode = 0;
    while (mode == 0) {
        std::cout << "\n";
        UI::printTitle("PathMux v" + std::string(APP_VERSION) + " -- Video Build");
        UI::printLine();
        UI::printLine("  [1]  Build from manifest");
        UI::printLine("  [2]  Build collage from existing camera files");
        UI::printLine();
        UI::printFooter("[Q] Quit");
        std::cout << "\nSelect mode: ";
        std::string sel;
        sel = UI::readCommand();
        char ch = std::toupper((unsigned char)sel[0]);
        if (ch == 'Q') return;
        if (ch == '1') { mode = 1; break; }
        if (ch == '2') { mode = 2; break; }
        std::cout << "  Invalid selection.\n";
    }

    if (mode == 2) {
        runCollageFromFiles(config);
        return;
    }

    // -----------------------------------------------------------------------
    // Mode 1: Build from manifest — loop so GO returns to trip picker,
    // GODONE exits after build.
    // -----------------------------------------------------------------------
    while (true) {
        std::string manifestId = config.getLastPath();
        std::vector<Trip> trips;
        if (!manifestId.empty())
            trips = config.loadTripCache(manifestId);

        if (trips.empty() && manifestId.empty()) {
            std::cout << "No manifest loaded. Use -s or -p to scan a path first.\n";
            return;
        }

        // Pick trip
        std::vector<Trip> selectedTrips;
        std::string selManifest = manifestId;
        int tripIdx = pickTrip(trips, config, selectedTrips, selManifest);
        if (tripIdx < 0) return;  // user quit

        Trip& trip = selectedTrips[tripIdx];

        std::cout << "\nSelected: Trip " << (tripIdx + 1)
                  << "  " << trip.date << " " << trip.startTime
                  << "  (" << trip.segments.size() << " segments)\n";
        if (!trip.note.empty())
            std::cout << "  Note: " << trip.note << "\n";

        // Configure build options — [N] may edit trip.note, [T] may set durationFFProbed
        VideoOptions opts = configureOptions(config, trip);
        opts.sourcePath = selManifest;
        opts.manifestId = config.getManifestIdForPath(selManifest);

        // Persist note edits and any durationFFProbed computed during options menu
        config.saveTripCache(selManifest, selectedTrips);

        // Handle navigation actions from Build Options menu
        if (opts.navAction == NavAction::QUIT) return;
        if (opts.navAction == NavAction::SWITCH_TRIP) continue;  // loop back to pickTrip
        if (opts.navAction == NavAction::SWITCH_MANIFEST) {
            // Force manifest switch by clearing lastPath so pickTrip shows switcher
            config.setLastPath("");
            continue;
        }

        // Check if user bailed without selecting anything (shouldn't happen with
        // explicit nav actions above, but guard anyway)
        bool anyWork = opts.buildFront || opts.buildRear || opts.buildLeft ||
                       opts.buildRight || opts.buildCollage4K ||
                       opts.buildCollage1080 || opts.buildAudio;
        if (!anyWork) {
            std::cout << "Nothing to build.\n";
            return;
        }

        // Ensure output dir exists
        std::string outDir = opts.outputDir.empty() ? "." : opts.outputDir;
        if (!fs::exists(outDir)) {
            std::error_code ec;
            fs::create_directories(outDir, ec);
            if (ec) {
                std::cerr << "Error: Could not create output directory: "
                          << outDir << "\n  " << ec.message() << "\n";
                return;
            }
        }
        opts.outputDir = outDir;

        // -----------------------------------------------------------------------
        // Per-camera files
        // -----------------------------------------------------------------------
        auto collectSegments = [&](const std::string TripSegment::* field)
                -> std::vector<std::string> {
            std::vector<std::string> files;
            for (const auto& seg : trip.segments) {
                const std::string& f = seg.*field;
                if (f != "-" && !f.empty()) files.push_back(f);
            }
            return files;
        };

        if (opts.buildFront)
            buildCameraFile(trip, "Front", collectSegments(&TripSegment::front), opts);
        if (opts.buildRear)
            buildCameraFile(trip, "Rear",  collectSegments(&TripSegment::rear),  opts);
        if (opts.buildLeft)
            buildCameraFile(trip, "Left",  collectSegments(&TripSegment::left),  opts);
        if (opts.buildRight)
            buildCameraFile(trip, "Right", collectSegments(&TripSegment::right), opts);

        // -----------------------------------------------------------------------
        // Collage
        // -----------------------------------------------------------------------
        std::string collage4KPath;
        if (opts.buildCollage4K) {
            collage4KPath = (fs::path(outDir) /
                             makeOutputName(trip, "Collage_4K", "mp4",
                                            opts.basenameOverride)).string();
            buildCollage4K(trip, opts);
        }

        if (opts.buildCollage1080) {
            if (!collage4KPath.empty()) {
                // Fast path: downscale from 4K master already built this session
                std::string proposed1080 = (fs::path(outDir) /
                                            makeOutputName(trip, "Collage_1080p", "mp4",
                                                           opts.basenameOverride)).string();
                std::string out1080 = UI::confirmOutputPath(proposed1080);
                if (out1080 != proposed1080) renamedFiles.push_back({proposed1080, out1080});
                buildCollage1080(collage4KPath, out1080, opts);
            } else {
                // Direct path: encode 1080p from source segments (no 4K master needed)
                buildCollage1080Direct(trip, opts);
            }
        }

        // -----------------------------------------------------------------------
        // Audio extract
        // -----------------------------------------------------------------------
        if (opts.buildAudio) {
            std::vector<std::string> audioSegs;
            const std::string& ac = opts.audioExtractCamera;
            for (const auto& seg : trip.segments) {
                std::string f;
                if      (ac == "front") f = seg.front;
                else if (ac == "rear")  f = seg.rear;
                else if (ac == "right") f = seg.right;
                else                    f = seg.left;
                if (f != "-" && !f.empty()) audioSegs.push_back(f);
            }
            buildAudioFile(trip, audioSegs, opts);
        }

        std::cout << "\nBuild complete.\n";

        // Persist durationFFProbed (if [T] was used) and append buildlog.
        config.saveTripCache(selManifest, selectedTrips);
        int outDur = (trip.durationFFProbed >= 0)
                     ? trip.durationFFProbed : trip.segDetectedDuration;
        appendBuildLog(trip, opts, outDur);

        if (!renamedFiles.empty()) {
            std::cout << "\n  NOTE: The following files were renamed to avoid overwriting existing files:\n";
            for (const auto& [orig, renamed] : renamedFiles) {
                std::cout << "    " << fs::path(orig).filename().string()
                          << "  ->  " << fs::path(renamed).filename().string() << "\n";
                LOG_NORMAL("Renamed: " + orig + " -> " + renamed);
            }
            renamedFiles.clear();
        }

        if (opts.exitAfterBuild) return;
        // GO — loop back to trip picker for another build
    }
}
// SN: 00087
