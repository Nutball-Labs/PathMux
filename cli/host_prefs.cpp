#include "host_prefs.hpp"
#include "prefs.hpp"
#include "ui_helpers.hpp"
#include "version.hpp"
#include <iostream>
#include <string>

using namespace Pathmux;

bool HostPrefsEditor::run(ConfigManager& config) {
    AppSettings working = config.getSettings();
    bool changed = false;

    while (true) {
        auto fmtPath = [](const std::string& p, const std::string& def) -> std::string {
            return p.empty() ? def : p;
        };

        std::string title = std::string("PathMux v") + APP_VERSION
                            + " Host Preferences (" + config.getHostname() + ")";
        std::string unsaved = changed ? "  * unsaved changes" : "";

        std::cout << "\n";
        UI::printTitle(title);
        UI::printLine("  Settings here override base prefs for this machine only.");
        UI::printLine("  File: " + config.getHostSettingsFile());
        UI::printLine();
        UI::warnMissingTools(working.ffmpegPath, working.exiftoolPath);
        UI::printLine("[A]  FFmpeg path            "
                      + fmtPath(working.ffmpegPath, "ffmpeg"));
        UI::printLine("[B]  ExifTool path          "
                      + fmtPath(working.exiftoolPath, "exiftool"));
        UI::printLine("[C]  ExifTool options       "
                      + (working.exiftoolOptions.empty()
                         ? "-ee3 -p '$GPSDateTime ...' (default)"
                         : working.exiftoolOptions));
        UI::printLine("[D]  Default output dir     "
                      + fmtPath(working.defaultExportDir, "(current directory)"));
        UI::printLine("[E]  Temp directory         "
                      + (working.tmpDir.empty()
                         ? "(auto: <output dir>/pm_tmp)"
                         : working.tmpDir));
        UI::printLine("[F]  Log level              " + working.logLevel
                      + "  (off|normal|debug)");
        UI::printLine("[G]  Encoder settings  -->  " + working.encode.preset
                      + " / " + working.encode.collageEncoder);
        UI::printLine();
        UI::printLine(unsaved);
        UI::printFooter("[S] Save to host file   [Q] Quit");

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
            config.saveHostSettings();
            std::cout << "Host settings saved to: "
                      << config.getHostSettingsFile() << "\n";
            return true;
        }

        if (ch == 'A') {
            working.ffmpegPath = UI::promptString(
                "FFmpeg path", fmtPath(working.ffmpegPath, "ffmpeg"));
            UI::confirmExecutable(working.ffmpegPath);
            changed = true;
        }
        else if (ch == 'B') {
            working.exiftoolPath = UI::promptString(
                "ExifTool path", fmtPath(working.exiftoolPath, "exiftool"));
            UI::confirmExecutable(working.exiftoolPath);
            changed = true;
        }
        else if (ch == 'C') {
            std::string cur = working.exiftoolOptions.empty()
                ? "-ee3 -p '$GPSDateTime $GPSLatitude# $GPSLongitude# $GPSSpeed# $GPSTrack#'"
                : working.exiftoolOptions;
            std::cout << "  Current: " << cur << "\n\n"
                      << "  This is the COMPLETE options string appended to the exiftool binary.\n"
                      << "  Field order in the -p format string is fixed — do not change it.\n\n";
            working.exiftoolOptions = UI::promptString("ExifTool options", cur);
            changed = true;
        }
        else if (ch == 'D') {
            working.defaultExportDir = UI::promptString(
                "Default output directory", working.defaultExportDir);
            UI::confirmExists(working.defaultExportDir);
            changed = true;
        }
        else if (ch == 'E') {
            working.tmpDir = UI::promptString(
                "Temp directory (empty = auto)", working.tmpDir);
            if (!working.tmpDir.empty())
                UI::confirmExists(working.tmpDir);
            changed = true;
        }
        else if (ch == 'F') {
            static const std::vector<std::string> logLevels = {"off","normal","debug"};
            int cur = 0;
            for (int i = 0; i < (int)logLevels.size(); ++i)
                if (logLevels[i] == working.logLevel) { cur = i; break; }
            working.logLevel = logLevels[UI::promptChoice("Log level", logLevels, cur)];
            changed = true;
        }
        else if (ch == 'G') {
            // Launch encoder sub-menu; it now saves to the host file as well.
            EncoderPrefsEditor enc;
            config.applySettings(working);  // push working state before sub-menu
            enc.run(config);
            working = config.getSettings(); // pull back after sub-menu save
            changed = true;
        }
        else {
            std::cout << "  Invalid option.\n";
        }
    }
}
// SN: 00087
