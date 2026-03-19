# CHANGELOG

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
  SN format (`<!-- SN: -->`). All three SN formats now covered.
- **SN stamps**: `<!-- SN: 00081 -->` added to all `.md` files in the project
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
<!-- SN: 00087 -->
