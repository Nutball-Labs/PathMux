// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#include "profile_detector.hpp"
#include <filesystem>
#include <regex>
#include <algorithm>

namespace fs = std::filesystem;

namespace CamClops {

// Count files in a slot's scan directory whose filename matches the profile
// regex and, if the slot has a filenameToken, the token capture group.
static int countSlotFilesInDir(const fs::path& scanDir,
                               const CameraSlot& slot,
                               const std::regex& pattern,
                               int tokenCaptureGroup)
{
    if (!fs::exists(scanDir)) return 0;
    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(scanDir, ec)) {
        if (entry.is_directory()) continue;
        std::string filename = entry.path().filename().string();
        if (filename.empty() || filename[0] == '.') continue;
        std::smatch match;
        if (!std::regex_search(filename, match, pattern)) continue;
        if ((int)match.size() > tokenCaptureGroup
                && match[tokenCaptureGroup].matched
                && !slot.filenameToken.empty()) {
            if (match[tokenCaptureGroup].str() != slot.filenameToken) continue;
        }
        ++count;
    }
    return count;
}

// Recursive search: return the subdirectory (up to maxDepth levels) with the
// most files matching the slot pattern.  Used when the declared scanSubdir and
// all candidates yield zero matches.
static fs::path findBestMatchDir(const fs::path& root,
                                  const CameraSlot& slot,
                                  const std::regex& pattern,
                                  int tokenCaptureGroup,
                                  int maxDepth)
{
    if (maxDepth <= 0) return {};
    fs::path best;
    int bestN = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        int n = countSlotFilesInDir(entry.path(), slot, pattern, tokenCaptureGroup);
        if (n > bestN) { bestN = n; best = entry.path(); }
        fs::path deeper = findBestMatchDir(entry.path(), slot, pattern,
                                           tokenCaptureGroup, maxDepth - 1);
        if (!deeper.empty()) {
            int dn = countSlotFilesInDir(deeper, slot, pattern, tokenCaptureGroup);
            if (dn > bestN) { bestN = dn; best = deeper; }
        }
    }
    return best;
}

static int countSlotFiles(const std::string& rootPath,
                          const CameraSlot& slot,
                          const std::regex& pattern,
                          int tokenCaptureGroup)
{
    // 1. Declared primary subdir
    fs::path primary = slot.scanSubdir.empty()
                     ? fs::path(rootPath)
                     : fs::path(rootPath) / slot.scanSubdir;
    int n = countSlotFilesInDir(primary, slot, pattern, tokenCaptureGroup);
    if (n > 0) return n;

    // 2. Known alternative layouts
    for (const auto& cand : slot.scanSubdirCandidates) {
        n = countSlotFilesInDir(fs::path(rootPath) / cand, slot, pattern, tokenCaptureGroup);
        if (n > 0) return n;
    }

    // 3. Deep search up to 4 levels — handles unexpected directory structures
    fs::path found = findBestMatchDir(fs::path(rootPath), slot, pattern, tokenCaptureGroup, 4);
    if (!found.empty())
        return countSlotFilesInDir(found, slot, pattern, tokenCaptureGroup);

    return 0;
}

ProfileMatch detectProfile(const std::string& path,
                           const std::vector<CameraProfile>& candidates)
{
    struct Scored {
        CameraProfile profile;
        int score;
        int primaryFiles;
        std::string detail;
    };
    std::vector<Scored> hits; // all profiles with primary files > 0 and score >= hasMatch

    for (const auto& profile : candidates) {
        if (!profile.isValid()) continue;

        std::regex pattern;
        try {
            pattern = std::regex(profile.filenameRegex,
                                 std::regex::ECMAScript | std::regex::icase);
        } catch (...) { continue; }

        const CameraSlot* primary = nullptr;
        for (const auto& s : profile.cameraSlots)
            if (s.isPrimary) { primary = &s; break; }
        if (!primary) continue;

        int primaryCount = countSlotFiles(path, *primary, pattern,
                                          profile.tokenCaptureGroup);
        if (primaryCount == 0) continue;

        int score = 60;
        for (const auto& slot : profile.cameraSlots) {
            if (slot.isPrimary) continue;
            int n = countSlotFiles(path, slot, pattern, profile.tokenCaptureGroup);
            if (n > 0) score += 20;
            else        score -= 5;
        }
        score = std::max(0, score);
        if (score < 55) continue; // below hasMatch threshold

        hits.push_back({profile, score, primaryCount,
                        std::to_string(primaryCount) + " primary file"
                        + (primaryCount == 1 ? "" : "s") + " matched"});
    }

    if (hits.empty()) return {};

    std::sort(hits.begin(), hits.end(),
              [](const Scored& a, const Scored& b){ return a.score > b.score; });

    ProfileMatch result;
    result.profile      = hits[0].profile;
    result.score        = hits[0].score;
    result.primaryFiles = hits[0].primaryFiles;
    result.detail       = hits[0].detail;

    if (hits.size() > 1) {
        result.isAmbiguous = (hits[0].score - hits[1].score) <= 10;
        // Multiple distinct profiles found: user is probably scanning a root
        // that contains footage from more than one dashcam model.
        result.isMixedContent = true;
        for (const auto& h : hits) result.mixedProfiles.push_back(h.profile);
    }

    return result;
}

} // namespace CamClops
// SN: 00104
