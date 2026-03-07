# PathMux Dashcam Explorer — Development Roadmap

This document outlines planned features and future development direction for PathMux. Items are organized by development phase and priority.

---

## Phase 1: CLI Foundation (Current Focus)

### Trip Detection & Caching
- [x] Filesystem scan of Front/Rear/Left/Right camera directories
- [x] Timestamp-based trip grouping with configurable gap threshold
- [x] JSON manifest caching in ~~`~/.config/quadeye/`~~ `~/.config/pathmux/`
- [x] **ffprobe integration for accurate trip duration calculation**
  - `probeSegmentDuration()` called on last Front segment in `closeTrip()`
  - `probeVideoProfile()` called on first Front segment
  - Formula: `tripDur = (lastEpoch - firstEpoch) + lastDur`; fallback to
    timestamp estimate + 180s if ffprobe unavailable
- [x] Thumbnail detection (`.jpg` sidecar files — v0.9.9)
  - Per-segment: `frontThumb`, `rearThumb`, `leftThumb`, `rightThumb` in `TripSegment`
    (absolute path to .jpg sidecar, or "" if absent)
  - Trip-level: `firstFrontThumb`, `lastFrontThumb`, `firstRearThumb`, `lastRearThumb`,
    `firstLeftThumb`, `lastLeftThumb`, `firstRightThumb`, `lastRightThumb` in `Trip`
  - `first*` fields use segments[1] to avoid cold-start frames (garage door etc.);
    falls back to segments[0] on single-segment trips
  - `last*` fields always from segments.back()
  - All 8 trip-level + 4×N per-segment fields serialized to manifest JSON

### pm_gpsinfo Standalone Utility
**Status:** Active development alongside main CLI

- [x] Single-file GPS inspection: `pm_gpsinfo [options] <file.ts>`
  - JSON, CSV, XML, text output formats
  - `--first-lock` (default) and `--all` record modes
- [x] **`--scan-all-trips` batch mode** (v0.9.3): Reads all manifests from
  `~/.config/pathmux/manifests.json`, scans first Front segment of every trip,
  reports seconds-to-GPS-lock in a labeled column table (MID, TID, SegDur, LockAt,
  First Segment). Handles inaccessible footage gracefully (`!file`).
- [x] **`MID:TID` addressing convention** established (v0.9.3) — colon-separated
  manifest:trip ID syntax used in progress output; reserved for future batch manager
- [x] **`gpsLockSeconds` write-back** (post-0.9.4): `--scan-all-trips` now stores
  seconds-to-first-lock in the manifest (`Trip::gpsLockSeconds`); `saveTripCache`
  path maintains MD5 integrity automatically
- [x] **`pm_gpsinfo MID:TID` direct addressing** (post-0.9.4): invoke single-file
  GPS inspection by manifest:trip ID — no need to specify the segment file path

---

### GPS Data Extraction

**ExifTool status:** Phil Harvey fixed LIGOGPSINFO coordinate decode in response to GitHub issue.
ExifTool 13.51+ released and working. PathMux does not validate the ExifTool version at
runtime — if GPS extraction returns no data, the user contacts the ExifTool maintainer directly.

**One GPS record per second** from LIGOGPSINFO binary stream:
fields: timestamp, lat, lon, speed, heading/track. Altitude present but incorrect (negative
values on D90) — stored but ignored. Speed in km/h.

**Scan-time GPS (during trip detection):**
- [x] `startLat/Lon` and `endLat/Lon` extracted during trip scan and stored in manifest
- [x] `gpsLockSeconds` — seconds to first valid fix, populated by `pm_gpsinfo --scan-all-trips`
- [x] `firstLockLat/Lon/Timestamp/Record` — first fix with non-zero coordinates

**Full track extraction (user-initiated):**
- [x] Full GPS track extraction — one point per second via ExifTool; stored in manifest
  `gpsTrack` array; `gpsTrackStatus` tracks none/complete per trip
- [x] GPX export (`[X]` in `-G` flow)
- [x] KML export (`[K]` in `-G` flow) with visual prefs
- [x] GeoJSON export (`[J]` in `-G` flow, RFC 7946 FeatureCollection)

**What might be done in the future:**
- Pull altitude data from a publically accessible database and rewrite the manifest with accurate
  data.  Would be off for any sample on a bridge or over water but close enough to be overly detailed
  to our purposes.  Have to be mindful of too much traffic to the outside resource and it should be
  an optional process triggered by the user on purpose and then run in a background thread.
---

### Completed Infrastructure

- [x] **Library restructure** (v0.9.4) — `libpathmuxlib` (static) + `pathmux` CLI + `tools/`
- [x] **Manifest ID-based filenames** (v0.9.7a) — `pm_manifest_<id>.json`; transparent migration
- [x] **Stale manifest archive** (v0.9.8) — `manifests_stale.json`; `--show-stale` / `--clear-stale`
- [x] **Manifest management UX** (v0.9.8) — `Manifest management:` help section; `--force` order-independent
- [x] **Per-camera thumbnails** (v0.9.9) — `TripSegment` + `Trip` thumb fields; cold-start skip logic

---

### Phase 1 Active Work

**Must ship before v1.0 (gate items):**
- [ ] **License decision** — GPL vs MIT; CMakeLists.txt already has GPL-2.0 as a
  placeholder — confirm or change. Apply header to all source files; add `LICENSE`
  to repo root; update README. Repo is private — no urgency until public release.
- [ ] **README refresh** — currently documents internal state; needs public-audience rewrite
- [ ] **Packaging audit** — verify architecture doesn't block RPM/DEB (see Packaging section)

**Camera profile abstraction:**
- [ ] Extract camera format detection from `trip_detection.cpp` into a
  `CameraProfile`/`StorageFormat` layer; `TripDetection` consumes it; rest of
  pipeline sees normalized output. Enables per-camera bug fixes without touching
  trip detection. See "Multi-Brand Dashcam Support" section for full spec.

**Man page:**
- [x] Updated `pathmux.1` to v0.9.9 — `-s` scan prompt, GeoJSON in `-G` flow,
  `Manifest Management` subsection, `--show-stale` / `--clear-stale`, ExifTool
  format string corrected (7 fields), `manifests_stale.json` in FILES

