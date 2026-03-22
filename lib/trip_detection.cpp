#include "trip_detection.hpp"
#include "camera_profile.hpp"
#include "compat.hpp"
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

    // Extract DateTimeOriginal from video metadata via exiftool.
    // Used when profile.timestampSource == "exiftool_metadata".
    // exiftool outputs the value as "YYYY:MM:DD HH:MM:SS" (UTC for QuickTime).
    // Returns 0 on failure.
    std::time_t exiftoolTimestamp(const std::string& filePath,
                                   const std::string& exiftoolPath) {
        std::string cmd = "\"" + exiftoolPath + "\""
                        + " -DateTimeOriginal -s3"
                        + " \"" + filePath + "\" " NULL_REDIRECT;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return 0;
        char buf[64] = {};
        fgets(buf, sizeof(buf), pipe);
        pclose(pipe);

        std::string ts(buf);
        while (!ts.empty() && (ts.back() == '\n' || ts.back() == '\r' || ts.back() == ' '))
            ts.pop_back();
        if (ts.empty()) return 0;

        // Parse "YYYY:MM:DD HH:MM:SS" and treat as UTC (QuickTime convention).
        std::tm t = {};
        std::istringstream ss(ts);
        ss >> std::get_time(&t, "%Y:%m:%d %H:%M:%S");
        if (ss.fail()) return 0;
        return timegm(&t);
    }

    std::time_t stringToTimestamp(const std::string& ts, const std::string& fmt) {
        std::tm t = {};
        std::istringstream ss(ts);
        ss >> std::get_time(&t, fmt.c_str());
        t.tm_isdst = -1;
        return std::mktime(&t);
    }

    // Return the sidecar thumbnail path for a video file, or "" if absent.
    // method: "replace_ext" — swap extension with ".jpg"
    //         "ths_sidecar" — replace extension with "_ths.jpg" (Pruveeo D90)
    //         anything else — no thumbnail
    std::string thumbFor(const std::string& videoPath, const std::string& method) {
        if (videoPath.empty() || videoPath == "-") return "";
        if (method == "none") return "";
        auto dot = videoPath.rfind('.');
        if (dot == std::string::npos) return "";
        std::string candidate;
        if (method == "ths_sidecar")
            candidate = videoPath.substr(0, dot) + "_ths.jpg";
        else
            candidate = videoPath.substr(0, dot) + ".jpg";
        return fs::exists(candidate) ? candidate : "";
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
            " \"" + filePath + "\" " NULL_REDIRECT;

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
            " \"" + filePath + "\" " NULL_REDIRECT;

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return 0;

        char buf[64] = {};
        if (!fgets(buf, sizeof(buf), pipe)) { pclose(pipe); return 0; }
        pclose(pipe);

        try { return static_cast<int>(std::stod(buf)); } catch (...) { return 0; }
    }


