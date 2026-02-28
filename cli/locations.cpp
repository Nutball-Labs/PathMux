#include "locations.hpp"
#include "ui_helpers.hpp"
#include "version.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

using namespace Pathmux;

void LocationsEditor::run(ConfigManager& config) {
    while (true) {
        std::vector<NamedLocation> locs = config.loadLocations();

        std::string title = std::string("PathMux v") + APP_VERSION + " Known Locations";
        std::cout << "\n";
        UI::printCenteredTitle(title);
        UI::printLine();

        if (locs.empty()) {
            UI::printLine("  (No locations defined)");
        } else {
            // Column layout (fits in INNER_WIDTH=61):
            // ID(3) sp(2) Name(17) sp(2) Lat(10) sp(2) Lon(11) sp(2) Radius(6)
            const std::string hdr =
                "ID   Name                Lat          Lon          Radius";
            const std::string sep =
                "---  ------------------  ----------  -----------  ------";
            UI::printLine(hdr);
            UI::printLine(sep);
            for (int i = 0; i < (int)locs.size(); ++i) {
                const auto& loc = locs[i];
                std::ostringstream row;
                row << std::right << std::setw(3)  << (i + 1) << "  "
                    << std::left  << std::setw(18) << loc.name.substr(0, 18) << "  "
                    << std::right << std::fixed << std::setprecision(6)
                    << std::setw(10) << loc.lat << "  "
                    << std::setw(11) << loc.lon << "  "
                    << std::setw(4)  << loc.radiusMetres << "m";
                UI::printLine(row.str());
            }
        }

        UI::printLine();
        UI::printFooter("[A] Add   [X] Delete   [Q] Quit");

        std::string sel = UI::readCommand();

        char ch = std::toupper((unsigned char)sel[0]);

        if (ch == 'Q') return;

        if (ch == 'A') {
            // Add new location
            NamedLocation loc;
            loc.name = UI::promptString("Name", "");
            if (loc.name.empty()) { std::cout << "  Cancelled.\n"; continue; }

            std::string latStr = UI::promptString("Latitude  (decimal degrees)", "");
            std::string lonStr = UI::promptString("Longitude (decimal degrees)", "");
            try {
                loc.lat = std::stod(latStr);
                loc.lon = std::stod(lonStr);
            } catch (...) {
                std::cout << "  Invalid coordinates — cancelled.\n";
                continue;
            }
            loc.radiusMetres = UI::promptInt("Proximity radius (metres)", 50, 1, 10000);
            loc.icon         = UI::promptString("Icon style", "pin");

            locs.push_back(loc);
            config.saveLocations(locs);
            std::cout << "  Location added.\n";
            continue;
        }

        if (ch == 'X') {
            if (locs.empty()) { std::cout << "  No locations to delete.\n"; continue; }
            int id = UI::promptInt("Delete ID", -1, 1, (int)locs.size());
            if (id < 1 || id > (int)locs.size()) {
                std::cout << "  Invalid ID.\n"; continue;
            }
            std::cout << "  Delete \"" << locs[id - 1].name << "\"? [Y/N]: ";
            std::string confirm;
            std::getline(std::cin >> std::ws, confirm);
            if (confirm == "y" || confirm == "Y") {
                locs.erase(locs.begin() + (id - 1));
                config.saveLocations(locs);
                std::cout << "  Deleted.\n";
            }
            continue;
        }

        // Numeric input — edit existing
        int id = -1;
        try { id = std::stoi(sel); } catch (...) {}
        if (id < 1 || id > (int)locs.size()) {
            std::cout << "  Invalid selection.\n"; continue;
        }

        auto& loc = locs[id - 1];
        std::cout << "  Editing: " << loc.name << "\n"
                  << "  Press Enter to keep current value.\n";

        std::string newName = UI::promptString("Name", loc.name);
        if (!newName.empty()) loc.name = newName;

        std::string latStr = UI::promptString(
            "Latitude",  std::to_string(loc.lat));
        std::string lonStr = UI::promptString(
            "Longitude", std::to_string(loc.lon));
        try {
            if (!latStr.empty()) loc.lat = std::stod(latStr);
            if (!lonStr.empty()) loc.lon = std::stod(lonStr);
        } catch (...) {
            std::cout << "  Invalid coordinates — keeping original values.\n";
        }

        loc.radiusMetres = UI::promptInt(
            "Proximity radius (metres)", loc.radiusMetres, 1, 10000);
        std::string newIcon = UI::promptString("Icon style", loc.icon);
        if (!newIcon.empty()) loc.icon = newIcon;

        config.saveLocations(locs);
        std::cout << "  Updated.\n";
    }
}
// SN: 00071
