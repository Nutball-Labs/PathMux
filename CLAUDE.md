# PathMux — Claude Code Project Context

This file is read automatically by Claude Code at session startup.
It contains standing instructions, architecture decisions, and conventions
for working on the PathMux project. Read this before touching any code.

---

## Project Overview

**PathMux** is a C++17 CLI tool for Alma Linux 9.x that scans dashcam footage,
groups video segments into trips, caches results as JSON manifests, and
extracts/exports GPS tracks. Phase 2 Qt6 GUI is in active development.
Private GitHub repo at https://github.com/Nutball-Labs/PathMux — all work on `main` branch.

**Current version:** 1.2.0 (SN 00095)
**Config dir:** `~/.config/pathmux/`
**Build system:** CMake (primary) + legacy Makefile

---

## Developer Profile

Experienced Linux sysadmin (Perl/Python/bash), learning C++ via AI assistance.
Full root on Alma 9.x dev machine. Uses VSCode + vim. Has 40+ years sysadmin
experience — don't over-explain Linux basics. Does need help with C++ idioms.

---

## Source Files

### Library (`lib/`) — compiled into `libpathmuxlib.a`

| File | Role |
|---|---|
| `lib/trip_detection.cpp/.hpp` | Filesystem scan, trip grouping, ffprobe calls — `namespace Pathmux` |
| `lib/config_manager.cpp/.hpp` | JSON manifest read/write, manifest index, settings — `namespace Pathmux` |
| `lib/camera_profile.cpp/.hpp` | `CameraSlot`, `CameraProfile` structs, JSON load/save, `d90Default()` — `namespace Pathmux` |
| `lib/gps_export.cpp/.hpp` | GPS extraction (exiftool LIGO), GeoJSON/GPX/KML write — `namespace Pathmux` |
| `lib/trip_format.hpp` | Header-only CSV/XML structured output helpers; shared by pathmux and pm_ls — `namespace Pathmux` |
| `lib/compat.hpp` | Cross-platform portability shims: popen/pclose, WEXITSTATUS, localtime_r, timegm, pathBasename |
| `lib/platform.cpp/.hpp` | OS abstraction: home/config paths, terminal width — `namespace Pathmux::Platform` |
| `lib/format_helpers.hpp` | Pure math/format functions (haversine, formatDistance, etc.) — `namespace Pathmux` |
| `lib/logger.hpp` | Logging singleton — `namespace Pathmux` |
| `lib/version.hpp` | Version string from components via macros |
| `lib/pathmux.hpp` | Umbrella public API header |

### CLI front-end (`cli/`) — compiled into `pathmux` binary

| File | Role |
|---|---|
| `cli/main.cpp` | CLI argument parsing, orchestration |
| `cli/find_trips.cpp/.hpp` | Display: summary, details, interactive browser, -t/-T |
| `cli/gpx_export.cpp/.hpp` | Interactive GPS menu, extraction, GPX/KML export |
| `cli/prefs.cpp/.hpp` | Interactive preferences menu |
| `cli/video_build.cpp/.hpp` | Video build/collage orchestration |
| `cli/kml_prefs.cpp/.hpp` | KML export preferences |
| `cli/locations.cpp/.hpp` | Named location management |
| `cli/host_prefs.cpp/.hpp` | Host-specific config overlay (`HostPrefsEditor` class) |
| `cli/ui_helpers.hpp` | Terminal box UI and input helpers (POSIX only); includes format_helpers.hpp |

### Tools (`tools/`) — standalone binaries

| File | Role |
|---|---|
| `tools/pm_probe.cpp` | Camera compatibility profiler: single file, `--card` SD root, `--wizard` setup wizard |
| `tools/pm_gpsinfo.cpp` | GPS info utility; `--scan-all-trips` batch lock scanner; `MID:TID` addressing |
| `tools/pm_findgpslock.cpp` | Scan raw .ts files and report GPS lock acquisition time |
| `tools/pm_gpsexport.cpp` | Non-interactive GPS track exporter |
| `tools/pm_ls.cpp` | Non-interactive trip lister; supports `--format` / `--fields` |
| `tools/pm_audit.cpp` | Footage integrity checker |
| `tools/pm_tripdebug.cpp` | Trip detection debug/inspection tool |

### GUI (`gui/`) — compiled into `pathmux-gui` binary (Qt6, Phase 2)

