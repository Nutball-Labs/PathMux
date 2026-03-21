cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Version — read from lib/version.hpp to keep single source of truth
# ---------------------------------------------------------------------------
file(READ "${CMAKE_SOURCE_DIR}/lib/version.hpp" VERSION_HPP)

string(REGEX MATCH "VERSION_MAJOR ([0-9]+)" _ ${VERSION_HPP})
set(VER_MAJOR ${CMAKE_MATCH_1})

string(REGEX MATCH "VERSION_MINOR ([0-9]+)" _ ${VERSION_HPP})
set(VER_MINOR ${CMAKE_MATCH_1})

string(REGEX MATCH "VERSION_PATCH ([0-9]+)" _ ${VERSION_HPP})
set(VER_PATCH ${CMAKE_MATCH_1})

string(REGEX MATCH "VERSION_SUFFIX \"([^\"]*)\"" _ ${VERSION_HPP})
set(VER_SUFFIX ${CMAKE_MATCH_1})

set(PATHMUX_VERSION "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}${VER_SUFFIX}")

project(pathmux
    VERSION "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}"
    DESCRIPTION "Multi-camera dashcam footage manager, trip organizer, and video builder"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Build banner
# ---------------------------------------------------------------------------
message(STATUS "********************************************")
message(STATUS "*                                          *")
message(STATUS "* Building PathMux version ${PATHMUX_VERSION}        *")
message(STATUS "*                                          *")
message(STATUS "********************************************")

# ---------------------------------------------------------------------------
# ccache — speeds up incremental and clean rebuilds by caching compiled objects.
# Each machine primes its own cache on first build; subsequent builds are fast.
# Install: sudo dnf install ccache
# ---------------------------------------------------------------------------
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "ccache found: ${CCACHE_PROGRAM}")
else()
    message(STATUS "ccache not found — builds will not be cached")
endif()

# ---------------------------------------------------------------------------
# Standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W3 /O2)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

# ---------------------------------------------------------------------------
# Static library — core logic shared by CLI and future Qt6 GUI
# ---------------------------------------------------------------------------
add_library(pathmuxlib STATIC
    lib/camera_profile.cpp
    lib/trip_detection.cpp
    lib/config_manager.cpp
    lib/platform.cpp
    lib/gps_export.cpp
)

# lib/ headers are PUBLIC so all targets linking pathmuxlib get them.
# Root is included for json.hpp (vendored, not in lib/).
target_include_directories(pathmuxlib PUBLIC
    lib/
    ${CMAKE_SOURCE_DIR}
)

if(UNIX AND NOT APPLE)
    target_link_libraries(pathmuxlib PUBLIC stdc++fs)
endif()

# Precompiled header for json.hpp — compiled once, reused by all lib sources.
# Eliminates the per-file GCC 14 template analysis cost on every rebuild.
target_precompile_headers(pathmuxlib PRIVATE "${CMAKE_SOURCE_DIR}/json.hpp")

# ---------------------------------------------------------------------------
# CLI executable
# ---------------------------------------------------------------------------
add_executable(pathmux
    cli/main.cpp
    cli/find_trips.cpp
    cli/gpx_export.cpp
    cli/prefs.cpp
    cli/host_prefs.cpp
    cli/kml_prefs.cpp
    cli/locations.cpp
    cli/video_build.cpp
)

set_target_properties(pathmux PROPERTIES OUTPUT_NAME pathmux)

# cli/ headers (ui_helpers.hpp etc.) are PRIVATE to the CLI target.
target_include_directories(pathmux PRIVATE cli/)

target_link_libraries(pathmux PRIVATE pathmuxlib)