// Runs exiftool on the primary-camera file of the first and last segments to
// find the first valid GPS fix (non-zero lat/lon) and the last valid fix.
// Populates firstLock*, startLat/Lon, and endLat/Lon on the trip.
// Does nothing if exiftool is unavailable or the segment has no GPS data.
// ---------------------------------------------------------------------------
void extractStartEndGps(Trip& trip,
                        const std::string& primarySlot,
                        const std::string& exiftoolPath,
                        const std::string& exiftoolOptions)
{
    if (trip.segments.empty()) return;

    const std::string firstSeg = camPath(trip.segments.front(), primarySlot);
    const std::string lastSeg  = camPath(trip.segments.back(),  primarySlot);
    if (firstSeg == "-" || firstSeg.empty()) return;

    const std::string exifCmd = exiftoolPath + " " + exiftoolOptions + " ";

    // --- First segment: find first valid fix ---
    {
        std::string cmd = exifCmd + "\"" + firstSeg + "\" " NULL_REDIRECT;
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
                break;
            }
            ++recordIdx;
        }
        pclose(pipe);
    }

    // --- Last segment: find last valid fix ---
    // (may be same as first segment on a single-segment trip)
    {
        const std::string& seg = (lastSeg.empty() || lastSeg == "-")
                                 ? firstSeg : lastSeg;
        std::string cmd = exifCmd + "\"" + seg + "\" " NULL_REDIRECT;
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
                                              const CameraProfile& profile,
                                              int gapThresholdSeconds,
                                              int fuzzyWindowSeconds,
                                              const std::string& ffprobePath,
                                              const std::string& exiftoolPath,
                                              const std::string& exiftoolOptions)
{
    std::vector<Trip> trips;
    if (!fs::exists(path)) return trips;

    const std::string primary = profile.primarySlot();
    if (primary.empty()) return trips;

    std::regex filePattern(profile.filenameRegex);

    // Build one timestamp->path map per slot.
    std::map<std::string, std::map<std::time_t, std::string>> slotFiles;
    for (const auto& slot : profile.slots)
        slotFiles[slot.name] = {};

    auto scanSlot = [&](const CameraSlot& slot) {
        fs::path scanDir = slot.scanSubdir.empty()
                         ? fs::path(path)
                         : fs::path(path) / slot.scanSubdir;
        if (!fs::exists(scanDir)) return;

        auto& targetMap = slotFiles[slot.name];
        for (const auto& entry : fs::directory_iterator(scanDir)) {
            std::string filename = entry.path().filename().string();
            if (filename.empty() || filename[0] == '.') continue;

            std::smatch match;
            if (!std::regex_search(filename, match, filePattern)) continue;

            // Group 1 = timestamp string.
            // Group 2 (optional) = camera token.  If present and a
            // filenameToken is configured for this slot, verify they match.
            if (match.size() > 2 && match[2].matched && !slot.filenameToken.empty()) {
                if (match[2].str() != slot.filenameToken) continue;
            }

            std::time_t epoch;
            if (profile.timestampSource == "exiftool_metadata") {
                epoch = exiftoolTimestamp(entry.path().string(), exiftoolPath);
            } else {
                epoch = stringToTimestamp(match[1].str(), profile.timestampFormat);
            }
            if (epoch <= 0) continue;
            targetMap[epoch] = entry.path().string();
        }
    };

    for (const auto& slot : profile.slots)
        scanSlot(slot);

    if (slotFiles[primary].empty()) return trips;

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

    // seg.timestamp is always written by strftime with this fixed format regardless
    // of camera profile — use it consistently for all epoch arithmetic in closeTrip.
    static constexpr const char* kSegFmt = "%Y%m%d_%H%M%S";

    auto closeTrip = [&](Trip& trip) {
        const int segCount = (int)trip.segments.size();
        time_t firstEpoch = stringToTimestamp(trip.segments.front().timestamp, kSegFmt);
        time_t lastEpoch  = stringToTimestamp(trip.segments.back().timestamp,  kSegFmt);

        // Trip-level thumbnail convenience fields.
        // firstThumbs: segments[1] to avoid cold-start frames; fall back to [0].
        const TripSegment& firstSeg = (segCount >= 2) ? trip.segments[1] : trip.segments[0];
        const TripSegment& lastSeg  = trip.segments.back();
        trip.firstThumbs = firstSeg.thumbs;
        trip.lastThumbs  = lastSeg.thumbs;

        // segDetectedDuration: pure timestamp arithmetic.
        int nominalSegdur = (segCount > 1)
            ? static_cast<int>(stringToTimestamp(trip.segments[1].timestamp,
                                                 kSegFmt) - firstEpoch)
            : 0;
        trip.segDetectedDuration = static_cast<int>(lastEpoch - firstEpoch) + nominalSegdur;

        // Probe last segment for display duration string.
        int lastDur = probeSegmentDuration(camPath(trip.segments.back(), primary),
                                           ffprobePath);

        if (lastDur == 0) {
            int estimate = static_cast<int>(lastEpoch - firstEpoch) + 180;
            trip.duration = std::to_string(estimate / 60) + "m "
                          + std::to_string(estimate % 60) + "s";
            trip.segdur = 0;
            return;
        }

        int total = static_cast<int>(lastEpoch - firstEpoch) + lastDur;
        trip.duration = std::to_string(total / 60) + "m "
                      + std::to_string(total % 60) + "s";

        if (segCount >= 3)
            trip.segdur = static_cast<int>(
                stringToTimestamp(trip.segments[2].timestamp, kSegFmt)
              - stringToTimestamp(trip.segments[1].timestamp, kSegFmt));
        else if (segCount >= 2)
            trip.segdur = static_cast<int>(
                stringToTimestamp(trip.segments[1].timestamp, kSegFmt)
              - firstEpoch);
        else
            trip.segdur = lastDur;
    };

    bool inTrip = false;
    std::time_t lastTime = 0;

    for (auto const& [fTime, fPath] : slotFiles[primary]) {

        if (inTrip && (fTime - lastTime > gapThresholdSeconds)) {
            closeTrip(currentTrip);
            trips.push_back(currentTrip);
            inTrip = false;
        }

        if (!inTrip) {
            currentTrip = Trip();
            std::tm tmBuf{};
            localtime_r(&fTime, &tmBuf);
            char dateBuf[20], timeBuf[20];
            std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &tmBuf);
            std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);
            currentTrip.date         = dateBuf;
            currentTrip.startTime    = timeBuf;
            currentTrip.startEpoch   = fTime;
            currentTrip.videoProfile = probeVideoProfile(fPath, ffprobePath);
            inTrip = true;
        }

        TripSegment seg;
        std::tm tmBuf{};
        localtime_r(&fTime, &tmBuf);
        char tsBuf[20];
        std::strftime(tsBuf, sizeof(tsBuf), "%Y%m%d_%H%M%S", &tmBuf);
        seg.timestamp = tsBuf;

        // Primary camera — already resolved from slotFiles[primary].
        seg.cameras[primary] = fPath;
        seg.thumbs[primary]  = thumbFor(fPath, profile.thumbnailMethod);

        // Non-primary cameras — fuzzy-matched against primary timestamp.
        for (const auto& slot : profile.slots) {
            if (slot.isPrimary) continue;
            std::string p = findClosest(fTime, slotFiles[slot.name]);
            seg.cameras[slot.name] = p;
            seg.thumbs[slot.name]  = thumbFor(p, profile.thumbnailMethod);
        }

        currentTrip.segments.push_back(seg);
        lastTime = fTime;
    }

    if (inTrip) {
        closeTrip(currentTrip);
        trips.push_back(currentTrip);
    }

    // GPS extraction pass — find first lock and end coords for each trip.
    // Skip entirely if the profile has no GPS source.
    if (profile.gpsMethod != "none") {
        std::cout << "  Extracting GPS start/end coords";
        for (auto& trip : trips) {
            extractStartEndGps(trip, primary, exiftoolPath, exiftoolOptions);
            std::cout << "." << std::flush;
        }
        std::cout << "\n";
    }

    return trips;
}

} // namespace Pathmux
// SN: 00088
