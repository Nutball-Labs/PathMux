# PathMux

Organize your dashcam footage into trips, export GPS tracks, and build
synchronized multi-camera videos — from a desktop GUI or the command line.

PathMux scans your dashcam's SD card (or a copy of it), groups video
segments into trips by timestamp, and caches everything in a compact JSON
manifest so subsequent loads are instant. From there you can browse trips,
extract GPS tracks for mileage logging, export GPX/KML files, or assemble
per-camera MP4 files and 4K collages.

---

## Download

Pre-built packages for the latest release are on the
[GitHub releases page](https://github.com/Nutball-Labs/PathMux/releases/latest):

| Platform | Package |
|---|---|
| Linux — RHEL / Alma / Fedora | `.rpm` |
| Linux — Debian / Ubuntu | `.deb` |
| Linux — generic x86_64 | `.tar.gz` |
| Windows 10/11 | `.msi` installer or `.zip` portable |

All packages require `ffmpeg`/`ffprobe` installed separately
(see [Requirements](#requirements)). ExifTool is required only for GPS
extraction from cameras that embed GPS data.

To build from source instead, see [Building from Source](#building-from-source).

---

## Who it's for

- **Anyone with a multi-camera dashcam** who wants more than a phone app.
- Power users who prefer local tools over cloud upload services.
- **Drivers who want organized trip records and GPS tracks** — whether for
  personal reference, incident documentation, or route review.

> **GPS lock warning**
>
> Most dashcams need 30–120 seconds (sometimes longer) to acquire a GPS fix
> after startup. The GPS track for each trip's first minute or two may be
> incomplete or missing entirely. PathMux reports the lock time per trip
> (`gpsLockSeconds`) so you can see how much data was lost, but it cannot
> recover GPS data the camera never recorded.

---

## Features

### GUI (pathmux-gui)
- **Manifest browser** — left panel lists all footage sources; right panel shows
  trip tiles with thumbnail, date, duration, and GPS status at a glance
- **Trip Properties** — full segment list, GPS tab with one-click extraction and
  GPX/KML/GeoJSON export, Map tab for moving-map MP4 generation, Dashboard tab
  for animated instrument panel video
- **Collage build dialog** — tabbed layout with a live 2×2 quadrant grid; assign
  any camera to any quadrant, mix in map/dashboard MP4s or external video files;
  center overlay for moving map or logo watermark; Preview Frame grabs one frame
  per slot before committing to the full encode
- **Logo morph animation** — "None" quadrant slots and the overlay play a looping
  PathMux ↔ Nutball-Labs cross-fade in the actual built video (not a black frame)
- **Build progress** — per-stage progress rows; concurrent per-camera concat;
  verbose mode streams live ffmpeg output; cancel via the X button at any time
- **Moving map** — GPS-synced overhead map MP4 from the trip's GeoJSON track;
  composited as center overlay on the 4K collage
- **Instrument dashboard** — animated 960×540 panel showing speed, heading, trip
  odometer, and weather conditions (Open-Meteo archive API, no key required);
  dual units (km/h + mph, km + mi, °C + °F)
- **Settings & Setup Wizard** — ffmpeg path, encoder presets (CPU / NVENC / QSV /
  VAAPI), export directory, camera profile, HiDPI scale; wizard re-runs from Tools menu

### CLI (pathmux + tools)
- **Trip detection** — groups video segments into trips by timestamp gap;
  configurable threshold (default 15 minutes)
- **Multi-camera support** — aligns Front, Rear, Left, and Right streams by
  timestamp; handles optional cameras gracefully
- **GPS extraction** — one fix per second via ExifTool; stores lat, lon, speed,
  heading in the manifest
- **GPS export** — GPX, KML, and GeoJSON (RFC 7946) output
- **Video build** — per-camera MP4 concat and synchronized 4K collage via
  ffmpeg; hardware acceleration (NVENC, QSV, VAAPI) configurable
- **Camera profiles** — JSON profiles for different dashcam models; create
  your own with the interactive `pm_probe --wizard`
- **Manifest caching** — scan once, load instantly; MD5 integrity checking
- **Structured output** — `--format=json/csv/xml` and `--fields` filtering
  for scripting and fleet integration
- **Multi-host support** — shared footage library with per-machine encoder
  settings via `--hostprefs`

---

## Supported Cameras

| Camera | Layout | GPS | Status |
|---|---|---|---|
| Pruveeo D90 360° | `Front/` `Rear/` `Left/` `Right/` subdirs, `.ts` | LIGOGPSINFO via ExifTool | ✅ Confirmed |
| Cobra CCDC4500 | Flat single directory, `.MOV`, H.264 | None | ✅ Confirmed (no GPS) |

**Don't see your camera?** See [Adding Camera Support](#adding-camera-support) below.

---

## Requirements

**Platform:** Linux (x86_64), macOS, and Windows. Primary development and
testing is on Linux (Alma 9.x / RHEL 9).

**Build dependencies:**
- g++ with C++17 support (GCC 11+ recommended)
- CMake 3.16+
- Qt6 Widgets (for pathmux-gui; CLI builds proceed without it)
- Python3 + Pillow (for logo morph animation generation at build time;
  a pre-built `logo_morph.mp4` is included so this is optional)

**Runtime dependencies:**
- `ffmpeg` / `ffprobe` — not in base RHEL/Alma repos; install from
  [RPM Fusion](https://rpmfusion.org/) or use a static build
- `exiftool` — for GPS extraction from cameras that embed GPS in footage.
  Version requirements vary by camera GPS format; if extraction fails,
  try updating ExifTool first.
- Python3 + Pillow + requests — for moving map and dashboard generation
  (`scripts/pm_maprender.py`, `scripts/pm_dashboard.py`)

---

## Building from Source

```bash
git clone https://github.com/Nutball-Labs/PathMux.git
cd PathMux
mkdir build-linux && cd build-linux
cmake ..
make
```

Binaries are placed in `build-linux/`:

| Binary | Purpose |
|---|---|
| `pathmux-gui` | Desktop GUI — scan, browse, build video, export GPS |
| `pathmux` | Main CLI — scan, browse, export, build video |
| `pm_probe` | Camera profiler — fingerprint and wizard |
| `pm_gpsinfo` | GPS inspection and batch lock-time scan |
| `pm_gpsexport` | Export GPS tracks from existing manifests |
| `pm_ls` | Quick manifest listing |
| `pm_audit` | Manifest integrity checker |

Qt6 is detected automatically. If not found, `pathmux-gui` is silently
skipped and all CLI tools build normally.

---

## Quick Start (GUI)

Launch `pathmux-gui`. On first run the Setup Wizard opens automatically —
configure your ffmpeg path and encoder preset, then close.

1. **Manifests → Scan Source Directory** (Ctrl+O) — point at your footage root
2. Click a trip tile to select it; double-click to open Trip Properties
3. **Build Video** from Trip Properties to assemble your collage

---

## Quick Start (CLI)

### 1. Profile your camera (first time only)

```bash
./pm_probe --wizard /path/to/sdcard
```

The wizard walks through camera layout, filename format, GPS method, and
timezone. It saves a profile to `~/.config/pathmux/profiles/` and runs a
trial scan to confirm it works.

### 2. Select your profile

```bash
./pathmux --prefs    # [N] Camera profile → pick from list → [S] Save
```

### 3. Scan your footage

```bash
./pathmux -s /path/to/sdcard
```

PathMux detects trips and prints a one-line summary per trip. The manifest
is saved alongside your footage.

### 4. Browse trips

```bash
./pathmux -I        # interactive browser — select manifest → trip → details
./pathmux -t        # quick one-line summary per manifest
./pathmux -T        # full trip list across all manifests
```

### 5. Extract GPS and export

```bash
./pathmux -G        # interactive GPS menu
                    # Select trip → [G] Extract → [X] GPX  [K] KML  [J] GeoJSON
```

### 6. Build video

From the interactive browser (`-I`): select a trip → **[V] Build video**.
Configure cameras, resolution, output directory, then **GO**.

---

## GPS and Mileage Logging

GPS tracks are extracted from the footage on demand (they are not parsed
during the initial scan to keep scanning fast).

```bash
./pathmux -G                       # interactive — pick trip, extract, export
./pm_gpsinfo --scan-all-trips      # batch: scan every trip for GPS lock time
```

The `gpsLockSeconds` field in each trip manifest records how many seconds
elapsed before the first valid GPS fix. Use `pm_gpsinfo --scan-all-trips`
to populate this field across all manifests after a scan.

Exported GPX and KML files open in Google Earth, Google Maps, OsmAnd,
and most mapping applications.

---

## Adding Camera Support

### Try the wizard first

```bash
./pm_probe --wizard /path/to/sdcard
```

The wizard handles subdirectory layouts (one folder per camera) and flat
layouts (all cameras in one directory, distinguished by filename token).
It detects timestamp format, GPS method, and thumbnail handling
automatically where possible.

### If the wizard doesn't produce a working profile

Open a GitHub issue so we can build a profile without needing the hardware
in hand. There are two ways to collect the diagnostic info we need:

**Option A — Collection scripts (easiest, no PathMux required)**

Download the [`Cam_Profile_Detection/`](https://github.com/Nutball-Labs/PathMux/tree/main/Cam_Profile_Detection)
folder, run the script for your OS, and attach the generated `.txt` file to
the issue:

- **Linux / macOS:** `./collect_cam_profile.sh`
- **Windows:** double-click `collect_cam_profile.bat`

The script walks you through each step, runs `ffprobe` and `exiftool`
automatically, and saves everything to a single file.

**Option B — pm_probe (if PathMux is already installed)**

```bash
./pm_probe --card /path/to/sdcard --json > camera_report.json
```

Attach `camera_report.json` to the issue.

---

Open issues at **https://github.com/Nutball-Labs/PathMux/issues** and include
the camera make and model in the title.

---

## Configuration

Settings are stored in `~/.config/pathmux/`:

| File | Purpose |
|---|---|
| `pathmux.json` | Base preferences (all hosts) |
| `pathmux_<hostname>.json` | Host-specific overlay (encoder, paths, UI scale) |
| `manifests.json` | Index of all scanned footage paths |
| `profiles/<name>.json` | Camera profiles |

**Manifests** (`pm_manifest_XX.json`) are stored alongside your footage. One manifest
file covers all platforms — if the same footage is accessible from Linux, Windows, and
macOS (e.g. a NAS), each OS registers its local path in the manifest's `path_map`
section (`os_hostname` key). Segment paths are stored relative to the source root so
the manifest is portable across mount points and drive letters without editing.

**GUI:** all settings are accessible from the Settings dialog (Ctrl+,) or the
Setup Wizard (Tools menu).

**CLI:**
```bash
./pathmux --prefs          # general preferences
./pathmux --encoderprefs   # hardware encoder settings (NVENC, QSV, VAAPI)
./pathmux --hostprefs      # per-machine settings (paths, encoder, output dir)
./pathmux --kmlprefs       # KML visual settings (colors, line width, pins)
./pathmux --locations      # named locations for KML proximity pins
```

---

## Structured Output

```bash
./pathmux -T --format=csv --fields=date,start_time,duration,distance_km
./pathmux -T --format=json
./pathmux -T --format=xml
```

Available fields: `manifest_id`, `trip_id`, `date`, `start_time`,
`start_epoch`, `duration`, `duration_seconds`, `segment_count`, `note`,
`start_lat`, `start_lon`, `end_lat`, `end_lon`, `distance_km`,
`distance_mi`, `gps_lock_seconds`, `gps_track_status`.

---

## Brag Board

Fastest 4K collage builds, ranked by realtime multiplier
(footage duration ÷ encode time — higher is faster).

**Qualifying requirements:** daytime trip, minimum 20 minutes, all four cameras
(Front/Rear/Left/Right), 4K collage build.

To submit: open a GitHub issue with your machine specs, footage duration,
encode time, and encoder used. Find your timings in `pm_buildlog.json` in
your footage source directory after a qualifying build.

| | Machine | Encoder | 4K realtime | Encode time | User |
|---|---|---|---|---|---|
| 🥇 | i7 / RTX 5060 / NVMe | hevc_nvenc | **6.04x** | 6m 58s (42 min trip) | Nutball-Labs |

---

## Development Paradigm

PathMux is the product of a collaboration between a self-described geek with 40+ years
of experience in communications, computer systems, and Linux sysadmin work — and Claude,
Anthropic's AI, which handled the low-level C++ implementation.

The architecture, feature decisions, hardware knowledge, and real-world dashcam testing
are entirely human-driven. Claude translated that domain expertise into C++17 code under
continuous guidance and review. No C++ bits or bytes were harmed in the making of this software.

This project is offered as a demonstration that deep systems knowledge and AI-assisted
implementation can produce production-quality tooling — even when the human half has never
written a line of C++ before.

---

## License

GNU General Public License v3 — see [LICENSE](LICENSE).
Copyright (C) 2026 Nutball Labs / Stephen Berg

---

## Project Status

**Phase 1 (CLI):** Feature-complete for core functionality; ongoing polish.

**Phase 2 (Qt6 GUI):** Actively shipping. The GUI covers all major workflows:
scan, browse, GPS extraction and export, moving map generation, instrument
dashboard, and video build with full collage layout control.

See [ROADMAP.md](ROADMAP.md) for the full plan.

<!-- SN: 00102 -->