| File | Role |
|---|---|
| `gui/main.cpp` | Qt application entry point |
| `gui/MainWindow.cpp/.h` | Main window: manifest list, trip grid, menus |
| `gui/ManifestPanel.cpp/.h` | Left-panel manifest browser |
| `gui/TripGridPanel.cpp/.h` | Right-panel trip tile grid |
| `gui/TripTile.cpp/.h` | Individual trip card widget |
| `gui/TripPropertiesDialog.cpp/.h` | Trip details; double-click segment opens in system viewer |
| `gui/TripBuildDialog.cpp/.h` | Video/collage build options dialog |
| `gui/BuildProgressDialog.cpp/.h` | Per-stage progress rows; QProcess runner; concurrent concat via std::thread |
| `gui/ScanProgressDialog.cpp/.h` | Progress dialog for manifest scan |
| `gui/ManifestManagerDialog.cpp/.h` | Manifests menu management dialog |
| `gui/SettingsDialog.cpp/.h` | Settings / preferences dialog |
| `gui/SetupWizard.cpp/.h` | First-run setup wizard |
| `gui/AboutDialog.cpp/.h` | About dialog (Nutball-Labs logo + PathMux icon) |
| `gui/EmptyManifestWidget.cpp/.h` | Placeholder widget when no manifests loaded |

### Other

| File | Role |
|---|---|
| `json.hpp` | nlohmann/json v3.11.3 (vendored, root, do not modify) |
| `CMakeLists.txt` | Primary build system |
| `pathmux.1` | Man page (canonical location: `man1/pathmux.1`) |

---

## Serial Number (SN) Convention

Every source file and the Makefile carries a serial number comment at the
bottom of the file:

```cpp
// SN: 00071       ← C++ files and headers
# SN: 00071        ← Makefile and cmake files
<!-- SN: 00071 --> ← Markdown (.md) files (HTML comment — invisible when rendered)
```

**Rules:**
- There is one project-wide **high-water mark** SN, currently `00095`
- When files are modified in a build/fix session, bump their SN to the
  current high-water mark
- When cutting a new release, increment the high-water mark by 1 and apply
  it to ALL files touched in that release
- Files that were NOT changed in a session keep their existing SN — do not
  bump unchanged files
- Use `^// SN:` and `^# SN:` grep anchors to find/replace SNs and avoid
  false matches in comments or documentation
- Run `cmake --build build-linux --target sn-audit` to regenerate `sn_audit.txt`
  (build dir is `build-linux` — NOT `build`)
- `sn_audit.cmake` uses **last-match-wins** per file — intentional, prevents doc
  examples in CLAUDE.md from generating false duplicate entries. Do not change this.

**Example workflow:**
- Working on a bug fix: change the files, bump their SN to current HWM
- Cutting a release: increment HWM (e.g. 00071 → 00072), apply to all
  changed files, update `version.hpp` with new patch/minor version

---

## Deliverable Format

**When working in VSCode + Claude Code (current setup):** tarball production is
not needed. Files are edited in-place and delivered via `git push origin main`.
"Cut the tarball" in this context means: implement changes, verify clean build,
bump SNs, bump version, commit, and push.

**Tarball format (if ever needed outside VSCode):**
- Output to `/mnt/user-data/outputs/pathmux_X.Y.Z.tgz`
- Format is always `.tgz` — never `.zip` or `.tar.gz` (redundant extension)
- Standard contents: all `.cpp`, `.hpp`, `CMakeLists.txt`, `pathmux.1`,
  `cmake/`, `ROADMAP.md`, `CHANGELOG.md`
- Do NOT include: `build/`, `*.o`, `*.a`, `archive/`, test video files,
  `pm_ls-lR.txt`, `json.hpp` (too large, user already has it)

---

## Architecture — Critical Decisions

### Timestamp Handling
- **ALWAYS use `time_t` epoch for timestamp comparison** — never string
  comparison. Handles midnight crossings and year boundaries correctly.
- `stringToTimestamp()` converts `YYYYMMDD_HHMMSS` filenames to `time_t`
- This is non-negotiable — do not suggest string comparison as an alternative

### Camera Structure
- Dashcam writes to: `<path>/Front/`, `<path>/Rear/`, `<path>/Left/`, `<path>/Right/`
- Filenames: `YYYYMMDD_HHMMSS_X.ts` (video) + `YYYYMMDD_HHMMSS_F_ths.jpg` (thumbnail sidecar)
- Front camera is primary for trip detection
- Other cameras fuzzy-matched within ±5 seconds of Front timestamps
- Optional cameras (e.g. rear not connected): handle **both** empty-directory and
  absent-directory cases gracefully — different cameras behave differently

### CameraProfile / StorageFormat Abstraction — DONE
- `lib/camera_profile.hpp/.cpp`: `CameraSlot`, `CameraProfile` structs; JSON load/save; `d90Default()`
- `TripDetection` consumes the profile; the rest of the pipeline sees a normalized
  description and does not care what brand produced the footage
