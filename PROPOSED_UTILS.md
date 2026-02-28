# PathMux — Proposed Utility Tools

This document describes utility tools planned for the `pm_*` suite. These are
not yet implemented. Each tool is a standalone binary that links against
`libpathmuxlib` and follows the conventions established by `pm_gpsinfo`.

---

## `pm_ls` — Non-Interactive Trip Lister

**Purpose:** Scriptable, pipe-friendly alternative to `pathmux -t`. Emits one
line per trip to stdout without entering any interactive flow. Suitable for
use in shell scripts and pipelines.

**Usage:**
```
pm_ls                    List all manifests and their trips (summary)
pm_ls MID                List trips for a specific manifest
pm_ls MID:TID            Show details for a single trip
pm_ls --json             JSON output (all manifests)
pm_ls --json MID         JSON output for one manifest
```

**Output (default, one line per trip):**
```
F0  4P  2026-02-26  08:40:09  19m 14s   7 segs  8.9 km
F0  9T  2026-02-26  10:06:08   6m 37s   3 segs  2.0 km
F0  3V  2026-02-26  10:36:28   3m 37s   2 segs  0.7 km
```

**Implementation notes:**
- Loads index via `ConfigManager::loadManifestIndex()`
- Loads trips via `ConfigManager::loadTripCache()`
- MID:TID resolution via the same `resolveMidTid` pattern as `pm_gpsinfo`
- No interactive prompts; all output to stdout; errors to stderr
- JSON mode emits the full trip vector as a JSON array — feeds `jq` pipelines

---

## `pm_audit` — Footage Integrity Checker

**Purpose:** Walk all manifests and verify that the source footage files are
still accessible and unmodified. Catches common problems: drive not mounted,
SD card swapped, files deleted or corrupted. Gives a per-trip pass/fail
report without touching the manifests themselves.

**Usage:**
```
pm_audit                 Audit all manifests (validation file MD5 check)
pm_audit MID             Audit a single manifest
pm_audit MID:TID         Audit a single trip
pm_audit --deep          Also run ffprobe on every segment (slow — detects
                         truncated or corrupt video files)
pm_audit --json          Machine-readable output
```

**Output (default):**
```
[F0]  /z/srcdash/ex1
  4P  OK       2026-02-26 08:40:09  7 segs
  9T  OK       2026-02-26 10:06:08  3 segs
  3V  MISSING  2026-02-26 10:36:28  2 segs  (drive not mounted?)

1 of 3 trips have problems.
```

**Implementation notes:**
- Reads validation files from `Trip::validationFiles` (3 MD5-checked samples
  per trip, selected at scan time and stored in the manifest)
- `--deep` adds an ffprobe pass per segment to catch truncated files; expected
  to be slow on large collections — warn the user before starting
- Exit code 0 = all OK; exit code 1 = one or more failures (scriptable)
- Does not write to any manifest; read-only

---

## `pm_gpsexport` — Non-Interactive GPS Track Exporter

**Purpose:** Scriptable GPS track export. Extracts and exports a trip's GPS
track to GPX or KML without navigating the interactive menus in `pathmux -G`.
If the track is already extracted and stored in the manifest, it converts
directly. If not, it runs ExifTool extraction first, stores the result, then
exports.

Since format is specified at export time, this makes a separate `pm_convert`
tool redundant — re-exporting in a different format is just another invocation.

**Usage:**
```
pm_gpsexport MID:TID --gpx output.gpx
pm_gpsexport MID:TID --kml output.kml
pm_gpsexport MID:TID --gpx output.gpx --kml output.kml   (both at once)
pm_gpsexport MID:TID --gpx /output/dir/   (auto-name: YYYYMMDD_HHMMSS.gpx)
pm_gpsexport MID:TID --force              (overwrite existing output)
```

**Behavior:**
1. Resolve MID:TID via `ConfigManager`
2. Check `trip.gpsTrackStatus` — if `"complete"`, skip to step 4
3. Run ExifTool extraction on all Front segments; store track in manifest
   via `ConfigManager::saveTripCache()`
