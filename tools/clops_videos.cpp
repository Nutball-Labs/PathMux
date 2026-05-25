// clops_videos — batch map / dashboard / HUD video generator
//
// Processes one or more manifests or individual trips, invoking the Python
// overlay scripts (clops_maprender.py, clops_dashboard.py, clops_hud.py) in sequence.
// Designed to run unattended in the background; updates the manifest after
// each video completes so partial runs are resumable.
//
// Usage:
//   clops_videos --mid=XX [--mid=YY ...] --all
//   clops_videos --tid=MM:TT [--tid=MM:UU ...] --map --dash
//   clops_videos --mid=XX --force --dry-run

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "lib/camclops.hpp"
#include "lib/config_manager.hpp"
#include "lib/compat.hpp"

namespace fs = std::filesystem;
using namespace CamClops;
using json = nlohmann::json;

// ---------------------------------------------------------------------------

static void printUsage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " SELECTION TYPE [--force] [--dry-run]\n"
        << "\n"
        << "Selection (repeatable, may be mixed):\n"
        << "  --mid=XX        All trips in manifest XX\n"
        << "  --tid=MM:TT     Specific trip MM:TT\n"
        << "\n"
        << "Video type (at least one required):\n"
        << "  --map           Moving-map MP4\n"
        << "  --dash          Dashboard MP4\n"
        << "  --hud           HUD overlay WebM (4K, composited at build time)\n"
        << "  --all           All three\n"
        << "\n"
        << "  --force         Regenerate even if output file already exists\n"
        << "  --dry-run       Show plan without running anything\n"
        << "\n"
        << "GPS extraction runs automatically if a trip has no GPS track.\n";
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string normalId(const std::string& id) {
    std::string s = id;
    for (auto& c : s) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return s;
}

// sh-safe quoting: wraps in single quotes, escaping embedded single quotes.
static std::string shellEsc(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else           r += c;
    }
    r += "'";
    return r;
}

