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

set(CAMCLOPS_VERSION "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}${VER_SUFFIX}")

# macOS: set minimum deployment target before project() so all compiler tests
# and find_package() calls see the correct SDK constraints.
# Monterey (12.0) is the oldest release that ships std::filesystem without extra libs.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version (Monterey)")
endif()

project(camclops
    VERSION "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}"
    DESCRIPTION "Multi-camera dashcam footage manager, trip organizer, and video builder"
    LANGUAGES CXX
)

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
# Shared library — RPATH for installed binaries
# macOS: @executable_path-relative so the app bundle is self-contained.
# Linux: no RPATH — ldconfig (invoked by the RPM/DEB post-install scriptlet)
#   registers the library in /etc/ld.so.cache; RPM's check-rpaths rejects any
#   RPATH that points at a standard system path like /usr/lib64.
# ---------------------------------------------------------------------------
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

# Windows: auto-export all symbols from the DLL (no __declspec annotations needed)
if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# ---------------------------------------------------------------------------
# Shared library — core logic shared by CLI and future Qt6 GUI
# ---------------------------------------------------------------------------
add_library(camclopslib SHARED
    lib/camera_profile.cpp
    lib/trip_detection.cpp
    lib/config_manager.cpp
    lib/platform.cpp
    lib/gps_export.cpp
    lib/profile_detector.cpp
)

set_target_properties(camclopslib PROPERTIES
    OUTPUT_NAME "camclops"
    SOVERSION   1
    VERSION     "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}"
)

# lib/ headers are PUBLIC so all targets linking camclopslib get them.
# Root is included for json.hpp (vendored, not in lib/).
target_include_directories(camclopslib PUBLIC
    lib/
    ${CMAKE_SOURCE_DIR}
)

if(UNIX AND NOT APPLE)
    target_link_libraries(camclopslib PUBLIC stdc++fs)
endif()

# Precompiled header for json.hpp — compiled once, reused by all lib sources.
# Eliminates the per-file GCC 14 template analysis cost on every rebuild.
target_precompile_headers(camclopslib PRIVATE "${CMAKE_SOURCE_DIR}/json.hpp")

# ---------------------------------------------------------------------------
# CLI executable
# ---------------------------------------------------------------------------
add_executable(camclops
    cli/main.cpp
    cli/find_trips.cpp
    cli/gpx_export.cpp
    cli/prefs.cpp
    cli/host_prefs.cpp
    cli/kml_prefs.cpp
    cli/locations.cpp
    cli/video_build.cpp
)

set_target_properties(camclops PROPERTIES OUTPUT_NAME camclops)

# cli/ headers (ui_helpers.hpp etc.) are PRIVATE to the CLI target.
target_include_directories(camclops PRIVATE cli/)

target_link_libraries(camclops PRIVATE camclopslib)