- `detectTrips()` takes a `CameraProfile` param (defaults to `d90Default()`)
- `activeProfileId` field in `AppSettings` (default `"pruveeo_d90"`)
- `ConfigManager::getCameraProfile()` loads from `~/.config/pathmux/profiles/<id>.json`,
  falls back to `d90Default()` if absent/invalid
- `pm_probe --wizard` detects camera layout and saves profile to disk
- Profile saved to: `~/.config/pathmux/profiles/<sanitized_name>.json`
- Design: filename token is authoritative for camera ID; `scanSubdir` is scan hint only
- `CameraProfile::slots` → renamed `cameraSlots` (Qt macro conflict — permanent)

### Trip Detection
- Gap threshold: configurable, default 900s (15 minutes)
- Duration formula: `tripDur = (lastSegEpoch - firstSegEpoch) + lastDur`
- `segdur` derived from `segments[1].timestamp - segments[0].timestamp`
  (avoids cold-start stub at segments[0])
- ffprobe called once per trip on last segment for `lastDur`
- Stream selection: use `-map 0:d:0` not `-map 0:2` (select by type not index)

### Manifest Storage
- Colocated with footage: `<path>/pm_manifest_<id>.json` (2-char base36 manifest ID)
- Fallback: `~/.config/pathmux/pm_manifest_<id>.json` if footage dir not writable
- `getManifestFilePath()` — write path; calls `ensureManifestId()` to guarantee ID exists
- `lookupManifestFilePath()` — read-only path; returns "" if not in index; transparently
  migrates old `pm_manifest_<sanitized_path>.json` filenames to ID-based names via `fs::rename()`
- `sanitizePath()` retained for migration detection only
- Manifest index: `~/.config/pathmux/manifests.json`
- Blacklisted names: `pathmux`, `manifests`, `lastpath`
- MD5-based integrity checking via `updateManifestMd5()`

### Segment File Paths
- `TripSegment::cameras` map (keyed by camera ID string, e.g. `"front"`, `"rear"`) stores
  **absolute paths** in the manifest (e.g. `/z/srcdash/ex1/Front/20260225_044424F.ts`)
- `TripSegment::thumbs` map similarly stores absolute thumbnail paths
- `Trip::firstThumbs` / `Trip::lastThumbs` maps replace the old 8 named thumbnail fields
- Do NOT prepend `sourcePath` when constructing file paths from these fields —
  the path is already complete. Use the value directly.
- Old manifests with named `front`/`rear`/`left`/`right` fields are migrated transparently
  on read by `config_manager`

### Base36 IDs
- Manifests and trips get two-character base36 IDs (0-9, A-Z)
- Used throughout UI for quick reference: `[G1]`, `6K`, etc.
- Ambiguity between characters handled, 1 vs l, 0 vs O, etc.
- **`MID:TID` colon-separated addressing** — established convention for
  referencing a specific trip across tools (e.g. `CQ:73`). Reserved for
  future batch manager; use consistently in progress output and CLI args.

### GPS Extraction
- **Requires ExifTool 13.51+** — EPEL version 13.10 did NOT work with the D90
  - Users can work with exiftool maintainer should it not work with their camera(s)
- Extracts LIGOGPSINFO binary stream from Pruveeo D90
- One GPS record per second: timestamp, lat, lon, alt, speed, heading, accel
- Altitude from D90 is incorrect (negative values at sea level) — stored but ignored
- Starts scanning first segment to find first sample with GPS lock. Avoids the cold start lag on the GPS
- Format string: `-ee3 -p '$GPSDateTime $GPSLatitude# $GPSLongitude# $GPSAltitude# $GPSSpeed# $GPSTrack# $Accelerometer'`
- Output stored as GeoJSON FeatureCollection: `pm_trip_<ID>_track.geojson`
- GPS lock time: D90 firmware does NOT write records before lock — `gpsLockSeconds` uses
  epoch arithmetic (`filenameToEpoch()` + `gpsTimestampToEpoch()`), not record index
- GPS extraction implemented in `lib/gps_export.cpp` (`Pathmux::extractGps()`)

### GPS Export
- GPX and KML generated from extracted track data (`lib/gps_export.cpp`)
- Preferences menu for kml so the user can select multiple kml options
- `resolveOutputPath()` handles both directory and full filepath input —
  if user types `/path/to/file.kml` it uses that filename, not a directory

### Units
- All values stored internally in metric (km, km/h, m)
- `useImperial` pref toggles display to imperial (mi, mph, ft)
- Format helpers in `ui_helpers.hpp`: `formatDistance()`, `formatSpeed()`, `formatAltitude()`
- `haversineKm()` in `ui_helpers.hpp` for great-circle distance

