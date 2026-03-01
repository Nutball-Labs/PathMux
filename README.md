# PathMux Dashcam Manager

PathMux is a lightweight C++ tool designed to organize and manage multi-camera dashcam footage. It automatically groups related video segments into "Trips" and provides both human-readable trees and machine-readable JSON manifests.

## Key Features

- **Smart Path Discovery**: Just run `./pathmux -s <path>`. It will find dashcam footage and analyze the data for distinct trips.
- **Fast Caching**: Scans once and saves a manifest in `<path>` for instant subsequent loads.
- **Multi-Camera Support**: Automatically aligns Front, Rear, Left, and Right camera segments by timestamp.
- **Ignore Hidden**: Automatically ignores hidden dot files (`.` files).
- **GPS Extraction**: Extracts per-second GPS tracks via ExifTool 13.51+ including lat, lon, altitude, speed, heading, and accelerometer data.
- **GPS Export**: Exports tracks to GPX, KML, or GeoJSON (FeatureCollection, one feature per track point).
- **Base36 IDs**: Manifests and trips get short two-character base36 IDs for quick reference.
- **Crow's Distance**: Haversine displacement calculation between trip start and end points.
- **Metric/Imperial**: All values stored in metric; display toggleable to imperial in preferences.
- **Manifest Validation**: MD5-based manifest integrity checking with `--validate` for cron use.

## Requirements

- Alma Linux 9.x (or compatible RHEL 9 derivative)
- g++ with C++17 support
- CMake 3.16+
- ffmpeg/ffprobe (RPM Fusion or static build)
- ExifTool (minimum version is camera-dependent; see GPS Extraction note below)

## Build

```bash
mkdir build && cd build
cmake ..
make
```

## Quick Start

1. **Compile**: `cmake .. && make` (from a `build/` subdirectory)
2. **Run**: `./pathmux` Shows usage.
3. **Scan**: `./pathmux -s <path>` Scans path for dashcam videos and runs trip detection routines.
4. **List Manifests**: `./pathmux -t`
5. **Full Tree**: `./pathmux -T` Shows manifests and the trips contained in each.
6. **Interactive Browser**: `./pathmux -I`
7. **GPS Menu**: `./pathmux -G`
8. **Validate Manifests**: `./pathmux --validate` (exit 0=ok, 1=problems)

## GPS Workflow

```bash
./pathmux -G        # Opens interactive GPS menu
                    # Select manifest → select trip → [G] Extract GPS to file
                    # Produces pm_trip_<ID>_track.geojson colocated with manifest
                    # Then [X] export GPX or [K] export KML as needed
```

> **Note**: The minimum ExifTool version required for GPS extraction depends on your camera hardware. For the Pruveeo D90 specifically, version 13.51+ is required — the EPEL package (13.10) does not correctly decode the D90's LIGOGPSINFO binary stream. Other cameras may work with older versions. If GPS extraction fails, check your ExifTool version with `exiftool -ver` and consider updating.
>
> If updating ExifTool does not resolve extraction for your camera, run `pm_probe --card <path>` to generate a structured camera fingerprint report, then open an issue on this repo with the report attached. In some cases the ExifTool maintainer may need sample footage segments to analyze the embedded GPS stream format — the issue template will guide you through what to provide and how to submit it.

## Manifest Location

Manifests are stored colocated with the footage:

```
/path/to/footage/pm_manifest_<id>.json
```

If the footage path is not writable, falls back to `~/.config/pathmux/`. The manifest index at `~/.config/pathmux/manifests.json` tracks all known manifests.

## Camera Support

**Confirmed Working**

- **Pruveeo D90 360°** — developed and tested on this camera. Produces MPEG-2 Transport Stream (`.ts`) segments in `Front/`, `Rear/`, `Left/`, `Right/` subdirectories with `YYYYMMDD_HHMMSS_X.ts` filename convention. GPS extraction via ExifTool 13.51+ from embedded LIGOGPSINFO stream.

**Adding Support for Other Cameras**

If you have a dashcam that PathMux doesn't recognize, the following information would allow analysis of the footage format for inclusion in trip detection:

1. **Directory listing** — run `find /path/to/footage -type f | sort` and share the output. Shows the directory structure and filename conventions.

2. **Sample filename** — a few representative filenames from each camera channel, e.g. `20260225_135653F.ts`. The timestamp encoding and channel suffix conventions vary between manufacturers.

3. **File format probe** — run the following on one segment from each channel:
   ```bash
   ffprobe -v quiet -print_format json -show_format -show_streams yourfile.ts
   ```

4. **GPS tag dump** — if your camera embeds GPS data, run:
   ```bash
   exiftool -ee3 -G1 -a -s yourfile.ts | grep -i gps | head -40
   ```
   This shows what GPS fields are present and their format. ExifTool 13.51+ is required for some camera formats.

5. **Segment duration** — typical segment length in seconds (60, 120, 180, 300 are common).

6. **Gap behavior** — does the camera produce a short stub segment at startup before the first full-length segment? If so, approximately how long is it?

Open an issue on GitHub with the above information and I'll analyze the format and add detection support.

## Project Status

Active CLI development (Phase 1). Qt6 GUI planned for Phase 2.

## License

Private repository — all rights reserved.
