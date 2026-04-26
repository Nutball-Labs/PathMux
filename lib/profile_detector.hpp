// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef PROFILE_DETECTOR_HPP
#define PROFILE_DETECTOR_HPP

#include "camera_profile.hpp"
#include <string>
#include <vector>

namespace Pathmux {

// ---------------------------------------------------------------------------
// ProfileMatch — result returned by detectProfile().
//
// score:        0–100. 60 = primary camera files found (minimum for a match).
//               +20 for each secondary camera also found.
//               -5  for each expected secondary camera NOT found.
// isAmbiguous:  true when the runner-up score is within 10 pts of the winner.
//               Caller should show the top choice but offer an override list.
// ---------------------------------------------------------------------------
struct ProfileMatch {
    CameraProfile              profile;
    int                        score        = 0;
    int                        primaryFiles = 0;
    std::string                detail;
    bool                       isAmbiguous    = false;
    // Set when more than one profile scores a significant hit in the same path.
    // Indicates mixed footage from multiple dashcam models — user should scan
    // each camera's subdirectory individually.
    bool                       isMixedContent = false;
    std::vector<CameraProfile> mixedProfiles;  // all profiles with significant hits
    bool hasMatch() const { return score >= 55; }
};

// Probe path against each candidate and return the best-scoring profile.
// Returns a ProfileMatch with score=0 and empty profile if nothing matches.
ProfileMatch detectProfile(const std::string& path,
                           const std::vector<CameraProfile>& candidates);

} // namespace Pathmux

#endif
// SN: 00104
