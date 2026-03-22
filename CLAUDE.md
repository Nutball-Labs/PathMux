# PathMux — Claude Code Project Context

This file is read automatically by Claude Code at session startup.
It contains standing instructions, architecture decisions, and conventions
for working on the PathMux project. Read this before touching any code.

---

## Project Overview

**PathMux** is a C++17 CLI tool for Alma Linux 9.x that scans Pruveeo D90
360° dashcam footage, groups video segments into trips, caches results as
JSON manifests, and extracts/exports GPS tracks. Private GitHub repo at
https://github.com/Nutball-Labs/PathMux — all work on `main` branch.

**Current version:** 1.0.0 (SN 00089)
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
| `cli/ui_helpers.hpp` | Terminal box UI and input helpers (POSIX only); includes format_helpers.hpp |

### Tools (`tools/`) — standalone binaries

| File | Role |
|---|---|
| `tools/pm_gpsinfo.cpp` | Standalone GPS info utility; `--scan-all-trips` batch lock scanner |
| `tools/pm_tripdebug.cpp` | Trip detection debug/inspection tool |

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
- There is one project-wide **high-water mark** SN, currently `00089`
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
- Filenames: `YYYYMMDD_HHMMSS_X.ts` (video) + `YYYYMMDD_HHMMSS_X.jpg` (thumbnail)
- Front camera is primary for trip detection
- Other cameras fuzzy-matched within ±5 seconds of Front timestamps
- Optional cameras (e.g. rear not connected): handle **both** empty-directory and
  absent-directory cases gracefully — different cameras behave differently

### CameraProfile / StorageFormat Abstraction (Planned)
- Camera format detection will be extracted from `trip_detection.cpp` into a
  separate `CameraProfile`/`StorageFormat` layer in the library
- `TripDetection` consumes the profile; the rest of the pipeline sees a normalized
  description and does not care what brand produced the footage
- Separation goal: field bug reports for a new camera layout only require updating
  the detection layer, not touching trip detection logic
- What a profile captures: directory layout, filename pattern, container format,
  number of active cameras, GPS extraction method
- Detection approach: auto-detect by probing SD card structure; hybrid
  (auto-detect + user confirmation) is the long-term target
- `pm_probe --card` is the natural entry point for profile detection
- User support model for unknown layouts: `pm_probe --card <path>` + `ls -alR`
  pasted into a GitHub issue — gives everything needed to add support without
  having the hardware in hand
- See ROADMAP.md "Multi-Brand Dashcam Support" for full architecture and JSON
  profile format spec

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
- `TripSegment.front/rear/left/right` fields store **absolute paths** in the
  manifest (e.g. `/z/srcdash/ex1/Front/20260225_044424F.ts`)
- Do NOT prepend `sourcePath` when constructing file paths from these fields —
  the path is already complete. Use the value directly.
- Basename extraction: `front.rfind('/')` + substring for display-only use

### Base36 IDs
- Manifests and trips get two-character base36 IDs (0-9, A-Z)
- Used throughout UI for quick reference: `[G1]`, `6K`, etc.
- Ambiguity between characters handled, 1 vs l, 0 vs O, etc.
- **`MID:TID` colon-separated addressing** — established convention for
  referencing a specific trip across tools (e.g. `CQ:73`). Reserved for
  future batch manager; use consistently in progress output and CLI args.

### GPS Extraction
- First camera developed **Requires ExifTool 13.51+** — EPEL version 13.10 did NOT work
  - Users can work with exiftool maintainer should it not work with their camera(s)
- Extracts LIGOGPSINFO binary stream from Pruveeo D90
- One GPS record per second: timestamp, lat, lon, alt, speed, heading, accel
- Altitude from D90 is incorrect (negative values at sea level) — stored but ignored
- Starts scanning first segment to find first sample with GPS lock. Avoids the cold start lag on the GPS
- Format string: `-ee3 -p '$GPSDateTime $GPSLatitude# $GPSLongitude# $GPSAltitude# $GPSSpeed# $GPSTrack# $Accelerometer'`
- Output stored as GeoJSON FeatureCollection: `pm_trip_<ID>_track.geojson`

### GPS Export
- GPX and KML generated from extracted track data
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

---

## Hard Dependencies

| Tool | Notes |
|---|---|
| g++ | C++17, Alma 9 base |
| ffmpeg/ffprobe | RPM Fusion or static build — NOT in Alma base repos |
| ExifTool 13.51+ | EPEL 13.10 does NOT work with the camera initially developed from. Verify: `exiftool -ver` |

## Future Dependencies
- Qt6 — Phase 2 GUI
- ImageMagick — animated GIF thumbnails

---

## Known Issues / Pending Work

- ~~Man page needs update for `-G` interactive flow, `--validate`, `-t` flags~~ — done v0.9.11
- ~~GPX/KML output default should be manifest directory, not global `defaultExportDir`~~ — fixed v0.9.6a
- `pm_gpsinfo` enhancements:
  - ~~Fix argument order to [options] \<file.ts\> (POSIX convention)~~ — done v0.9.3
  - ~~`--scan-all-trips`: scan first segment of every trip, report GPS lock time~~ — done v0.9.3
  - ~~Store GPS lock time back into manifest (`gpsLockSeconds` field per trip)~~ — done post-0.9.4
  - ~~`pm_gpsinfo MID:TID` direct addressing~~ — done post-0.9.4
- GPS extraction to GeoJSON not yet implemented (architecture decided, code pending)
- ~~`selectTrip()` has unused `mode` parameter~~ — fixed v0.9.5a
- ~~Duplicate `// SN:` in pm_gpsinfo.cpp header~~ — fixed v0.9.5a
- ~~Interactive manifest browser: bare Enter at "Output directory" prompt~~ — verified clean v0.9.5a
- ~~`--clear-cache` / `--clear-stale` UX rework~~ — done v0.9.8
- ~~`manifests_stale.json` archive for pruned entries~~ — done v0.9.8
- ~~Usage output `Manifest management:` section~~ — done v0.9.8

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

- **Phase 1 (CLI):** Active development — see ROADMAP.md
- **Phase 2 (Qt6 GUI):** Planned, not started
- All CLI work on `main` branch; GUI will branch when CLI is complete


<!-- SN: 00089 -->