**pm_* utility suite** (see PROPOSED_UTILS.md for full specs):
- [x] `pm_gpsexport` — export GPS track to GPX/KML; `--gpx` / `--kml` runtime flags
- [x] `pm_ls` — quick manifest listing; `pm_audit` — integrity check across all manifests
- [x] `pm_probe` — camera fingerprinting tool; single-file mode + `--card` SD card report
- [x] Rename `trip_debug` → `pm_tripdebug` (binary, CMakeLists target, source, man page)

**CLI polish:**
- [ ] `--format=[json,csv,xml]` — replaces `--jsondump`/`--csvdump`/`--xmldump` as a
  serialization modifier on `--dump` / `--fulldump`
- [ ] `--dump`/`--fulldump` field filtering (`--fields id,date,duration`)
- [ ] `recordingProfile` config field — part of CameraProfile implementation; see "Multi-Brand Dashcam Support" section
- [ ] `extra_hw_frames` CPU encoder guard

**Known deferred (low priority, no blocker):**
- `std::localtime` not thread-safe — irrelevant until parallelism added
- Path reconstruction lossy if source path contains underscores — edge case; design
  deferred post-library-restructure (already done — revisit if it surfaces)

---

## Phase 2: Qt6 GUI Development

### Tile-Based Trip Browser
- Main window displays trips as tiles in a scrollable grid
- Each tile shows:
  - **Left side:** Animated thumbnail (`firstFrontThumb` ↔ `lastFrontThumb` cycling slowly, ~2-3 second loop; per-camera data available for richer previews)
  - **Right side:** Trip metadata
    - Date and start time
    - Trip duration (`tripDur` from manifest)
    - Number of segments
    - Start/end coordinates (if GPS available)
  - **Bottom of tile:** Selection controls
    - Checkbox for each camera (Front/Rear/Left/Right)
    - Collage options: 2x2 grid, picture-in-picture, side-by-side
    - Output resolution: 1080p, 4K, or both
    - "Build Video" button

### Trip Management
- **Merge trips:**
  - Select two or more adjacent trips
  - Choose merge style:
    - **Hard merge:** Direct concatenation, no transition
    - **Separator merge:** Transition sequence between trips
      - Last frame of Trip A holds as still (all cameras)
      - Fade to black (synchronized across cameras)
      - PathMux logo animation/spin (all cameras)
      - Fade to first frame of Trip B
      - Resume playback
      - **Camera sync handling:** Cameras with fewer frames start fade early, show logo during buffer period to keep all outputs same length
  - Merged trip rewrites manifest: concatenates segments, sums `tripDur`, records gap duration
  - Undo: rescan path to rebuild from scratch
- **Split trips:**
  - User selects a trip and chooses segment boundary to split at
  - Creates two separate trip entries in manifest
  - Non-destructive to original footage
- **Refresh/rescan:** Force re-detection of trips for a given path


### Camera Synchronization During Collage Generation

**Critical requirement:** Multi-camera collages must maintain frame sync across the entire trip, not just per-segment.

**Problem:** Dashcam startup lag causes frame count differences between cameras in each segment:
- Front camera: 1803 frames in segment
- Rear camera: 1798 frames in segment (started 5 frames late)
- Left camera: 1801 frames
- Right camera: 1800 frames

If segments are naively concatenated without compensation, sync drift accumulates:
- After 1 segment: 5 frames out of sync (~0.2 seconds at 25fps)
- After 10 segments: 50 frames out of sync (~2 seconds)
- After 100 segments (long trip): 500 frames out of sync (~20 seconds)

**Solution:** Frame count normalization during collage generation:
1. For each segment, determine max frame count across all 4 cameras
2. Cameras with fewer frames get padded with:
   - Last frame held as still (freeze frame), OR
   - Black frames, OR
   - Smooth slow-motion stretch of final second
3. All cameras output exactly the same frame count per segment
4. Concatenation maintains perfect sync across entire trip

**Implementation notes:**
- This applies to ALL collage generation, not just separator merges
- Padding is typically 1-2 seconds maximum per segment
- User should not notice padding on normal playback (brief freeze at segment boundaries)
- Alternative: Use audio as sync reference and drop/duplicate video frames to match
- ffmpeg can handle this with `-vsync` and `-af apad` flags

**Priority:** High — without this, long trips become unwatchable due to A/V desync

### Video Export
- Multi-camera collage generation using ffmpeg
- User-selectable layouts:
  - 2x2 grid (quad view)
  - Picture-in-picture (main + 3 small)
  - Side-by-side comparisons
  - Single camera full-screen
- Output resolution options: 1080p, 4K
- Optional: Apply transitions (separator merge style) between segments
- Background processing with progress indicator
- Queue multiple export jobs

### Batch Mode — Collage Layout Source Selection (Shower Thought 2026-03-05)

When implementing batch/CLI mode, collage quadrant assignment should use named
positional flags mapping stream sources to frame quadrants:

```
--UL=front     upper-left  → front camera stream
--UR=rear      upper-right → rear camera stream
--LL=rear      lower-left  → rear camera stream
--URLR=map     right half (full height) → moving map
```

Quadrant flags:
- `--UL`   — upper-left
- `--UR`   — upper-right
- `--LL`   — lower-left
- `--LR`   — lower-right
- `--ULLL` — left half (full height, UL + LL)
- `--URLR` — right half (full height, UR + LR)
- `--ULUR` — top half (full width, UL + UR)
- `--LLLR` — bottom half (full width, LL + LR)

Stream source names (TBD): `front`, `rear`, `left`, `right`, `map`, etc.

**Panoramic / dashcam note:** If the input device is a dashcam with a panoramic
(180°+) camera, both `--LL` and `--LR` (or the combined `--LLLR`) could be fed
from a single wide stream — the compositor handles letterboxing or cropping rather
than requiring two separate streams. Same logic applies to `--ULLL` / `--ULUR` for
wide cameras covering a full half.

This design generalizes to N-up layouts without requiring a different flag scheme
per layout — the flags describe the destination slot, not a fixed layout template.

---

### Smart Collage — Points of Interest and Variable Speed (Shower Thought 2026-03-05)

When cameras and moving map are integrated, the collage builder should support
an **incident/POI timeline** that drives variable-speed playback:

**Quadrant + map assignment:**
- User assigns each stream (front, rear, left, right, map) to a collage quadrant
  using the existing `--UL`/`--UR`/`--LL`/`--LR` flag scheme (or GUI equivalent)
