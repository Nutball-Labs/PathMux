#ifndef HOST_PREFS_HPP
#define HOST_PREFS_HPP

#include "config_manager.hpp"

using namespace Pathmux;

// ---------------------------------------------------------------------------
// HostPrefsEditor — launched via --hostprefs.
// Manages settings that vary per machine: encoder, tool paths, output dirs.
// Saves to ~/.config/pathmux/pathmux_<hostname>.json, not the shared base.
// ---------------------------------------------------------------------------
class HostPrefsEditor {
public:
    // Launch interactive host preferences UI.
    // Returns true if settings were saved, false if user quit without saving.
    bool run(ConfigManager& config);
};

#endif
// SN: 00087
