// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "kml_prefs.hpp"
#include "ui_helpers.hpp"
#include "version.hpp"
#include <iostream>
#include <string>

using namespace CamClops;

bool KmlPrefsEditor::run(ConfigManager& config) {
    KmlSettings working = config.getKmlSettings();
    bool changed = false;

    while (true) {
        std::string title = std::string("CamClops v") + APP_VERSION + " KML Preferences";
        std::string unsaved = changed ? "  * unsaved changes" : "";

        std::cout << "\n";
        UI::printCenteredTitle(title);
        UI::printLine();
        UI::printLine("[A]  Track ahead color    " + working.trackAheadColor
                      + "  (bright green)");
        UI::printLine("[B]  Track behind color   " + working.trackBehindColor
                      + "  (red)");
        UI::printLine("[C]  Track line width     " + std::to_string(working.trackLineWidth));
        UI::printLine("[D]  Waypoint color       " + working.waypointColor
                      + "  (blue)");
        UI::printLine("[E]  Start pin icon URL");
        UI::printLine("     " + working.startPinUrl);
        UI::printLine("[F]  End pin icon URL");
        UI::printLine("     " + working.endPinUrl);
        UI::printLine("[G]  Show known locations "
                      + std::string(working.showKnownLocations ? "yes" : "no"));
        UI::printLine();
        UI::printLine("Colors are in KML AABBGGRR hex format.");
        UI::printLine("AA=alpha BB=blue GG=green RR=red (ff=opaque)");
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
            config.applyKmlSettings(working);
            config.saveSettings();
            std::cout << "KML settings saved.\n";
            return true;
        }

        if (ch == 'A') {
            working.trackAheadColor = UI::promptString(
                "Track ahead color (AABBGGRR)", working.trackAheadColor);
            changed = true;
        }
        else if (ch == 'B') {
            working.trackBehindColor = UI::promptString(
                "Track behind color (AABBGGRR)", working.trackBehindColor);
            changed = true;
        }
        else if (ch == 'C') {
            working.trackLineWidth = UI::promptInt(
                "Track line width", working.trackLineWidth, 1, 10);
            changed = true;
        }
        else if (ch == 'D') {
            working.waypointColor = UI::promptString(
                "Waypoint color (AABBGGRR)", working.waypointColor);
            changed = true;
        }
        else if (ch == 'E') {
            std::cout << "  Google KML icon URLs — examples:\n"
                      << "    http://maps.google.com/mapfiles/kml/pushpin/ylw-pushpin.png\n"
                      << "    http://maps.google.com/mapfiles/kml/paddle/wht-blank.png\n"
                      << "    http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png\n";
            working.startPinUrl = UI::promptString(
                "Start pin icon URL", working.startPinUrl);
            changed = true;
        }
        else if (ch == 'F') {
            working.endPinUrl = UI::promptString(
                "End pin icon URL", working.endPinUrl);
            changed = true;
        }
        else if (ch == 'G') {
            working.showKnownLocations = !working.showKnownLocations;
            std::cout << "  Show known locations: "
                      << (working.showKnownLocations ? "yes" : "no") << "\n";
            changed = true;
        }
        else {
            std::cout << "  Invalid option.\n";
        }
    }
}
// SN: 00089