### UI
- Terminal box drawing uses ASCII: `+`, `=`, `-`, `|`
- `boxWidth()` queries terminal width dynamically, clamps to 65–120
- `innerWidth()` = `boxWidth() - 6`
- `utf8DisplayWidth()` for padding calculations — multi-byte chars (em-dash etc.)
  are 1 display column but 3 bytes; `std::string::size()` gives wrong padding
- `UI::waitEnter()` — bare Enter proceeds, no re-prompt
- `UI::promptLine()` — bare Enter returns default, no `cin >> ws` interference
- Never mix `cin >>` with `getline` — route everything through `readCommand()`
  or `promptLine()` to avoid leftover newline issues

### Cross-Platform Portability
- `lib/compat.hpp` provides shims for: `popen`/`pclose` (Windows → `CREATE_NO_WINDOW`),
  `WEXITSTATUS`, `localtime_r`, `timegm`, `pathBasename`
- `timegm()` is absent in MSVC — shim provided in `compat.hpp`
- POSIX terminal code in `ui_helpers.hpp` is CLI-only; Qt6 GUI bypasses it entirely
- ffmpeg/exiftool dependency story per platform: RPM Fusion (Linux), Homebrew (macOS),
  bundled installer (Windows)

---

## Hard Dependencies

| Tool | Notes |
|---|---|
| g++ | C++17, Alma 9 base |
| ffmpeg/ffprobe | RPM Fusion or static build — NOT in Alma base repos |
| ExifTool 13.51+ | EPEL 13.10 does NOT work with the D90. Verify: `exiftool -ver` |

## Future Dependencies
- Qt6 — Phase 2 GUI (in active development)
- ImageMagick — animated GIF thumbnails

---

## Known Issues / Pending Work

- ~~Man page needs update for `-G` interactive flow, `--validate`, `-t` flags~~ — done v0.9.11
- ~~GPX/KML output default should be manifest directory, not global `defaultExportDir`~~ — fixed v0.9.6a
- ~~`pm_gpsinfo` enhancements~~ — all done (POSIX arg order, `--scan-all-trips`, `gpsLockSeconds`, `MID:TID`)
- ~~`selectTrip()` has unused `mode` parameter~~ — fixed v0.9.5a
- ~~`--clear-cache` / `--clear-stale` UX rework~~ — done v0.9.8
- ~~`manifests_stale.json` archive for pruned entries~~ — done v0.9.8
- ~~CameraProfile abstraction layer~~ — done v1.0.0 (2026-03-21)
- ~~`pm_probe --wizard` trial scan~~ — done (D90 + Cobra confirmed working)
- ~~ffmpeg build progress + ETA display~~ — done v0.9.11a; Qt callback hook ready
- ~~Distribution packaging (RPM/DEB/pkg)~~ — done v1.0.1a; packages posted to GitHub 2026-03-29
- GPS extraction to GeoJSON — architecture done, `lib/gps_export.cpp` exists; GUI integration pending
- Default encoder + HW profile system — CPU-safe default; community HW profile contribution model
- Cross-platform batch jobs — Phase 2. Design fully documented in memory.
- Batch sleep/hibernation inhibit — see `memory/shower_batch_sleep_inhibit.md`
- README public-audience rewrite + packaging audit (RPM/DEB)
- Qt6 GUI polish — see `memory/project_qt6_gui_design.md`; Settings, Manifests menu, wizard shipped in v1.2.0

---

## Git Workflow

- All work on `main` branch
- Commit after each stable version cut
- Commit message format: `"Fix/Add/Update description — PathMux vX.Y.Z (SN NNNNN)"`
- Remote: `git@github.com:Nutball-Labs/PathMux.git` (SSH key auth)
- After committing: `git push origin main`

---

## What "Cut the Tarball" Means

When working in **VSCode + Claude Code**, "cut the tarball" or "cut as X.Y.Z" means:
1. Implement all requested changes
2. Verify `cmake --build` compiles with no errors
3. Note warnings — fix if trivial, add to queue if not
4. Bump SN on all changed files to current high-water mark
5. If this is a new version: increment HWM, update `version.hpp`
6. Commit and `git push origin main`
7. Summarize what changed

No tarball file is produced — `git push` is the delivery mechanism.
Steps 6–7 of the old flow (`produce .tgz`, `present_files`) are skipped.

---

## What "Shower Thought" or "Workout Thought" Means

When a communication is prefaced with either label:
- Thought is to be recorded into knowledge base for the project.
- Add an entry on the ToDo list for the thought.
- Do not take any action at that time.
- Virtual Post-It note stuck on the virtual keyboard.

---

## Phase Status

- **Phase 1 (CLI):** Feature-complete for core functionality; ongoing polish
- **Phase 2 (Qt6 GUI):** Active development — `gui/` directory; see `memory/project_qt6_gui_design.md`
- All work on `main` branch

<!-- SN: 00095 -->
