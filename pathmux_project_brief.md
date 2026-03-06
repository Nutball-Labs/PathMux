# QuadEye Dashcam Explorer — Project Brief
## For use at the start of new AI sessions to restore context quickly

---

## What It Is
A C++17 command-line tool for Linux that scans dashcam footage directories,
groups video segments into trips, caches the results as JSON manifests, and
provides both summary and interactive browsing of those trips.

Target hardware: Pruveeo D90 360-degree dashcam (4 cameras: Front, Rear, Left, Right).
Target OS: Alma Linux 9.x (RHEL 9 based).
Future: Qt6 GUI planned after CLI is rock solid.

---

## Developer Background
- Experienced Perl, Python3, bash/DOS scripting; Linux sysadmin for government entity.
- C/C++ is new territory — using AI to write C++ from workflow prompts.
- Full root on Alma Linux 9.x dev machine. VSCode + vim. Private GitHub repo.
- Repo is synced to v0.6.0k. All CLI work commits directly to main.
- Qt GUI work will branch from main when CLI is complete.

---

## Source Files
| File | Role |
|---|---|
| `main.cpp` | CLI argument parsing, orchestration |
| `trip_detection.cpp/.hpp` | Filesystem scan, trip grouping, ffprobe calls |
| `config_manager.cpp/.hpp` | JSON manifest read/write, cache management |
| `find_trips.cpp/.hpp` | Display: summary, details, interactive browser |
| `gpx_export.cpp/.hpp` | GPS track export to GPX format |
| `version.hpp` | Version string built from components via macros |
| `json.hpp` | nlohmann/json v3.11.3 (single header, already present) |
| `Makefile` | Build, archive, sn-audit targets |

---

## File Tracking Convention
Every source file carries a serial number comment at the bottom:
`// SN: 00008`
The Makefile `sn-audit` target collects these into `sn_audit.txt` on every build.
`main.cpp` and `version.hpp` carry the project-wide high-water mark SN.
Files changed in a build get their SN bumped to the new high-water mark.

---

## Key Design Decisions

### Trip Detection (trip_detection.cpp)
- Dashcam writes segments to: `<path>/Front/`, `<path>/Rear/`, `<path>/Left/`, `<path>/Right/`
- Each segment: `YYYYMMDD_HHMMSS_X.ts` video + `YYYYMMDD_HHMMSS_X.jpg` thumbnail sidecar
- Timestamps converted to `time_t` epoch for comparison — handles midnight/year-boundary edge cases correctly. Do not replace with string comparison.
- Front camera is primary: drives trip detection. Other cameras fuzzy-matched within ±5s.
- ffprobe runs ONCE per trip on first segment → `segdur` (integer seconds: 60, 120, 180, or 300)
- ffprobe runs ONCE per trip on last segment → `lastDur` (integer seconds, usually < segdur)
- Trip duration formula: `tripDur = (segdur * (segCount - 1)) + lastDur`
- Gap threshold for new trip: `segdur + 30` seconds
- ffprobe is a hard dependency — no fallback. User is responsible for installation.
- ImageMagick is a future dependency for animated GIF thumbnails (not yet implemented).

### Application Settings (config_manager.cpp)
- Stored in `~/.config/quadeye/quadeye.json` (distinct from trip manifests)
- Loaded on startup; missing or corrupt file silently replaced with defaults
- `--clear-cache` preserves this file — only trip manifests are wiped
- Current settings:
  - `gapThresholdSeconds` (default 900 = 15 minutes) — configurable via `--prefs gap=<seconds>`
  - `fuzzyWindowSeconds` (default 5) — camera timestamp match tolerance
  - `schemaVersion` (currently 1) — for future migration
- `segdur` is NOT a setting — determined per-trip by ffprobe, stored in each trip manifest

### Manifest Cache (config_manager.cpp)
- Cache files: `~/.config/quadeye/<sanitized_path>.json`
- Path sanitization: strip leading `/`, replace `/` with `_`
- Format: valid JSON using nlohmann/json (NOT pipe-delimited — a previous AI engine
  introduced pipe-delimited format without disclosure; that was corrected back to JSON)