- Moving map occupies one or more quadrants alongside camera streams
- Configuration saved per-collage or as a named layout preset

**Point of Interest (POI) timestamps:**
- User marks timestamps where something of interest occurs
- Each POI can have: label/caption, attached photos, attached supplemental video clips
- POIs stored in the manifest or a sidecar alongside the trip data

**Variable speed playback:**
- **Trip start:** begins at real-time (1x), then **ramps up** fairly quickly to
  timelapse speed — gives the viewer context before the fast-forward kicks in
- Between POIs: collage plays at **timelapse speed** (automatically calculated — see below)
- Approaching a POI: **speed ramp down** to normal or slow-motion
- During POI: **normal/slow-motion playback** with caption overlay
- After POI: **speed ramp up** back to timelapse
- At end of trip: slow down naturally (parking, garage, etc.)
- Ramp timing (opening ramp, approach/exit ramp length) is user-configurable

**Target-duration mode (Shower Thought 2026-03-05 addendum):**

Rather than the user specifying a timelapse multiplier, they specify a **target
output duration** (e.g., "I want a 4-minute video") and the system calculates
the required timelapse speed automatically:

```
total_trip_source       = known from manifest
poi_source_time         = sum of each POI's source footage window
ramp_source_time        = sum of all ramp-in/ramp-out source windows
timelapse_source_time   = total_trip_source - poi_source_time - ramp_source_time

poi_output_time         = sum of POI playback durations (real-time or slo-mo)
ramp_output_time        = sum of ramp durations (user config)
timelapse_budget        = target_duration - poi_output_time - ramp_output_time

required_speed          = timelapse_source_time / timelapse_budget
```

- If `required_speed < 1x`: the timelapse sections would have to be in slow-motion
  to fill the target — warn the user and suggest a longer target or fewer POIs
- If `required_speed` is very high (e.g., 60x+): warn that detail will be lost
  in the timelapse sections; suggest a longer target or shorter POI durations
- Display the calculated speed to the user before rendering so they can adjust

This makes the workflow: *"I have a 90-minute trip, I want a 5-minute highlight reel,
here are the 3 incidents I care about"* → PathMux does the math, renders the result.

**Speed map preview (Shower Thought 2026-03-05 addendum):**

Before rendering, present the user with a plain-text timeline of the computed
speed plan so they can review and approve — or ask for adjustments:

```
Begin at 1x for 15 seconds -> 8x for 90 seconds -> 1x for 3 minutes during incident
  -> 10x for 75 seconds -> 1x for 20 seconds while I park in the garage
```

- Each segment shows speed and duration in output time, with a label for POI sections
- Total output duration shown as a footer: `Total: 4m 40s`
- User accepts (proceeds to render) or requests modifications:
  - Adjust target duration → recalculates timelapse speeds and re-presents
  - Trim a POI window → updates that segment, re-presents
  - Change ramp timing → updates transition segments, re-presents
- Natural interaction surface for both CLI (text prompt) and GUI (editable timeline widget)
- In CLI mode this could be a simple confirm/edit loop; in GUI it becomes a
  drag-and-drop timeline with speed annotations

**Caption overlay:**
- Free-text caption per POI, positioned and styled in the GUI
- Fade in/fade out timed to match the speed ramp

**Supplemental media:**
- Photos attached to a POI appear as a picture-in-picture or full overlay during
  the slow-down window
- Supplemental video clips can replace or overlay one of the collage quadrants
  during the POI window, then return to dashcam streams after

**ffmpeg implementation sketch:**
- timelapse segments: `setpts=PTS/N` filter with configurable N
- speed ramps: `setpts` with a keyframe-aligned ramp expression
- caption: `drawtext` filter per POI with time-bounded `enable` expression
- supplemental clips: spliced in via `concat` or `overlay` filter at POI timestamps

**Priority:** Phase 2/3 — depends on GUI, GPS sync, and collage generation
being in place first. Architecture should be kept in mind during collage pipeline
design so POI timestamps can be attached to trips from the start.

---

### GPS Features (Pending Coordinate Decode Resolution)
- **Phase 1 GPS (during trip scan):**
  - Extract `startLat/Lon` and `endLat/Lon` from first and last segments
  - Store in manifest
  - Display on trip tile (e.g., "Gulfport MS → Bay St. Louis MS")
- **Phase 2 GPS (user-initiated background task):**
  - "Build GPS Track" button on trip tile
  - Background thread extracts coordinates every 30 seconds from all segments
  - Writes to `gpsTrack` array in manifest
  - Tile shows "Scanning GPS track..." → "GPS track complete"
  - Status field: `gpsTrackStatus` = "none" | "scanning" | "complete"
- **GPX Export:**
  - Convert `gpsTrack` array to GPX format on demand
  - Open in Google Earth, Google Maps, or any GPX viewer
  - Internal storage stays JSON; GPX is presentation-layer export
- **Animated GIF thumbnails (future):**
  - `thumbGif` field reserved in manifest (currently empty string)
  - Background process creates animated GIF from `firstThumb` + `lastThumb`
  - Slow fade/cycle between start and end of trip for tile preview
  - Uses ImageMagick's `convert` command
  - Generated on-demand or as background task after scan

### Combo Compass/Speedometer Gauge Widget (Shower Thought 2026-03-06)
- Unified round gauge widget for any view that needs heading and/or speed
- **Layout:** Compass rose in the center; speed displayed on a semicircular bar graph around the perimeter
- **Speed color bands:**
  - Blue: 0–25 mph
  - Green: 25–70 mph
  - Yellow: 71–79 mph
  - Red: 80–100 mph
- Applies to both the Qt6 GUI playback overlay and any CLI-side ASCII/text equivalent
- Source: GitHub issue #4

### UI/UX Polish
- Keyboard shortcuts for common actions
- Drag-and-drop trip reordering
- Multi-select for batch operations (merge multiple trips, export multiple, delete)
- Search/filter trips by date range, duration, location
- Timeline view showing trips on a calendar
- Dark mode support
- Thumbnail caching for fast scrolling

---

## Phase 3: Advanced Features (Future)

### Data Export & Scripting
**Target audience:** Fleet operators, power users, third-party integrations

**Design decision:** Replace separate `--jsondump`, `--csvdump`, `--xmldump` flags with
a single `--format=[json,csv,xml]` modifier that applies to `--dump` and `--fulldump`.
Keeps the CLI orthogonal: what-to-dump is separate from how-to-serialize-it.

