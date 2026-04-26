# CHANGELOG

## [v1.9.0 / SN: 00104] - 2026-04-26

### Added
- **HUD overlay renderer** (`scripts/pm_hud.py`): military-style heads-up display
  as a full-frame transparent WebM (VP9/alpha) for compositing over collage footage.
  Three elements: left speed tape (KPH), right speed tape (MPH), circular compass
  rose at bottom-center. Transparent design — elements float over camera footage with
  no backing strip. Compass rose has rotating ring with cardinal/intercardinal labels
  and a fixed 12-o'clock pointer. Speed tapes have scale line, floating tick marks,
  and a full-width readout box at current speed. Strip-based rendering for performance
  (element bounding boxes only — eliminates 32 MB/frame full-frame allocation).
  Args: `--tape-width`, `--visible-range`, `--font-scale`, `--line-scale`,
  `--color-hex`, `--heading-height` (compass radius), `--heading-x/y` (compass center).
- **HUD tab in TripPropertiesDialog**: Style group (font scale, line scale, color hex
  seeded from Settings defaults), Speed Tapes group (tape width, visible range,
  per-tape position), Compass Rose group (radius, center position). HUD files tracked
  in manifest as `hudVideos`.
- **Settings → HUD tab**: global defaults for font scale, line scale, color hex that
  pre-populate the Properties HUD tab on open.
- **`hudVideos` manifest field** (`lib/trip_detection.hpp`, `lib/config_manager.cpp`):
  HUD WebM paths stored and loaded alongside `mapVideos` and `dashVideos`.
- **AppSettings HUD fields**: `hudFontScale`, `hudLineScale`, `hudColor` in
  `lib/config_manager.hpp/.cpp`.
- **Full-screen HUD overlay in TripBuildDialog** (`gui/TripBuildDialog.cpp/.h`,
  `cli/video_build.hpp/.cpp`): dedicated "HUD overlay (full-screen)" row below the
  4-quadrant grid, independent of the center map/dashboard overlay cell. Enable
  checkbox opts in; if enabled with no HUD video available a dialog warns the user
  and offers to return to Trip Properties to build one first. The HUD WebM is
  composited at full 3840×2160 over the complete collage output using VP9 alpha
  blending — elements float transparently over all four camera quadrants.
  `VideoOptions::hudOverlayPath` / `hudOverlayLoop` fields added.

### Changed
- **TripPropertiesDialog two-row tab bar**: top row holds General + camera tabs;
  bottom row holds GPS / Map / Dashboard / HUD. Bottom-row tabs show a green ✓ or
  red ✗ status icon that updates live when GPS is extracted or a video is generated.
- **MapProgressDialog** (`gui/MapProgressDialog.cpp/.h`): added Cancel button with
  thread-safe cancellation (`MapWorker::cancel()` via QMutex + QAtomicInt + proc.kill()).
  `closeEvent()` and `reject()` now trigger cancel-and-wait instead of destroying a
  live thread. Removed hardcoded `--render-fps 10` override.
- **Python font paths** (`scripts/pm_hud.py`, `scripts/pm_dashboard.py`): added
  correct Alma Linux 9 font paths as first candidates; restores proper TrueType
  rendering (previous renders on penny used PIL 10px bitmap font).
- **HUD compass proportions** (`scripts/pm_hud.py`): intercardinal labels (NE/SE/SW/NW)
  reduced to ~1/3 the size of cardinal labels (N/S/E/W) using a new "micro" font tier;
  KPH/MPH unit labels halved in size; compass sunk so the bottom 1/5 of the ring is
  off-frame for a more cinematic look.
- **TripBuildDialog overlay/HUD separation**: center overlay cell (960×540 centered,
  for map and dashboard videos) and HUD overlay (full-screen transparent WebM) are now
  entirely separate controls. HUD was previously only available as a center overlay
  item, which scaled it to an inset box and ignored the alpha channel.
- **Checkbox visibility** in overlay cell and HUD row: text color #e8e8ff, bold, 9pt;
  indicator border styled for dark-theme contrast; HUD row has a subtle panel
  background to read as a distinct control section.

### Fixed
- **VP9 alpha not decoded in collage build**: added `-c:v libvpx-vp9` to the HUD
  input in the ffmpeg command (`cli/video_build.cpp`). Hardware VP9 decoders (QSV,
  NVENC, VAAPI) silently drop the VP9 alpha track, causing the HUD to composite as
  an opaque black sheet covering all camera footage. The software libvpx-vp9 decoder
  correctly reads the separate alpha bitstream from the WebM container.
- **GPS sync between map/HUD overlay and video** (`lib/gps_export.cpp`):
  `gpsLockSeconds` is now computed and stored automatically during GPS extraction.
  Previously it was only populated by `pm_gpsinfo --scan-all-trips`; trips extracted
  via the GUI had `gpsLockSeconds = -1` (treated as 0 by both scripts), causing the
  moving map and HUD compass to run ahead of the video by the GPS cold-start duration.
  The calculation uses the same epoch arithmetic as `pm_gpsinfo` so values are
  identical between the two paths.
- **Preview frame HUD rendering**: forced `-c:v libvpx-vp9` in the frame-grab
  ffmpeg command and switched to `-vf format=rgba` (filter-chain conversion, preserves
  alpha) from `-pix_fmt rgba` (output-side flag, strips alpha). Composite pixmap
  upgraded to `QImage::Format_ARGB32_Premultiplied` so QPainter alpha-blends the HUD
  frame correctly over the camera tile composite.
- Inactive tab bar selected-tab text invisible in Adwaita light theme.
- `QDoubleSpinBox` missing forward-declaration in `TripPropertiesDialog.h`.

## [v1.7.1 / SN: 00102] - 2026-04-23

### Changed
- **Cross-platform manifest unification** (`lib/config_manager.cpp`): footage
  directories now share a single manifest file across all operating systems.
  Previously each OS (Linux, Windows, macOS) created its own `pm_manifest_*.json`
  file because each saw a different absolute path to the same footage. The new design
  stores all segment paths as relative paths within the manifest and adds a `path_map`
  section keyed by `os_hostname` (e.g. `linux_penny`, `windows_nutball1`). When a
  machine opens a directory containing an existing manifest from another OS, it adopts
  that manifest and registers its own path entry rather than creating a duplicate.
  Cross-platform absolute paths are automatically translated via `path_map` prefix
  substitution on load.
- **Manifest adoption in `ensureManifestId()`**: before minting a new manifest ID,
  the directory is scanned for existing `pm_manifest_*.json` files. The best candidate
  (prioritising one that already contains our `os_hostname` key, then any new-format
  manifest, then trip count) is adopted and registered in the local index.
- **Schema version 3**: manifests written by v1.7.1 carry `"schema_version": 3`.
  Rescan any existing footage directories to regenerate manifests in the new format.

## [v1.7.0 / SN: 00101] - 2026-04-19

### Added
- **Built-in camera profiles** (`lib/camera_profile.cpp/.hpp`): D90 (Pruveeo),
  Cobra CCDC4500, Cobra GPS (CAM1+CAM2), and Prilotte (AVI/MJPEG) profiles ship
  with the binary. No wizard run required for these cameras.
- **Profile auto-detection at scan time** (`lib/profile_detector.*`): camera layout
  is fingerprinted on first scan; `profile_id` stored in the manifest so subsequent
  loads use the correct profile without re-detection.
- **mtime timestamp source** for cameras with no embedded metadata (Prilotte).
- **`loadAllProfiles()`**: loads built-in profiles plus any user-created profiles
  from `~/.config/pathmux/profiles/`; merged list presented in Settings and wizard.
- **GPS accelerometer fields optional**: profiles without accelerometer data no longer
  require the field; extraction and display handle absence gracefully.

## [v1.5.1b / SN: 00101] - 2026-04-19

### Added
- **Logo morph animation** (`scripts/gen_logo_morph.py`, `gui/resources/logo_morph.mp4`,
  `gui/LogoMorphWidget.cpp/.h`): 6-second PathMux ↔ Nutball-Labs cross-fade, sine ease-in-out,
  960×540 H.264 CRF23, 30fps. 266 KB embedded in QRC. Generated at build time via CMake
  custom command (Python3 + Pillow + ffmpeg); pre-built file used if those tools are absent.
  `LogoMorphWidget` shows the animation live in the dialog (QElapsedTimer + 30fps QTimer, only
  active when visible). In the actual built video the morph loops indefinitely in blank quadrants
  via `-stream_loop -1` (`ExternalSlot::loop = true`).
- **Logo morph as video watermark**: enabling the overlay and selecting "None" from the overlay
  combo routes `logo_morph.mp4` to the center overlay position with `-stream_loop -1`
  (`VideoOptions::mapOverlayLoop`). Gives a branded 960×540 center watermark on every 4K collage
  at zero additional encode cost.
- **Free-form quadrant camera assignment** (`gui/TripBuildDialog.cpp`, `cli/video_build.hpp/.cpp`):
  every quadrant combo now lists all cameras present in the trip (native position marked ★),
  plus map/dashboard files from the manifest, external browse, and None. Any camera can fill any
  position — front in all four quadrants is valid. Backend: `VideoOptions::cameraRemap`
  (`std::map<string,string>`) maps collage position → source camera; `effectiveSegs()` lambda in
  `buildCollage4K` redirects the concat list accordingly. Complements (does not replace) the
  external-slot mechanism.
- **Overlay source combo** (`gui/TripBuildDialog.cpp`): overlay cell now has the same source combo
  as the quadrants (all cameras, map/dash files, external browse, None). Replaces the plain
  file-path text field.
- **Preview Frame implemented** (`gui/TripBuildDialog.cpp`): `onPreviewFrame()` grabs one frame
  per active quadrant via `ffmpeg -ss 2 -i … -vframes 1`, composites them into a 960×540
  QPainter image (xstack layout), composites the overlay at centre if enabled, and shows the
  result in a small dialog. Blank/disabled quadrants show a dark placeholder with position label.
  Temp PNGs cleaned up on dialog close.