# ---------------------------------------------------------------------------
# clops_gpsinfo — standalone GPS inspection tool
# ---------------------------------------------------------------------------
add_executable(clops_gpsinfo tools/clops_gpsinfo.cpp)
target_compile_features(clops_gpsinfo PRIVATE cxx_std_17)
target_include_directories(clops_gpsinfo PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(clops_gpsinfo PRIVATE camclopslib)

# ---------------------------------------------------------------------------
# clops_audit — footage integrity checker
# ---------------------------------------------------------------------------
add_executable(clops_audit tools/clops_audit.cpp)
target_compile_features(clops_audit PRIVATE cxx_std_17)
target_include_directories(clops_audit PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(clops_audit PRIVATE camclopslib)

# ---------------------------------------------------------------------------
# clops_ls — non-interactive trip lister
# ---------------------------------------------------------------------------
add_executable(clops_ls tools/clops_ls.cpp)
target_compile_features(clops_ls PRIVATE cxx_std_17)
target_include_directories(clops_ls PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(clops_ls PRIVATE camclopslib)

# ---------------------------------------------------------------------------
# clops_gpsexport — non-interactive GPS track exporter
# ---------------------------------------------------------------------------
add_executable(clops_gpsexport tools/clops_gpsexport.cpp)
target_compile_features(clops_gpsexport PRIVATE cxx_std_17)
target_include_directories(clops_gpsexport PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(clops_gpsexport PRIVATE camclopslib)

# ---------------------------------------------------------------------------
# clops_findgpslock — GPS lock diagnostic scanner
# ---------------------------------------------------------------------------
add_executable(clops_findgpslock tools/clops_findgpslock.cpp)
target_compile_features(clops_findgpslock PRIVATE cxx_std_17)
target_include_directories(clops_findgpslock PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(clops_findgpslock PRIVATE camclopslib)

# ---------------------------------------------------------------------------
# clops_probe — camera compatibility profiler
# ---------------------------------------------------------------------------
add_executable(clops_probe tools/clops_probe.cpp)
target_compile_features(clops_probe PRIVATE cxx_std_17)
target_include_directories(clops_probe PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(clops_probe PRIVATE camclopslib)

# ---------------------------------------------------------------------------
# clops_tripdebug — standalone debug tool (not installed by default)
# Build with: cmake --build build --target clops_tripdebug
# ---------------------------------------------------------------------------
add_executable(clops_tripdebug tools/clops_tripdebug.cpp)
target_link_libraries(clops_tripdebug PRIVATE camclopslib)

# ---------------------------------------------------------------------------
# clops_videos — batch map/dashboard/HUD video generator
# ---------------------------------------------------------------------------
add_executable(clops_videos tools/clops_videos.cpp)
target_compile_features(clops_videos PRIVATE cxx_std_17)
target_include_directories(clops_videos PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(clops_videos PRIVATE camclopslib)

# ---------------------------------------------------------------------------
# Install targets — platform-specific layouts
# ---------------------------------------------------------------------------
if(WIN32)
    # Windows: all binaries + DLL at the package root; README and LICENSE alongside
    install(TARGETS camclops clops_gpsinfo clops_gpsexport clops_ls clops_audit clops_findgpslock clops_probe clops_tripdebug clops_videos
        RUNTIME DESTINATION "."
    )
    install(TARGETS camclopslib RUNTIME DESTINATION ".")
    install(FILES
            "${CMAKE_SOURCE_DIR}/LICENSE"
            "${CMAKE_SOURCE_DIR}/README.md"
        DESTINATION "."
    )
    # Python overlay scripts — installed into scripts\ so the GUI can find them
    install(FILES
            "${CMAKE_SOURCE_DIR}/scripts/clops_maprender.py"
            "${CMAKE_SOURCE_DIR}/scripts/clops_dashboard.py"
            "${CMAKE_SOURCE_DIR}/scripts/clops_hud.py"
            "${CMAKE_SOURCE_DIR}/scripts/clops_sync_analyze.py"
            "${CMAKE_SOURCE_DIR}/scripts/clops_monitor.py"
        DESTINATION "scripts"
    )
else()
    install(TARGETS camclops clops_gpsinfo clops_gpsexport clops_ls clops_audit clops_findgpslock clops_probe clops_tripdebug clops_videos
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(TARGETS camclopslib LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

    # Compress man pages — both RPM and DEB ecosystems expect .gz man pages.
    # Generate .1.gz files in the build dir at build time; install those.
    set(MANPAGE_NAMES camclops clops_gpsinfo clops_gpsexport clops_ls clops_audit clops_probe clops_tripdebug clops_findgpslock)
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
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/camclops"
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
# macOS app icon — camclops.icns generated from PNG sources via iconutil.
# Both camclops-gui and camclops-tl include this in their Contents/Resources/.
# The custom command fires whenever the source PNGs change.
# ---------------------------------------------------------------------------
if(APPLE)
    set(CAMCLOPS_ICNS_OUTPUT "${CMAKE_BINARY_DIR}/camclops.icns")
    set(CAMCLOPS_ICONSET_DIR "${CMAKE_BINARY_DIR}/camclops.iconset")
    add_custom_command(
        OUTPUT  "${CAMCLOPS_ICNS_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CAMCLOPS_ICONSET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_16.png"
                "${CAMCLOPS_ICONSET_DIR}/icon_16x16.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_32.png"
                "${CAMCLOPS_ICONSET_DIR}/icon_16x16@2x.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_32.png"
                "${CAMCLOPS_ICONSET_DIR}/icon_32x32.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_64.png"
                "${CAMCLOPS_ICONSET_DIR}/icon_32x32@2x.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_128.png"
                "${CAMCLOPS_ICONSET_DIR}/icon_128x128.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_256.png"
                "${CAMCLOPS_ICONSET_DIR}/icon_128x128@2x.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_256.png"
                "${CAMCLOPS_ICONSET_DIR}/icon_256x256.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_512.png"
                "${CAMCLOPS_ICONSET_DIR}/icon_256x256@2x.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_512.png"
                "${CAMCLOPS_ICONSET_DIR}/icon_512x512.png"
        COMMAND iconutil -c icns "${CAMCLOPS_ICONSET_DIR}" -o "${CAMCLOPS_ICNS_OUTPUT}"
        DEPENDS
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_16.png"
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_32.png"
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_64.png"
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_128.png"
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_256.png"
                "${CMAKE_SOURCE_DIR}/gui/resources/camclops_512.png"
        COMMENT "Generating camclops.icns from PNG sources"
    )
    set_source_files_properties("${CAMCLOPS_ICNS_OUTPUT}" PROPERTIES
        GENERATED               TRUE
        MACOSX_PACKAGE_LOCATION "Resources"
    )
endif()

# ---------------------------------------------------------------------------
# camclops-gui — Qt6 Widgets front-end
# Skipped silently if Qt6 is not installed; CLI builds are unaffected.
# Linux install:   sudo dnf install qt6-qtbase-devel
# macOS install:   brew install qt6
# Windows install: Qt Online Installer from qt.io
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Widgets OPTIONAL_COMPONENTS DBus)
if(Qt6_FOUND)
    add_executable(camclops-gui
        gui/main.cpp
        gui/MainWindow.cpp
        gui/ManifestPanel.cpp
        gui/TripGridPanel.cpp
        gui/TripTile.cpp
        gui/EmptyManifestWidget.cpp
        gui/TripPropertiesDialog.cpp
        gui/TripSearchDialog.cpp
        gui/ExtrasDialog.cpp
        gui/TripBuildDialog.cpp
        gui/BuildProgressDialog.cpp
        gui/MapProgressDialog.cpp
        gui/BuildAllDialog.cpp
        gui/AboutDialog.cpp
        gui/SettingsDialog.cpp
        gui/CameraProfilesDialog.cpp
        gui/ManifestManagerDialog.cpp
        gui/SetupWizard.cpp
        gui/HelpDialog.cpp
        gui/LogoMorphWidget.cpp
        gui/JobQueue.cpp
        gui/JobQueuePanel.cpp
        gui/JobQueueMonitor.cpp
        cli/video_build.cpp
        gui/resources/camclops.qrc
    )
    set_target_properties(camclops-gui PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )
    if(WIN32)
        # WIN32_EXECUTABLE suppresses the console window on Windows.
        # camclops.rc embeds camclops.ico into the executable for taskbar/Explorer.
        set_target_properties(camclops-gui PROPERTIES WIN32_EXECUTABLE TRUE)
        target_sources(camclops-gui PRIVATE gui/resources/camclops.rc)
    elseif(APPLE)
        set_target_properties(camclops-gui PROPERTIES
            MACOSX_BUNDLE                    TRUE
            MACOSX_BUNDLE_BUNDLE_NAME        "CamClops"
            MACOSX_BUNDLE_GUI_IDENTIFIER     "com.nutballlabs.camclops"
            MACOSX_BUNDLE_BUNDLE_VERSION     "${CAMCLOPS_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${CAMCLOPS_VERSION}"
            MACOSX_BUNDLE_INFO_PLIST         "${CMAKE_SOURCE_DIR}/gui/Info.plist.in"
            MACOSX_BUNDLE_ICON_FILE          "camclops"
        )
        target_sources(camclops-gui PRIVATE "${CAMCLOPS_ICNS_OUTPUT}")
    endif()
    target_include_directories(camclops-gui PRIVATE gui/ cli/ ${CMAKE_SOURCE_DIR})
    target_link_libraries(camclops-gui PRIVATE camclopslib Qt6::Widgets)
    if(UNIX AND NOT APPLE AND TARGET Qt6::DBus)
        target_link_libraries(camclops-gui PRIVATE Qt6::DBus)
        target_compile_definitions(camclops-gui PRIVATE CAMCLOPS_HAVE_DBUS)
        message(STATUS "Qt6::DBus found — sleep inhibitor enabled (Linux)")
    elseif(UNIX AND NOT APPLE)
        message(STATUS "Qt6::DBus not found — sleep inhibitor disabled on Linux")
    endif()
    if(APPLE)
        target_link_libraries(camclops-gui PRIVATE "-framework IOKit")
    endif()
    if(WIN32)
        install(TARGETS camclops-gui RUNTIME DESTINATION ".")
        # Run windeployqt at install time so CPack ZIP captures Qt DLLs and
        # plugin directories.  We locate windeployqt relative to the Qt6 bin dir
        # so this works regardless of Qt version or install path.
        find_program(WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt windeployqt6
            HINTS "${Qt6_DIR}/../../../bin"
            NO_DEFAULT_PATH
        )
        if(WINDEPLOYQT_EXECUTABLE)
            install(CODE "
                message(STATUS \"Running windeployqt on installed camclops-gui.exe ...\")
                execute_process(
                    COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                        --release --no-translations
                        \"\${CMAKE_INSTALL_PREFIX}/camclops-gui.exe\"
                    RESULT_VARIABLE _wdqt_result
                )
                if(NOT _wdqt_result EQUAL 0)
                    message(WARNING \"windeployqt exited with \${_wdqt_result}\")
                endif()
            ")
            message(STATUS "windeployqt found: ${WINDEPLOYQT_EXECUTABLE} — Qt DLLs will be bundled in ZIP")
        else()
            message(WARNING "windeployqt not found — Qt DLLs will NOT be bundled in the ZIP package")
        endif()
    elseif(APPLE)
        # macOS: stage bundle under /usr/local (unsigned pkgs can install there);
        # the postinstall script moves it to /Applications at install time.
        # SKIP_*_RPATH: macdeployqt (POST_BUILD) rewrites Qt load paths before
        # cmake --install runs, so CMake's rpath fixup would fail trying to delete
        # a path macdeployqt already removed. Both flags suppress that step.
        set_target_properties(camclops-gui PROPERTIES
            SKIP_BUILD_RPATH    TRUE
            SKIP_INSTALL_RPATH  TRUE
        )
        install(TARGETS camclops-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                                    BUNDLE  DESTINATION .)
        # Python overlay scripts — installed into the bundle's Contents/scripts/
        # so findScript() can locate them via applicationDirPath()/../scripts/
        install(FILES
                "${CMAKE_SOURCE_DIR}/scripts/clops_maprender.py"
                "${CMAKE_SOURCE_DIR}/scripts/clops_dashboard.py"
                "${CMAKE_SOURCE_DIR}/scripts/clops_hud.py"
                "${CMAKE_SOURCE_DIR}/scripts/clops_sync_analyze.py"
                "${CMAKE_SOURCE_DIR}/scripts/clops_monitor.py"
            DESTINATION "camclops-gui.app/Contents/scripts"
        )
        # Uninstall helper — installed to bin/ so users can run: sudo camclops-uninstall
        # Also placed inside the bundle's Resources/ as a fallback if bin/ entry is gone.
        install(PROGRAMS "${CMAKE_SOURCE_DIR}/packaging/uninstall-camclops.sh"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            RENAME camclops-uninstall
        )
        install(PROGRAMS "${CMAKE_SOURCE_DIR}/packaging/uninstall-camclops.sh"
            DESTINATION "camclops-gui.app/Contents/Resources"
            RENAME uninstall-camclops.sh
        )
    else()
        install(TARGETS camclops-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                                    BUNDLE  DESTINATION .)
        # Python overlay scripts — installed into share/camclops/scripts/ so
        # findScript() can locate them via applicationDirPath()/../share/camclops/scripts/
        install(FILES
                "${CMAKE_SOURCE_DIR}/scripts/clops_maprender.py"
                "${CMAKE_SOURCE_DIR}/scripts/clops_dashboard.py"
                "${CMAKE_SOURCE_DIR}/scripts/clops_hud.py"
                "${CMAKE_SOURCE_DIR}/scripts/clops_sync_analyze.py"
                "${CMAKE_SOURCE_DIR}/scripts/clops_monitor.py"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/camclops/scripts"
        )
        # XDG icon hierarchy for Linux desktop environments
        foreach(sz 16 32 48 64 128 256 512)
            install(FILES "gui/resources/camclops_${sz}.png"
                    DESTINATION "share/icons/hicolor/${sz}x${sz}/apps"
                    RENAME "camclops.png")
        endforeach()
        # .desktop file — appears in GNOME/KDE application menus
        install(FILES "gui/camclops.desktop"
                DESTINATION "share/applications")
    endif()
    # Windows: copy Python overlay scripts into scripts\ next to the exe at
    # build time so findScript() works when running from the build directory.
    # (install() rules only run during cmake --install / cpack.)
    # build-windows.ps1 copies all subdirectories from the build dir to
    # build-win\, so the scripts\ folder flows through automatically.
    if(WIN32)
        add_custom_command(TARGET camclops-gui POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "$<TARGET_FILE_DIR:camclops-gui>/scripts"
            COMMAND "${CMAKE_COMMAND}" -E copy
                    "${CMAKE_SOURCE_DIR}/scripts/clops_maprender.py"
                    "${CMAKE_SOURCE_DIR}/scripts/clops_dashboard.py"
                    "${CMAKE_SOURCE_DIR}/scripts/clops_hud.py"
                    "${CMAKE_SOURCE_DIR}/scripts/clops_sync_analyze.py"
                    "${CMAKE_SOURCE_DIR}/scripts/clops_monitor.py"
                    "$<TARGET_FILE_DIR:camclops-gui>/scripts/"
            COMMENT "Copying Python overlay scripts into build directory"
        )
    endif()

    # macOS: run macdeployqt after each build to rewrite Qt load paths from
    # the Homebrew absolute paths to @executable_path/../Frameworks/.
    # Without this, both the bundled Qt and the system Homebrew Qt get loaded,
    # causing Objective-C class conflicts and a cocoa plugin crash at startup.
    if(APPLE)
        find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
            HINTS "${Qt6_DIR}/../../../bin"
            REQUIRED
        )
        add_custom_command(TARGET camclops-gui POST_BUILD
            COMMAND "${MACDEPLOYQT_EXECUTABLE}" "$<TARGET_BUNDLE_DIR:camclops-gui>" -codesign=-
            COMMAND find "$<TARGET_BUNDLE_DIR:camclops-gui>" -name "._*" -delete
            VERBATIM
            COMMENT "Running macdeployqt and ad-hoc signing camclops-gui.app"
        )
        # Copy Python overlay scripts into the build-time bundle so findScript()
        # works when running directly from the build directory during development.
        # (install() rules only run during cmake --install / cpack.)
        add_custom_command(TARGET camclops-gui POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "$<TARGET_BUNDLE_DIR:camclops-gui>/Contents/scripts"
            COMMAND "${CMAKE_COMMAND}" -E copy
                    "${CMAKE_SOURCE_DIR}/scripts/clops_maprender.py"
                    "${CMAKE_SOURCE_DIR}/scripts/clops_dashboard.py"
                    "${CMAKE_SOURCE_DIR}/scripts/clops_hud.py"
                    "${CMAKE_SOURCE_DIR}/scripts/clops_sync_analyze.py"
                    "${CMAKE_SOURCE_DIR}/scripts/clops_monitor.py"
                    "$<TARGET_BUNDLE_DIR:camclops-gui>/Contents/scripts/"
            COMMENT "Copying Python overlay scripts into macOS app bundle"
        )
    endif()

    # ── Logo morph animation ─────────────────────────────────────────────────
    # Generates gui/resources/logo_morph.mp4 from the two logo PNGs at build
    # time.  Requires Python3 + Pillow + ffmpeg on the build machine.
    # The MP4 is embedded in the binary via QRC (266 KB).
    find_package(Python3 QUIET COMPONENTS Interpreter)
    # Verify the found Python3 has PIL; if not, fall back to 'python3' from PATH
    if(Python3_FOUND)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -c "import PIL"
            RESULT_VARIABLE _pil_rc OUTPUT_QUIET ERROR_QUIET)
        if(NOT _pil_rc EQUAL 0)
            find_program(_python3_fallback NAMES python3)
            if(_python3_fallback)
                execute_process(
                    COMMAND "${_python3_fallback}" -c "import PIL"
                    RESULT_VARIABLE _pil_fb_rc OUTPUT_QUIET ERROR_QUIET)
                if(_pil_fb_rc EQUAL 0)
                    set(Python3_EXECUTABLE "${_python3_fallback}")
                    message(STATUS "Logo morph: ${Python3_EXECUTABLE} has no PIL — falling back to ${_python3_fallback}")
                else()
                    set(Python3_FOUND FALSE)
                    message(STATUS "Logo morph: no Python3 with PIL found — will use pre-built mp4 if available")
                endif()
            else()
                set(Python3_FOUND FALSE)
            endif()
        endif()
    endif()
    find_program(FFMPEG_BUILD_TOOL NAMES ffmpeg
        HINTS /usr/bin /usr/local/bin /opt/homebrew/bin)
    set(LOGO_MORPH_MP4 "${CMAKE_SOURCE_DIR}/gui/resources/logo_morph.mp4")
    if(Python3_FOUND AND FFMPEG_BUILD_TOOL)
        add_custom_command(
            OUTPUT  "${LOGO_MORPH_MP4}"
            COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/scripts/gen_logo_morph.py"
                    "${FFMPEG_BUILD_TOOL}"
                    "${LOGO_MORPH_MP4}"
                    "${CMAKE_SOURCE_DIR}/gui/resources/camclops_256.png"
                    "${CMAKE_SOURCE_DIR}/gui/resources/Nutball-Labs_logo.png"
            DEPENDS "${CMAKE_SOURCE_DIR}/scripts/gen_logo_morph.py"
                    "${CMAKE_SOURCE_DIR}/gui/resources/camclops_256.png"
                    "${CMAKE_SOURCE_DIR}/gui/resources/Nutball-Labs_logo.png"
            COMMENT "Generating logo_morph.mp4 (CamClops ↔ Nutball-Labs cross-fade)"
        )
        add_custom_target(gen_logo_morph DEPENDS "${LOGO_MORPH_MP4}")
        add_dependencies(camclops-gui gen_logo_morph)
        message(STATUS "Logo morph: Python3 + ffmpeg found — logo_morph.mp4 generated at build time")
    else()
        if(NOT EXISTS "${LOGO_MORPH_MP4}")
            message(WARNING "Logo morph: Python3 or ffmpeg not found and logo_morph.mp4 missing — run scripts/gen_logo_morph.py manually")
        else()
            message(STATUS "Logo morph: using pre-built gui/resources/logo_morph.mp4")
        endif()
    endif()

    message(STATUS "Qt6 found (${Qt6_VERSION}) — camclops-gui target enabled")
else()
    message(STATUS "Qt6 not found — camclops-gui skipped (install qt6-qtbase-devel)")
endif()

# ---------------------------------------------------------------------------
# camclops-tl — CamClops Timelapse Editor (standalone Qt6 GUI app)
# Requires Qt6::Multimedia and Qt6::MultimediaWidgets in addition to Widgets.
# On Alma Linux: dnf install qt6-qtmultimedia-devel gstreamer1-plugins-good
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Widgets Multimedia MultimediaWidgets)
if(Qt6_FOUND AND Qt6Multimedia_FOUND AND Qt6MultimediaWidgets_FOUND)
    add_executable(camclops-tl
        camclops-tl/main.cpp
        camclops-tl/MainWindow.cpp
        camclops-tl/TimelineWidget.cpp
        camclops-tl/MarkDialog.cpp
        camclops-tl/FrameStrip.cpp
        camclops-tl/camclops_tl.qrc
    )
    set_target_properties(camclops-tl PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )
    target_include_directories(camclops-tl PRIVATE camclops-tl ${CMAKE_SOURCE_DIR}/lib)
    target_link_libraries(camclops-tl
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::MultimediaWidgets
    )
    if(WIN32)
        set_target_properties(camclops-tl PROPERTIES WIN32_EXECUTABLE TRUE)
        target_sources(camclops-tl PRIVATE camclops-tl/camclops-tl.rc)
        install(TARGETS camclops-tl RUNTIME DESTINATION ".")
        if(WINDEPLOYQT_EXECUTABLE)
            install(CODE "
                message(STATUS \"Running windeployqt on camclops-tl.exe ...\")
                execute_process(
                    COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                        --release --no-translations
                        \"\${CMAKE_INSTALL_PREFIX}/camclops-tl.exe\"
                    RESULT_VARIABLE _wdqt_result
                )
                if(NOT _wdqt_result EQUAL 0)
                    message(WARNING \"windeployqt (camclops-tl) exited with \${_wdqt_result}\")
                endif()
            ")
        endif()
    elseif(APPLE)
        set_target_properties(camclops-tl PROPERTIES
            SKIP_BUILD_RPATH                 TRUE
            SKIP_INSTALL_RPATH               TRUE
            MACOSX_BUNDLE                    TRUE
            MACOSX_BUNDLE_BUNDLE_NAME        "CamClops Timelapse"
            MACOSX_BUNDLE_GUI_IDENTIFIER     "com.nutballlabs.camclops-tl"
            MACOSX_BUNDLE_BUNDLE_VERSION     "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_INFO_PLIST         "${CMAKE_SOURCE_DIR}/camclops-tl/Info.plist.in"
            MACOSX_BUNDLE_ICON_FILE          "camclops"
        )
        target_sources(camclops-tl PRIVATE "${CAMCLOPS_ICNS_OUTPUT}")
        install(TARGETS camclops-tl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                                    BUNDLE  DESTINATION .)
        if(MACDEPLOYQT_EXECUTABLE)
            # camclops-tl is always shipped alongside camclops-gui; share its
            # Frameworks dir rather than duplicating ~33 MB of Qt libraries.
            # macdeployqt must still run first to rewrite Homebrew absolute load
            # paths → @executable_path/../Frameworks/…; the symlink is then
            # substituted so the binary resolves through camclops-gui.app's copy.
            add_dependencies(camclops-tl camclops-gui)
            add_custom_command(TARGET camclops-tl POST_BUILD
                COMMAND "${MACDEPLOYQT_EXECUTABLE}" "$<TARGET_BUNDLE_DIR:camclops-tl>" -codesign=-
                COMMAND find "$<TARGET_BUNDLE_DIR:camclops-tl>" -name "._*" -delete
                COMMAND rm -rf "$<TARGET_BUNDLE_DIR:camclops-tl>/Contents/Frameworks"
                COMMAND ln -s "../../camclops-gui.app/Contents/Frameworks"
                              "$<TARGET_BUNDLE_DIR:camclops-tl>/Contents/Frameworks"
                VERBATIM
                COMMENT "macdeployqt + sign camclops-tl; replace Frameworks with symlink to camclops-gui"
            )
        endif()
    else()
        install(TARGETS camclops-tl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES "camclops-tl/camclops-tl.desktop"
                DESTINATION "share/applications"
                OPTIONAL)
    endif()
    message(STATUS "Qt6 Multimedia found — camclops-tl target enabled")
else()
    message(STATUS "Qt6 Multimedia not found — camclops-tl skipped "
                   "(install qt6-qtmultimedia-devel + gstreamer1-plugins-good)")
endif()

# ---------------------------------------------------------------------------
# MSI installer — Windows only, requires WiX 6 (wix.exe in PATH)
# Run: cmake --build build-win --target msi
# Output: packages/camclops-<version>-win64.msi
# Install WiX 6: https://wixtoolset.org/releases/
# NOTE: Run windeployqt on camclops-gui.exe before this target — the WiX
#   template references Qt DLLs that windeployqt copies into the build dir.
# ---------------------------------------------------------------------------
if(WIN32)
    # Accept explicit path from run-build.ps1 (-DWIX_EXECUTABLE=...) before searching PATH.
    # WiX 6 installs as a .NET global tool and may not be visible in all shell environments.
    if(NOT WIX_EXECUTABLE)
        find_program(WIX_EXECUTABLE wix)
    endif()
    if(WIX_EXECUTABLE)
        configure_file(
            "${CMAKE_SOURCE_DIR}/wix/camclops.wxs.in"
            "${CMAKE_BINARY_DIR}/camclops.wxs"
            @ONLY
        )
        # Build dependency list — include GUI only if Qt6 was found
        set(_MSI_DEPS camclops clops_gpsinfo clops_gpsexport clops_ls clops_audit clops_findgpslock clops_probe clops_tripdebug clops_videos)
        if(Qt6_FOUND)
            list(APPEND _MSI_DEPS camclops-gui)
        endif()
        add_custom_target(msi
            COMMAND "${WIX_EXECUTABLE}" build
                -arch x64
                -o "${CMAKE_SOURCE_DIR}/packages/camclops-${CAMCLOPS_VERSION}-win64.msi"
                "${CMAKE_BINARY_DIR}/camclops.wxs"
                -d BinDir="${CMAKE_BINARY_DIR}"
                -d SrcDir="${CMAKE_SOURCE_DIR}"
            DEPENDS ${_MSI_DEPS}
            COMMENT "Building MSI with WiX 6"
        )
        message(STATUS "wix found: ${WIX_EXECUTABLE} — 'msi' target enabled")
    else()
        message(STATUS "wix.exe not found — 'msi' target unavailable (install WiX 6 from wixtoolset.org)")
    endif()
endif()

# ---------------------------------------------------------------------------
# macOS .pkg installer — macOS only, requires pkgbuild (Xcode Command Line Tools)
# Run: cmake --build build-mac --target pkg
# Output: packages/camclops-<version>-macOS.pkg
# ---------------------------------------------------------------------------
if(APPLE)
    find_program(PKGBUILD_EXECUTABLE pkgbuild)
    if(PKGBUILD_EXECUTABLE)
        set(_PKG_STAGING "${CMAKE_BINARY_DIR}/pkg-staging")
        set(_PKG_OUTPUT  "${CMAKE_SOURCE_DIR}/packages/camclops-${CAMCLOPS_VERSION}-macOS.pkg")

        set(_PKG_DEPS camclops camclopslib clops_gpsinfo clops_gpsexport clops_ls clops_audit clops_findgpslock clops_probe clops_videos)
        if(Qt6_FOUND)
            list(APPEND _PKG_DEPS camclops-gui)
        endif()

        set(_PKG_SCRIPTS "${CMAKE_SOURCE_DIR}/packaging/macos-scripts")

        add_custom_target(pkg
            # Clear and re-populate staging root.
            # cmake --install stages everything under /usr/local (safe for
            # unsigned pkgs). The postinstall script (packaging/macos-scripts/)
            # moves camclops-gui.app from /usr/local to /Applications at install
            # time using the elevated privileges the macOS installer already holds.
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_PKG_STAGING}"
            COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
                    --prefix "${_PKG_STAGING}/payload"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_SOURCE_DIR}/packages"
            COMMAND "${PKGBUILD_EXECUTABLE}"
                    --root            "${_PKG_STAGING}/payload"
                    --scripts         "${_PKG_SCRIPTS}"
                    --identifier      "com.nutballlabs.camclops"
                    --version         "${CAMCLOPS_VERSION}"
                    --install-location "/usr/local"
                    "${_PKG_OUTPUT}"
            DEPENDS ${_PKG_DEPS}
            COMMENT "Building macOS .pkg installer -> packages/camclops-${CAMCLOPS_VERSION}-macOS.pkg"
        )
        message(STATUS "pkgbuild found: ${PKGBUILD_EXECUTABLE} — 'pkg' target enabled")
    else()
        message(STATUS "pkgbuild not found — 'pkg' target unavailable (install Xcode Command Line Tools)")
    endif()
endif()

# ---------------------------------------------------------------------------
# CPack — binary package generation
# Linux:   cmake --build build-linux --target package  -> RPM, DEB, TGZ
# macOS:   cmake --build build-mac   --target package  -> TGZ, ZIP
#          cmake --build build-mac   --target pkg      -> .pkg installer
# Windows: cmake --build build-win   --target package  -> ZIP
# Output lands in packages/ at the project root.
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "camclops")
set(CPACK_PACKAGE_VERSION "${CAMCLOPS_VERSION}")
set(CPACK_PACKAGE_VENDOR "Nutball Labs")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_CONTACT "Stephen Berg <camclops@iceberg.email>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Nutball-Labs/CamClops")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Direct all package output to packages/ at the project root.
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/packages")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_FILE_NAME "camclops-${CAMCLOPS_VERSION}-win64")
elseif(APPLE)
    # macOS: TGZ + ZIP (generic binary tarballs for manual install)
    set(CPACK_GENERATOR "TGZ;ZIP")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
    set(CPACK_PACKAGE_FILE_NAME "camclops-${CAMCLOPS_VERSION}-macOS")
else()
    # Linux: RPM (Alma/RHEL), DEB (Ubuntu/Debian), TGZ (generic binary tarball)
    set(CPACK_GENERATOR "RPM;DEB;TGZ")

    # /usr is correct for RPM/DEB — the package manager owns this namespace.
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    # Use ecosystem-standard arch-suffixed filenames:
    #   camclops-1.0.1-1.x86_64.rpm  /  camclops_1.0.1_amd64.deb
    set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

    # RPM specific
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
    set(CPACK_RPM_PACKAGE_URL "https://github.com/Nutball-Labs/CamClops")
    set(CPACK_RPM_PACKAGE_RELEASE "1")
    set(CPACK_RPM_PACKAGE_DESCRIPTION
"CamClops scans multi-camera dashcam storage, groups video segments into trips,
caches results as JSON manifests, and builds per-camera MP4 files or
synchronized 4K collages. The clops_probe --wizard command profiles any dashcam
layout automatically. Requires ffmpeg (RPM Fusion) and ExifTool for full
functionality.")

    # DEB specific
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Stephen Berg <camclops@iceberg.email>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Nutball-Labs/CamClops")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libstdc++6")

    # Post-install / post-remove scriptlets:
    #   1. ldconfig          — refresh the shared-library linker cache
    #   2. gtk-update-icon-cache — register the hicolor icons so launchers show them
    #   3. update-desktop-database — index the .desktop files for MIME/app lookups
    # All icon/desktop commands are silenced and non-fatal so the package install
    # does not fail on minimal systems without a desktop environment.
    set(_CLOPS_POSTINST_BODY
        "ldconfig\n"
        "gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true\n"
        "update-desktop-database /usr/share/applications 2>/dev/null || true\n")
    file(WRITE "${CMAKE_BINARY_DIR}/postinst" "#!/bin/sh\nset -e\n${_CLOPS_POSTINST_BODY}")
    file(WRITE "${CMAKE_BINARY_DIR}/postrm"   "#!/bin/sh\nset -e\n${_CLOPS_POSTINST_BODY}")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_BINARY_DIR}/postinst"
        "${CMAKE_BINARY_DIR}/postrm")
    file(WRITE "${CMAKE_BINARY_DIR}/clops_postinst.sh"
        "#!/bin/sh\n/sbin/ldconfig\n${_CLOPS_POSTINST_BODY}")
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE   "${CMAKE_BINARY_DIR}/clops_postinst.sh")
    set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_BINARY_DIR}/clops_postinst.sh")
endif()

include(CPack)

# SN: 00119