**`--format` with `--dump`/`--fulldump` (planned):**
- `--dump --format=json` — JSON output (replaces `--jsondump`)
- `--dump --format=csv` — CSV output (replaces `--csvdump`)
- `--dump --format=xml` — XML output (replaces `--xmldump`)
- `--fulldump --format=json` — full detail in JSON
- `--dump --fields id,date,start_time,duration` — field filtering (all formats)
- `--dump --manifest <ID>` — limit to one manifest
- Makes piping to `jq`, `python`, shell scripts much cleaner for automation

**CSV format:**
- One row per trip, columns for all scalar fields
- Optional segment expansion: one row per segment
- Fleet operators can drop directly into Excel, Google Sheets, or fleet management systems
- Example output:
  ```
  manifest_id,trip_id,date,start_time,duration,segment_count,note
  F0,0W,2026-02-26,08:40:09,21m 1s,7,
  F0,9S,2026-02-26,10:06:08,9m 1s,3,fuel stop
  ```

**XML format:**
- Same data as JSON dump, XML structure
- For integration with older enterprise tooling and ELD/fleet compliance systems
- Schema designed to be human-readable, not namespace-heavy

**Priority:** Medium — implement after libpathmux.a restructure so export logic
lives in `libpathmux` and all three formats share the same data pipeline.

### Video Analysis
- Automatic event detection (hard braking, sharp turns, impacts)
- Speed overlay graph on exported videos
- Accelerometer data visualization (if available in LIGOGPSINFO stream)

### Cloud Integration
- Optional backup to cloud storage (user-configured)
- Share trips via link (privacy-respecting, user-controlled)
- Sync manifests across devices

### Mobile App
- iOS/Android companion app for reviewing trips on mobile
- Upload dashcam footage directly from phone when SD card transferred
- Push notifications for trip completion/export

### AI Features
- Object detection in footage (pedestrians, vehicles, license plates)
- Automatic trip naming based on destination (if GPS available)
- Highlight reels: extract interesting moments automatically

---

## Open Questions / Design Decisions Pending Testing

### Transition Timing (Separator Merge)
- Logo spin duration: fixed (e.g., 2 seconds) or variable based on frame mismatch?
- Fade duration: fixed (e.g., 1 second) or user-configurable?
- **Decision:** Wait to see it in action before finalizing

### Gap Threshold for Trip Detection
- Current: `segdur + 30 seconds` (~3.5 minutes for 180s segments)
- Real-world issue: Fuel stop + quick errand (15-20 minutes) gets split into separate trips
- User feedback: Would prefer these stay as one trip, use merge feature if needed
- **Options:**
  - Increase to fixed 30-minute threshold
  - Make threshold user-configurable in settings
  - Use conservative detection + easy merge workflow
- **Decision:** Pending real-world testing with multiple users

### GPS Track Density
- Currently: Extract at t=0, t=30, t=60... (every 30 seconds)
- Alternative: Extract every 10 seconds for higher resolution
- Trade-off: File size vs. accuracy
- **Decision:** Start with 30 seconds, make configurable later if users request it

---

## Dependencies

| Tool | Version | Required | Phase | Notes |
|------|---------|----------|-------|-------|
| g++ | C++17+ | Yes | 1 | Alma Linux 9 base repos |
| ffmpeg/ffprobe | Any recent | Yes | 1 | RPM Fusion or static build |
| ExifTool | 13.51+ | Yes | 1-2 | For GPS extraction — EPEL 13.10 does NOT work |
| Qt6 | 6.x | Yes | 2 | Alma Linux 9 repos |
| ImageMagick | Any recent | Future | 2-3 | For animated GIF thumbnails |

---

## Community & Contributions

PathMux is currently a solo project by a Linux sysadmin learning C++ through AI-assisted development. Contributions, bug reports, and feature requests are welcome once the project reaches public release.

**Current blockers before public release:**
1. ffprobe integration for accurate trip duration
2. CLI stability and bug fixes
3. ExifTool 13.51 release (GPS extraction ready, awaiting upstream release)

**Target audience:**
- Dashcam owners (Pruveeo D90 initially, expandable to other 4-camera systems)
- Linux power users comfortable with command-line tools
- Users wanting local control over dashcam footage (no cloud required)

---

## License & Legal

- PathMux source code: To be determined (likely GPL or MIT)
- ExifTool: Perl Artistic License / GPL
- Qt6: LGPL (open source usage)
- ffmpeg: LGPL or GPL depending on build configuration

---

*Last updated: 2026-03-01*
*Project status: Phase 1 (CLI development)*
*Current version: 0.9.9 (SN 00079)*

**Audio sync reference:**
- Audio track always sourced from **Left camera** (driver position)
- Ensures driver's speech syncs with visible lip movement
- Other camera audio tracks ignored during collage generation
- Exception: If Left camera not included in collage, fallback hierarchy TBD
  - Possible: Left → Front → Rear → Right
  - Alternative: User selects audio source camera
  - Decision deferred to collage generation implementation


### Intelligent Audio Switching (VAD)
- **Voice Activity Detection (VAD)** to automatically switch audio source based on who's speaking
- During collage generation, run VAD on all 4 camera audio tracks simultaneously
- When speech detected, dynamically switch output audio to that camera's track
- Priority when multiple speakers: Left → Front → Right → Rear (or user-configurable)
- **Tools:**
  - WebRTC VAD: Lightweight, real-time capable, C library
  - Silero VAD: ML-based, high accuracy, ONNX model
  - ffmpeg `silencedetect`: Simpler threshold-based approach (may be sufficient)
- **Implementation options:**
  - Low effort: ffmpeg silencedetect for speech/silence boundaries, post-process audio switching
  - Medium effort: Integrate WebRTC VAD for frame-accurate switching
  - High effort: ML-based VAD with speaker identification
- **Bonus feature request:** Detect singing and... suppress? *(Technical answer: Speaker identification could theoretically classify "singing" vs "speech" but this is firmly in the "advanced ML" category and definitely Phase 4 material. More realistically: manual mute zones or a "karaoke filter" that detects music + singing and ducks the audio. Comedy value: high. Implementation priority: low.)*


### GPS-Based Frame-Perfect Sync
**Status:** Implementation ready once ExifTool 13.51+ is released

