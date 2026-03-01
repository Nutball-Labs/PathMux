#include "trip_detection.hpp"
#include <filesystem>
#include <algorithm>
#include <regex>
#include <map>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdio>

namespace fs = std::filesystem;

namespace Pathmux {

namespace {
    std::time_t stringToTimestamp(const std::string& ts) {
        std::tm t = {};
        std::istringstream ss(ts);
        ss >> std::get_time(&t, "%Y%m%d_%H%M%S");
        t.tm_isdst = -1;
        return std::mktime(&t);
    }

    // Probe pixel format and color characteristics from first video stream.
    // Returns defaults if ffprobe fails.
    // Output format: width,height,pix_fmt,color_range,color_space,r_frame_rate
    VideoProfile probeVideoProfile(const std::string& filePath,
                                   const std::string& ffprobePath) {
        VideoProfile vp;
        if (filePath.empty() || filePath == "-") return vp;

        std::string cmd = ffprobePath +
            " -v error"
            " -select_streams v:0"
            " -show_entries stream=width,height,pix_fmt,color_range,color_space,r_frame_rate"
            " -of csv=p=0"
            " \"" + filePath + "\" 2>/dev/null";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return vp;

        char buf[256] = {};
        if (!fgets(buf, sizeof(buf), pipe)) { pclose(pipe); return vp; }
        pclose(pipe);

        std::istringstream ss(buf);
        std::string tok;
        std::vector<std::string> fields;
        while (std::getline(ss, tok, ',')) {
            while (!tok.empty() && (tok.back() == '\n' || tok.back() == '\r'
                                    || tok.back() == ' '))
                tok.pop_back();
            fields.push_back(tok);
        }

        if (fields.size() >= 1 && !fields[0].empty())
            try { vp.width  = std::stoi(fields[0]); } catch (...) {}
        if (fields.size() >= 2 && !fields[1].empty())
            try { vp.height = std::stoi(fields[1]); } catch (...) {}
        if (fields.size() >= 3 && !fields[2].empty()) vp.pixFmt     = fields[2];
        if (fields.size() >= 4 && !fields[3].empty()) vp.colorRange = fields[3];
        if (fields.size() >= 5 && !fields[4].empty()) vp.colorSpace = fields[4];
        if (fields.size() >= 6 && !fields[5].empty()) vp.frameRate  = fields[5];

        return vp;
    }

    // Probe duration of a single segment in seconds using ffprobe.
    // Returns 0 on failure.
    int probeSegmentDuration(const std::string& filePath,
                             const std::string& ffprobePath) {
        if (filePath.empty() || filePath == "-") return 0;

        std::string cmd = ffprobePath +
            " -v error"
            " -show_entries format=duration"
            " -of csv=p=0"
            " \"" + filePath + "\" 2>/dev/null";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return 0;

        char buf[64] = {};
        if (!fgets(buf, sizeof(buf), pipe)) { pclose(pipe); return 0; }
        pclose(pipe);

        try { return static_cast<int>(std::stod(buf)); } catch (...) { return 0; }
    }


// Runs exiftool on firstSeg to find the first valid fix (non-zero lat/lon),
// and on lastSeg to find the last valid fix.  Populates firstLock* fields,
// startLat/Lon, and endLat/Lon on the trip.
// Does nothing if exiftool is unavailable or the segment has no GPS data.
// ---------------------------------------------------------------------------
void extractStartEndGps(Trip& trip,
                        const std::string& exiftoolPath,
                        const std::string& exiftoolOptions)
{
    if (trip.segments.empty()) return;

    const std::string& firstSeg = trip.segments.front().front;
    const std::string& lastSeg  = trip.segments.back().front;
    if (firstSeg == "-" || firstSeg.empty()) return;

    const std::string exifCmd = exiftoolPath + " " + exiftoolOptions + " ";

    // --- First segment: find first valid fix ---
    {
        std::string cmd = exifCmd + "\"" + firstSeg + "\" 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return;

        char linebuf[256];
        int  recordIdx = 0;
        bool foundLock = false;

        while (fgets(linebuf, sizeof(linebuf), pipe)) {
            std::string line(linebuf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'
                                     || line.back() == ' '))
                line.pop_back();
            if (line.empty()) { ++recordIdx; continue; }

            std::istringstream iss(line);
            std::string datePart, timePart;
            double lat, lon;
            if (!(iss >> datePart >> timePart >> lat >> lon)) {
                ++recordIdx; continue;
            }

            if (!foundLock && lat != 0.0 && lon != 0.0) {
                trip.firstLockLat       = lat;
                trip.firstLockLon       = lon;
                trip.firstLockTimestamp = datePart + " " + timePart;
                trip.firstLockRecord    = recordIdx;
                trip.startLat           = lat;
                trip.startLon           = lon;
                foundLock = true;
                // Don't break — keep reading to allow early exit is fine,
                // but we need endLat from last segment anyway.
                break;
            }
            ++recordIdx;
        }
        pclose(pipe);
    }

