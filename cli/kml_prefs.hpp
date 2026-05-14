// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef KML_PREFS_HPP
#define KML_PREFS_HPP

#include "config_manager.hpp"

using namespace CamClops;

class KmlPrefsEditor {
public:
    // Launch interactive KML preferences UI.
    // Returns true if settings were saved, false if user quit without saving.
    bool run(ConfigManager& config);
};

#endif
// SN: 00089
