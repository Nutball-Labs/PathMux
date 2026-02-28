#ifndef KML_PREFS_HPP
#define KML_PREFS_HPP

#include "config_manager.hpp"

using namespace Pathmux;

class KmlPrefsEditor {
public:
    // Launch interactive KML preferences UI.
    // Returns true if settings were saved, false if user quit without saving.
    bool run(ConfigManager& config);
};

#endif
// SN: 00071