Once LIGOGPSINFO stream is correctly decoded, each segment will have:
- 180 GPS records (one per second for 180-second segments)
- Each record has precise timestamp: `2026:02:16 16:10:26`
- All 4 cameras write synchronized GPS timestamps to their respective segments

**Benefits:**
- Hyperaccurate time hack for every second of footage across all cameras
- Build frame-to-timestamp mapping for entire trip
- Camera sync becomes **trivial** — align all cameras by GPS timestamp instead of guessing from frame counts
- Eliminates frame drift accumulation entirely
- Handles variable segment lengths (startup clips, end-of-trip fragments) automatically

**Implementation:**
1. During trip scan, extract GPS timestamps from all 4 camera segments
2. Build sync map: `{ timestamp → { front: frame_N, rear: frame_M, left: frame_K, right: frame_J } }`
3. During collage generation, use sync map to ensure all cameras stay locked to GPS time
4. Frame padding/dropping handled automatically based on GPS-derived ground truth

**Priority:** High — implemented; ExifTool 13.51+ required at runtime

---

### PathMux Archive Format (.pathmux)

**Long-term goal:** Package entire trips into a single portable archive file for sharing or backup.

**Contents of `.pathmux` archive:**
- All `.ts` video segments (Front/Rear/Left/Right)
- All `.jpg` thumbnail sidecar files
- Trip manifest JSON (includes GPS track, sync data, metadata)
- Optional: Pre-rendered collages or preview videos
- Optional: GPX export of GPS track
- Optional: Known locations snapshot (`locations.json`) — allows recipient to
  open the archive on another machine and see the same named location pins
  (Home, Work, etc.) on the KML overlay without having to re-enter them.
  User controls whether to include this at export time (privacy consideration —
  known locations reveal home address and routine destinations).

**Use cases:**
- **Sneakernet:** Share entire trip on USB drive without cloud upload
- **Backup:** Single file to archive to NAS or external storage
- **Collaboration:** Send trip to friend/family/insurance for review
- **Portability:** Open `.pathmux` file on any machine with PathMux installed

**Format considerations:**
- **Compression:** Use standard container (ZIP, TAR.GZ, or custom)
  - Video already compressed (H.264), so focus on metadata/thumbnail compression
  - Optional: Offer multiple compression levels (fast/balanced/maximum)
- **Integrity:** Include checksums (SHA256) for all files
- **Metadata:** Embed manifest version, PathMux version, creation timestamp
- **Privacy:** Option to strip GPS data before creating archive
- **Compatibility:** Design for forward compatibility — newer PathMux versions can read older archives

**Import workflow:**
1. User drags `.pathmux` file into PathMux GUI
2. App extracts to temporary directory
3. Validates manifest and checksums
4. Imports trip into local cache
5. User can view, edit, export as if it were scanned from SD card

**Export workflow:**
1. User selects trip(s) to export
2. Choose compression level and privacy options
3. PathMux bundles all segments + manifest + thumbnails
4. Writes `.pathmux` file to user-selected location
5. Optional: Generate QR code linking to file for easy sharing

**File extension registration:**
- `.pathmux` files associated with PathMux application
- Double-click to open in PathMux GUI
- Icon shows thumbnail preview (if OS supports)

**Priority:** Medium — implement after core trip detection and collage generation are solid


---

## Packaging

**Goal:** Propose an RPM package for EPEL (RHEL/Alma/Fedora ecosystem) and a DEB
package for the Debian/Ubuntu equivalent (likely Debian backports or Ubuntu PPA).

**Target repositories:**
- RPM: EPEL (Extra Packages for Enterprise Linux) — covers Alma 9, RHEL 9, CentOS Stream
- DEB: Debian mentors / Launchpad PPA — covers Debian stable and Ubuntu LTS

### Packaging Checklist (audit before v1.0)

**CMake install targets:**
- [x] `pathmux` binary → `/usr/bin/` — `install(TARGETS pathmux ...)` in CMakeLists.txt
- [x] `pm_gpsinfo` binary → `/usr/bin/` — same install target
- [x] `pm_tripdebug` binary → `/usr/bin/`
- [x] `pathmux.1` man page → `/usr/share/man/man1/` — `install(FILES ...)` in CMakeLists.txt
- [ ] `LICENSE` file → `/usr/share/licenses/pathmux/` — blocked on license decision
- [x] `libpathmuxlib.a` not installed — internal static lib only; correct

**FHS compliance — verified by architecture:**
- [x] No hardcoded developer paths — all paths resolved at runtime
- [x] Config dir via `Platform::getConfigDir()` — correct for packaged installs
- [x] External tools discovered via `$PATH` — no hardcoded `/usr/bin/ffprobe` etc.
- [x] No runtime writes to `/usr/` — only `~/.config/pathmux/` and footage dirs

**Runtime dependencies:**
- `ffmpeg` / `ffprobe` — RPM Fusion on RHEL; `ffmpeg` on Debian/Ubuntu (soft dep)
- `perl-Image-ExifTool >= 13.51` — see ExifTool problem below
- No Qt6 dependency until Phase 2 GUI

**Build dependencies:**
- `gcc-c++` with C++17, `cmake >= 3.15`, `make`

**CPack:**
- [x] CPack configured in CMakeLists.txt — RPM and DEB generators, package metadata,
  description. `include(CPack)` active.
- [ ] `rpm/pathmux.spec` — hand-crafted spec for EPEL submission (CPack-generated
  spec may need customization for `%post` man page registration and dep handling)
- [ ] `debian/` directory — `control`, `rules`, `changelog`, `copyright` files

**⚠ Known issues to fix before packaging:**

1. **Hard ExifTool Requires in CMakeLists.txt** — `CPACK_RPM_PACKAGE_REQUIRES`
   currently declares `exiftool >= 13.51`, which will break RPM install on Alma 9
   since EPEL ships 13.10.  Must be changed to a soft `Recommends:` (or removed)
   with a runtime version check and clear error message in the app.

2. **License pre-set to GPL-2.0** — `CPACK_PACKAGE_LICENSE` and
   `CPACK_RPM_PACKAGE_LICENSE` are already set to `GPL-2.0` / `GPLv2` in
   CMakeLists.txt.  If the license decision is actually MIT, these need updating.
   Resolve the license decision (v1.0 gate item) and update accordingly.

**Priority:** Medium — keep packaging compatibility in mind during development;
do a formal audit before cutting v1.0 release candidate.