static std::string fmtDouble(double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

static std::string fmtElapsed(int seconds) {
    int m = seconds / 60, s = seconds % 60;
    char buf[32];
    snprintf(buf, sizeof(buf), "%dm %02ds", m, s);
    return buf;
}

// Mirror of MapProgressDialog::findScript: looks relative to the binary.
static std::string findScript(const std::string& argv0, const std::string& scriptName) {
    std::error_code ec;
    fs::path binDir;
    auto canon = fs::canonical(argv0, ec);
    binDir = ec ? fs::path(argv0).parent_path() : canon.parent_path();

    const std::vector<fs::path> candidates = {
        binDir / "scripts" / scriptName,                          // Windows next-to-binary
        binDir / ".." / "scripts" / scriptName,                   // macOS bundle
        binDir / ".." / "share" / "camclops" / "scripts" / scriptName, // Linux installed
        binDir / scriptName,                                       // flat layout fallback
    };
    for (const auto& p : candidates) {
        if (fs::exists(p, ec))
            return fs::canonical(p, ec).string();
    }
    return {};
}

// ---------------------------------------------------------------------------
// Queue entry
// ---------------------------------------------------------------------------

struct QueueEntry {
    std::string manifestFile;
    std::string sourcePath;
    std::string mid;
    Trip        trip;
};

// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::vector<std::string> mids;
    std::vector<std::string> tids;
    bool doMap  = false;
    bool doDash = false;
    bool doHud  = false;
    bool force  = false;
    bool dryRun = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg.rfind("--mid=", 0) == 0) mids.push_back(normalId(arg.substr(6)));
        else if (arg.rfind("--tid=", 0) == 0) tids.push_back(normalId(arg.substr(6)));
        else if (arg == "--map")               doMap  = true;
        else if (arg == "--dash")              doDash = true;
        else if (arg == "--hud")               doHud  = true;
        else if (arg == "--all")               { doMap = doDash = doHud = true; }
        else if (arg == "--force")             force  = true;
        else if (arg == "--dry-run")           dryRun = true;
        else if (arg == "--help" || arg == "-h") { printUsage(argv[0]); return 0; }
        else { std::cerr << "Unknown option: " << arg << "\n"; printUsage(argv[0]); return 1; }
    }

    if (mids.empty() && tids.empty()) {
        std::cerr << "Error: specify at least one --mid or --tid\n\n";
        printUsage(argv[0]);
        return 1;
    }
    if (!doMap && !doDash && !doHud) {
        std::cerr << "Error: specify at least one of --map, --dash, --hud, --all\n\n";
        printUsage(argv[0]);
        return 1;
    }

    // Load settings
    ConfigManager config;
    config.loadSettings();
    const AppSettings& settings = config.getSettings();
    const std::string  ffmpegPath = settings.ffmpegPath;
    const std::string  unitsArg   = settings.useImperial ? "imperial" : "metric";

    // Locate scripts
    std::string mapScript, dashScript, hudScript;
    if (doMap)  mapScript  = findScript(argv[0], "clops_maprender.py");
    if (doDash) dashScript = findScript(argv[0], "clops_dashboard.py");
    if (doHud)  hudScript  = findScript(argv[0], "clops_hud.py");

    if (doMap  && mapScript.empty())  { std::cerr << "Error: clops_maprender.py not found\n"; return 1; }
    if (doDash && dashScript.empty()) { std::cerr << "Error: clops_dashboard.py not found\n"; return 1; }
    if (doHud  && hudScript.empty())  { std::cerr << "Error: clops_hud.py not found\n";       return 1; }

    // Load manifest index
    auto index = config.loadManifestIndex();

    // Build queue with deduplication on (manifestFile, tripId)
    std::vector<QueueEntry> queue;
    std::set<std::pair<std::string,std::string>> seen;

    auto enqueueAll = [&](const ManifestEntry& entry) {
        auto trips = config.loadTripCache(entry.manifestFile);
        for (const auto& t : trips) {
            auto key = std::make_pair(entry.manifestFile, normalId(t.id));
            if (seen.count(key)) continue;
            seen.insert(key);
            queue.push_back({ entry.manifestFile, entry.path, normalId(entry.id), t });
        }
    };

    // Expand --mid entries
    for (const auto& mid : mids) {
        bool found = false;
        for (const auto& entry : index) {
            if (normalId(entry.id) == mid) { enqueueAll(entry); found = true; break; }
        }
        if (!found) std::cerr << "Warning: manifest " << mid << " not found\n";
    }

    // Expand --tid entries
    for (const auto& tid : tids) {
        auto colon = tid.find(':');
        if (colon == std::string::npos) {
            std::cerr << "Warning: --tid=" << tid << " not in MM:TT format, skipping\n";
            continue;
        }
        std::string mid    = tid.substr(0, colon);
        std::string tripId = tid.substr(colon + 1);

        bool foundMid = false;
        for (const auto& entry : index) {
            if (normalId(entry.id) != mid) continue;
            foundMid = true;
            auto trips = config.loadTripCache(entry.manifestFile);
            bool foundTrip = false;
            for (const auto& t : trips) {
                if (normalId(t.id) != normalId(tripId)) continue;
                foundTrip = true;
                auto key = std::make_pair(entry.manifestFile, normalId(t.id));
                if (!seen.count(key)) {
                    seen.insert(key);
                    queue.push_back({ entry.manifestFile, entry.path, mid, t });
                }
                break;
            }
            if (!foundTrip) std::cerr << "Warning: trip " << tid << " not found\n";
            break;
        }
        if (!foundMid) std::cerr << "Warning: manifest " << mid << " not found\n";
    }

    if (queue.empty()) {
        std::cerr << "No trips to process.\n";
        return 1;
    }

    // HUD renders at 4K by default (matches TripPropertiesDialog spinbox defaults)
    constexpr int HUD_W = 3840, HUD_H = 2160;

    // Counters
    int totalGenerated = 0;
    int totalSkipped   = 0;
    int totalErrors    = 0;
    std::vector<std::string> errorLog;

    if (dryRun)
        std::cout << "[dry-run] " << queue.size() << " trip(s) queued\n";

    // -----------------------------------------------------------------------
    // Process each trip
    // -----------------------------------------------------------------------
    for (const auto& qe : queue) {
        std::cout << "\n[" << qe.mid << ":" << normalId(qe.trip.id) << "]"
                  << "  " << qe.trip.date
                  << "  " << qe.trip.duration << "\n";

        // Reload trip list so we hold mutable copies and can save back.
        auto trips = config.loadTripCache(qe.manifestFile);
        Trip* tp = nullptr;
        for (auto& t : trips)
            if (normalId(t.id) == normalId(qe.trip.id)) { tp = &t; break; }
        if (!tp) {
            std::cerr << "  Error: trip " << qe.trip.id << " vanished from manifest\n";
            ++totalErrors;
            errorLog.push_back(qe.mid + ":" + qe.trip.id + " — not in manifest after reload");
            continue;
        }

        std::string srcDir = qe.sourcePath;
        if (!srcDir.empty() && srcDir.back() == '/') srcDir.pop_back();

        bool manifestDirty = false;

        // GPS extraction — all three video types (map, dash, hud) require GPS data.
        // If the trip does not yet have a complete GPS track, extract it now before
        // attempting any video generation.
        if (tp->gpsTrackStatus != "complete") {
            std::cout << "  gps: not extracted — running exiftool...\n";
            if (!dryRun) {
                json root;
                {
                    std::ifstream ifs(qe.manifestFile);
                    if (!ifs.is_open()) {
                        std::cerr << "  gps: cannot open manifest — skipping trip\n";
                        ++totalErrors;
                        errorLog.push_back(qe.mid + ":" + tp->id + " — manifest open failed");
                        continue;
                    }
                    try { ifs >> root; } catch (...) {
                        std::cerr << "  gps: manifest JSON parse error — skipping trip\n";
                        ++totalErrors;
                        errorLog.push_back(qe.mid + ":" + tp->id + " — manifest parse failed");
                        continue;
                    }
                }

                int tripIdx = -1;
                if (root.contains("trips") && root["trips"].is_array()) {
                    for (int i = 0; i < (int)root["trips"].size(); ++i) {
                        if (normalId(root["trips"][i].value("id", "")) == normalId(tp->id)) {
                            tripIdx = i;
                            break;
                        }
                    }
                }

                if (tripIdx < 0) {
                    std::cerr << "  gps: trip not found in manifest JSON — skipping trip\n";
                    ++totalErrors;
                    errorLog.push_back(qe.mid + ":" + tp->id + " — trip missing from manifest JSON");
                    continue;
                }

                int seg = 0;
                bool gpsOk = CamClops::extractGps(root, tripIdx, qe.manifestFile,
                    config.getExiftoolPath(), /*verbose=*/false,
                    [&seg](int done, int /*total*/) {
                        if (done != seg) {
                            std::cout << "\r  gps: segment " << done << "  " << std::flush;
                            seg = done;
                        }
                    });
                std::cout << "\n";

                if (!gpsOk) {
                    std::cerr << "  gps: extraction failed — map/hud/dash will likely fail too\n";
                    ++totalErrors;
                    errorLog.push_back(qe.mid + ":" + tp->id + " — GPS extraction failed");
                    continue;
                }

                // Reload so tp reflects the updated gpsTrackStatus and gpsLockSeconds.
                trips = config.loadTripCache(qe.manifestFile);
                tp = nullptr;
                for (auto& t : trips)
                    if (normalId(t.id) == normalId(qe.trip.id)) { tp = &t; break; }
                if (!tp) {
                    std::cerr << "  Error: trip vanished after GPS extraction\n";
                    ++totalErrors;
                    errorLog.push_back(qe.mid + ":" + qe.trip.id + " — trip missing post-GPS");
                    continue;
                }
                std::cout << "  gps: done\n";
            }
        }

        // Helper: attempt one video type.
        // Returns true on success or skip, false on error.
        auto buildVideo = [&](
            const std::string& label,
            const std::string& script,
            const std::string& outPath,
            int width, int height,
            const std::vector<std::string>& extraArgs,
            std::vector<std::string>& videoVec) -> bool
        {
            // Skip if the output already exists (use --force to override)
            if (!force && fs::exists(outPath)) {
                std::cout << "  " << label << ": [skip]  " << outPath << "\n";
                ++totalSkipped;
                return true;
            }

            std::cout << "  " << label << ": " << outPath << "\n";
            if (dryRun) return true;

            // Build command string
            std::string cmd = "python3 " + shellEsc(script)
                + " --manifest " + shellEsc(qe.manifestFile)
                + " --trip "     + shellEsc(tp->id)
                + " --output "   + shellEsc(outPath)
                + " --width "    + std::to_string(width)
                + " --height "   + std::to_string(height)
                + " --fps 30";
            for (const auto& a : extraArgs)
                cmd += " " + shellEsc(a);
            if (!ffmpegPath.empty())
                cmd += " --ffmpeg " + shellEsc(ffmpegPath);

            auto t0  = std::chrono::steady_clock::now();
            int  ret = std::system(cmd.c_str());
            int  elapsed = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - t0).count());

            if (WEXITSTATUS(ret) != 0) {
                std::cout << "  " << label << ": FAILED (exit " << WEXITSTATUS(ret) << ")\n";
                return false;
            }

            std::cout << "  " << label << ": done (" << fmtElapsed(elapsed) << ")\n";

            // Record output path in manifest (replace any stale entry)
            videoVec.clear();
            videoVec.push_back(outPath);
            manifestDirty = true;
            ++totalGenerated;
            return true;
        };

        bool ok = true;

        if (doMap) {
            std::vector<std::string> extra;
            ok &= buildVideo("map",
                mapScript,
                srcDir + "/clops_trip_" + tp->id + "_map.mp4",
                tp->videoProfile.width, tp->videoProfile.height,
                extra, tp->mapVideos);
        }

        if (doDash) {
            std::vector<std::string> extra = { "--units", unitsArg };
            ok &= buildVideo("dash",
                dashScript,
                srcDir + "/clops_trip_" + tp->id + "_dash.mp4",
                960, 540,
                extra, tp->dashVideos);
        }

        if (doHud) {
            std::vector<std::string> extra = {
                "--color-hex",  settings.hudColor,
                "--font-scale", fmtDouble(settings.hudFontScale),
                "--line-scale", fmtDouble(settings.hudLineScale),
            };
            ok &= buildVideo("hud",
                hudScript,
                srcDir + "/clops_trip_" + tp->id + "_hud.webm",
                HUD_W, HUD_H,
                extra, tp->hudVideos);
        }

        if (!ok) {
            ++totalErrors;
            errorLog.push_back(qe.mid + ":" + tp->id);
        }

        if (manifestDirty && !dryRun)
            config.saveTripCache(qe.sourcePath, trips);
    }

    // -----------------------------------------------------------------------
    // Summary
    // -----------------------------------------------------------------------
    std::cout << "\n";
    if (dryRun) {
        std::cout << "dry-run: nothing built\n";
    } else {
        std::cout << "Done: " << totalGenerated << " generated";
        if (totalSkipped) std::cout << ", " << totalSkipped << " skipped";
        if (totalErrors)  std::cout << ", " << totalErrors  << " failed";
        std::cout << "\n";
        for (const auto& e : errorLog)
            std::cout << "  FAILED: " << e << "\n";
    }

    return totalErrors ? 1 : 0;
}

// SN: 00122