4. Write GPX 1.1 / KML 2.2 from the stored `trip.gpsTrack`

**Implementation notes:**
- Re-uses the GPX and KML writer logic from `cli/gpx_export.cpp` — that code
  should be factored into `libpathmuxlib` before this tool is built, otherwise
  it would duplicate the writers
- `--gpx` and `--kml` may be combined in one invocation (single extraction
  pass, two output files)
- Progress to stderr; exported file paths to stdout (pipeable)
- Depends on GPS extraction to GeoJSON being implemented in the library first

---

## `pm_probe` — Camera Compatibility Profiler

**Purpose:** Two-mode diagnostic tool for understanding what a dashcam
produces. In single-file mode, it probes a `.ts` (or `.mp4`, etc.) segment
and reports its video characteristics and GPS metadata format. In SD card
mode, it fingerprints an entire dashcam storage root — directory structure,
filename patterns, segment lengths, GPS method — and produces a structured
report suitable for filing a GitHub issue to request support for a new camera.

This is the primary path for community contribution of new camera profiles.
A user with an unsupported dashcam runs `pm_probe --card /media/dashcam/` and
pastes or attaches the output to a GitHub issue. The developer gets everything
needed to build a camera profile without access to the hardware.

**Usage:**
```
pm_probe <file.ts>               Probe a single segment (video + GPS metadata)
pm_probe MID:TID                 Probe first Front segment of a known trip
pm_probe --card /media/dashcam/  Fingerprint a full SD card / dashcam root
pm_probe --card /media/dashcam/ --json   Machine-readable fingerprint
```

**Single-file output (text):**
```
File:         /z/srcdash/ex1/Front/20260226_084009F.ts
Container:    MPEG-TS (.ts)
Resolution:   1920x1080
Frame rate:   25/1
Pixel format: yuvj420p  (full-range)
Color space:  bt709
Streams:      video(0) audio(1) data(2)
GPS method:   LIGOGPSINFO (stream index 2) — ExifTool 13.51+ required
GPS sample:   2026:02:26 14:40:11  lat=30.418872  lon=-89.025238
```

**SD card fingerprint output (text):**
```
--- pm_probe SD Card Fingerprint ---
Root:       /media/dashcam
Dirs found: Front/  Rear/  Left/  Right/
Extensions: .ts  .jpg
Filename pattern (sample):
  Front/20260226_084009F.ts
  Front/20260226_084009F.jpg
Segment lengths observed: 180s 181s 37s (short clip at end)
Video profile (from first Front segment):
  1920x1080  25fps  yuvj420p  bt709  full-range
GPS method: LIGOGPSINFO (ExifTool 13.51+)

--- Submit this output to: https://github.com/BiloxiGeek/PathMux/issues ---
```

**Implementation notes:**
- Video probing: calls `ffprobe` (same path as `ConfigManager::getFfprobePath()`)
  and parses stream info — re-uses or mirrors the `VideoProfile` probe in
  `trip_detection.cpp`
- GPS detection: calls ExifTool with `-liststreams` or a minimal `-ee3` pass
  to identify what GPS metadata is present and in what format
- `--card` mode: walks the root directory, samples up to 5 segments per camera
  directory, infers filename pattern by regex, measures segment lengths,
  reports everything in a format a developer can act on directly
- Does not require an existing manifest — works on a freshly inserted SD card
  before any `pathmux` scan has been run

---

## Implementation Priority

| Tool | Value | Effort | Depends On |
|---|---|---|---|
| `pm_ls` | High — scripting gap | Low | Nothing new |
| `pm_probe` | High — community growth | Medium | ffprobe call (already in lib) |
| `pm_audit` | Medium — ops/maintenance | Low | ValidationFile already in Trip |
| `pm_gpsexport` | Medium — scripting gap | Medium | GPS extraction in lib first |