- **Logo transparency**: all PathMux icon PNGs (`gui/resources/pathmux_{16..512}.png`,
  `pathmux.iconset/icon_*.png`) and `Nutball-Labs_logo.png` converted to transparent background
  via ImageMagick corner flood-fill (interior white — Kali's fur/paws — preserved). Logos now
  composite cleanly on any background colour.

### Changed
- **Collage Layout grid**: changed from 3×3 to 2-column / 3-row layout. Row 0: TL/TR quadrants.
  Row 1: overlay cell centred, spanning both columns. Row 2: BL/BR quadrants. Eliminates empty
  corner cells and makes the spatial relationship to the actual output clearer.
- **None slot behaviour**: selecting "None" or unchecking a quadrant now routes `logo_morph.mp4`
  (looping) through the slot instead of a `lavfi color=black` source. Falls back to black if the
  resource is unavailable.
- **Overlay combo dark theme**: added `QComboBox` and `QComboBox QAbstractItemView` rules to the
  overlay cell stylesheet; popup list was showing with white background / invisible text on hover.
- **ExternalSlot** (`cli/video_build.hpp`): added `bool loop = false` field.
  `addInput()` in `buildCollage4K` prepends `-stream_loop -1` when set.

## [v1.5.1a / SN: 00101] - 2026-04-19

### Added
- **Collage build dialog redesigned** (`gui/TripBuildDialog.cpp/.h`): replaced stacked group
  boxes with a tabbed layout. New **Collage Layout** tab shows a dark-themed 2×2 quadrant grid
  matching the xstack layout (TL=Front, TR=Rear, BL=Right, BR=Left); each quadrant has a source
  combo populated with the native camera, any map/dashboard files already in the manifest, and
  an "External video…" browse option. Separate **Camera Files** and **Output** tabs reduce
  clutter. Map overlay file picker now accepts any video (map or dashboard).
- **Instrument dashboard script** (`scripts/pm_dashboard.py`): new Python script renders an
  animated 960×540 instrument panel (compass/heading, speedometer, trip odometer, weather
  conditions) from the GPS track in a PathMux manifest. Pipes raw RGB frames to ffmpeg.
  Weather via Open-Meteo archive API (free, no key). Both °C/°F, km/h and mph, km and mi
  displayed simultaneously.
- **Dashboard tab in Trip Properties** (`gui/TripPropertiesDialog.cpp/.h`): Generate Dashboard
  button launches `pm_dashboard.py` via the generalised `MapProgressDialog`. Output path
  written to manifest `dashVideos` array; missing-GPS guard shows informative message.
- **Map/dashboard video manifest tracking** (`lib/trip_detection.hpp`,
  `lib/config_manager.cpp`): `Trip` struct gains `mapVideos` and `dashVideos` string vectors.
  Absolute paths stored in manifest JSON on successful render; supports multiple outputs per
  trip. Properties dialog lists manifest-tracked files with size; double-click opens in system
  viewer. Missing files shown in gray.
- **Non-blocking build progress dialog** (`gui/BuildProgressDialog.cpp/.h`): removed
  `setModal(true)`; X button now cancels the build via `std::atomic<bool>` cancel flag and
  `QProcess::kill()`.
- **Overwrite confirmation** (`gui/TripPropertiesDialog.cpp`): Generate Map and Generate
  Dashboard prompt before overwriting an existing output file.

### Changed
- `MapProgressDialog` generalised (`gui/MapProgressDialog.cpp/.h`): optional `scriptName`,
  `title`, and `extraArgs` constructor parameters allow reuse for dashboard generation
  without duplicating the class. Backward compatible.
- Dashboard temperature always shows both °C and °F; speed shows km/h primary with mph
  secondary; odometer shows both km and mi.

## [Unreleased / SN: 00095] - 2026-04-10

### Fixed
- **CLI parallel video concats running sequentially** (`cli/video_build.cpp`): `parallelConcats`
  flag was never set to `true` for the CLI path; root `run()` build block also lacked the
  parallel launch structure. Both paths now launch per-camera ffmpeg jobs concurrently.
- **CLI parallel concat progress bars overwriting a single line** (`cli/video_build.cpp`):
  ANSI cursor-up/down sequences (`\033[NA` / `\033[NB`) with a `std::mutex` per-row guard;
  each of the 4 concurrent ffmpeg processes updates its own terminal line independently.
- **CLI parallel concat `system()` thread-safety hang** (`cli/video_build.cpp`): concurrent
  `system()` calls fight over `waitpid(-1,...)`, stealing each other's child exit statuses and
  hanging indefinitely. Replaced with `fork()`+`exec()`+`waitpid(specific_pid)` on Linux/macOS
  and `CreateProcess`+`WaitForSingleObject` on Windows.
- **GUI parallel concat terminal pollution** (`cli/video_build.cpp`): initial per-camera
  progress bar print block ran unconditionally; guarded with `!ffmpegRunner` so GUI builds
  do not write ANSI sequences to the shell that launched `pathmux-gui`.
- **Build options `[N]` note line showed no current value** (`cli/video_build.cpp`): the
  `[N]` menu line now displays the current note (truncated to 40 chars) like every other
  toggle — `[N]  Note    Home from firestone`.
- **Build options note not saved to disk until menu exit** (`cli/video_build.cpp/.hpp`,
  `cli/find_trips.cpp`): `configureOptions()` now takes an optional `sourcePath` parameter;
  when `[N]` saves a note it immediately calls `loadTripCache` + updates + `saveTripCache`
  without waiting for the user to press GO/GODONE/Q.
- **Note input not trimmed or size-limited** (`cli/video_build.cpp`): leading/trailing
  whitespace is stripped (mirrors Qt `.trimmed()`); stored value capped at 200 chars.
- **`TripPropertiesDialog` was read-only** (`gui/TripPropertiesDialog.cpp/.h`): replaced the
  read-only note display with an editable `QLineEdit`; saves to manifest via `saveTripCache`
  when the user clicks OK (only if note actually changed).
- **Trip note not visible on tile after setting it** (`gui/TripTile.cpp/.h`): added
  `m_noteLabel` (italic, grey `#555`, hidden when note is empty); refreshes live after
  Properties dialog accepts without requiring a manifest reload.
- **Tile zoom only scaled text — thumbnails and layout stayed at 1×** (`gui/TripTile.cpp/.h`,
  `gui/TripGridPanel.cpp`): renamed `setTextZoom` → `setZoom`; now proportionally scales tile
  size, all child label geometries and fonts, thumbnail placeholders, and the vertical divider.
- **Duplicate manifest index entries** (`lib/config_manager.cpp`): two-part fix:
  - `saveTripCache` and `saveManifestNote` now reject any `path` containing `pm_manifest_`
    (a manifest file path, not a source directory) with a `std::cerr` warning and early return.
  - `loadManifestIndex` auto-removes and rewrites bogus entries where `path` contains
    `pm_manifest_`; also deletes the orphaned fallback manifest file in `~/.config/pathmux/`.
    Existing stale entries (e.g. from the previous session's bug) are silently cleaned up on
    next startup.

### Added
- **Trip ID badge on tile** (`gui/TripTile.cpp/.h`): `[XX]` displayed top-right in subdued
  8pt font; scales with zoom.
- **Manifest ID badge in manifest panel** (`gui/ManifestPanel.cpp`): `[XX]` right-aligned on
  line 1 of each list item via `ManifestItemDelegate`; path elide shortened to avoid overlap.
- **HiDPI / 4K display scaling** (`lib/config_manager.hpp/.cpp`, `gui/main.cpp`,
  `gui/SettingsDialog.cpp/.h`):
  - `AppSettings::uiScale` (default 1.0) stored in `pathmux.json`.
  - `gui/main.cpp` pre-reads `uiScale` before `QApplication` is constructed (Qt bakes DPI
    metrics at startup); sets `QT_SCALE_FACTOR` accordingly.
  - `--scale N` / `--scale=N` command-line flag (highest priority; overrides config).
  - "UI scale factor" combo box (100%–300%) in Settings → General tab with restart note.
  - `QT_SCALE_FACTOR` env var still honoured if already set (not overridden).

---

## [1.1.0a / SN: 00093] - 2026-04-04
### Fixed
- **BuildProgressDialog: verbose mode + failure visibility** (`gui/BuildProgressDialog.cpp/.h`):
  - Show resolved output directory in dialog header so users can find generated files.
  - Capture ffmpeg stderr; display first error line in status footer when a stage fails.
  - Show the exact ffmpeg command in the footer as each stage starts (pct==-2 verbose sentinel).
  - Footer now shows "⚠ Completed with errors" when any stage has a red ✗, not "✓ Build complete".
  - Switch `-loglevel quiet` → `-loglevel error` so ffmpeg errors are captured and shown.
  - Explicit `Qt::QueuedConnection` on progress signal to guarantee signal ordering when stages
    run on parallel `std::thread`s inside `buildTrip`.
  - `StageRow::failed` flag prevents `onFinished(true)` from overwriting red ✗ rows with ✓.
  - Final progress emit now always fires (removed `totalUs > 0` guard) so fast-failing stages
    (bad path, permission denied, missing input) report failure instead of silently succeeding.
- **Windows GUI: cmd.exe for ffmpeg** (`gui/BuildProgressDialog.cpp`): `sh -c` → `cmd.exe /c`
  on Windows; `sh` doesn't exist in a standard Windows install, causing silent build failure.

### Notes
- Windows packages only in this patch (`pathmux-1.1.0a-win64.zip`, `pathmux-1.1.0a-win64.msi`).
- Root cause of "all stages succeed in 2–3 seconds" on Windows was twofold: (1) `sh` not found
  so process never started; (2) even after cmd.exe fix, fast ffmpeg failures with no progress
  output fell through a `totalUs > 0` guard without emitting an error signal.

---

## [Unreleased — Qt6 GUI initial implementation] - 2026-03-29
### Added
- **Qt6 GUI (`gui/`):** Initial `pathmux-gui` binary — Qt Widgets two-pane dashcam explorer.
  - `MainWindow`: `QSplitter` host; wires all cross-panel signals; distributes Ctrl+scroll zoom.
  - `ManifestPanel`: left pane — manifest list with `ManifestItemDelegate` (two-line: nickname bold + trip count subdued), sort pulldown (Most Recent / Most Trips / Name A→Z), "+" scan button.
  - `TripGridPanel`: right pane — three-page `QStackedWidget` (no manifests / none selected / tile grid); manual-geometry tile layout recalculates column count on resize; deferred per-event-loop thumbnail loading via `QTimer::singleShot(0)` chain.
  - `TripTile`: fixed 360×200 tile — front/rear `QLabel` thumbnail placeholders (160×90 each, stacked), start date+time (bold 10pt), duration, detail row (segs · crow's distance · GPS ✓/~/—).
  - `EmptyManifestWidget`: first-run/no-manifest state — theme camera icon with "?" composited, hover tint, click → scan.
  - `ScanProgressDialog`: background `QThread` scan worker; indeterminate progress bar; emits `scanComplete(ManifestEntry)` on success.
- **`ManifestEntry::nickname`** (`lib/config_manager.hpp/.cpp`): optional user label stored in `manifests.json`; defaults to source path for display.
- **Ctrl+scroll text zoom** (`gui/TripGridPanel`, `gui/ManifestPanel`, `gui/TripTile`): `setTextZoom(double)` on tiles scales label font sizes proportionally; manifest list font scales via `m_list->setFont()`; both panes stay in sync via `MainWindow::onZoomChanged`; thumbnails intentionally unaffected.
- **`libpathmux.so` shared library** (`CMakeLists.txt`): library target changed from `STATIC` to `SHARED`; `OUTPUT_NAME "pathmux"`, `SOVERSION 1`; RPATH set for both Apple (`@executable_path/../lib`) and Linux (`${CMAKE_INSTALL_LIBDIR}`); `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON`; ldconfig post-install scriptlets for RPM/DEB.

### Fixed
- **`CameraProfile::cameraSlots`** (`lib/camera_profile.hpp/.cpp`, `lib/trip_detection.cpp`): renamed from `slots` — Qt's `#define slots` macro (expands to empty via `Q_SLOTS`) caused template parse errors in the GUI build. JSON key `"slots"` unchanged.
- **Haversine endpoint guard** (`gui/TripTile.cpp`): crow's distance now only computed if both `startLat/Lon` AND `endLat/Lon` are non-zero; previously could compute distance to 0,0 (Gulf of Guinea) if GPS extraction succeeded for trip start but failed on last segment.
- **`build-mac` directory references** (`CMakeLists.txt`): corrected from `build-macos` to match macOS default build directory.

### Notes
- **Thumbnail availability**: tiles show grey placeholders for trips in manifests scanned before the CameraProfile abstraction layer (pre-2026-03-21). Rescan those source directories to populate `firstThumbs`. Not a GUI bug.
- **GUI cosmetic polish in progress** — a few visual glitches noted; continuing next session.

---

## [1.0.1a / SN: 00090] - 2026-03-28
### Fixed
- **pm_tmp fallback to config dir** (`cli/video_build.cpp`): `buildCollage4K()` and
  `buildCollage1080Direct()` now fall back to `getConfigDir()/pm_tmp` when the primary
  `outDir/pm_tmp` cannot be created (e.g. source footage on a read-only or network drive).
  Previously failed with "Access is denied" and aborted the collage. Affects all platforms.

### Added
- **WiX 6 MSI packaging** (`wix/pathmux.wxs.in`, `CMakeLists.txt`): Windows MSI installer
  targeting `C:\Program Files\PathMux` (x64). Adds all `pm_*` tools to system PATH via
  `Environment` component. `MajorUpgrade` element handles in-place upgrades; downgrades
  blocked. Single embedded cabinet; `perMachine` scope.
- **4-part MSI version** (`lib/version.hpp`, `CMakeLists.txt`): `VERSION_BUILD` macro
  provides the 4th numeric component required by Windows Installer (`1.0.1.1` for `1.0.1a`).
  Allows lettered patch releases to trigger upgrade detection. Display version and filenames
  retain the human-readable suffix (`1.0.1a`).
- **`run-build.ps1`**: PowerShell build script — configures, builds, packages ZIP (CPack)
  and MSI (WiX) in one shot. `-NoPack` flag skips packaging for fast build-only runs.
- **`-arch x64` flag** (`CMakeLists.txt`): passed to `wix build` to ensure install lands
  in `C:\Program Files` rather than `C:\Program Files (x86)`.
- **`.gitignore`**: MSI, wixpdb, and `build_out.txt` added to exclusions.

---

## [1.0.1 / SN: 00090] - 2026-03-28
### Added
- **Default CPU encoder** (`lib/config_manager.hpp`, `cli/video_build.cpp`): Out-of-box
  encoder preset is now `cpu` (`libx265 -crf 18 -preset fast`) — works on any machine
  without hardware configuration.  `applyEncodePreset()` now sets `extraDownArgs` for
  all preset branches (cpu/qsv/vaapi).
- **`buildCollageFromSlots` encoder wiring** (`cli/video_build.cpp`): Collage builder
  now uses `opts.encode` settings (hwDevice init, pixFmt, collageEncoder, quality flags,
  extraCollageArgs) instead of the hardcoded `libx265 -crf 18 -preset slow` that was
  left in from early development.
- **`[M] Extra downscale args`** menu entry added to `EncoderPrefsEditor` (`cli/prefs.cpp`).
- **CPack packaging** (`CMakeLists.txt`): RPM, DEB, and TGZ packages generated via
  `cmake --build build-linux --target package`.  Output lands in `packages/` at project
  root.  RPM uses ecosystem arch-suffix filenames; DEB uses DEB-DEFAULT naming.
- **`packages/` directory** tracked in git via `.gitkeep`; built packages excluded via
  `.gitignore`.
- **Dotfile filtering** (`tools/pm_probe.cpp`): All six `directory_iterator` loops now
  skip entries whose filename begins with `.` (covers `._` resource forks, `.DS_Store`,
  etc.) via new `isDotFile()` helper.

### Changed
- **Platform support** (`README.md`): Linux (x86_64), macOS, and Windows CLI all working
  (was "Linux only; macOS/Windows planned").
- **Man page `--encoderprefs`** (`man1/pathmux.1`): Rewritten with cpu-default guidance
  and full 13-field reference list.
- **Man page GPS cold-start warning** (`man1/pathmux.1`): Prominent warning block added
  covering 30–120+ second GPS acquisition delay and implications for mileage/documentation use.
- **Man page `pm_probe.1`** (`man1/pm_probe.1`): Flat-layout token description expanded;
  hidden file filtering noted; wizard camera assignment UI description updated.
- **RTX 5060 Brag Board entry corrected** (`man1/pathmux.1`, `README.md`): Hardware was
  listed as RTX 4060; corrected to RTX 5060.

---

## [1.0.0 / SN: 00089] - 2026-03-22 / 2026-03-25
### Changed
- **License: MIT → GNU General Public License v3 or later** — prevents closed-source
  rebranded redistribution. `LICENSE` file replaced with canonical FSF GPL v3 text.
  CMake packaging metadata updated (`GPL-3.0-or-later`). README license section updated.
- **SPDX headers + copyright notice** added to all 38 source files
  (`// SPDX-License-Identifier: GPL-3.0-or-later` + `// Copyright (C) 2026 Nutball Labs / Stephen Berg`).
- **License canary** embedded in binary via `PATHMUX_LICENSE_NOTICE[]` in `version.hpp`:
  copyright, license, URL, and Kali's editorial opinion. Survives strip;
  visible with `strings pathmux | grep -A 14 "GNU General"`.

### Added
- **Build phase timing** (`cli/video_build.cpp`): wall-clock seconds recorded for each
  build phase — `concat_seconds`, `collage_4k_seconds`, `collage_1080p_seconds` — written
  to `pm_buildlog.json` alongside existing provenance data.  Value is `null` for phases
  not run in a given build.
- **`BuildTimings` struct** (`cli/video_build.cpp`): carries per-phase elapsed seconds
  from `buildTrip()` / `run()` to `appendBuildLog()`.  Uses `std::chrono::steady_clock`
  — negligible overhead.

### Fixed
- **Buildlog fallback to config dir** (`cli/video_build.cpp`): `appendBuildLog()` now
  probes writability of the source path before committing to it.  If not writable, falls
  back to `getConfigDir()/pm_buildlog.json` and prints a notice.  Previously the log
  entry was silently dropped when the source filesystem was read-only.

### Documentation
- **`man1/pathmux.1` BRAG BOARD section** reworked: placeholder entries removed; real
  submission requirements documented (daytime, 20 min minimum, all four cameras, 4K
  collage); scoring formula defined (`footage_minutes / collage_4k_minutes`); first real
  entry added (Nutball Labs, i7/RTX 4060/NVMe, 42m trip, score 6.04x).  Submission
  workflow updated to reference `pm_buildlog.json`.

---

## [0.9.11a / SN: 00088] - 2026-03-20 / 2026-03-21
### Fixed
- **VideoToolbox `-q` rejection**: `h264_videotoolbox` and `hevc_videotoolbox` do not
  accept `-q` (quality scale). Replaced with `-b:v <quality>M` when encoder name
  contains `videotoolbox`. Applies to `buildCollage4K`, `buildCollage1080`,
  `buildCollage1080Direct`. Quality values now interpreted as Mbps for VideoToolbox.
- **1080p proceeds on 4K failure**: `buildCollageFromSlots` and `buildTrip` now check
  the 4K build return value before attempting the 1080p downscale. Prints
  "Skipping 1080p — 4K collage failed." on failure.
- **`CollageOptions` encode settings not propagated**: `CollageOptions` struct lacked
  an `encode` field; `vopts` for 1080p-from-4K was constructed with bare defaults,
  falling back to QSV regardless of host config. Added `EncodeSettings encode` to
  `CollageOptions` and populated it from config in `runCollageFromFiles()`.
- **Host settings stale in interactive session**: `--encoderprefs` run as a separate
  invocation updated the host file but the running `-I` session kept old in-memory
  settings. Added `ConfigManager::reloadHostSettings()` (public wrapper for
  `loadHostOverlay()`); called before `configureOptions()` in both interactive
  build paths. Encoder/path changes now take effect without restart.

### Platform
- **macOS confirmed working**: full build and collage pipeline verified on MacBook Air
  (i5-8210Y, Intel UHD 617, macOS, Homebrew ffmpeg). All three platforms now
  build and produce collages: Linux (QSV/NVENC), macOS (VideoToolbox), Windows (pending collage test).

### Added (2026-03-21)
- **CameraProfile abstraction layer** (`lib/camera_profile.hpp/.cpp`): `CameraSlot` and
  `CameraProfile` structs — slot list, filename regex, timestamp format, container ext,
  thumbnail method, GPS method, default layout. `d90Default()` factory, JSON load/save,
  `primarySlot()` / `slotByName()` / `isValid()` helpers.
- **Slot-based `TripSegment` / `Trip`**: named front/rear/left/right fields replaced with
  `cameras` and `thumbs` maps keyed by slot name; `Trip::firstThumbs`/`lastThumbs` maps
  replace 8 named thumbnail fields. `camPath()` / `camThumb()` inline helpers added.
- **`detectTrips()` profile parameter**: `const CameraProfile&` added (default
  `d90Default()`); all existing callers unchanged. Scanning now driven by profile slot
  list — supports flat and subdir camera layouts; filename token verified from regex group 2
  when present.
- **Manifest migration**: `config_manager` detects old flat-key format on read and
  reshapes to `cameras`/`thumbs`/`firstThumbs`/`lastThumbs` transparently.
- **`ConfigManager::getCameraProfile()`**: loads
  `~/.config/pathmux/profiles/<activeProfileId>.json`, validates, falls back to
  `d90Default()` if absent or invalid. `activeProfileId` stored in `pathmux.json`
  (default `"pruveeo_d90"`). All `detectTrips()` call sites updated — D90 hardcoding
  fully removed.

### Fixed (2026-03-21)
- **Progress bar freeze during moov atom write** (`cli/video_build.cpp`): when ffmpeg
  stops emitting progress events while finalizing (e.g. NFS seek-back for moov atom),
  the bar now detects a 2-second stall and switches to a spinning `writing |` indicator
  at the last-known percentage. Resumes normal bar if progress events resume.
  Added `drawFinalizingLine()` static helper; `lastProgressTime` tracked via
  `std::chrono::steady_clock`. `<chrono>` added to includes.
- **`promptString()` and `promptInt()` hang on bare Enter** (`cli/ui_helpers.hpp`):
  `std::getline(std::cin >> std::ws, input)` consumed the newline then blocked waiting
  for additional input. Replaced with plain `std::getline(std::cin, input)` + manual
  leading/trailing whitespace trim — consistent with `promptLine()`.

---

## [0.9.11a / SN: 00087] - 2026-03-19
### Added
- **ffmpeg build progress tracking** (`cli/video_build.cpp/.hpp`): live `\r`-overwritten
  progress bar with ETA displayed during per-camera concat and collage encode steps.
  Uses ffmpeg's `-progress <named_pipe>` for machine-readable `out_time_us=`/`speed=`
  updates. Format: `  concat:Front      [=======>     ] 34%  ETA: 0:08`.
  Stage labels: `concat:<camera>`, `collage:4K`, `collage:1080p`.
- **`VideoBuilder::progressCallback`** — `std::function<void(label, pct, etaSecs)>`
  member. Null = draw to terminal. Qt layer sets this to route updates to per-stage
  progress bar widgets (Phase 2 hook). Stage labels are now established as the
  binding key for Qt progress bars.
- **`runFfmpegWithProgress()`** falls back to `runFfmpeg()` for unknown duration,
  debug mode, Windows, or pipe creation failure — no regression on any existing path.

---

## [0.9.11 / SN: 00087] - 2026-03-15
### Added
- **Host-specific config overlay** (`--hostprefs`): per-hostname settings file
  `~/.config/pathmux/pathmux_<hostname>.json` overlays the shared `pathmux.json`.
  Encoder settings, ffmpeg/exiftool paths, export dir, tmp dir, and log level are
  host-specific; all other prefs remain shared. Transparent load order: base first,
  host overlay wins on any present key.
- **`HostPrefsEditor`** (`cli/host_prefs.cpp/.hpp`): interactive menu for host
  settings; encoder sub-menu via `EncoderPrefsEditor`; saves only to host file.
- **`getShortHostname()`** in `lib/compat.hpp`: cross-platform hostname (Windows
  `GetComputerNameA`, POSIX `gethostname` + strip domain). No Winsock init required.
- **`--format=[json|csv|xml]` modifier for `-T`** (`pathmux`): structured trip output
  with optional `--fields=<f1,f2,...>` column selection. Delegates to new
  `lib/trip_format.hpp` shared library.
- **`--format=[json|csv|xml]` and `--fields`** in `pm_ls`: same structured output
  with optional MID / MID:TID scoping. `--format=json` is now an alias for `--json`.
- **`lib/trip_format.hpp`** (new, header-only): shared structured output helpers
  (`tripFieldVal`, `csvQuote`, `writeTripsCSV`, `writeTripsXML`, `defaultTripFields`).
  Used by both `pathmux` (find_trips.cpp) and `pm_ls`.
- **ccache + PCH** in `CMakeLists.txt`: ccache auto-detected and wired as
  `CMAKE_CXX_COMPILER_LAUNCHER`; `json.hpp` compiled as PCH for `pathmuxlib`.
  Dramatically reduces build time on GCC 14 (Alma 10).

### Fixed
- **NVENC collage 1080p downscale**: replaced `scale_cuda=1920:1080` (not universally
  compiled into ffmpeg) with CPU `scale=1920:1080` + hwupload — portable fix.
- **`EncoderPrefsEditor`** now saves to host file (`saveHostSettings()`) instead of
  base prefs; encoder settings are machine-specific.

### Tested
- Linux/NVENC collage (RTX5060, Alma 10): 4K collage confirmed clean — direct Roku
  Ultra play, no transcode; visual quality confirmed via frame grab (Gulfport MS).
  constqp QP 24 chosen as default: ~44 Mbps, visually identical to QP 20/22 on 85" 4K.

---

## [0.9.10g / SN: 00082] - 2026-03-07
### Added
- **`pm_probe --wizard`**: interactive camera profile builder (full implementation).
  Probes every camera directory (not just primary) for per-camera video and audio
  stream info; D90 rear camera confirmed to have no audio track.
  - Settings review table with numbered items; CONFIRM to accept, [1-7] to edit
  - `<path>` legend row in table header — shows the root passed on the command line;
    all camera mappings displayed as `<path>/DirName/` so stored values are
    root-relative, not locked to an absolute path
  - Camera remapping UX: user types the subdirectory name after a `<path>/` prompt;
    wizard validates existence and video content; sets `[!] needs attention` flag on
    failure; clears flag on success
  - Profile saved to `~/.config/pathmux/profiles/<sanitized_name>.json`

### Fixed
- **Pipe alignment on Profile name placeholder**: em dash (`—`) in `(not set — required)`
  caused right border to shift 2 columns left due to UTF-8 byte/display-width mismatch
  in `wizRow()`. Replaced with `--`.

### Documented (ROADMAP)
- Hardware-agnostic design principle: app ships with no active profile; first-run
  warning (CLI and GUI) when no profile is configured. `--prefs` profile selection
  held until CameraProfile C++ layer is implemented.
- CameraProfile extraction from `trip_detection.cpp` flagged as next critical step.

---

## [housekeeping] - 2026-03-05
### Documentation & Infrastructure
- **`man1/pathmux.1`**: Expanded `-G` section with full interactive flow reference
  (trip picker commands, action menu, output directory logic). Added `BRAG BOARD`
  section — community encode timing leaderboard seeded with placeholder entries;
  points users to `buildHistory` JSON block for submission.
- **`cmake/sn_audit.cmake`**: Updated to glob `*.md` files and match HTML comment
  SN format (`<!-- SN: 00097-->`). All three SN formats now covered.
- **SN stamps**: `<!-- SN: 00097 -->` added to all `.md` files in the project
  (CHANGELOG, CLAUDE, ROADMAP, README, Session_Log, PROPOSED_UTILS,
  pathmux_project_brief, ROADMAP_MacOS, ROADMAP_WINDOWS). HTML comment —
  invisible when rendered, greppable in raw file.
- **`.gitignore`**: Added `sn_audit.txt` — working audit file, not for commit.
- **CLAUDE.md**: Remote URL updated to `Nutball-Labs/PathMux`; SN convention
  updated to document all three formats; GPX/KML default output path bug cleared
  (verified fixed in v0.9.6a).
- **ROADMAP.md**: Fixed duplicate `What's done:` header in GPS section; updated
  ExifTool status note to reflect current no-version-check policy.
- **Nutball-Labs GitHub org**: Both PathMux and SRoute repos moved to
  `github.com/Nutball-Labs`. Local remote updated accordingly.

---

## [housekeeping] - 2026-03-04
### Documentation
- **CHANGELOG.md backfilled** for v0.9.10 through v0.9.10d — entries were missing.
- **CLAUDE.md** updated: version/HWM to v0.9.10e/SN 00081; fixed stale
  `debug_main.cpp` → `pm_tripdebug.cpp` in tools table; added
  `CameraProfile/StorageFormat Abstraction` architecture section; added
  optional-camera (empty-dir vs absent-dir) note.
- **ROADMAP.md**: utility suite checkboxes corrected (pm_gpsexport, pm_ls,
  pm_audit, pm_probe, pm_tripdebug all marked done); added CameraProfile
  extraction TODO under Phase 1; added `Optional Camera Handling` and
  `User Support Model` sections to Multi-Brand Dashcam Support.

---

## [0.9.10f / SN: 00081] - 2026-03-06
### Added
- **`pm_findgpslock`** — new standalone GPS lock diagnostic scanner (`tools/pm_findgpslock.cpp`):
  - Accepts one or more `.ts` files; prints a header line per file then GPS samples
    until the first fully-locked reading (valid lat/lon AND synchronized clock).
  - Pre-lock samples labelled `NO_POS`, `NO_TIME`, or `NO_POS+NO_TIME`; normal output
    is two lines per file (header + sample 0 already locked).
  - `--verbose` flag passes ExifTool stderr to terminal for diagnostics.
  - Added to CMake build and install targets.
- **`pm_gpsexport --dump`** — prints all extracted GPS track points to stdout in a
  tabular format (index, timestamp, lat, lon, speed, heading, alt); useful for
  quick inspection without opening a GeoJSON file.
- **`gpsTrack` manifest fields**: `pre_position_lock_samples` and
  `pre_time_lock_samples` now stored per-trip after GPS extraction — count of
  skipped records before position and clock lock respectively.

### Fixed
- **`gps_export.cpp` ExifTool quiet flag**: extraction now passes `-q` when not in
  verbose mode, suppressing `[Minor] Tag not defined` ANSI warnings that were
  leaking to the terminal. Remaining stderr still redirected to `/dev/null`.
- **Cold-start clock skip**: GPS records with `year < 2000` are now skipped during
  extraction (covers `1900:01:00` all-zero register and `1970:01:01` epoch variants).
  Previously only zero lat/lon was checked.

### Changed
- **`pm_gpsexport` progress output**: progress dots moved from stdout to stderr;
  exported file paths remain on stdout. Allows clean pipeline use.

---

## [0.9.10e / SN: 00081] - 2026-03-04
### Changed
- **`trip_debug` renamed to `pm_tripdebug`**: source (`tools/pm_tripdebug.cpp`),
  CMake target, install target, and man page (`man1/pm_tripdebug.1`) all updated
  to the `pm_` naming convention. Completes the pm_* utility suite rename.
- **`pm_tripdebug` added to install targets**: now included in RPM/DEB packaging
  alongside the other pm_* tools.

---

## [0.9.10d / SN: 00080] - 2026-03-02
### Fixed
- **`pm_gpsinfo` timezone labels**: UTC vs local time now correctly labeled in
  timestamp output — no more ambiguity in GPS inspection output.
- **`VERSION_SUFFIX` in `version.hpp`**: Suffix component (e.g. `"d"`) is now set
  directly in `version.hpp` rather than inferred at runtime, so `pathmux -v` always
  reports the correct suffix. Must be updated on every suffix bump.

---

## [0.9.10c / SN: 00080] - 2026-03-02
### Added
- **`pm_probe`** — camera compatibility profiler (`tools/pm_probe.cpp`):
  - Single-file mode: `pm_probe <file.ts>` or `pm_probe MID:TID` — reports
    container, resolution, frame rate, pixel format, color space, duration,
    stream list, GPS method, and first GPS fix (timestamp, lat, lon).
  - Card mode: `pm_probe --card <path>` — fingerprints full dashcam SD card root;
    finds camera dirs, samples up to 5 segment durations, probes primary camera.
    Output formatted for pasting into a GitHub issue.
  - `--json` flag for all modes.
  - Man page: `man1/pm_probe.1` — "PathMux Suite - Camera Profiler".

### Changed
- **ExifTool version policy removed**: PathMux no longer validates or requires a
  minimum ExifTool version. If GPS extraction returns no or corrupted data, users
  should contact the ExifTool maintainer. `rpm/pathmux.spec` Requires changed from
  `exiftool >= 13.51` to `exiftool`.
- **Man page URL cleanup**: Removed inline `https://exiftool.org` URLs from man page
  prose (non-standard). Replaced with "contact the ExifTool maintainer directly"
  and `SEE ALSO exiftool(1)`.
- **GPS first-fix fields renamed** `sample_*` → `first_*` (`first_lat`,
  `first_lon`, `first_timestamp`, `has_fix`) — clearer naming; not a statistical
  sample.

---

## [0.9.10b / SN: 00080] - 2026-03-02
### Added
- **`pm_audit`** — footage integrity checker (`tools/pm_audit.cpp`):
  - Checks ValidationFile sample against disk (exists + MD5).
  - Status values: `OK` / `MISSING` / `MODIFIED` / `NO_VALIDATION` / `CORRUPT`.
  - `--deep`: ffprobe pass on every Front segment to detect truncated files.
  - `--json`: machine-readable output; exit 0 = all OK, exit 1 = problems found.
  - Man page: `man1/pm_audit.1` — "PathMux Suite - Footage Auditor".

---

## [0.9.10a / SN: 00080] - 2026-03-02
### Added
- **`pm_ls`** — non-interactive trip lister (`tools/pm_ls.cpp`):
  - Default: one line per trip (MID TID date start duration segs distance GPS).
  - `MID:TID` argument: multi-line trip detail block.
  - `--json`: machine-readable output for jq pipelines.
  - Respects `useImperial` pref for distance display.
- **Man pages** for `pm_gpsinfo`, `pm_gpsexport`, `pm_ls` (`man1/pm_gpsinfo.1`,
  `man1/pm_gpsexport.1`, `man1/pm_ls.1`); `pathmux.1` SEE ALSO updated for all
  new utilities.

---

## [0.9.10 / SN: 00080] - 2026-03-02
### Added
- **`pm_gpsexport`** — standalone non-interactive GPS track exporter
  (`tools/pm_gpsexport.cpp`): `MID:TID --gpx/--kml/--geojson PATH [--force]
  [--verbose]`; extracts GPS if needed, writes one or more formats, paths to stdout.
- **`lib/gps_export.hpp/.cpp`**: `extractGps()`, `writeGpx()`, `writeKml()`,
  `writeGeoJson()` extracted from `cli/gpx_export.cpp` into `namespace Pathmux`
  (libpathmuxlib). Enables tools and future GUI to share GPS write logic.

### Changed
- `cli/gpx_export.cpp`: GPS/track writer implementations removed; call sites
  updated to `Pathmux::` namespace.
- `pathmux.hpp`: `gps_export.hpp` added to umbrella header.

---

## [0.9.9 / SN: 00079] - 2026-03-01
### Added
- **Per-camera thumbnail fields** in `TripSegment`: `frontThumb`, `rearThumb`,
  `leftThumb`, `rightThumb` — absolute path to `.jpg` sidecar, or `""` if absent.
  Populated at scan time by replacing the `.ts` extension and checking `fs::exists()`.
- **Trip-level thumbnail convenience fields** in `Trip`: `firstFrontThumb`,
  `lastFrontThumb`, `firstRearThumb`, `lastRearThumb`, `firstLeftThumb`,
  `lastLeftThumb`, `firstRightThumb`, `lastRightThumb`.
  - `first*` fields use `segments[1]` to avoid cold-start frames (garage door,
    parking lot, etc.); falls back to `segments[0]` on single-segment trips.
  - `last*` fields always from `segments.back()`.
- All new fields serialized to manifest JSON (only written when non-empty;
  backward-compatible load defaults to `""`).

### Notes
- ffprobe integration for trip duration confirmed complete (was already implemented;
  ROADMAP checkbox updated to reflect actual state).
- This completes all Trip Detection & Caching items in the Phase 1 roadmap.

---

## [0.9.8 / SN: 00079] - 2026-03-01
### Added
- **`manifests_stale.json` archive**: Stale index entries (manifest file missing at
  startup validation) are now appended to `~/.config/pathmux/manifests_stale.json`
  before being pruned from the live index. Preserves `id`, `path`, `manifestFile`,
  `lastScan`, `tripCount`, `note`, and a `pruned` timestamp for troubleshooting.
- **`--show-stale`**: Displays contents of the stale archive in a formatted table
  (ID, pruned date, trip count, path).
- **`--clear-stale [--force]`**: Wipes the stale archive with confirmation prompt;
  `--force` skips the prompt for scripted use.

### Changed
- **Usage output**: New `Manifest management:` section groups `--clear-cache`,
  `--show-stale`, `--clear-stale`, and `--validate` together, separate from Settings.
  `--show-config` stays in Settings (it shows settings, not manifest state).
- **`--force` is now order-independent** after `--clear-cache`: previously required
  to immediately follow `ALL`; now scanned from remaining argv so
  `--clear-cache ALL --force` and any positional variant both work.
- **`validateManifestIndex()` stale message** updated: now says "archived" instead
  of "removed" and cites `--show-stale`.

### Fixed
- **`clearCache()` used `std::cin >>`**: Both the "Wipe ALL?" prompt and the
  interactive ID loop now use `std::getline(std::cin >> std::ws, ...)`, consistent
  with `validateManifestIndex()` and the project input-handling rule (no `cin >>`
  mixing with `getline`).

---

## [0.9.5a / HWM: 00072] - 2026-03-01
### Fixed
- **`selectTrip()` unused parameter**: Silenced `-Wunused-parameter` warning by
  commenting out the `mode` argument name (`ExportMode /*mode*/`) in both
  declaration (`gpx_export.hpp`) and definition (`gpx_export.cpp`). The parameter
  is retained in the signature for future use.
- **Duplicate `// SN:` in `pm_gpsinfo.cpp`**: Removed the stray `// SN:` line
  from the top-of-file header comment block. Canonical SN belongs at the bottom
  of the file only; the duplicate caused `sn-audit` to emit two entries for the
  same file.
- **`promptLine()` bare-Enter at "Output directory" prompt**: Verified clean —
  interactive GPS export flow uses `readCommand()`/`getline` throughout with no
  `cin >>` mixing; `promptLine()` correctly returns the configured default on
  bare Enter. No code change required.

---

## [housekeeping / out-of-session] - 2026-02-28
### Fixed
- **`cmake/archive_files.txt`**: Updated all source paths to reflect post-0.9.4
  directory layout (`lib/`, `cli/`, `tools/`). Added new files from the refactor
  (`format_helpers.hpp`, `platform.cpp/.hpp`, `pathmux.hpp`, `PROPOSED_UTILS.md`,
  `Session_Log.md`, `man1/pathmux.1`). Dropped `json.hpp` (vendored, not distributed).
- **`cmake/archive.cmake`**: Fixed hardcoded `${SRC}/version.hpp` path — moved to
  `${SRC}/lib/version.hpp` during refactor. Archive target was broken; now verified
  clean producing a correct 35-file tarball.

---

## [post-0.9.4 / HWM: 00071] - 2026-02-28
### Added
- **`Trip::gpsLockSeconds`**: New field in the `Trip` struct (`-1` = not yet
  scanned; `>=0` = seconds from segment start to first valid GPS fix). Serialized
  in manifest JSON; loaded back on next scan. Populated by `--scan-all-trips`.
- **`pm_gpsinfo MID:TID` direct addressing**: Any single-file GPS inspection can
  now be invoked with a manifest:trip ID pair instead of a file path.
  `pm_gpsinfo F0:4P --text` resolves the trip via `ConfigManager`, hands the
  first Front segment path to the existing single-file pipeline. ID normalization
  applied (O→0, I→1, L→1, case-insensitive). Error messages distinguish unknown
  manifest ID from unknown trip ID.
### Changed
- **`pm_gpsinfo --scan-all-trips`** now uses `ConfigManager` for all manifest I/O
  (`loadManifestIndex()`, `loadTripCache()`, `saveTripCache()`) instead of raw
  JSON parsing. Lock times are written back to the manifest after each scan;
  MD5 integrity tracking maintained automatically via the `saveTripCache` path.
  `json.hpp` no longer included directly by `pm_gpsinfo.cpp`.

---

## [0.9.4 / HWM: 00071] - 2026-02-28
### Changed
- **Library restructure**: Codebase reorganized into `libpathmux.a` (static library)
  plus a thin CLI front-end. Prepares for Phase 2 Qt6 GUI without code duplication.
  - `lib/` — core library sources: `trip_detection`, `config_manager`, `platform`,
    `format_helpers`, `logger`, `version`, `pathmux` umbrella header
  - `cli/` — CLI front-end only: `main`, `find_trips`, `gpx_export`, `prefs`,
    `kml_prefs`, `locations`, `video_build`, `ui_helpers`
  - `tools/` — standalone binaries: `pm_gpsinfo`, `debug_main`
### Added
- **`lib/platform.cpp/.hpp`**: OS abstraction layer (`namespace Pathmux::Platform`).
  Provides `getHomePath()`, `getConfigDir()`, and `getTerminalWidth()` replacing
  direct `getenv("HOME")` and `ioctl(TIOCGWINSZ)` calls. Windows/macOS stubs
  documented — only Linux path exercised.
- **`lib/format_helpers.hpp`**: Pure math/format functions extracted from
  `cli/ui_helpers.hpp` into `namespace Pathmux`: `haversineKm()`,
  `formatDistance()`, `formatSpeed()`, `formatAltitude()`, `utf8DisplayWidth()`,
  `truncate()`. No POSIX dependencies — safe to include in Qt6 GUI on all platforms.
- **`lib/pathmux.hpp`**: Umbrella public API header. Qt6 GUI (and any future consumer)
  includes only this one file to get the full library interface.
- **`namespace Pathmux`**: All library types and functions wrapped in `namespace Pathmux`
  — `Trip`, `TripSegment`, `GpsPoint`, `VideoProfile`, `TripDetection`, `ConfigManager`,
  `AppSettings`, etc. CLI files use `using namespace Pathmux;` to avoid churn.
### Maintenance
- **CMakeLists.txt** rewritten: `pathmuxlib` STATIC target with PUBLIC include
  propagation; `pathmux` CLI and tools link against it. No manual `-I` flags needed.
- **cmake/sn_audit.cmake** updated to scan `lib/`, `cli/`, `tools/` subdirectories.
- **SN**: High-water mark `00071`. All touched files updated.

---

## [0.9.3 / HWM: 00070] - 2026-02-28
### Added
- **`pm_gpsinfo --scan-all-trips`**: New batch scan mode. Reads all manifests
  from `~/.config/pathmux/manifests.json`, iterates every trip, runs ExifTool
  on the first Front segment of each trip, and reports how many seconds into
  the segment elapsed before GPS lock was achieved.
  - Output: fixed-width table — `MID`, `TID`, `SegDur`, `LockAt`, `First Segment`
  - `LockAt` shows `0s`–`Ns` (seconds), `none` (no lock in segment), or `!file`
    (segment not accessible — drive not mounted, etc.)
  - Progress lines written to stderr in `MID:TID  filename ...` format
  - `MID:TID` colon-separated addressing convention established for future
    batch manager feature — consistent from the start
  - `--scan-all-trips` is mutually exclusive with a file argument
### Fixed
- **`pm_gpsinfo` argument order**: Usage corrected to POSIX convention
  `[options] <file.ts>` (options before file).
### Changed
- **`pm_gpsinfo` now links `json.hpp`** for manifest parsing in batch mode.
  Single-file mode behaviour unchanged.
### Maintenance
- **SN**: High-water mark `00070`. Changed files: `pm_gpsinfo.cpp`, `version.hpp`.

---

## [0.8.12] - 2026-02-24
### Fixed
- **`-p` lastPath**: Now calls `setLastPath()` on cache-hit paths, not just after a fresh scan.
### Added
- **`--show-config`**: Dumps raw configuration key/value pairs and exits (wired to existing `showSettings()`).
- **`--clear-cache` overhaul**:
  - `--clear-cache` (no args): Interactive numbered list; pick one manifest to delete, list redisplays after each deletion, loop until [Q].
  - `--clear-cache ALL`: Shows full manifest list, prompts `[y/N]` confirmation before wiping all (settings and locations preserved).
  - `--clear-cache ALL --force`: No prompt, wipes all manifests, exits (scriptable/unattended).
- **Centered menu titles**: Added `UI::printCenteredLine()` and `UI::printCenteredTitle()` to `ui_helpers.hpp`. Applied to `kml_prefs` and `locations` menus. Left padding = `(INNER_WIDTH - title.length()) / 2`.
### Maintenance
- **SN**: High-water mark `00030` (ui_helpers.hpp, kml_prefs.cpp, locations.cpp, config_manager.cpp, config_manager.hpp, main.cpp, version.hpp).

All notable changes to the QuadEye Dashcam Explorer project will be documented in this file.

# CHANGELOG

All notable changes to the QuadEye Dashcam Explorer project will be documented in this file.

## [0.8.11 / HWM: 29] - 2026-02-22
### Fixed
- **All temp files moved from `/tmp` to output directory** — eliminates
  `/tmp` space exhaustion on long collage builds. All `quadeye_tmp_*`
  files now written alongside the final output and cleaned up after build.
  Affected functions: `buildCameraFile`, `buildCollage4K`,
  `buildAudioFile`, `buildPaddedInput`, `runCollageFromFiles`.
- **Em-dash in empty slot label** replaced with plain ASCII hyphen —
  fixes box border misalignment in collage-from-files slot list.
### Added
- **`[N] Output filename`** in collage-from-files menu — user can override
  the auto-generated timestamp filename. Auto-generate remains default.
  `_4K.mp4` / `_1080p.mp4` suffixes appended automatically.
  Output paths shown before encode starts so user knows exactly what
  will be written.
### Maintenance
- **SN**: High-water mark `00029`. Changed files: `video_build.hpp`,
  `video_build.cpp`, `CMakeLists.txt`, `version.hpp`.

---


### Added
- **`GODONE`** — new build action in `-V` options menu. Runs the selected
  build and exits `-V` entirely when complete. Complements `GO` which
  returns to the trip picker for another build.
- **`GO` now loops** — after a build completes, returns to the mode
  selection menu so the user can build another trip or switch to
  collage-from-files without re-entering `-V`.
### Changed
- **`[OFF]` moved** above the footer line into the options section where
  it belongs — it is a selection action, not a terminal command.
- **Footer** updated:
  `[GO] Build and continue   [GODONE] Build and exit   [Q] Quit`
- **`VideoOptions.exitAfterBuild`** flag added — set by `GODONE`,
  checked by `run()` after build completes.
### Maintenance
- **SN**: High-water mark `00027`. Changed files: `video_build.hpp`,
  `video_build.cpp`, `CMakeLists.txt`, `version.hpp`.

---


### Added
- **Mode 2: Collage from existing files** (`-V` → `[2]`):
  - Four independent slots (top-left, top-right, bottom-left, bottom-right)
  - Any camera from any trip can fill any slot — enables cross-trip
    comparisons (e.g. four front-camera commutes side by side)
  - `[1]-[4]` to assign a file path to each slot; blank entry clears slot
  - Empty slots render as logo-on-dark-blue placeholder for full duration
  - `[A]` cycles audio source to next filled slot
  - `[F]`/`[G]` toggle 4K/1080p output
  - `[OFF]` clears all slots
  - `[GO]` validates (at least one filled slot, output selected, audio
    slot is filled) then builds
- **Duration normalization across slots**:
  - ffprobe measures duration of each filled slot
  - All slots padded to match longest via `buildPaddedInput()`:
    - Filled slots: original clip → 0.5s fade to black → logo-on-dark-blue
      hold for remainder
    - Empty slots: full-duration logo-on-dark-blue
  - Ensures xstack receives identical-length inputs — no sync drift
- **`getFileDuration()`** — ffprobe helper, returns seconds as double
- **`buildPaddedInput()`** — per-slot padding pipeline
- **`buildCollageFromSlots()`** — xstack assembly from four pre-padded inputs
- **`runCollageFromFiles()`** — full mode 2 interactive flow
- Output filename uses current timestamp: `YYYYMMDD_HHMMSS_Collage_4K.mp4`
### Notes
- Logo path currently reads from `images/quadeye_logo_final.jpg` relative
  to working directory. Will be replaced by baked binary asset when xxd
  integration lands.
### Maintenance
- **SN**: High-water mark `00026`. Changed files: `video_build.hpp`,
  `video_build.cpp`, `CMakeLists.txt`, `version.hpp`.

---


### Added
- **`[GO]` to trigger build** — replaces `[B]` which conflicted with Rear
  camera toggle. Case-insensitive full-string match handles `go/GO/Go/gO`.
- **`[OFF]` to deselect all** — resets all output toggles to off in one
  shot. Source/format preferences preserved. Useful for selecting a single
  output without toggling off each item individually.
- **Audio extract** — new independent output option:
  - `[S]` toggles audio extract on/off (default: off)
  - `[T]` selects source camera (left/right/front/rear — all four always
    available regardless of video camera toggles)
  - `[U]` selects format: `m4a` (default, lossless AAC remux), `mp3`
    (libmp3lame -q:a 2), `aac` (raw AAC remux)
  - Output filename: `YYYYMMDD_HHMMSS_Audio_Left.m4a`
  - `[AUDIO]` footer command removed — audio is just another output
    toggle, included in the same GO pass
- **Collage/camera independence note** in menu — clarifies that collage
  uses raw `.ts` segments directly; camera toggles `[A-D]` only control
  whether per-camera mp4 files are written as deliverables.
- **Deselect guard on camera toggles** — attempting to disable a camera
  that is the current collage audio source (`[H]`) is blocked with a
  message directing the user to change `[H]` first.
- **GO validation** — warns and stays in menu if nothing is selected.
### Changed
- `[H]` label updated to `Collage audio` for clarity.
- `configureOptions` prompt updated to `Option:` (was
  `Toggle option or [B] build, [Q] quit:`).
### Maintenance
- **SN**: High-water mark `00025`. Changed files: `video_build.hpp`,
  `video_build.cpp`, `CMakeLists.txt`, `version.hpp`.

---


### Fixed
- **CMake `sn-audit` and `archive` targets**: Previous inline `sh -c`
  shell strings caused path mangling and permission errors on Alma 9.
  Replaced with:
  - `cmake/sn_audit.cmake` — pure CMake script, no shell dependency.
    Scans all `.cpp`, `.hpp`, `CMakeLists.txt`, and
    `quadeye_project_brief.md` for `^// SN:` and `^# SN:` patterns.
    Writes aligned `sn_audit.txt` to source root.
  - `cmake/archive_files.txt` — explicit file list for `cmake -E tar`,
    avoids shell glob expansion entirely.
  - Both targets now portable — no grep/awk/sh dependency.
### Build workflow
```bash
cmake --build build --target sn-audit   # writes sn_audit.txt
cmake --build build --target archive    # runs sn-audit first, then tars
```
### Maintenance
- **SN**: High-water mark `00024`. Changed files: `CMakeLists.txt`,
  `version.hpp`. New files: `cmake/sn_audit.cmake`,
  `cmake/archive_files.txt`.

---


### Added
- **`-V` mode selection menu**: Top-level menu before trip selection:
  - `[1] Build from manifest` — full pipeline (existing behavior)
  - `[2] Build collage from existing camera files` — stub, next build
- **`default_audio_source` preference** (`[J]` in prefs editor):
  - Values: `left | right | front | rear`. Default: `left` (driver
    position in left-hand-drive countries; RHD users set to `right`).
  - Persisted in `quadeye.json` as `defaultAudioSource`.
  - Loaded into `VideoOptions.audioSource` at build time.
- **`[H] Audio source` in build options menu**: Per-build override of
  `default_audio_source` preference. Cycles left/right/front/rear.
### Changed
- **ffmpeg logging**: All ffmpeg invocations now prepend
  `-loglevel warning -stats` automatically in `runFfmpeg()`. Suppresses
  banner and stream negotiation spam; retains single-line updating
  progress output.
- **Collage layout corrected**: xstack input order changed from
  `[v0][v1][v2][v3]` to `[v0][v1][v3][v2]` — Right camera now
  bottom-left, Left camera bottom-right. Matches intuitive top-down
  vehicle perspective for rear-facing side cameras:
  ```
  [ Front ] [ Rear  ]
  [ Right ] [ Left  ]
  ```
- **Collage audio source** wired to `VideoOptions.audioSource` rather
  than hardcoded `2:a:0`. Resolves input index at build time from
  preference string.
### Maintenance
- **SN**: High-water mark `00023`. Changed files: `config_manager.hpp`,
  `config_manager.cpp`, `prefs.cpp`, `video_build.hpp`, `video_build.cpp`,
  `CMakeLists.txt`, `debug_main.cpp`, `version.hpp`.

---


### Added
- **`trip_debug` build target**: Standalone debug binary added to
  `CMakeLists.txt`. Build with:
  `cmake --build build --target trip_debug`
  Not included in `install` target — dev tool only.
- **`trip_debug -v`/`--version`**: Prints version and exits.
- **`trip_debug -T`/`--tree`**: Prints full segment tree for every trip in
  the manifest and exits non-interactively. Shows trip header (date, start
  time, duration, segment count), then each segment with camera presence
  flags `[FBLR]` (`.` when absent) and filenames.
- **`trip_debug` now uses `ConfigManager`**: Gap threshold and fuzzy window
  come from user settings rather than hardcoded defaults.
### Changed
- `trip_debug` interactive prompt uses `getline` consistently — fixes
  input handling edge cases from the old `cin >>` approach.
- `showTripDetails` in `-f` mode now reuses `showTripTree` rather than
  duplicating display logic.
### Maintenance
- **SN**: High-water mark `00022`. Changed files: `debug_main.cpp`,
  `CMakeLists.txt`, `version.hpp`.

---


### Changed
- **Build system**: Replaced `Makefile` with `CMakeLists.txt` (CMake 3.16+).
  - Version extracted from `version.hpp` — single source of truth maintained.
  - `sn-audit` and `archive` preserved as custom targets:
    `cmake --build build --target sn-audit`
    `cmake --build build --target archive`
  - `install` target follows GNUInstallDirs — binary to `bin/`, man page to
    `man/man1/`. Correct for RPM/DEB packaging and EPEL submission.
  - CPack configured for RPM and DEB generation with correct metadata,
    license (GPLv2), and dependency declarations. ffmpeg noted as soft
    dependency (RPM Fusion) since it cannot be an EPEL hard dependency.
  - Platform-specific `stdc++fs` link handled conditionally — groundwork
    for future Mac and Windows builds.
  - `CMAKE_CXX_EXTENSIONS OFF` ensures strict C++17 portability.
### Build workflow change
```
# Configure (once)
mkdir build && cd build && cmake ..

# Build
cmake --build build

# SN audit
cmake --build build --target sn-audit

# Archive
cmake --build build --target archive

# Install
cmake --install build --prefix /usr/local
```
### Maintenance
- **SN**: High-water mark `00021`. Changed files: `CMakeLists.txt` (replaces
  `Makefile`), `version.hpp`.

---


### Added
- **`-V`/`--video`**: Interactive video build menu. Loads lastPath manifest,
  shows trip list with date/time/segment count/duration. `[M]` switches to any
  other cached manifest. User picks a trip then configures build options.
- **Build options menu**: Toggle per-camera files (Front/Rear/Left/Right),
  container format (mp4/mkv/mov/avi/mpg), 4K collage, 1080p copy, output
  directory. `[B]` to build, `[Q]` to abort.
- **`video_build.hpp/.cpp`**: `VideoBuilder` class — full ffmpeg orchestration:
  - **Per-camera files**: Concat all `.ts` segments via ffmpeg concat demuxer,
    stream copy (`-c copy`), `-map 0:v:0 -map 0:a:0` strips GPS/data tracks
    from MPEG-TS container. `-movflags +faststart` for mp4 streaming.
  - **Sync normalization**: Per-segment, get frame count of all 4 cameras via
    ffprobe, pad shorter cameras to longest using `tpad` filter with
    fade-to-black on tail (`fade=t=out`), `apad` for audio. Ensures frame-locked
    sync across entire trip with no drift accumulation.
  - **4K collage**: 4-input ffmpeg filter graph, `xstack=inputs=4` in 2x2
    layout (Front/Rear top, Left/Right bottom), each cell scaled to 1920x1080,
    output 3840x2160. Codec: `libx265 -crf 18 -preset slow` (visually lossless).
    Audio: Left camera only (`-map 2:a:0`) — driver position for lip sync.
  - **1080p collage**: Transcode of 4K master via `scale=1920:1080`,
    `libx264 -crf 20 -preset fast`. Audio stream copied, no re-encode.
    Always derived from 4K master, never assembled from sources again.
- **`videoFormat` setting**: Added to `AppSettings`, persisted in `quadeye.json`.
  Accessible via `config.getVideoFormat()`. Default `mp4`.
- **Prefs `[I]`**: Video output format field added to interactive preferences
  editor. Cycles through mp4/mkv/mov/avi/mpg.
- **Output naming**: `<YYYYMMDD>_<HHMMSS>_<Label>.<ext>` —
  e.g. `20260216_095422_Front.mp4`, `20260216_095422_Collage_4K.mp4`.
### Notes
- Spinning logo placeholder noted in sync normalization — logo GIF will be
  composited into large sync gaps in a future build once the asset exists.
- ffprobe path auto-derived from ffmpeg path setting.
### Maintenance
- **SN**: High-water mark `00020`. Changed files: `config_manager.hpp`,
  `config_manager.cpp`, `main.cpp`, `prefs.cpp`, `version.hpp`, `Makefile`.
  New files: `video_build.hpp`, `video_build.cpp`.

---


### Fixed
- **Box right border misaligned**: `INNER_WIDTH` was `BOX_WIDTH - 4` but the
  actual overhead per line is 6 characters: `|`(1) + ` `(2) + content + ` `(2)
  + `|`(1). Changed to `BOX_WIDTH - 6`. Right `|` border now flush with `+`
  corners on every line.
### Maintenance
- **SN**: High-water mark `00019`. Changed files: `ui_helpers.hpp`, `version.hpp`.

---


### Changed
- **Box drawing**: Swapped border weights — outer border now uses `=` (heavier,
  frame-like), internal dividers use `-` (lighter, table-rule feel). More logical
  visual hierarchy: frame > content separator.
### Maintenance
- **SN**: High-water mark `00018`. Changed files: `ui_helpers.hpp`, `version.hpp`.

---


### Fixed
- **Box drawing**: Replaced ASCII `/`, `\` corners with `+`/`-`/`=`/`|` layout.
  The `/` and `\` corner approach still caused compiler warnings (`-Wcomment`) due
  to backslash handling. `+` corners with `-` top/bottom, `=` dividers, and `|`
  verticals is unambiguous, warning-free, and gives a clean double-line visual
  distinction between borders and dividers.
### Maintenance
- **SN**: High-water mark `00017`. Changed files: `ui_helpers.hpp`, `version.hpp`.

---


### Fixed
- **Box drawing**: Replaced all UTF-8 box-drawing characters (╔ ═ ║ etc.) with
  plain ASCII (/ - \ |). Unicode box-drawing characters are "East Asian Width
  ambiguous" — Gnome Terminal and Terminator both rendered them as 2 columns wide
  causing right border misalignment regardless of visibleLen() fix. Plain ASCII
  is single-width on every terminal, font, and locale without exception.
- **Compiler warnings**: Removed ASCII art from `//` comments in `ui_helpers.hpp`
  — backslash at end of a `//` comment line is treated as a line continuation,
  triggering `-Wcomment` warnings.
### Maintenance
- **SN**: High-water mark `00016`. Changed files: `ui_helpers.hpp`, `version.hpp`.

---


### Fixed
- **Box border alignment**: `ui_helpers.hpp` `printLine()` was using `std::string::size()`
  for padding calculations, which counts bytes not display columns. UTF-8 box-drawing
  characters (║ etc.) are 3 bytes but 1 terminal column, causing right border to drift
  right. Fixed with `visibleLen()` which counts display columns by skipping UTF-8
  continuation bytes (10xxxxxx). All padding math now uses `visibleLen()`.
- **Box width**: Increased `BOX_WIDTH` from 54 to 65 columns. Full Google Maps KML
  icon URLs now fit without truncation. 65 columns is safe for any terminal at 80+
  columns (standard baseline).
- **URL truncation**: Added `truncateVisible()` — truncates to max visible columns and
  appends `…` (U+2026) if content overflows. Handles multi-byte UTF-8 correctly by
  walking codepoint boundaries rather than raw bytes.
### Maintenance
- **SN**: High-water mark `00015`. Changed files: `ui_helpers.hpp`, `version.hpp`.

---


### Added
- **Interactive preferences editor** (`-P`/`--prefs`): minicom-style box-drawn UI.
  Settings: Trip Gap, Fuzzy window, GPS cold-start skip, Default export directory,
  ExifTool path, FFmpeg path, Timestamp format, Time display (24/12-hour).
  Unsaved-change detection with Y/N confirmation on quit.
- **KML visual preferences** (`--kmlprefs`): Dedicated editor for KML styling.
  Settings: track ahead color (default bright green `ff00ff00`), track behind color
  (default red `ff0000ff`), line width, waypoint color (default blue `ffff0000`),
  start/end pin icon URLs (Google dropped pin default), show known locations toggle.
  Colors in KML AABBGGRR format with hint text shown in UI.
- **Known locations manager** (`--locations`): List/add/edit/delete named points of
  interest stored in `~/.config/quadeye/locations.json`. Fields: name, lat, lon,
  proximity radius (metres), icon style. Used for KML overlay pin drops when a
  GPS track passes within radius of a known location.
- **`ui_helpers.hpp`**: Shared ANSI box-drawing and input utilities used by all
  three interactive editors. Box width 54 chars, UTF-8 box-drawing characters,
  `promptString`, `promptInt`, `promptChoice`, `confirmExists` helpers. No ncurses
  dependency — pure ANSI escape codes.
- **`KmlSettings` struct**: KML visual preferences persisted under `"kml"` key in
  `quadeye.json`. Full serialize/deserialize in `config_manager`.
- **`NamedLocation` struct**: name, lat, lon, radiusMetres, icon. Persisted to
  `~/.config/quadeye/locations.json` as a JSON array.
- **`AppSettings` expansion**: New fields: `gpsColStartSkip` (default 45s),
  `exiftoolPath`, `ffmpegPath`, `defaultExportDir`, `timestampFormat`
  (`YYYYMMDD-HHMMSS` default), `timeDisplay` (`24-hour` default).
- **`--set <key=val>`**: Non-interactive preference setter for scripting.
  Currently supports `gap=<seconds>`. Replaces old `-P key=val` syntax.
- **`[B] Back` fix**: `-i` mode no longer shows `[B] Back to manifests` option.
  `[B]` only appears when launched from `-I` manifest browser. `FindTrips::run()`
  now takes a `bool showBack` parameter.
### Changed
- **`-P`/`--prefs`**: Now launches interactive UI instead of inline key=value.
- **`--clear-cache`**: Message updated — notes that settings AND locations are preserved.
- **`locations.json`** added to `isBlacklisted()` — never treated as a trip manifest.
- **ROADMAP**: `.quadeye` export package updated — optional `locations.json` snapshot
  with privacy note (user controls inclusion at export time).
### Maintenance
- **SN**: High-water mark `00014`. Changed files: `config_manager.hpp`,
  `config_manager.cpp`, `main.cpp`, `find_trips.hpp`, `find_trips.cpp`,
  `version.hpp`, `CHANGELOG.md`, `ROADMAP.md`.
  New files: `ui_helpers.hpp`, `prefs.hpp`, `prefs.cpp`, `kml_prefs.hpp`,
  `kml_prefs.cpp`, `locations.hpp`, `locations.cpp`.

---

## [0.6.1b / HWM: 14] - 2026-02-21
### Added
- **Preferences UI**: New `prefs.hpp`/`prefs.cpp` — minicom-style interactive configurator
  launched via `-P`/`--prefs`. Box-drawn UI with ANSI escape codes, no ncurses dependency.
  Title shows `QuadEye v<version> Preferences`. Settings labeled `Trip Gap (seconds)`,
  `Fuzzy window (seconds)`. Save/quit with unsaved-change confirmation.
- **Batch pref setting**: `--set <key=val>` long-only flag for scripting (e.g. `--set gap=1800`).
### Maintenance
- **SN**: High-water mark `00014`. Changed files: `prefs.hpp`, `prefs.cpp`, `main.cpp`, `version.hpp`.

---

## [0.6.1a / HWM: 13] - 2026-02-21
### Added
- **GPS extraction** (`-g`/`--gps`): Extract GPS from `.ts` segments into manifest cache
  without writing any output file. Prompts for trip selection with "all" option. Bulk
  extraction warns user of time cost and requires Y/N confirmation.
- **GPX export** (`-G`/`--gpx`): Extract GPS if needed and write GPX 1.1 file. Default
  filename derived from `segments[0].front` timestamp stem (e.g. `20260216_095422.gpx`).
- **KML export** (`-K`/`--kml`): Extract GPS if needed and write KML 2.2 file using
  `gx:Track` with per-point timestamps, speed, and heading extended data. Start/end
  placemarks with distinct pin styles. Default filename e.g. `20260216_095422.kml`.
- **Output path options**: `--gpxpath <dir>`, `--gpxfile <name>`, `--kmlpath <dir>`,
  `--kmlfile <name>`. Explicit filename with "all" uses `.1` `.2` numeric suffix collision
  avoidance. `--force` overwrites without prompting.
- **Altitude in GPS track**: `GpsPoint` struct and JSON manifest now store `altitude` field
  (metres, as-is from ExifTool — D90 values known unreliable but preserved for future use).
  GPX emits `<ele>`, KML passes altitude to `gx:coord`.
- **ExifTool GPS extraction**: `extractGps()` runs ExifTool 13.51+ on every Front camera
  segment, skips first 45s of first segment (cold-start noise), stores full track in
  manifest. One progress dot per segment. Rewrites manifest to disk on completion.
### Changed
- **Export architecture**: `gpx_export.hpp`/`.cpp` fully restructured. `ExportMode` enum
  (`GpsOnly`/`Gpx`/`Kml`), `ExportOptions` struct. Single `run()` entry point handles all
  three modes with shared trip selection UI.
- **Removed**: `-o`/`--outdir` replaced by `--gpxpath`/`--kmlpath`.
### Fixed
- **Stale reference bug**: After `extractGps()` rewrites `root`, downstream code now
  re-reads from `root["trips"][idx]` rather than a stale `const auto&` reference.
- **ODR crash** (`-i` core dump): Caused by partial rebuild after `trip_detection.hpp`
  struct layout change. Fixed by `make clean && make`.
### Maintenance
- **SN policy clarified**: Only modified files get SN bumped to high-water mark.
  Unchanged files retain their prior SN. Hand-corrected after erroneous bulk bump.
- **SN**: High-water mark `00013`. Changed files: `main.cpp`, `gpx_export.hpp`,
  `gpx_export.cpp`, `version.hpp`.

---

## [0.6.0p / HWM: 9] - 2026-02-21
### Added
- **Project brief**: `quadeye_project_brief.md` created for fast AI session context
  restoration. Uses `<!-- SN: -->` HTML comment (invisible when rendered on GitHub).
- **`gpx_export.hpp`/`.cpp`**: New files. GPX 1.1 writer, ExifTool GPS extraction,
  interactive trip/manifest selection UI for `-G` flag.
- **`gpx_export.o`**: Added to Makefile `SRC` list.
### Fixed
- **Makefile `sn-audit` target**: Corrected `%%`/`$$` escaping for awk format strings
  inside make recipes. Added `quadeye_project_brief.md` to audit grep and `archive` tar.
- **Settings restoration**: `AppSettings` struct with `gapThresholdSeconds` (default 900s),
  `fuzzyWindowSeconds` (default 5s), `schemaVersion`. Stored in `~/.config/quadeye/quadeye.json`.
  `--clear-cache` preserves settings file.
- **`getenv("HOME")` null guard**: Exits cleanly with error message if `HOME` unset.
- **`-s`/`--scan` flag validation**: Now checks next argv is not another flag.
- **`stringToTimestamp`**: Moved to anonymous namespace in `trip_detection.cpp`.
### Changed
- **Gap threshold**: Configurable via `-P`/`--prefs gap=<seconds>`, default 900s (15 min).
  Previously hardcoded as `segdur + 30s`.
- **`-P`/`--prefs`**: New flag for settings management (show and set).
### Maintenance
- **SN**: Changed files reach `00009`; `main.cpp`/`version.hpp` at high-water mark.

---

## [0.6.0k / HWM: 7] - 2026-02-16
### Fixed
- **Archive System**: Moved `sn_audit.txt` to the root of the tarball.
- **SN Policy**: Re-affirmed that Serial Numbers only increment upon file modification.
### Maintenance
- **SN**: High-water mark reached `00007` (Makefile, main.cpp, version.hpp).

## [0.6.0h / HWM: ~4] - 2026-02-16
### Added
- **Build System**: Introduced `sn-audit` target in Makefile.
- **Logging**: Automated generation of timestamped Serial Number logs in `sn_logs/` for every successful build.
- **Maintenance**: Unified project-wide Serial Number to `00004` for modified files (Makefile, main.cpp, version.hpp).

---

## [0.6.0g / HWM: ~3] - 2026-02-16
### Fixed
- **CLI Logic**: Actions `-t`, `-T`, and `-i` now correctly fall back to `lastPath` from the configuration if no explicit path is provided on the command line.
- **Batch Processing**: Resolved issue where summary output was omitted when processing multiple paths via `-s`.
- **Navigation**: Ensured interactive modes trigger correctly following batch scans.
### Maintenance
- **SN**: Incremented global Serial Number to `00003` for logic changes in `main.cpp` and `version.hpp`.

---

## [0.6.0e / HWM: ~2] - 2026-02-16
### Improved
- **Detection**: Increased fuzzy matching window to ±5s to account for dashcam camera drift.
- **Maintenance**: Introduced 5-digit sequential Serial Numbers (SN) at the end of all source files for tracking across compiles.

---

## [0.6.0c / HWM: n/a] - 2026-02-16
### Added
- **CLI**: Added `--clear-cache` as a long-only fail-safe option to remove all manifest files.
- **CLI**: Version bumped to 0.6.0c.
### Fixed
- **Logic**: Ensured `--clear-cache` executes as a terminal action before path-specific scanning logic.

---

## [0.6.0b / HWM: n/a] - 2026-02-16
### Fixed
- **main.cpp**: Synchronized with `version.hpp` using `APP_VERSION` and `APP_NAME`.
- **main.cpp**: Restored legacy usage block and command-line flag logic.
- **ConfigManager**: Added missing declarations for `getLastPath`, `setLastPath`, `isCached`, and `listCachedManifests`.
- **Display**: Corrected `-T` output to adhere to the "2nd segment thumbnail" rule.

---

## [0.5.2c / HWM: n/a] - 2026-02-16
### Fixed
- **CLI Error Codes**: Implemented `EX_USAGE` (64) exit code from `<sysexits.h>` for invalid command-line options, conforming to industry standards.
- **Error Messaging**: Custom error alerts now specify exactly which flag was invalid (e.g., "Invalid option '-z'") before showing usage and exiting.

## [0.5.2b / HWM: n/a] - 2026-02-16
### Fixed
- **Path Case Sensitivity**: Forced all configuration and cache paths to strictly lowercase `~/.config/quadeye/`.
- **Header Sync**: Synchronized `config_manager.hpp` and `.cpp` to resolve private member access errors.

### Changed
- **Data Decoupling**: Refactored storage into a three-tier system: `quadeye.json` (settings), `manifests.json` (registry), and path-specific `.json` files (trip data).
- **Apple Double Cleanup**: Completely removed all logic and comments related to `._` metadata files.

---

## [0.5.2 / HWM: n/a] - 2026-02-16
### Added
- **Global Tree View (-T, --alltrips)**: Expanded the tree view to iterate through every cached manifest in the system, providing a master overview of all drives and trips.
- **Multi-Scan Support**: The `-s` and `--scan=<path>` options now support multiple inputs in a single execution.
- **Standardized CLI**: Implemented industry-standard short and long options (e.g., `--inter`, `--manifests`, `--refresh`) with an aligned `-h` help output.

### Changed
- **CLI Flags**: Reassigned `-L` for the Interactive Manifest Browser and `-l` for the static inventory list.

---

## [0.5.1 / HWM: n/a] - 2026-02-16
### Added
- **Manifest Inventory (-l)**: List cached path manifests with Letter IDs and summary stats.
- **Interactive Manifest Browser (-I)**: Full-screen menu to select drives by Letter ID.
- **Hierarchical Navigation**: Added a `[b] back` option to return to the drive list when navigating via `-I`.
- **Persistent Letter IDs**: Paths now retain a stable 'A', 'B', 'C' identifier in `~/.quadeye.json`.

### Fixed
- **Menu Focus**: Ensured `-i` (single path) remains a flat, focused browser without "back" navigation.
## [0.5.0 / HWM: n/a] - 2026-02-15
### Added
- **Master Refresh (-R)**: New command-line flag to iterate through all directory paths stored in the manifest and perform a full re-scan/update of cached data.
- **Enhanced Build UI**: Integrated a professional version announcement box into both `make` and `make archive` using `printf` for guaranteed terminal border alignment.
- **Interactive Path Switching**: Refined the `p` (path) option within the interactive browser to switch directories and persist the choice in the configuration.

### Fixed
- **Cache Persistence**: Fixed a bug where `segmentCount` was being lost during JSON serialization; it is now correctly saved to and restored from the manifest.
- **JSON Manifest Loader**: Re-engineered the `ConfigManager` to correctly iterate and load all historical path keys from the local configuration file.
- **Rescan Logic**: Fixed the `-s` (scan) flag logic to properly bypass existing cache files and force a fresh file system scan.

---

## [0.3.7 / HWM: n/a] - 2026-02-15
### Added
- **Interactive Mode Improvements**: 
  - Added a `Segments` column to the trip list to show the count of Front camera segments.
  - Introduced the `>` prompt character for the interactive selection menu.
- **Persistent Pathing**: ConfigManager now correctly remembers and updates the `lastPath` when switching directories within Interactive Mode.

### Fixed
- **UI Alignment**: Synchronized the Header and Body column widths for perfect vertical alignment in the terminal.
- **ID Formatting**: Transitioned from bracketed IDs (`[01]`) to a cleaner, right-justified style in a 5-column field to handle 1 to 999+ trips gracefully.
- **Build Error**: Resolved macro naming conflicts in `version.hpp` and `main.cpp` that caused "unqualified-id" errors during compilation.
- **Hidden file fix**: Implemented a filter to skip hidden dot files `.` during directory scans.

### Changed
- **Visual Style**: Removed pipe delimiters from the header line for a more modern, breathable look, while maintaining them in the data rows for scannability.
- **Thumbnail Selection**: Defaulted the preview thumbnail to the second segment of the Front camera (index [1]) for better trip representation.

---

## [0.3.3 / HWM: n/a] - 2026-02-14
### Added
- **Interactive Mode**: Introduced the `-i` flag for a persistent trip browser.
- **Path Navigation**: Added the "p" option to switch directories and auto-scan within the interactive UI.

---

## [0.3.0 / HWM: n/a] - 2026-02-08
### Added
- **JSON Caching**: Implemented a caching layer to eliminate re-scan times.
- **Initial CLI**: Added support for Summary (`-t`) and Tree (`-T`) views.

---

## [0.2.0 / HWM: n/a] - 2026-02-01
### Added
- **Trip Grouping**: Logic to group individual front, rear, and side camera files into coherent "Trips" based on timestamps.
- **FFprobe Integration**: Added system calls to `ffprobe` to determine exact video segment durations.

---

## [0.1.0 / HWM: n/a] - 2026-01-25
### Added
- **Initial Release**: Basic directory scanning and file listing for Tesla dashcam footage.
<!-- SN: 00102 -->