---

## Cross-Platform Portability

**Current status (v0.9.4):** Platform abstraction layer implemented. CMake build
system in use. Core C++17 logic is portable. Linux fully exercised; Windows/macOS
code paths are stubbed and documented in `lib/platform.cpp` — not yet tested.

**Target platforms:** Linux (primary), Windows, macOS

### Platform Abstraction Layer ✓ Done (v0.9.4)

`lib/platform.cpp/.hpp` (`namespace Pathmux::Platform`):
- `getHomePath()` — Linux/macOS: `$HOME`; Windows: `%USERPROFILE%`
- `getConfigDir()` — Linux: `~/.config/pathmux/`; macOS: `~/Library/Application Support/pathmux/`; Windows: `%APPDATA%/pathmux/`
- `getTerminalWidth()` — POSIX: `ioctl(TIOCGWINSZ)`; Windows: `GetConsoleScreenBufferInfo`; fallback: 65
- `lib/format_helpers.hpp` — pure math/format helpers; no POSIX deps; safe for Qt6 GUI on any platform

Remaining: implement and test Windows/macOS paths when cross-compile CI is set up.

### Build System ✓ Done (CMake, v0.8.21)

CMake 3.16+ in use. `pathmuxlib STATIC` target with PUBLIC include propagation.
CLI and tools link against it. `sn-audit` and `archive` custom targets preserved.
CPack configured for RPM/DEB. Integrates with Qt6 for GUI phase.

### External Dependencies

**ffmpeg/ffprobe and ExifTool:**
- Available on all target platforms
- Installation methods vary:
  - Linux: Package manager (dnf, apt, pacman)
  - Windows: Manual download or Chocolatey/Scoop package managers
  - macOS: Homebrew (`brew install ffmpeg exiftool`)
- PathMux should:
  - Check for tools at startup, warn if missing
  - Provide clear error messages with install instructions per platform
  - Allow user to specify custom paths in config file

### Qt6 GUI Portability

**Already cross-platform by design:**
- Qt abstracts all platform differences (file dialogs, window chrome, keyboard shortcuts)
- Single codebase compiles on Linux/Windows/macOS
- Platform-specific packaging:
  - Linux: AppImage, Flatpak, or native packages (.deb, .rpm)
  - Windows: Installer (.exe via NSIS or WiX)
  - macOS: .app bundle with .dmg installer

### Testing Strategy

**Priority order:**
1. **Linux (Alma Linux 9.x):** Primary development platform, test first
2. **Windows 10/11:** Large potential user base for dashcam software
3. **macOS (Intel + Apple Silicon):** Smaller market but good for completeness

**CI/CD considerations (future):**
- GitHub Actions supports all three platforms
- Automated builds and tests on each commit
- Generate platform-specific release artifacts

### Implementation Priority

**Phase 1.5 (After CLI stable, before Qt GUI):**
- [x] Add `platform.cpp` abstraction layer (v0.9.4)
- [x] Migrate from Makefile to CMake (v0.8.21)
- [ ] Test on Windows and macOS
- [ ] Update documentation for multi-platform builds

**Rationale:**
- Small implementation cost now (~1-2 days work)
- Prevents technical debt accumulation
- Makes Qt GUI development smoother (Qt requires CMake)
- Opens potential market opportunities
- Demonstrates professional software engineering practices

**Estimated effort:** Low — core code is already portable, changes are localized to build system and path handling


---

## Multi-Brand Dashcam Support

**Current status:** PathMux is built and tested exclusively on Pruveeo D90 360° dashcam format. Architecture assumes specific directory structure, filename conventions, and GPS metadata format.

**Goal:** Abstract hardware-specific logic into a "camera profile" system that supports multiple dashcam brands and models with minimal code changes per new device.

### Video Format Detection (Implemented — v0.8.14)

**ffprobe-based video profile probing** is already implemented and removes the need for camera profiles to handle video encoding characteristics. During `detectTrips()`, the first Front segment of each trip is probed once via ffprobe and the results stored in the manifest `videoProfile` block:

```json
"videoProfile": {
    "pixFmt":     "yuvj420p",
    "colorRange": "pc",
    "colorSpace": "bt709",
    "frameRate":  "25/1",
    "width":      1920,
    "height":     1080
}
```

This means pixel format, color range, color space, resolution, and frame rate are read from the actual footage regardless of which camera produced it. The encode pipeline in `video_build.cpp` uses this ground truth directly — no assumptions, no camera-specific hardcoding for video format. Any camera whose footage ffprobe can read will encode correctly.

**What the probe handles automatically across all cameras:**
- Full-range (`pc`/`yuvj420p`) vs limited-range (`tv`/`yuv420p`) pixel formats
- Different resolutions (1080p vs 4K source)
- Different frame rates (25fps vs 30fps vs 60fps)
- Color space variations (bt709 vs bt601)

**What still requires camera profiles:**
- Directory structure (`Front/Rear/Left/Right` vs `front/left_repeater` etc.)
- Filename timestamp patterns
- Segment length detection
- GPS extraction method (LIGOGPSINFO vs MP4 metadata vs sidecar NMEA)
- Audio channel characteristics (mono 16kHz on D90 vs stereo 48kHz on others)
- Thumbnail sidecar conventions

The clean separation is: **profile handles "how do I find and parse your files"**, **probe handles "what are your files actually made of"**.

### Camera Profile System Architecture (Planned)

**File organization:**
- Pruveeo D90: `/Front/`, `/Rear/`, `/Left/`, `/Right/` directories
- TeslaCam: `/front/`, `/left_repeater/`, `/right_repeater/`, `/back/` directories
- Some brands: Single directory with filename prefixes (`F_timestamp.mp4`, `R_timestamp.mp4`)
- Others: Separate folders per recording session or date

**Video formats:**
- MPEG-TS (`.ts`) — Pruveeo D90, many Chinese brands
- MP4 (`.mp4`) — Tesla, BlackVue, Viofo, most modern dashcams
- AVI (`.avi`) — older models
- MOV (`.mov`) — some high-end units

**Segment lengths:**
- 1 minute: TeslaCam, many compact models
- 2 minutes: Common on budget dashcams
- 3 minutes: Pruveeo D90 default (configurable to 1/2/5 min)
- 5 minutes: Some commercial/fleet dashcams