# ---------------------------------------------------------------------------
# pm_gpsinfo — standalone GPS inspection tool
# ---------------------------------------------------------------------------
add_executable(pm_gpsinfo tools/pm_gpsinfo.cpp)
target_compile_features(pm_gpsinfo PRIVATE cxx_std_17)
target_include_directories(pm_gpsinfo PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(pm_gpsinfo PRIVATE pathmuxlib)

# ---------------------------------------------------------------------------
# pm_audit — footage integrity checker
# ---------------------------------------------------------------------------
add_executable(pm_audit tools/pm_audit.cpp)
target_compile_features(pm_audit PRIVATE cxx_std_17)
target_include_directories(pm_audit PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(pm_audit PRIVATE pathmuxlib)

# ---------------------------------------------------------------------------
# pm_ls — non-interactive trip lister
# ---------------------------------------------------------------------------
add_executable(pm_ls tools/pm_ls.cpp)
target_compile_features(pm_ls PRIVATE cxx_std_17)
target_include_directories(pm_ls PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(pm_ls PRIVATE pathmuxlib)

# ---------------------------------------------------------------------------
# pm_gpsexport — non-interactive GPS track exporter
# ---------------------------------------------------------------------------
add_executable(pm_gpsexport tools/pm_gpsexport.cpp)
target_compile_features(pm_gpsexport PRIVATE cxx_std_17)
target_include_directories(pm_gpsexport PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(pm_gpsexport PRIVATE pathmuxlib)

# ---------------------------------------------------------------------------
# pm_findgpslock — GPS lock diagnostic scanner
# ---------------------------------------------------------------------------
add_executable(pm_findgpslock tools/pm_findgpslock.cpp)
target_compile_features(pm_findgpslock PRIVATE cxx_std_17)
target_include_directories(pm_findgpslock PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(pm_findgpslock PRIVATE pathmuxlib)

# ---------------------------------------------------------------------------
# pm_probe — camera compatibility profiler
# ---------------------------------------------------------------------------
add_executable(pm_probe tools/pm_probe.cpp)
target_compile_features(pm_probe PRIVATE cxx_std_17)
target_include_directories(pm_probe PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(pm_probe PRIVATE pathmuxlib)

# ---------------------------------------------------------------------------
# pm_tripdebug — standalone debug tool (not installed by default)
# Build with: cmake --build build --target pm_tripdebug
# ---------------------------------------------------------------------------
add_executable(pm_tripdebug tools/pm_tripdebug.cpp)
target_link_libraries(pm_tripdebug PRIVATE pathmuxlib)

# ---------------------------------------------------------------------------
# Install targets (for RPM/DEB packaging and EPEL submission)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS pathmux pm_gpsinfo pm_gpsexport pm_ls pm_audit pm_findgpslock pm_probe pm_tripdebug
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
    "${CMAKE_SOURCE_DIR}/man1/pathmux.1"
    "${CMAKE_SOURCE_DIR}/man1/pm_gpsinfo.1"
    "${CMAKE_SOURCE_DIR}/man1/pm_gpsexport.1"
    "${CMAKE_SOURCE_DIR}/man1/pm_ls.1"
    "${CMAKE_SOURCE_DIR}/man1/pm_audit.1"
    "${CMAKE_SOURCE_DIR}/man1/pm_probe.1"
    "${CMAKE_SOURCE_DIR}/man1/pm_tripdebug.1"
    "${CMAKE_SOURCE_DIR}/man1/pm_findgpslock.1"
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
    OPTIONAL
)

# ---------------------------------------------------------------------------
# SN Audit target — scans all source files for SN: comments, writes
# sn_audit.txt to the source root. Uses a cmake script to avoid shell
# escaping issues across platforms.
# Run: cmake --build build --target sn-audit
# ---------------------------------------------------------------------------
add_custom_target(sn-audit
    COMMAND ${CMAKE_COMMAND}
        -DSRC=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/sn_audit.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running SN audit"
)

# ---------------------------------------------------------------------------
# Archive target — produces a versioned source tarball in archive/.
# Runs sn-audit first so sn_audit.txt is current.
# Run: cmake --build build --target archive
# ---------------------------------------------------------------------------
add_custom_target(archive
    COMMAND ${CMAKE_COMMAND}
        -DSRC=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/archive.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Archiving source (version read from lib/version.hpp at build time)"
    DEPENDS sn-audit
)

# ---------------------------------------------------------------------------
# CPack — for RPM and DEB package generation
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "pathmux")
set(CPACK_PACKAGE_VERSION "${PATHMUX_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_CONTACT "Stephen Berg")
set(CPACK_PACKAGE_LICENSE "GPL-2.0")

# RPM specific
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_LICENSE "GPLv2")
set(CPACK_RPM_PACKAGE_REQUIRES "exiftool")
# Note: ffmpeg is in RPM Fusion, not EPEL. Document as soft dependency.
# Users must install ffmpeg from RPM Fusion separately.
# Note: exiftool GPS support is camera-dependent. If GPS extraction fails,
# users should contact the ExifTool maintainer at https://exiftool.org
set(CPACK_RPM_PACKAGE_DESCRIPTION
"PathMux scans multi-camera dashcam storage, groups video segments into trips,
caches results as JSON manifests, and builds per-camera MP4 files or
synchronized 4K collages. Initial support targets the Pruveeo D90 four-channel
dashcam. Requires ffmpeg (RPM Fusion) and ExifTool for full
functionality.")

# DEB specific
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libstdc++6, exiftool")
# ffmpeg soft dependency — see RPM note above
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "ffmpeg")

include(CPack)

# SN: 00087
