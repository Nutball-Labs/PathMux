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

string(REGEX MATCH "VERSION_BUILD ([0-9]+)" _ ${VERSION_HPP})
set(VER_BUILD ${CMAKE_MATCH_1})
if(NOT VER_BUILD)
    set(VER_BUILD 0)
endif()

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
# Install dirs — needed early for RPATH calculation
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Shared library — RPATH so installed binaries find libpathmux at runtime
# ---------------------------------------------------------------------------
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
endif()
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Windows: auto-export all symbols from the DLL (no __declspec annotations needed)
if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# ---------------------------------------------------------------------------
# Shared library — core logic shared by CLI and future Qt6 GUI
# ---------------------------------------------------------------------------
add_library(pathmuxlib SHARED
    lib/camera_profile.cpp
    lib/trip_detection.cpp
    lib/config_manager.cpp
    lib/platform.cpp
    lib/gps_export.cpp
)

set_target_properties(pathmuxlib PROPERTIES
    OUTPUT_NAME "pathmux"
    SOVERSION   1
    VERSION     "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}"
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
# Install targets — platform-specific layouts
# ---------------------------------------------------------------------------
if(WIN32)
    # Windows: all binaries + DLL at the package root; README and LICENSE alongside
    install(TARGETS pathmux pm_gpsinfo pm_gpsexport pm_ls pm_audit pm_findgpslock pm_probe pm_tripdebug
        RUNTIME DESTINATION "."
    )
    install(TARGETS pathmuxlib RUNTIME DESTINATION ".")
    install(FILES
            "${CMAKE_SOURCE_DIR}/LICENSE"
            "${CMAKE_SOURCE_DIR}/README.md"
        DESTINATION "."
    )
else()
    install(TARGETS pathmux pm_gpsinfo pm_gpsexport pm_ls pm_audit pm_findgpslock pm_probe pm_tripdebug
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(TARGETS pathmuxlib LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

    # Compress man pages — both RPM and DEB ecosystems expect .gz man pages.
    # Generate .1.gz files in the build dir at build time; install those.
    set(MANPAGE_NAMES pathmux pm_gpsinfo pm_gpsexport pm_ls pm_audit pm_probe pm_tripdebug pm_findgpslock)
    set(COMPRESSED_MANPAGES "")
    foreach(MP ${MANPAGE_NAMES})
        set(SRC "${CMAKE_SOURCE_DIR}/man1/${MP}.1")
        set(GZ  "${CMAKE_BINARY_DIR}/man1/${MP}.1.gz")
        add_custom_command(
            OUTPUT  "${GZ}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/man1"
            COMMAND gzip -9 -c "${SRC}" > "${GZ}"
            DEPENDS "${SRC}"
            COMMENT "Compressing ${MP}.1"
        )
        list(APPEND COMPRESSED_MANPAGES "${GZ}")
    endforeach()
    add_custom_target(compress-manpages ALL DEPENDS ${COMPRESSED_MANPAGES})

    install(FILES ${COMPRESSED_MANPAGES}
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
    )

    install(FILES "${CMAKE_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/pathmux"
    )
endif()

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
# MSI installer — Windows only, requires WiX 6 (wix.exe in PATH)
# Run: cmake --build build-win --target msi
# Output: packages/pathmux-<version>-win64.msi
# Install WiX 6: https://wixtoolset.org/releases/
# ---------------------------------------------------------------------------
if(WIN32)
    # Accept explicit path from run-build.ps1 (-DWIX_EXECUTABLE=...) before searching PATH.
    # WiX 6 installs as a .NET global tool and may not be visible in all shell environments.
    if(NOT WIX_EXECUTABLE)
        find_program(WIX_EXECUTABLE wix)
    endif()
    if(WIX_EXECUTABLE)
        configure_file(
            "${CMAKE_SOURCE_DIR}/wix/pathmux.wxs.in"
            "${CMAKE_BINARY_DIR}/pathmux.wxs"
            @ONLY
        )
        add_custom_target(msi
            COMMAND "${WIX_EXECUTABLE}" build
                -arch x64
                -o "${CMAKE_SOURCE_DIR}/packages/pathmux-${PATHMUX_VERSION}-win64.msi"
                "${CMAKE_BINARY_DIR}/pathmux.wxs"
                -d BinDir="${CMAKE_BINARY_DIR}"
                -d SrcDir="${CMAKE_SOURCE_DIR}"
            DEPENDS pathmux pm_gpsinfo pm_gpsexport pm_ls pm_audit pm_findgpslock pm_probe pm_tripdebug
            COMMENT "Building MSI with WiX 6"
        )
        message(STATUS "wix found: ${WIX_EXECUTABLE} — 'msi' target enabled")
    else()
        message(STATUS "wix.exe not found — 'msi' target unavailable (install WiX 6 from wixtoolset.org)")
    endif()
endif()

# ---------------------------------------------------------------------------
# pathmux-gui — Qt6 Widgets front-end
# Skipped silently if Qt6 is not installed; CLI builds are unaffected.
# Install: sudo dnf install qt6-qtbase-devel
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    add_executable(pathmux-gui
        gui/main.cpp
        gui/MainWindow.cpp
        gui/ManifestPanel.cpp
        gui/TripGridPanel.cpp
        gui/TripTile.cpp
        gui/EmptyManifestWidget.cpp
        gui/ScanProgressDialog.cpp
        gui/TripPropertiesDialog.cpp
        gui/AboutDialog.cpp
        gui/resources/pathmux.qrc
    )
    set_target_properties(pathmux-gui PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )
    if(WIN32)
        set_target_properties(pathmux-gui PROPERTIES WIN32_EXECUTABLE TRUE)
    elseif(APPLE)
        set_target_properties(pathmux-gui PROPERTIES MACOSX_BUNDLE TRUE)
    endif()
    target_include_directories(pathmux-gui PRIVATE gui/ ${CMAKE_SOURCE_DIR})
    target_link_libraries(pathmux-gui PRIVATE pathmuxlib Qt6::Widgets)
    if(NOT WIN32)
        install(TARGETS pathmux-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                                    BUNDLE  DESTINATION .)
    else()
        install(TARGETS pathmux-gui RUNTIME DESTINATION ".")
    endif()
    message(STATUS "Qt6 found (${Qt6_VERSION}) — pathmux-gui target enabled")
else()
    message(STATUS "Qt6 not found — pathmux-gui skipped (install qt6-qtbase-devel)")
endif()

# ---------------------------------------------------------------------------
# CPack — binary package generation
# Linux: cmake --build build-linux --target package  -> RPM, DEB, TGZ
# Windows ZIP: cmake --build build-win --target package  -> ZIP
# Output lands in packages/ at the project root.
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "pathmux")
set(CPACK_PACKAGE_VERSION "${PATHMUX_VERSION}")
set(CPACK_PACKAGE_VENDOR "Nutball Labs")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_CONTACT "Stephen Berg <pathmux@iceberg.email>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Nutball-Labs/PathMux")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Direct all package output to packages/ at the project root.
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/packages")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_FILE_NAME "pathmux-${PATHMUX_VERSION}-win64")
elseif(APPLE)
    # macOS: TGZ + ZIP (generic binary tarballs for manual install)
    set(CPACK_GENERATOR "TGZ;ZIP")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
    set(CPACK_PACKAGE_FILE_NAME "pathmux-${PATHMUX_VERSION}-macOS")
else()
    # Linux: RPM (Alma/RHEL), DEB (Ubuntu/Debian), TGZ (generic binary tarball)
    set(CPACK_GENERATOR "RPM;DEB;TGZ")

    # /usr is correct for RPM/DEB — the package manager owns this namespace.
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    # Use ecosystem-standard arch-suffixed filenames:
    #   pathmux-1.0.1-1.x86_64.rpm  /  pathmux_1.0.1_amd64.deb
    set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

    # RPM specific
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
    set(CPACK_RPM_PACKAGE_URL "https://github.com/Nutball-Labs/PathMux")
    set(CPACK_RPM_PACKAGE_RELEASE "1")
    set(CPACK_RPM_PACKAGE_DESCRIPTION
"PathMux scans multi-camera dashcam storage, groups video segments into trips,
caches results as JSON manifests, and builds per-camera MP4 files or
synchronized 4K collages. The pm_probe --wizard command profiles any dashcam
layout automatically. Requires ffmpeg (RPM Fusion) and ExifTool for full
functionality.")

    # DEB specific
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Stephen Berg <pathmux@iceberg.email>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Nutball-Labs/PathMux")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libstdc++6")

    # ldconfig scriptlets — update the linker cache after install/remove
    file(WRITE "${CMAKE_BINARY_DIR}/postinst" "#!/bin/sh\nset -e\nldconfig\n")
    file(WRITE "${CMAKE_BINARY_DIR}/postrm"   "#!/bin/sh\nset -e\nldconfig\n")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_BINARY_DIR}/postinst"
        "${CMAKE_BINARY_DIR}/postrm")
    file(WRITE "${CMAKE_BINARY_DIR}/rpm_ldconfig.sh" "#!/bin/sh\n/sbin/ldconfig\n")
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE   "${CMAKE_BINARY_DIR}/rpm_ldconfig.sh")
    set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_BINARY_DIR}/rpm_ldconfig.sh")
endif()

include(CPack)

# SN: 00090