**Timestamp encoding:**
- `YYYYMMDD_HHMMSS` — Pruveeo D90
- `YYYY-MM-DD_HH-MM-SS` — common variant
- `YYYYMMDDHHMMSSxxx` (no separators, milliseconds appended)
- Unix epoch in filename
- Proprietary numbering schemes with timestamp in metadata only

**GPS data sources:**
- Embedded in video container (MP4 metadata tracks)
- LIGOGPSINFO binary stream (Pruveeo D90, some LIGO-based firmware)
- NMEA sentences in separate `.nmea` sidecar files
- GPX files in parallel directory structure
- OSD overlay only (no extractable metadata)
- No GPS at all (TeslaCam doesn't embed GPS in video files)

**Thumbnail handling:**
- `.jpg` sidecar files (Pruveeo D90)
- Embedded in video file (extract with ffmpeg)
- No thumbnails — must generate on first scan

### Camera Profile System Architecture

**Profile Definition (JSON format):**

Per-camera `video` and `audio` blocks are filled in by probing the first segment
of each camera directory. They serve two purposes: providing the collage layer
with stream parameters for extraction/mixing, and contributing to auto-detection
fingerprinting. Cameras without an audio track omit the `audio` key.

`segment_duration_seconds` records observed segment lengths as a detection hint
only — the scan engine derives segment duration from timestamp arithmetic and
a single ffprobe call, not from this field.

`filename_pattern` is derived by sampling filenames from **all** camera dirs,
stripping the detected timestamp portion, and characterizing what remains
(nothing, a constant suffix, a variable per-camera suffix). No particular suffix
convention (e.g. single letter) is assumed — the analysis is driven by what is
actually on the card. Directory structure alone identifies camera role.

`thumbnails.pattern` is auto-filled from file analysis: if sidecar `.jpg` files
are found and their basenames match the video files across ≥3 samples, the
pattern is derived by substituting the extension. If jpgs exist but cannot be
matched, the user is warned to set the pattern manually or file a GitHub issue.

```json
{
  "profile_name": "Pruveeo D90",
  "version": "1.0",
  "detection": {
    "directory_names": ["Front", "Rear", "Left", "Right"],
    "file_extension": ".ts",
    "filename_pattern": "^(\\d{8})_(\\d{6})[A-Z]\\.ts$"
  },
  "cameras": {
    "front": {
      "dir": "Front", "priority": 1,
      "video": { "width": 2560, "height": 1440, "frame_rate": "30000/1001", "pix_fmt": "yuv420p", "color_space": "bt709" },
      "audio": { "codec": "aac", "channels": 2, "sample_rate_hz": 48000 }
    },
    "rear": {
      "dir": "Rear", "priority": 2,
      "video": { "width": 1920, "height": 1080, "frame_rate": "30000/1001", "pix_fmt": "yuv420p", "color_space": "bt709" },
      "audio": { "codec": "aac", "channels": 2, "sample_rate_hz": 48000 }
    },
    "left": {
      "dir": "Left", "priority": 3,
      "video": { "width": 1920, "height": 1080, "frame_rate": "30000/1001", "pix_fmt": "yuv420p", "color_space": "bt709" },
      "audio": { "codec": "aac", "channels": 2, "sample_rate_hz": 48000 }
    },
    "right": {
      "dir": "Right", "priority": 4,
      "video": { "width": 1920, "height": 1080, "frame_rate": "30000/1001", "pix_fmt": "yuv420p", "color_space": "bt709" },
      "audio": { "codec": "aac", "channels": 2, "sample_rate_hz": 48000 }
    }
  },
  "timestamp": {
    "format": "YYYYMMDD_HHMMSS",
    "timezone": "local"
  },
  "gps": {
    "method": "exiftool_ligogps",
    "start_offset_seconds": 45
  },
  "thumbnails": {
    "source": "sidecar_jpg",
    "pattern": "^(\\d{8})_(\\d{6})[A-Z]\\.jpg$"
  },
  "segment_duration_seconds": [60, 120, 180, 300]
}
```

**Profile for TeslaCam (hypothetical — unverified, from documentation):**
```json
{
  "profile_name": "TeslaCam",
  "version": "1.0",
  "detection": {
    "directory_names": ["front", "back", "left_repeater", "right_repeater"],
    "file_extension": ".mp4",
    "filename_pattern": "^(\\d{4})-(\\d{2})-(\\d{2})_(\\d{2})-(\\d{2})-(\\d{2})-.+\\.mp4$"
  },
  "cameras": {
    "front": {
      "dir": "front", "priority": 1,
      "video": { "width": 1280, "height": 960, "frame_rate": "36/1", "pix_fmt": "yuv420p", "color_space": "bt709" },
      "audio": { "codec": "aac", "channels": 2, "sample_rate_hz": 44100 }
    },
    "rear":  { "dir": "back",           "priority": 2, "video": { "..." : "..." } },
    "left":  { "dir": "left_repeater",  "priority": 3, "video": { "..." : "..." } },
    "right": { "dir": "right_repeater", "priority": 4, "video": { "..." : "..." } }
  },
  "timestamp": {
    "format": "YYYY-MM-DD_HH-MM-SS",
    "timezone": "UTC"
  },
  "gps": {
    "method": "none",
    "note": "Tesla provides location via car UI, not embedded in video"
  },
  "thumbnails": {
    "source": "generate_ffmpeg",
    "frame_offset_seconds": 1
  },
  "segment_duration_seconds": [60]
}
```

**Known edge case — identical basenames across camera dirs:**
Some cameras may store files with identical basenames in each camera directory
(e.g. `Front/20260225_044424.ts` and `Rear/20260225_044424.ts`). The directory
is the only differentiator. No current code impact — absolute paths are used
throughout. No code changes needed until this is confirmed on real hardware.

**Auto-detection workflow:**
1. User specifies root directory (e.g., `/media/dashcam/`)
2. PathMux scans subdirectories and filenames
3. Matches against known profiles in `~/.config/pathmux/profiles/` directory
4. If match found, loads that profile and proceeds with scan
5. If no match, prompts user to:
   - Select from list of bundled profiles
   - Create custom profile via wizard (future GUI feature)
   - Submit directory structure to GitHub for new profile development

**Custom profile support:**
- User can create/edit profiles manually in `~/.config/pathmux/profiles/custom/`
- JSON format is human-readable and well-documented
- Community can share profiles via GitHub wiki or discussions

### Implementation Plan

**Phase 1: Refactor existing code**
- Extract all Pruveeo D90 assumptions into `profiles/pruveeo_d90.json`
- Create `CameraProfile` class to load and apply profile settings
- Modify `trip_detection.cpp` to use profile settings instead of hardcoded values

**Phase 1.5: `pm_probe --wizard` — interactive profile builder** *(in progress)*

`pm_probe --card` already collects the raw fingerprint (directory layout, file
extensions, sample filenames, segment durations, video profile, GPS method).
The wizard layer adds interactive Q&A on top of that scan to produce a
saveable `CameraProfile` JSON.

**Data collection (silent, before any prompts):**
- Probe first segment of **each** camera directory (not just primary) for video
  profile and audio stream info — feeds per-camera `video`/`audio` blocks
- Sample durations from up to 5 primary-camera segments
- Detect sidecar `.jpg` files and verify basename match against video files
  (≥3 matching samples required to auto-fill `thumbnails.pattern`)
- Sample filenames from all camera dirs to drive timestamp and suffix analysis

**Interactive steps:**
1. Camera role assignment — guess from dir name (`Front`→front, `back`→rear,
   etc.); user confirms or remaps each
2. Filename timestamp format — strip timestamp from sample basenames, present
   guess, user confirms or enters manually; suffix characterization (nothing /
   constant / variable) is derived from all dirs, no assumed convention
3. Thumbnail handling — auto-filled if basename match confirmed; if jpgs exist
   but can't be matched, warn user to set `thumbnails.pattern` manually or
   run `pm_probe --card <path>` and file a GitHub issue
4. Timezone — ask explicitly (local vs UTC); cannot auto-detect
5. GPS lock offset — approximate cold-start seconds; default 0
6. Profile name — free text, sanitized to filename

**Output:**
- Write finished profile JSON to `~/.config/pathmux/profiles/<sanitized_name>.json`
- Trial scan against card path deferred — requires CameraProfile C++ layer

**Why this matters before going public:** We only have verified ground truth
for the Pruveeo D90. All other profiles (TeslaCam etc.) are educated guesses
from documentation. The wizard gives real users a path to self-serve a profile
for their own hardware and submit it back to the community.

**Phase 2: Add profile auto-detection**
- Scan target directory, extract structural fingerprint
- Match against known profiles
- Fall back to manual profile selection if no match

**Phase 3: Community expansion**
- Bundle 3-5 common dashcam profiles with PathMux
- Create GitHub wiki page for user-submitted profiles
- Add "Submit new profile" workflow to GUI (collects directory structure, sample filenames, submits to GitHub issue)

**Phase 4: GPS abstraction**
- Support multiple GPS extraction methods:
  - ExifTool (LIGOGPSINFO, standard MP4 metadata)
  - NMEA parser (for `.nmea` sidecar files)
  - GPX import (for separate GPS tracks)
  - Manual GPX overlay (user provides their own GPS data)
  - None (trips still work, just no GPS features)

### Optional Camera Handling

Some cameras are physically optional (e.g. D90 rear camera not connected). The
resulting SD card may have either an empty camera directory or no directory at all —
different cameras handle this differently. Both cases must be handled gracefully:

1. **Directory present but empty** — scan finds no segments; channel treated as absent
2. **Directory completely absent** — no directory entry at all

The collage pipeline's dead-camera placeholder logic already covers the absent-channel
case at render time. Detection-layer handling for both cases must be verified during
camera profile implementation.

**Simulation plan (before Cobra card arrives):**
Populate `Front/`, `Left/`, `Right/` with real D90 segments; leave `Rear/` empty or
absent. Exercises both detection and collage code paths for a 3-camera configuration.

### User Support Model for Unknown Layouts

When a user reports an unsupported camera:
1. User runs: `pm_probe --card <path>` and `ls -alR <path>`
2. Pastes both into a GitHub issue using the camera support issue template
3. That output gives everything needed to reverse-engineer the layout and write a
   camera profile — no hardware required

`pm_probe --card` output is specifically formatted for this use case.

### Testing Strategy

**Real-world data collection:**
- Solicit USB sticks with sample footage from community members
- Test brands in priority order:
  1. Pruveeo D90 (primary, already supported)
  2. **Cobra CCDC4500** (Costco special — SD card in hand shortly for analysis)
     - Front (1080p) + rear/interior (720p); `.MOV` H.264 container
     - GPS source: TBD — needs `exiftool`/`ffprobe` analysis once SD card in hand
     - Filenames: `CLIP_YYYYMMDD_HHMMSS_F.MOV` (front), `CLIP_YYYYMMDD_HHMMSS_R.MOV` (rear)
     - Segments: 1, 3, or 5 minute (user-configurable)
     - TODO: Deep-dive on file layout, GPS extraction method, quirks
  3. TeslaCam (large user base, very different format)
  4. BlackVue (popular multi-camera system)
  5. Viofo (budget-friendly, common)
  6. Thinkware, Garmin, others as data becomes available

**Profile validation:**
- Each new profile requires:
  - Sample footage from at least one user
  - Successful trip detection across multiple recording sessions
  - GPS extraction working (if hardware supports it)
  - Documentation of any quirks or limitations

### Community Contribution Model

**When GitHub repo goes public:**
- Clear README with supported hardware list
- "Request support for your dashcam" issue template
- Instructions for submitting sample footage (privacy-aware — no faces/plates required)
- Credit contributors in CHANGELOG and profile metadata

**Value proposition to users:**
- "Your dashcam not supported? Send us 2 sample trips and we'll add it within a week"
- Low barrier to entry for community support
- Leverages existing PathMux architecture — most profiles are just JSON config, minimal code changes

### Long-term Vision

**Become the VLC of dashcam software:**
- Just like VLC plays every video format, PathMux handles every dashcam
- Community-driven profile library
- "It just works" reputation across brands
- Manufacturers start testing against PathMux during product development

### Under Consideration
- 💡 PathMux Viewer (mobile companion app) — video playback, incident/segment
  marking, and GPS track review for iOS/Android; reads and writes a sidecar
  project file synced via cloud storage; marked segments and edits picked up
  by desktop on next render; no rendering or ffmpeg dependency
**Priority:** Medium-High — critical for public release and community growth, but not blocking CLI development


<!-- SN: 00082 -->
