#include "prefs.hpp"
#include "ui_helpers.hpp"
#include "version.hpp"
#include <iostream>
#include <string>
#include <fstream>

using namespace Pathmux;

bool PrefsEditor::run(ConfigManager& config) {
    AppSettings working = config.getSettings();
    bool changed = false;

    while (true) {
        auto fmtTime = [](int s) -> std::string {
            if (s < 60) return std::to_string(s) + "s";
            if (s < 3600) {
                int m = s / 60, r = s % 60;
                return std::to_string(m) + "m"
                       + (r ? " " + std::to_string(r) + "s" : "");
            }
            int h = s / 3600, m = (s % 3600) / 60;
            return std::to_string(h) + "h"
                   + (m ? " " + std::to_string(m) + "m" : "");
        };

        auto fmtPath = [](const std::string& p) -> std::string {
            if (p.empty()) return "(current directory)";
            return p;
        };

        std::string title = std::string("PathMux v") + APP_VERSION + " Preferences";
        std::string unsaved = changed ? "  * unsaved changes" : "";

        std::cout << "\n";
        UI::printTitle(title);
        UI::warnMissingTools(working.ffmpegPath, working.exiftoolPath);
        UI::printLine();
        UI::printLine("[A]  Trip Gap (seconds)        "
                      + std::to_string(working.gapThresholdSeconds)
                      + "  (" + fmtTime(working.gapThresholdSeconds) + ")");
        UI::printLine("[B]  Fuzzy window (seconds)    "
                      + std::to_string(working.fuzzyWindowSeconds));
        UI::printLine("[C]  GPS cold-start skip (s)   "
                      + std::to_string(working.gpsColStartSkip));
        UI::printLine("[D]  Default export directory  "
                      + fmtPath(working.defaultExportDir));
        UI::printLine("[E]  ExifTool path             "
                      + (working.exiftoolPath.empty() ? "exiftool" : working.exiftoolPath));
        UI::printLine("[O]  ExifTool options          "
                      + (working.exiftoolOptions.empty()
                         ? "-ee3 -p '$GPSDateTime ...' (default)"
                         : working.exiftoolOptions));
        UI::printLine("[F]  FFmpeg path               "
                      + (working.ffmpegPath.empty() ? "ffmpeg" : working.ffmpegPath));
        UI::printLine("[G]  Timestamp format          " + working.timestampFormat);
        UI::printLine("[H]  Time display              " + working.timeDisplay);
        UI::printLine("[U]  Units                     "
                      + std::string(working.useImperial ? "imperial" : "metric"));
        UI::printLine("[I]  Video output format       " + working.videoFormat);
        UI::printLine("[J]  Default audio source      " + working.defaultAudioSource
                      + "  (left=driver side LHD, right=driver side RHD)");
        UI::printLine("[K]  Log level                 " + working.logLevel
                      + "  (off|normal|debug)");
        UI::printLine("[L]  Encode preset             " + working.encode.preset
                      + "  -- use --encoderprefs for encoder details");
        UI::printLine("[M]  Temp directory            "
                      + (working.tmpDir.empty() ? "(auto: <export dir>/pm_tmp)" : working.tmpDir));
        UI::printLine();
        UI::printLine(unsaved);
        UI::printFooter("[S] Save and exit   [Q] Quit");

        std::string sel = UI::readCommand();
        char ch = std::toupper((unsigned char)sel[0]);

        if (ch == 'Q') {
            if (changed) {
                std::cout << "Unsaved changes will be lost. Quit? [Y/N]: ";
                std::string confirm;
                std::getline(std::cin >> std::ws, confirm);
                if (confirm != "y" && confirm != "Y") continue;
            }
            return false;
        }
        if (ch == 'S') {
            config.applySettings(working);
            config.saveSettings();
            std::cout << "Settings saved.\n";
            return true;
        }

        if (ch == 'A') {
            working.gapThresholdSeconds = UI::promptInt(
                "Trip Gap (seconds)", working.gapThresholdSeconds, 30, 86400);
            changed = true;
        }
        else if (ch == 'B') {
            working.fuzzyWindowSeconds = UI::promptInt(
                "Fuzzy window (seconds)", working.fuzzyWindowSeconds, 1, 30);
            changed = true;
        }
        else if (ch == 'C') {
            working.gpsColStartSkip = UI::promptInt(
                "GPS cold-start skip (seconds)", working.gpsColStartSkip, 0, 300);
            changed = true;
        }
        else if (ch == 'D') {
            working.defaultExportDir = UI::promptString(
                "Default export directory", working.defaultExportDir);
            UI::confirmExists(working.defaultExportDir);
            changed = true;
        }
        else if (ch == 'E') {
            working.exiftoolPath = UI::promptString(
                "ExifTool path", working.exiftoolPath.empty()
                                 ? "exiftool" : working.exiftoolPath);
            UI::confirmExecutable(working.exiftoolPath);
            changed = true;
        }
        else if (ch == 'O') {
            std::string cur = working.exiftoolOptions.empty()
                ? "-ee3 -p '$GPSDateTime $GPSLatitude# $GPSLongitude# $GPSSpeed# $GPSTrack#'"
                : working.exiftoolOptions;
            std::cout << "  Current: " << cur << "\n\n"
                      << "  This is the COMPLETE options string appended to the exiftool binary path.\n"
                      << "  It must include extraction flags AND the -p format string.\n"
                      << "  The -p format string field order is fixed — the parser expects:\n"
                      << "    GPSDateTime  GPSLatitude#  GPSLongitude#  GPSSpeed#  GPSTrack#\n"
                      << "  Do not change field order or remove fields.\n"
                      << "  Single quotes around the format string are required.\n"
                      << "  Default works for Pruveeo D90 and most consumer dashcams.\n\n";
            working.exiftoolOptions = UI::promptString("ExifTool options", cur);
            changed = true;
        }
        else if (ch == 'F') {
            working.ffmpegPath = UI::promptString(
                "FFmpeg path", working.ffmpegPath.empty()
                               ? "ffmpeg" : working.ffmpegPath);
            UI::confirmExecutable(working.ffmpegPath);
            changed = true;
        }
        else if (ch == 'G') {
            static const std::vector<std::string> tsFmts = {
                "YYYYMMDD-HHMMSS", "YYYY-MM-DD HH:MM:SS", "YYYY/MM/DD HH:MM:SS"};
            int cur = 0;
            for (int i = 0; i < (int)tsFmts.size(); ++i)
                if (tsFmts[i] == working.timestampFormat) { cur = i; break; }
            working.timestampFormat = tsFmts[UI::promptChoice("Timestamp format", tsFmts, cur)];
            changed = true;
        }
        else if (ch == 'H') {
            static const std::vector<std::string> timeFmts = {"24-hour", "12-hour"};
            int cur = (working.timeDisplay == "12-hour") ? 1 : 0;
            working.timeDisplay = timeFmts[UI::promptChoice("Time display", timeFmts, cur)];
            changed = true;
        }
        else if (ch == 'U') {
            working.useImperial = !working.useImperial;
            std::cout << "  Units set to "
                      << (working.useImperial ? "imperial (mi, mph, ft)" : "metric (km, km/h, m)")
                      << "\n";
            changed = true;
        }
        else if (ch == 'I') {
            static const std::vector<std::string> vidFmts = {"mp4","mkv","mov","avi","mpg"};
            int cur = 0;
            for (int i = 0; i < (int)vidFmts.size(); ++i)
                if (vidFmts[i] == working.videoFormat) { cur = i; break; }
            working.videoFormat = vidFmts[UI::promptChoice("Video output format", vidFmts, cur)];
            changed = true;
        }
        else if (ch == 'J') {
            static const std::vector<std::string> audioSrcs = {"left","right","front","rear"};
            int cur = 0;
            for (int i = 0; i < (int)audioSrcs.size(); ++i)
                if (audioSrcs[i] == working.defaultAudioSource) { cur = i; break; }
            working.defaultAudioSource = audioSrcs[UI::promptChoice("Default audio source", audioSrcs, cur)];
            changed = true;
        }
        else if (ch == 'K') {
            static const std::vector<std::string> logLevels = {"off","normal","debug"};
            int cur = 0;
            for (int i = 0; i < (int)logLevels.size(); ++i)
                if (logLevels[i] == working.logLevel) { cur = i; break; }
            working.logLevel = logLevels[UI::promptChoice("Log level", logLevels, cur)];
            changed = true;
        }
        else if (ch == 'L') {
            static const std::vector<std::string> presets = {"qsv","nvenc","vaapi","cpu"};
            int cur = 0;
            for (int i = 0; i < (int)presets.size(); ++i)
                if (presets[i] == working.encode.preset) { cur = i; break; }
            int nxt = UI::promptChoice(
                "Encode preset (populates encoder defaults — use --encoderprefs to customize)",
                presets, cur);
            working.encode.preset = presets[nxt];
            ConfigManager tmpCfg;
            tmpCfg.applySettings(working);
            tmpCfg.applyEncodePreset(presets[nxt]);
            working.encode = tmpCfg.getSettings().encode;
            std::cout << "  Preset applied. Use --encoderprefs to customize individual values.\n";
            changed = true;
        }
        else if (ch == 'M') {
            working.tmpDir = UI::promptString(
                "Temp directory (empty = auto)", working.tmpDir);
            if (!working.tmpDir.empty())
                UI::confirmExists(working.tmpDir);
            changed = true;
        }
        else {
            std::cout << "  Invalid option.\n";
        }
    }
}

