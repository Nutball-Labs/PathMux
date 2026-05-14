// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef HOST_PREFS_HPP
#define HOST_PREFS_HPP

#include "config_manager.hpp"

using namespace CamClops;

// ---------------------------------------------------------------------------
// HostPrefsEditor — launched via --hostprefs.
// Manages settings that vary per machine: encoder, tool paths, output dirs.
// Saves to ~/.config/camclops/camclops_<hostname>.json, not the shared base.
// ---------------------------------------------------------------------------
class HostPrefsEditor {
public:
    // Launch interactive host preferences UI.
    // Returns true if settings were saved, false if user quit without saving.
    bool run(ConfigManager& config);
};

#endif
// SN: 00089
