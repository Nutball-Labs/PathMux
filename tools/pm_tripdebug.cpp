#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <iomanip>
#include "trip_detection.hpp"
#include "config_manager.hpp"
#include "version.hpp"

namespace fs = std::filesystem;
using namespace Pathmux;

void printUsage() {
    std::cout << "pm_tripdebug v" << APP_VERSION << "\n"
              << "Usage: pm_tripdebug [options] <path_to_dashcam_folder>\n\n"
              << "Options:\n"
              << "  -v, --version   Show version and exit\n"
              << "  -h, --help      Show this help message\n"
              << "  -T, --tree      Show full segment tree for all trips and exit\n"
              << "  -f, --full      Interactive mode: summary + drill-down by trip ID\n\n"
              << "Examples:\n"
              << "  ./pm_tripdebug /mnt/dashcam         (summary list)\n"
              << "  ./pm_tripdebug -T /mnt/dashcam      (full tree, all trips)\n"
              << "  ./pm_tripdebug -f /mnt/dashcam      (interactive drill-down)\n";
}

void showSummaryList(const std::string& path, const std::vector<Trip>& trips) {
    std::cout << "\n" << APP_NAME << " v" << APP_VERSION
              << " - Trip Detection Debug\n";
    std::cout << "Source: " << path << "\n";
    std::cout << std::string(65, '=') << "\n";
    std::cout << std::left
              << std::setw(6)  << "ID"
              << std::setw(20) << "Start Timestamp"
              << std::setw(15) << "Duration"
              << "Segments\n";
    std::cout << std::string(65, '-') << "\n";

    for (size_t i = 0; i < trips.size(); ++i) {
        std::string fullStart = trips[i].date + " " + trips[i].startTime;
        std::cout << std::left
                  << std::setw(6)  << (i + 1)
                  << std::setw(20) << fullStart
                  << std::setw(15) << trips[i].duration
                  << trips[i].segments.size() << "\n";
    }
    std::cout << std::string(65, '=') << "\n";
    std::cout << trips.size() << " trip(s) detected.\n";
}

void showTripTree(const Trip& trip, int id) {
    std::cout << "\nTrip #" << id
              << "  [" << trip.date << "  " << trip.startTime << "]"
              << "  duration=" << trip.duration
              << "  segments=" << trip.segments.size() << "\n";
    std::cout << std::string(65, '-') << "\n";

    for (size_t si = 0; si < trip.segments.size(); ++si) {
        const auto& seg = trip.segments[si];
        std::string f = (seg.front != "-" && !seg.front.empty()) ? "F" : ".";
        std::string b = (seg.rear  != "-" && !seg.rear.empty())  ? "B" : ".";
        std::string l = (seg.left  != "-" && !seg.left.empty())  ? "L" : ".";
        std::string r = (seg.right != "-" && !seg.right.empty()) ? "R" : ".";

        std::cout << "  [" << std::setw(3) << (si + 1) << "]  "
                  << seg.timestamp << "  [" << f << b << l << r << "]\n";

        auto showFile = [](const std::string& label, const std::string& path) {
            if (path != "-" && !path.empty())
                std::cout << "         " << label << ": "
                          << fs::path(path).filename().string() << "\n";
        };
        showFile("F", seg.front);
        showFile("B", seg.rear);
        showFile("L", seg.left);
        showFile("R", seg.right);
    }
    std::cout << std::string(65, '-') << "\n";
}

void showAllTrees(const std::string& path, const std::vector<Trip>& trips) {
    std::cout << "\n" << APP_NAME << " v" << APP_VERSION
              << " - Full Trip Tree\n";
    std::cout << "Source: " << path << "\n";
    std::cout << std::string(65, '=') << "\n";
    for (size_t i = 0; i < trips.size(); ++i)
        showTripTree(trips[i], (int)i + 1);
    std::cout << "\n" << trips.size() << " trip(s) total.\n";
}

void showTripDetails(const Trip& trip, int id) {
    showTripTree(trip, id);
    std::cout << "Options: [R] Return to list  [Q] Quit\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { printUsage(); return 1; }

    std::string path;
    bool fullMode = false;
    bool treeMode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")    { printUsage(); return 0; }
        if (arg == "-v" || arg == "--version") {
            std::cout << APP_NAME << " pm_tripdebug v" << APP_VERSION << "\n";
            return 0;
        }
        if (arg == "-T" || arg == "--tree")  { treeMode = true; continue; }
        if (arg == "-f" || arg == "--full")  { fullMode = true; continue; }
        if (arg[0] != '-')                   { path = arg; }
        else { std::cerr << "Unknown argument: " << arg << "\n"; printUsage(); return 1; }
    }

    if (path.empty() || !fs::exists(path)) {
        std::cerr << "Error: valid path required.\n";
        return 1;
    }

    TripDetection detector;
    ConfigManager config;
    std::vector<Trip> trips = detector.detectTrips(
        path,
        config.getGapThreshold(),
        config.getFuzzyWindow()
    );

    if (trips.empty()) {
        std::cout << "No trips detected in " << path << "\n";
        return 0;
    }

    if (treeMode) {
        showAllTrees(path, trips);
        return 0;
    }

    showSummaryList(path, trips);

    if (fullMode) {
        while (true) {
            std::cout << "\nEnter trip ID to inspect, [R] redraw, [Q] quit: ";
            std::string input;
            std::getline(std::cin >> std::ws, input);
            if (input.empty()) continue;

            char ch = std::toupper((unsigned char)input[0]);
            if (ch == 'Q') break;
            if (ch == 'R') { showSummaryList(path, trips); continue; }

            try {
                int id = std::stoi(input);
                if (id > 0 && id <= (int)trips.size()) {
                    showTripDetails(trips[id - 1], id);
                    std::string sub;
                    std::getline(std::cin >> std::ws, sub);
                    char sc = sub.empty() ? 'R'
                            : std::toupper((unsigned char)sub[0]);
                    if (sc == 'Q') break;
                    showSummaryList(path, trips);
                } else {
                    std::cout << "  Invalid ID. Range: 1-" << trips.size() << "\n";
                }
            } catch (...) {
                std::cout << "  Enter a numeric ID, [R], or [Q].\n";
            }
        }
    }

    std::cout << "Exiting pm_tripdebug.\n";
    return 0;
}
// SN: 00081