// ---------------------------------------------------------------------------
// EncoderPrefsEditor — power-user encoder settings.
// All fields are free-text except preset (constrained) and quality (int).
// This is the escape hatch for non-standard GPUs and custom ffmpeg flags.
// ---------------------------------------------------------------------------
bool EncoderPrefsEditor::run(ConfigManager& config) {
    AppSettings working = config.getSettings();
    bool changed = false;

    while (true) {
        std::string title = std::string("PathMux v") + APP_VERSION + " Encoder Preferences";
        std::string unsaved = changed ? "  * unsaved changes" : "";

        std::cout << "\n";
        UI::printTitle(title);
        UI::printLine();
        UI::printLine("  Active preset: " + working.encode.preset
                      + "  (use [A] to apply a preset, then customize)");
        UI::printLine();
        UI::printLine("[A]  Apply preset         qsv | nvenc | vaapi | cpu");
        UI::printLine("[B]  HW device string     " + working.encode.hwDevice);
        UI::printLine("[C]  HW device type       " + working.encode.hwDeviceType);
        UI::printLine("[D]  Norm encoder         " + working.encode.normEncoder);
        UI::printLine("[E]  Collage encoder      " + working.encode.collageEncoder);
        UI::printLine("[F]  Downscale encoder    " + working.encode.downEncoder);
        UI::printLine("[G]  Pixel format         " + working.encode.pixFmt);
        UI::printLine("[H]  Norm quality         " + working.encode.normQuality
                      + "  (lower = better quality / larger file)");
        UI::printLine("[I]  Collage quality      " + working.encode.collageQuality);
        UI::printLine("[J]  Downscale quality    " + working.encode.downQuality);
        UI::printLine("[K]  Extra norm args      "
                      + (working.encode.extraNormArgs.empty()
                         ? "(none)" : working.encode.extraNormArgs));
        UI::printLine("[L]  Extra collage args   "
                      + (working.encode.extraCollageArgs.empty()
                         ? "(none)" : working.encode.extraCollageArgs));
        UI::printLine();
        UI::printLine("  WARNING: These settings are passed directly to ffmpeg.");
        UI::printLine("  Invalid values will cause encode failures at build time.");
        UI::printLine();
        UI::printLine(unsaved);
        UI::printFooter("[S] Save and exit   [Q] Quit without saving");

        std::string sel = UI::readCommand();
        char ch = std::toupper((unsigned char)sel[0]);

        if (ch == 'Q') {
            if (changed) {
                std::cout << "Unsaved changes will be lost. Quit? [Y/N]: ";
                std::string confirm;
                std::getline(std::cin >> std::ws, confirm);
                if (confirm != "y" && confirm != "Y") continue;
            }
            return false;
        }
        if (ch == 'S') {
            config.applySettings(working);
            config.saveSettings();
            std::cout << "Encoder settings saved.\n";
            return true;
        }

        if (ch == 'A') {
            static const std::vector<std::string> presets = {"qsv","nvenc","vaapi","cpu"};
            int cur = 0;
            for (int i = 0; i < (int)presets.size(); ++i)
                if (presets[i] == working.encode.preset) { cur = i; break; }
            int nxt = UI::promptChoice("Apply preset", presets, cur);
            ConfigManager tmpCfg;
            tmpCfg.applySettings(working);
            tmpCfg.applyEncodePreset(presets[nxt]);
            working.encode = tmpCfg.getSettings().encode;
            std::cout << "  Preset '" << presets[nxt] << "' applied."
                         " Customize individual fields as needed.\n";
            changed = true;
        }
        else if (ch == 'B') {
            working.encode.hwDevice = UI::promptString(
                "HW device string", working.encode.hwDevice);
            changed = true;
        }
        else if (ch == 'C') {
            static const std::vector<std::string> types = {"qsv","vaapi","cuda","none"};
            int cur = 0;
            for (int i = 0; i < (int)types.size(); ++i)
                if (types[i] == working.encode.hwDeviceType) { cur = i; break; }
            working.encode.hwDeviceType = types[UI::promptChoice("HW device type", types, cur)];
            changed = true;
        }
        else if (ch == 'D') {
            working.encode.normEncoder = UI::promptString(
                "Norm encoder (e.g. h264_qsv, h264_nvenc, libx264)",
                working.encode.normEncoder);
            changed = true;
        }
        else if (ch == 'E') {
            working.encode.collageEncoder = UI::promptString(
                "Collage encoder (e.g. hevc_qsv, hevc_nvenc, libx265)",
                working.encode.collageEncoder);
            changed = true;
        }
        else if (ch == 'F') {
            working.encode.downEncoder = UI::promptString(
                "Downscale encoder (e.g. h264_qsv, h264_nvenc, libx264)",
                working.encode.downEncoder);
            changed = true;
        }
        else if (ch == 'G') {
            static const std::vector<std::string> fmts = {"nv12","yuv420p","yuv420p10le"};
            int cur = 0;
            for (int i = 0; i < (int)fmts.size(); ++i)
                if (fmts[i] == working.encode.pixFmt) { cur = i; break; }
            int nxt = UI::promptChoice("Pixel format", fmts, cur);
            if (nxt < (int)fmts.size()) {
                working.encode.pixFmt = fmts[nxt];
            } else {
                // Custom — allow free text if not in list
                working.encode.pixFmt = UI::promptString("Custom pixel format",
                                                          working.encode.pixFmt);
            }
            changed = true;
        }
        else if (ch == 'H') {
            working.encode.normQuality = std::to_string(UI::promptInt(
                "Norm quality (1-51, lower=better)", std::stoi(working.encode.normQuality), 1, 51));
            changed = true;
        }
        else if (ch == 'I') {
            working.encode.collageQuality = std::to_string(UI::promptInt(
                "Collage quality (1-51, lower=better)", std::stoi(working.encode.collageQuality), 1, 51));
            changed = true;
        }
        else if (ch == 'J') {
            working.encode.downQuality = std::to_string(UI::promptInt(
                "Downscale quality (1-51, lower=better)", std::stoi(working.encode.downQuality), 1, 51));
            changed = true;
        }
        else if (ch == 'K') {
            working.encode.extraNormArgs = UI::promptString(
                "Extra norm args (empty to clear)", working.encode.extraNormArgs);
            changed = true;
        }
        else if (ch == 'L') {
            working.encode.extraCollageArgs = UI::promptString(
                "Extra collage args (empty to clear)", working.encode.extraCollageArgs);
            changed = true;
        }
        else {
            std::cout << "  Invalid option.\n";
        }
    }
}
// SN: 00071