    // --- Last segment: find last valid fix ---
    // (may be same as first segment on a single-segment trip)
    {
        const std::string& seg = lastSeg.empty() || lastSeg == "-"
                                 ? firstSeg : lastSeg;
        std::string cmd = exifCmd + "\"" + seg + "\" 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return;

        char linebuf[256];
        double lastLat = 0.0, lastLon = 0.0;

        while (fgets(linebuf, sizeof(linebuf), pipe)) {
            std::string line(linebuf);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'
                                     || line.back() == ' '))
                line.pop_back();
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string datePart, timePart;
            double lat, lon;
            if (!(iss >> datePart >> timePart >> lat >> lon)) continue;
            if (lat != 0.0 && lon != 0.0) { lastLat = lat; lastLon = lon; }
        }
        pclose(pipe);

        if (lastLat != 0.0 || lastLon != 0.0) {
            trip.endLat = lastLat;
            trip.endLon = lastLon;
        }
    }
}

} // anonymous namespace

std::vector<Trip> TripDetection::detectTrips(const std::string& path,
                                              int gapThresholdSeconds,
                                              int fuzzyWindowSeconds,
                                              const std::string& ffprobePath,
                                              const std::string& exiftoolPath,
                                              const std::string& exiftoolOptions)
{
    std::vector<Trip> trips;
    if (!fs::exists(path)) return trips;

    std::regex filePattern(R"((\d{8})_(\d{6}).*\.[tT][sS])");
    std::map<std::time_t, std::string> frontFiles, rearFiles, leftFiles, rightFiles;

    auto scanDir = [&](const std::string& sub,
                       std::map<std::time_t, std::string>& targetMap) {
        fs::path subDir = fs::path(path) / sub;
        if (!fs::exists(subDir)) return;
        for (const auto& entry : fs::directory_iterator(subDir)) {
            std::string filename = entry.path().filename().string();
            if (filename.empty() || filename[0] == '.') continue;
            std::smatch match;
            if (std::regex_search(filename, match, filePattern)) {
                std::string tsStr = match[1].str() + "_" + match[2].str();
                targetMap[stringToTimestamp(tsStr)] = entry.path().string();
            }
        }
    };

    scanDir("Front", frontFiles);
    scanDir("Rear",  rearFiles);
    scanDir("Left",  leftFiles);
    scanDir("Right", rightFiles);

    if (frontFiles.empty()) return trips;

    auto findClosest = [&](std::time_t target,
                           std::map<std::time_t, std::string>& cameraMap) -> std::string {
        if (cameraMap.empty()) return "-";
        auto it = cameraMap.lower_bound(target);
        std::vector<std::time_t> candidates;
        if (it != cameraMap.end())   candidates.push_back(it->first);
        if (it != cameraMap.begin()) candidates.push_back(std::prev(it)->first);

        std::time_t bestTime = 0;
        long minDiff = static_cast<long>(fuzzyWindowSeconds) + 1;
        for (auto t : candidates) {
            long diff = std::abs(static_cast<long>(t - target));
            if (diff < minDiff) { minDiff = diff; bestTime = t; }
        }
        return (minDiff <= fuzzyWindowSeconds) ? cameraMap[bestTime] : "-";
    };

    Trip currentTrip;
    // Lambda: close a trip — compute timestamp-arithmetic duration and probe
    // the last segment for an ffprobe-refined display string.
    auto closeTrip = [&](Trip& trip) {
        const int segCount = (int)trip.segments.size();
        time_t firstEpoch = stringToTimestamp(trip.segments.front().timestamp);
        time_t lastEpoch  = stringToTimestamp(trip.segments.back().timestamp);

        // segDetectedDuration: pure timestamp arithmetic.
        // Nominal segment length = spacing between segments[0] and segments[1].
        int nominalSegdur = (segCount > 1)
            ? static_cast<int>(stringToTimestamp(trip.segments[1].timestamp) - firstEpoch)
            : 0;
        trip.segDetectedDuration = static_cast<int>(lastEpoch - firstEpoch) + nominalSegdur;

        // Probe last segment for display duration string.
        int lastDur = probeSegmentDuration(trip.segments.back().front, ffprobePath);

        if (lastDur == 0) {
            // ffprobe failed — fall back to timestamp estimate + one segment margin
            int estimate = static_cast<int>(lastEpoch - firstEpoch) + 180;
            trip.duration = std::to_string(estimate / 60) + "m "
                          + std::to_string(estimate % 60) + "s";
            trip.segdur = 0;
            return;
        }

        int total = static_cast<int>(lastEpoch - firstEpoch) + lastDur;
        trip.duration = std::to_string(total / 60) + "m "
                      + std::to_string(total % 60) + "s";

        // segdur from timestamp spacing.  Skip segments[0]/[1] boundary when
        // possible to avoid cold-start truncation bias on the first segment.
        if (segCount >= 3)
            trip.segdur = static_cast<int>(stringToTimestamp(trip.segments[2].timestamp)
                                         - stringToTimestamp(trip.segments[1].timestamp));
        else if (segCount >= 2)
            trip.segdur = static_cast<int>(stringToTimestamp(trip.segments[1].timestamp)
                                         - firstEpoch);
        else
            trip.segdur = lastDur;
    };

    bool inTrip = false;
    std::time_t lastTime = 0;

    for (auto const& [fTime, fPath] : frontFiles) {

        if (inTrip && (fTime - lastTime > gapThresholdSeconds)) {
            closeTrip(currentTrip);
            trips.push_back(currentTrip);
            inTrip = false;
        }

        if (!inTrip) {
            currentTrip = Trip();
            std::tm* tmPtr = std::localtime(&fTime);
            char dateBuf[20], timeBuf[20];
            std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", tmPtr);
            std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", tmPtr);
            currentTrip.date         = dateBuf;
            currentTrip.startTime    = timeBuf;
            currentTrip.startEpoch   = fTime;
            currentTrip.videoProfile = probeVideoProfile(fPath, ffprobePath);
            inTrip = true;
        }

        TripSegment seg;
        std::tm* tmPtr = std::localtime(&fTime);
        char tsBuf[20];
        std::strftime(tsBuf, sizeof(tsBuf), "%Y%m%d_%H%M%S", tmPtr);
        seg.timestamp = tsBuf;
        seg.front     = fPath;
        seg.rear      = findClosest(fTime, rearFiles);
        seg.left      = findClosest(fTime, leftFiles);
        seg.right     = findClosest(fTime, rightFiles);
        currentTrip.segments.push_back(seg);
        lastTime = fTime;
    }

    // Close the final trip
    if (inTrip) {
        closeTrip(currentTrip);
        trips.push_back(currentTrip);
    }

    // GPS extraction pass — find first lock and end coords for each trip.
    // Runs exiftool once per trip (first + last segment).  Non-fatal if
    // exiftool is absent or segments have no GPS data.
    std::cout << "  Extracting GPS start/end coords";
    for (auto& trip : trips) {
        extractStartEndGps(trip, exiftoolPath, exiftoolOptions);
        std::cout << "." << std::flush;
    }
    std::cout << "\n";

    return trips;
}

} // namespace Pathmux

// SN: 00074
