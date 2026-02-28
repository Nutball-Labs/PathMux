cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Version — read from version.hpp to keep single source of truth
# ---------------------------------------------------------------------------
file(READ "${CMAKE_SOURCE_DIR}/version.hpp" VERSION_HPP)

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
# Standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
add_compile_options(-Wall -Wextra -O2)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(PATHMUX_SOURCES
    main.cpp
    trip_detection.cpp
    config_manager.cpp
    find_trips.cpp
    gpx_export.cpp
    prefs.cpp
    kml_prefs.cpp
    locations.cpp
    video_build.cpp
)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(pathmux ${PATHMUX_SOURCES})

target_include_directories(pathmux PRIVATE ${CMAKE_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Platform-specific link libraries
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    # Linux — stdc++fs needed on older toolchains, harmless on newer
    target_link_libraries(pathmux PRIVATE stdc++fs)
endif()

# ---------------------------------------------------------------------------
# Install targets (for RPM/DEB packaging and EPEL submission)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS pathmux
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES "${CMAKE_SOURCE_DIR}/pathmux.1"
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
    OPTIONAL
)

# ---------------------------------------------------------------------------
# trip_debug target — standalone debug tool, not installed by default
# ---------------------------------------------------------------------------
set(TRIP_DEBUG_SOURCES
    debug_main.cpp
    trip_detection.cpp
    config_manager.cpp
)

add_executable(trip_debug ${TRIP_DEBUG_SOURCES})
target_include_directories(trip_debug PRIVATE ${CMAKE_SOURCE_DIR})

if(UNIX AND NOT APPLE)
    target_link_libraries(trip_debug PRIVATE stdc++fs)
endif()

# trip_debug is a dev tool — excluded from install target intentionally.
# Build with: cmake --build build --target trip_debug

# ---------------------------------------------------------------------------
# pm_gpsinfo target — standalone GPS inspection tool
# ---------------------------------------------------------------------------
add_executable(pm_gpsinfo pm_gpsinfo.cpp)
target_compile_features(pm_gpsinfo PRIVATE cxx_std_17)
target_include_directories(pm_gpsinfo PRIVATE ${CMAKE_SOURCE_DIR})

if(UNIX AND NOT APPLE)
    target_link_libraries(pm_gpsinfo PRIVATE stdc++fs)
endif()

install(TARGETS pm_gpsinfo
    RUNTIME DESTINATION bin
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
    COMMENT "Archiving source (version read from version.hpp at build time)"
    DEPENDS sn-audit
)

# ---------------------------------------------------------------------------
# CPack — for RPM and DEB package generation
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "pathmux")
set(CPACK_PACKAGE_VERSION "${PATHMUX_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_CONTACT "Steve Berg")
set(CPACK_PACKAGE_LICENSE "GPL-2.0")

# RPM specific
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_LICENSE "GPLv2")
set(CPACK_RPM_PACKAGE_REQUIRES "exiftool >= 13.51")
# Note: ffmpeg is in RPM Fusion, not EPEL. Document as soft dependency.
# Users must install ffmpeg from RPM Fusion separately.
set(CPACK_RPM_PACKAGE_DESCRIPTION
"PathMux scans multi-camera dashcam storage, groups video segments into trips,
caches results as JSON manifests, and builds per-camera MP4 files or
synchronized 4K collages. Initial support targets the Pruveeo D90 four-channel
dashcam. Requires ffmpeg (RPM Fusion) and ExifTool 13.51+ for full
functionality.")

# DEB specific
set(CPACK_DEBIAN_PACKAGE_SECTION "video")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libstdc++6, exiftool")
# ffmpeg soft dependency — see RPM note above
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "ffmpeg")

include(CPack)

# SN: 00026