- Old pipe-delimited files: hard fail with warning, user must rescan. No migration code.
- Blacklisted filenames: `quadeye`, `manifests`, `lastpath` (prevent shadowing internal files)

### Per-Trip JSON Schema
```json
{
  "source_path": "/media/usb/dashcam",
  "trips": [
    {
      "date": "2026-02-16",
      "start_time": "08:43:12",
      "segdur": 180,
      "tripDur": 1394,
      "firstThumb": "/media/usb/dashcam/Front/20260216_084632_A.jpg",
      "lastThumb":  "/media/usb/dashcam/Front/20260216_090846_A.jpg",
      "thumbGif":   "",
      "segments": [
        {
          "timestamp": "20260216_084312",
          "front": "/path/Front/20260216_084312_A.ts",
          "back":  "/path/Rear/20260216_084315_A.ts",
          "left":  "/path/Left/20260216_084313_A.ts",
          "right": "-",
          "frontThumb": "/path/Front/20260216_084312_A.jpg",
          "backThumb":  "/path/Rear/20260216_084315_A.jpg",
          "leftThumb":  "/path/Left/20260216_084313_A.jpg",
          "rightThumb": "-"
        }
      ]
    }
  ]
}
```

### Thumbnail Logic
- Each `.ts` segment has a sidecar `.jpg` with identical base filename
- `firstThumb`: Front camera `.jpg` from second segment (index 1), or first if only one segment
- `lastThumb`: Front camera `.jpg` from last segment
- `thumbGif`: empty string placeholder — animated GIF (firstThumb + lastThumb cycling slowly)
  to be generated as background task by Qt GUI layer. Field reserved now, zero scan overhead.
- Per-segment thumbnail paths stored for all four cameras for future GUI use

### Future Merge Feature (not yet implemented)
- Qt GUI will show trips as tiles with firstThumb/lastThumb animation
- User can select two adjacent trips and merge them
- Merge rewrites cache manifest: concatenates segments, sums tripDur values, records gap duration
- Mixed segdur across merged trips is handled cleanly because tripDur is pre-calculated
- Undo: simply rescan the path to rebuild from scratch

### CLI Interface
```
./quadeye [options] [path]
  -s, --scan <path>         Force scan a directory
  -p, --path <path>         Use cache if available, else scan
  -t, --trips               Show trip summary (default)
  -T, --alltrips            Show full tree for all cached manifests
  -l, --list                List all cached manifests
  -I, --manifests           Interactive manifest browser
  -i, --inter               Interactive trip browser for lastPath
  -R, --refresh             Refresh manifest for current path
  -G, --gpx [path]          Export GPS track as GPX file
  -o, --outdir <dir>        Output directory for -G (default: .)
  -P, --prefs               Show current settings
  -P, --prefs gap=<seconds> Set gap threshold and save
      --clear-cache         Wipe all cached manifests (preserves settings)
  -v, --version             Show version info
  -h, --help                Show help
```

---

## Known Issues / Pending Work
- ~~`getenv("HOME")` has no null guard~~ — **FIXED** (exits cleanly with error message)
- ~~`-s/--scan` does not check that next argv isn't another flag~~ — **FIXED**
- ~~`stringToTimestamp` should be in anonymous namespace~~ — **FIXED**
- ~~Gap threshold hardcoded~~ — **FIXED** (configurable via `--prefs gap=<seconds>`, default 900s)
- `std::localtime` not thread-safe (not urgent until parallelism is added)
- Interactive manifest browser breaks beyond 26 cached paths (fix pending)
- Path round-trip reconstruction is lossy if original path contained underscores
- trip_detection.cpp ffprobe integration not yet written (next major task)

---

## Dependencies
| Tool | Required | Notes |
|---|---|---|
| g++ | Yes | C++17, available in Alma 9 base |
| ffmpeg/ffprobe | Yes | Hard dependency, not in base repos — RPM Fusion or static build |
| ImageMagick | Future | For thumbGif generation, available in Alma 9 base repos |
| Qt6 | Future | For GUI, available in Alma 9 |

---

## Current Version
0.6.0p (SN 00012)
Previous stable: 0.6.0k (tagged in GitHub)

<!-- SN: 00081 -->
