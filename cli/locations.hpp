// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nutball Labs / Stephen Berg
#ifndef LOCATIONS_HPP
#define LOCATIONS_HPP

#include "config_manager.hpp"

using namespace Pathmux;

class LocationsEditor {
public:
    // Launch interactive known locations manager.
    void run(ConfigManager& config);
};

#endif
// SN: 00089
