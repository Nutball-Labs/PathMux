#ifndef PREFS_HPP
#define PREFS_HPP

#include "config_manager.hpp"

using namespace Pathmux;

class PrefsEditor {
public:
    // Launch interactive preferences UI.
    // Returns true if settings were saved, false if user quit without saving.
    bool run(ConfigManager& config);
};

// ---------------------------------------------------------------------------
// EncoderPrefsEditor — launched via --encoderprefs.
// Power-user escape hatch for hardware encoder configuration.
// Free-text entry allowed — invalid values cause encode failures at build time.
// ---------------------------------------------------------------------------
class EncoderPrefsEditor {
public:
    bool run(ConfigManager& config);
};

#endif
// SN: 00071
