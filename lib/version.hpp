#ifndef VERSION_HPP
#define VERSION_HPP

// Global application identifiers
#define APP_NAME "PathMux Dashcam Explorer"

// 1. Define discrete version components
#define VERSION_MAJOR 0
#define VERSION_MINOR 9
#define VERSION_PATCH 10
#define VERSION_SUFFIX "k"

// 2. Stringification macros
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

// 3. Automatically build the APP_VERSION string
#define APP_VERSION STRINGIFY(VERSION_MAJOR) "." \
                    STRINGIFY(VERSION_MINOR) "." \
                    STRINGIFY(VERSION_PATCH) VERSION_SUFFIX

#endif
// SN: 00086
