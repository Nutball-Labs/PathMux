# CamClops Session Log

Running log of development sessions. Captures decisions, reasoning, and context
that CHANGELOG and ROADMAP don't cover. One entry per working session.

---

## 2026-05-24

### Session — Thumbnail throttle, manifest location fix, GUI scan hang fixes

**Focus:** Pure bug-fix session. All five bugs surfaced during live Viofo A229 Ultra
testing with a large parking-event dataset (408 events).

---

#### 1. "Too many open files" crash — unbounded grabVideoThumb QProcess instances

**Bug:** Loading a Viofo manifest with many trips (three camera slots each) launched
one `QProcess` ffmpeg instance per tile per slot simultaneously. With enough tiles the
process hit the OS fd limit (1024 on Alma 9), crashing GNOME entirely.

**Fix (`gui/TripGridPanel`):** Replaced the fire-and-forget `grabVideoThumb` with a
throttled queue. `ProcRequest` struct holds all parameters; `m_procQueue` is a
`std::deque` of pending requests; `m_activeProcs` tracks running count.
`drainProcQueue()` starts new processes only while `m_activeProcs < MAX_THUMB_PROCS`
(= 4). `QPointer<TripTile>` used to guard tile access across async completions —
required `#include <QPointer>` in the header (the forward-declaration in `qmetatype.h`
is insufficient for template instantiation).

---

#### 2. Thumbnails not persisting across manifest switches

**Bug:** After thumbnails were grabbed and displayed, switching to another manifest and
back caused all thumbnails to re-grab from scratch. Nothing was saved to disk.

**Fix (`gui/TripGridPanel`):** Added `saveGrabbedThumb()`: saves the raw JPEG bytes to
`<manifest-parent>/clops_thumbs_<id>/<eventId>_<slot>.jpg` and patches the manifest
JSON `firstThumbs[slot]` with the relative path. Critical ordering: `saveGrabbedThumb`
must be called **before** the `if (!req.tile)` null-check — when the user switches
manifests mid-grab, tiles are destroyed and `req.tile` is null, but the save should
still happen so the next manifest load finds the cached file.

---

#### 3. Parking manifests landing in `~/.config` instead of footage directory

**Bug:** Parking event manifests always wrote to `~/.config/camclops/` regardless of
the footage directory's writability. Driving manifests co-locate with footage correctly.

**Root cause (`lib/config_manager.cpp`):** `getManifestFilePath()` passed the literal
`"park:/footage/path"` string to `fs::exists()` — this path doesn't exist, so the
writability test always failed and the config-dir fallback always fired.

**Fix:** Call `realSourcePath(sourcePath)` before the writability test to strip the
`"park:"` prefix. The thumbnails directory follows automatically since
`saveGrabbedThumb` derives its path from the manifest file's parent directory.

---

#### 4. Viofo rescan GUI hang — 6777 lsof entries, GNOME "force quit"

**Bug:** After wiping manifests and rescanning Viofo footage, the GUI became
unresponsive. `lsof | grep camclops | wc -l` = 6777. Job queue showed driving and GPS
steps completing, then the GPS bar stalled orange indefinitely.

**Root cause:** After `detectTrips` returns, `saveTripCache` calls `extractFrame()` →
`system(ffmpeg)` once per driving trip per camera slot (Viofo: 3 slots). These are
blocking sequential subprocess calls on the worker thread. With no progress signal
emitted during this phase, the GPS step bar stays orange the entire time. The duration
is long enough for GNOME to flag the process as unresponsive.

**Fix (`lib/config_manager.cpp`, `gui/JobQueue.cpp`):** Added `bool skipFrameExtract =
false` parameter to `saveTripCache`. Defaults `false` so CLI behavior is unchanged.
`ScanJobWorker` passes `true` for both the driving and parking `saveTripCache` calls.
With the flag set, the entire `extractFrame`/`extractFrameScene` block is bypassed.
`grabVideoThumb` already handles Viofo thumbnails lazily — this is the correct
long-term design. Comment explains the reasoning at the call site.

---

#### 5. Parking scan hangs after progress bar reaches 100%

**Bug:** With 408 parking events, the parking `detectTrips` phase completed (bar hit
100%) but the scan never finished — the job remained Running indefinitely.

**Root cause:** After `detectTrips`, `saveTripCache` calls `selectValidationFiles()`
for every trip with empty `validationFiles`. `selectValidationFiles` calls `fileMd5()`
(via `popen("md5sum ...")`) on up to 3 video files per event. For 408 parking events
× 3 files × ~100 MB each = reading ~120 GB just to record checksums. Parking clips are
transient (the dashcam auto-rotates them), so the validation serves no purpose.

**Fix (`lib/config_manager.cpp`):** Added `!isParking` guard to the
`selectValidationFiles` call. Parking events skip checksum computation entirely.

---

**Files changed:** `gui/TripGridPanel.h`, `gui/TripGridPanel.cpp`,
`lib/config_manager.hpp`, `lib/config_manager.cpp`, `gui/JobQueue.cpp`

**Version:** 2.9.1 / SN 00122 (HWM unchanged — all files already at 00122).

---

## 2026-05-20

### Session — Sync-pad drift fix for multi-startup trips

**Focus:** Single bug fix. Diagnosed and corrected cumulative sync drift in
synced collage builds for trips that span multiple camera restarts.

**Bug report:** At the 17:59 mark of `Sync_verify_Collage_1080p.mp4` (UZ:78,
evening commute with gas station stop), a passing produce truck appeared clearly
out of sync — front camera showed the truck still ahead while the right camera
showed it already past the rear wheels. User estimated 2–3 seconds of offset.

**Diagnosis:** Extracted frames from both the 4K collage and the raw right-camera
segment at the calculated sync-correct position. The OSD GPS timestamps told the
story unambiguously: front showed `19:20:25`, right showed `19:20:27` at the same
collage timecode. Raw right camera at the expected synced position (117.030s into
the segment) also showed `19:20:25` — a perfect match to front, confirming the
sync analysis values were correct and the problem was in the collage builder.

**Root cause:** The sync-pad approach (`Tier 2`) prepends ghost frames to
late-starting cameras and appends clone frames to early-starting cameras, making
each camera's stream output `raw_duration + maxTrimF/fps`. This invariant assumes
all cameras record the same raw duration per segment. It holds for normal 3-minute
segments because each camera cuts its file independently after 3 minutes. It
breaks for **short segments at restart boundaries** — when the dashcam power cuts
all cameras at the same physical instant, a camera that started 1.76 seconds late
records a segment that is ~1.76 seconds shorter than front. The ghost-pad delay
is correct, but the shorter raw content means that camera's padded output is also
shorter, so its stream runs ahead in the xstack timeline. For this trip, the two
gas-station segments (24.352 s and 23.473 s front, 22.838 s and 22.318 s right)
contributed 1.514 s + 1.155 s = 2.669 s of cumulative drift by segment 7.
Confirmed: file duration was 29.37 s over raw total (≈ expected 29.92 s), proving
the sync-pad path was taken — the drift was equalization failure, not a missing
sync path.

**Fix (`cli/video_build.cpp`):** In both `buildCollage4K` and
`buildCollage1080Direct` sync-pad paths, after computing `segDelayF`/`segHoldF`,
probe all four camera segment durations (front was already probed for Tier 1;
only the other three are new). For each camera whose raw segment is shorter than
the front camera's, compute `segDurEqF` = extra clone frames to append. Add these
to the existing `HF` tpad hold count for both the video filter chain and the audio
apad duration. All four streams now output identical frame counts per segment
regardless of per-startup start-time variation.

**Side note:** This fix is also a prerequisite for the planned "Join Trip"
feature. Any joined trip is inherently a multi-startup trip (the gap that caused
detection to split them means the cameras restarted), so without this fix, joined
trips would have had silent sync drift at the seam.

**HWM bumped:** 00120 → 00121. `lib/version.hpp` and rebuild left to user.
Version: 2.6.1.

---

## 2026-05-23

### Session — Viofo support: interior camera collage, parking scan, sidecar-less thumbnails, loadTripCache hardening

**Focus:** Multi-part session centred on the Viofo A229 Ultra dashcam. Fixed several
latent bugs that only surface with non-standard (3-camera, "interior") profiles, fully
wired parking mode scanning, added on-demand thumbnail extraction for cameras that
produce no JPEG sidecars, and tracked down a `loadTripCache` crash that occurred after
GPS extraction on Viofo trips.

---

#### 1. Collage remap for non-standard camera profiles

**Bug:** Selecting "Interior" for a collage quadrant in `TripBuildDialog` produced front
footage in that slot instead of interior footage. Two separate root causes.

**Root cause A — `effectiveSegs` hardcoded chain (`cli/video_build.cpp`):** The lambda
used `if (cam == "rear") ... if (cam == "left") ... return srcFront` — any camera name
not in the four standard names fell back silently to front. Fixed by replacing the
four named `std::vector` accumulators with a `std::map<std::string, vector>` keyed by
camera ID; `effectiveSegs` now does a map lookup, returning an empty vector (→ black
lavfi) for absent cameras rather than recycling front footage. Applied to
`buildCollage4K`, `buildCollage4KChunked`, and `buildCollage1080Direct`.

**Root cause B — `kCollagePos` mismatch (`gui/TripBuildDialog`):** `buildOptions()` used
the profile slot name (`m_kSlot[i]`, e.g. `"interior"`) as the key for `cameraRemap`,
`blankSlots`, and `externalSlots`, but the collage build always queries these by the
fixed xstack position names (`"front"`, `"rear"`, `"right"`, `"left"`). For a Viofo
with `m_kSlot[2] = "interior"` in the BL slot, nothing ever set `cameraRemap["right"]`,
so BL received black. Added `kCollagePos = {"front","rear","right","left"}` (note
BL=right, BR=left per xstack ordering) and switched all collage key writes to use
`kCollagePos[i]` instead of slot name. Audio source also updated to use collage position
name. Fixed `lastThumbs` iteration guard (was iterating without checking `contains`).

**Non-standard camera files (`buildExtraCams`):** `buildOptions()` was silently dropping
Camera Files checkboxes for slots not named front/rear/left/right. Added
`VideoOptions::buildExtraCams` vector; unknown slot names push to it; `buildVideo()`
iterates it and calls `buildCameraFile()` sequentially after the standard four.

**`fmtStage` generalization (`gui/JobQueue.cpp`):** Stage label formatter was hardcoded
to `concat:Front/Rear/Left/Right`; non-standard cameras showed the raw label (e.g.
`concat:Interior`). Changed to a generic `startsWith("concat:")` handler that strips the
prefix and appends " - join segments".

---

#### 2. Parking mode scanning — Viofo A229 Ultra

The scanning infrastructure (`parkingProfile()`, `roProfile()`, `ScanProgressDialog`
parking pass, `footageType`/`readOnly`/`triggerType` fields, manifest serialization) was
already fully implemented. The only missing piece was thumbnail extraction.

**`noThumbsYet` bug (`lib/config_manager.cpp`):** For `thumbnailMethod = "extract_frame"`
cameras, `thumbFor()` returns `""` for each slot (no sidecars found), populating
`firstThumbs` as `{"front":"","interior":"","rear":""}`. The extraction guard used
`trip.firstThumbs.empty()`, which was always false (map had keys, just empty values).
Fixed to `noThumbsYet = std::none_of(... !kv.second.empty())`.

**Parking thumbnails skip in scan-time extraction:** With 265 parking clips × 3 cameras,
265×3 = 795 synchronous `system()` ffmpeg calls would hang the scan for 10+ minutes
(the session-1 "crash" was this hang). Added `!isParking` guard to the extraction block;
parking thumbnails are handled lazily by the GUI instead.

**`extractFrameScene()` helper:** For normal (non-parking) trips without sidecars, a
scene-detection pass with `select=gt(scene,0.10)` finds the first significant motion
frame in a configurable window [10, 25]s; falls back to the midpoint on no match.
Used for parking clips when triggered from the GUI.

---

#### 3. On-demand thumbnail extraction in TripGridPanel

**Problem:** After scanning Viofo footage, trip tiles showed grey placeholders — no JPEG
sidecars exist and the scan-time extraction was skipped for parking clips.

**`grabVideoThumb()` (`gui/TripGridPanel`):** Async `QProcess` ffmpeg grab for tiles
where `firstThumbs[slot]` exists but is empty. Normal trips seek to a random offset
in [90, 300]s (from `pickThumbSource`); parking clips use scene detection over [10, 25]s
with 17.5s fallback.

**Thumbnail storage location:** Moved from `~/.config/camclops/thumbs/<id>/` to
`<manifest-parent>/clops_thumbs_<id>/` — colocated with the manifest so they don't
accumulate in config and are naturally removed when the manifest is deleted.

**Interior slot:** Tile thumbnail loop now covers "front", "rear", and "interior" (was
only "front" and "rear"). `TripTile::setThumbnail` ignores unknown slots silently.

---

#### 4. Available Extras panel in TripBuildDialog

Added "Available Extras" QGroupBox to the Collage Layout tab, shown only when at least
one of `mapVideos`, `dashVideos`, or `hudVideos` is non-empty. Displays 192×108
thumbnail cards (loaded asynchronously via `startThumbGrab()`) for each available
output type. HUD thumbnails composite the VP9 alpha stream over `#111118` background
so the speed tapes and compass rose are visible.

Added "● available" green indicator next to the HUD checkbox when `hudVideos` is
non-empty. Checkbox remains unchecked by default (opt-in).

---

#### 5. loadTripCache crash — GPS extraction on Viofo

**Symptom:** After extracting GPS from a Viofo trip, `TripGridPanel::onJobFinished`
called `loadTripCache` and the app crashed with
`nlohmann::json_abi_v3_11_3::detail::type_error [302] type must be string, but is number`.

**Trace:** `GpsExtractJob::startSyncPhase` runs `clops_sync_analyze.py --write`, which
re-writes the entire manifest; `finished(true)` fires; `onJobFinished` reloads the
manifest; `loadTripCache` hits an unguarded `v.get<std::string>()` on a numeric JSON
value. Root cause not yet isolated (all inspected string-expected fields look correct in
Python; debug symbols not available on installed binary; `std::cout << "."` per GPS
segment confirmed the dots in crash output, confirming the crash was user-triggered, not
startup). Defensive `is_string()` guards added to all six unguarded `get<std::string>()`
call sites in `loadTripCache` with stderr warnings; the next GPS extraction will surface
the problematic field via the warning log.

**Version:** 2.7.3c / SN 00121 (no HWM bump this session).

---

## 2026-05-15

### Session — GitHub repo rename, /update-push release skill

**Focus:** Housekeeping and tooling. No source code changes.

**GitHub repo rename:** Repository renamed from `Nutball-Labs/PathMux` to
`Nutball-Labs/CamClops` via `gh repo rename`. Local remote URL updated to
`git@github.com:Nutball-Labs/CamClops.git`. GitHub automatically maintains
a redirect from the old URL so existing links do not immediately break.

**`/update-push` skill** (`.claude/commands/update-push.md`): new Claude Code
slash command that drives the full release-prep and publish workflow. Phase 1
audits all doc files (CHANGELOG, ROADMAP, README, Session_Log, man pages,
camclops_project_brief), finds packages for the current version, drafts GitHub
release notes, and stops for review. Phase 2 (after explicit "approved") commits
doc changes, pushes to `main`, creates the GitHub release tag, uploads packages,
and marks the release as latest.

**History policy clarified:** CHANGELOG entries written before the rebrand retain
their PathMux/pm_* names. No history rewriting in the changelog; the rebrand
commit (`42b4e46`) is the canonical marker.

**Man page `.TH` headers corrected:** All `clops_*.1` man pages had stale
`PM_*` names in their `.TH` macro (e.g. `PM_FINDGPSLOCK` instead of
`CLOPS_FINDGPSLOCK`). All nine man pages updated; version bumped from
`v1.9.10a` to `v2.0.1a`.

---

## 2026-05-14 (session 2)

### Session — Rebrand close-out, build fixes, hardware probe, Delete Trip in Properties, DangerousDialog removal

**Focus:** First session under the CamClops name. Closed out post-rebrand build
issues, fixed the setup wizard GPU detector, added proper trip deletion to the
Properties dialog, and retired DangerousDialog.

**Project memory migration:** Memory files from the old `pathmux` project directory
were copied to the new CamClops project directory so future sessions have full
context without needing the old path.

**Build directory stale cache:** After the directory rename from
`/z/Nutball-Labs/pathmux` to `/z/Nutball-Labs/CamClops`, all three build
directories (`build-linux`, `build-macos`, `build-windows`) held stale
`CMakeCache.txt` files pointing to the old path. Renamed them (`old-build-*`)
to force a clean reconfigure.

**CMake Python PIL fallback (`CMakeLists.txt`):** `find_package(Python3)` picks
the highest Python version present (3.12 on penny). The RPM Fusion ffmpeg build
has Pillow installed under Python 3.9, not 3.12. Added a post-find verification
block: after finding Python3, CMake runs `python3 -c "import PIL"` on the found
interpreter; if it fails, falls back to the `python3` name on PATH and retests.
If neither has PIL, `logo_morph.mp4` generation is skipped gracefully (pre-built
file used if present). Prevents build failure on machines where the system Python3
has Pillow but a newer Python does not.

**Setup wizard hardware probe (`gui/SetupWizard.cpp`):** The probe step ran
`ffmpeg -encoders` and checked if `h264_nvenc`/`h264_qsv`/`h264_vaapi` appeared
in the output. RPM Fusion's ffmpeg is compiled with all three regardless of
hardware, so penny (Intel iGPU, no NVIDIA) was incorrectly reported as having
NVENC. Fixed: after the encoder list pass, each compiled-in encoder family is
validated by running a 1-frame null encode (`ffmpeg -f lavfi -i nullsrc=s=64x64:d=1
-c:v <enc> -frames:v 1 -f null -`). NVENC fails with "Cannot load libcuda.so.1";
QSV and VAAPI succeed. Wizard now correctly suggests QSV on penny.

**Delete Trip in TripPropertiesDialog (`gui/TripPropertiesDialog`):** A red
"Delete Trip…" button added to the left of the button box. Two-click confirmation:
first click arms the button (label → "Confirm — delete N segment(s)? Click again",
bright red fill), starting a 4-second auto-disarm timer. Second click within the
window deletes all `.ts` and `.jpg` segment files, removes the trip entry from
the manifest via `loadTripCache`/`saveTripCache`, emits `tripDeleted()`, and
closes the dialog. The timer resets the button to its idle state if the second
click does not arrive.

**DangerousDialog removed:** `DangerousDialog.cpp/.h` deleted. The segment-level
delete/archive workflow it provided is superseded by camclops-tl (more accurate,
frame-level). Removed from `CMakeLists.txt`, the `#include` from `TripTile.cpp`,
and the "Dangerous…" context menu item. `TripTile` now connects
`TripPropertiesDialog::tripDeleted → TripTile::tripChanged` so the grid refreshes
after a deletion from Properties.

**SN high-water mark:** 00117 → 00118. Version: 2.0.1a.

---

## 2026-05-14

### Session — Job Queue step bars, metadata branding, v2.0.0 prep

**Focus:** Job queue UX overhaul (per-step progress bars, layout redesign), provenance
metadata in all generated video files, build tooling hardening, and several
accumulated bug fixes ahead of the v2.0.0 release.

**Job Queue — per-step progress bars:**  `stepStarted`, `stepProgress`, and `stepDone`
signals added to `Job`. `GpsExtractJob` (two stages: GPS extraction → camera sync)
and `CollageJob` (up to six stages: four concats + 4K collage + 1080p pass) emit
them. `JobRowWidget` redesigned: a collapsible steps area below the header shows one
sub-row per stage, each with its own progress bar, per-step elapsed timer, and
colour-coded dot. The header shows overall accumulated elapsed. Single-step jobs
(MapRenderJob, SyncAnalysisJob, ManifestScanJob) are unchanged — no expand button.
Step rows grow dynamically as each stage starts; all bars and elapsed times freeze
on completion.

**Job Queue layout:**  Description label was `QSizePolicy::Expanding`, producing a
large dead zone between the job name and the status text. Now the progress bar
expands to fill available width; description and status labels are `Preferred`/
max-capped at 220 px. Same fix applied to step sub-row bars. Nutball-Labs logo added
as a 5% opacity watermark in the scroll area background.

**Remote monitor redesign (`pm_monitor.py`):**  Per-step rows with individual bars
under each job card. Expand/collapse (▶/▼) persists across 2-second auto-refresh
polls via a JS `Set` keyed on job description. Logo embedded as base64 CSS watermark.
Default port changed from 8080 → 8647. Fixed a critical onclick bug: `JSON.stringify`
produced unescaped double-quotes inside an HTML `onclick="..."` attribute, silently
breaking the expand button. Fixed with `esc(JSON.stringify(...))`.

**HUD progress fixes:**  Three compounding issues resolved:
1. `\r` + `end=""` output style — `processBuffer` split on `\r` but left the
   content in the buffer until the next `\r` arrived (one-step lag); the final
   update arrived only when the `\n`-terminated "Done" line flushed the buffer,
   making it appear as a 0% → 100% jump.
2. No signal during the setup phase (GPS load, font init, ffmpeg subprocess start)
   — bar stayed at 0% for up to a minute, then jumped.
3. Fast-rendering trips emit many % updates between 100 ms polls; the bar jumped
   rather than filling smoothly.
   Fixes: plain `\n`-terminated output; pre-loop `0%` line; 100 ms time throttle.

**Concat stage progress:**  `probeSegmentDurations` gained an optional per-segment
callback. `buildCameraFile` now emits progress 0–24% during the ffprobe phase,
then 25–100% during ffmpeg concat. `ffmpegRunner` in `CollageWorker` scales the
ffmpeg pct into the 25–100% band for "concat:" labels.

**MP4/WebM output branding:**  `brandOutputFile()` in `video_build.cpp` and
`_brand_output()` in the three Python render scripts and `pathmux-tl/MainWindow.cpp`.
After each successful encode, a silent `ffmpeg -c copy -metadata ...` pass tags the
file with `vendor`, `product`, `version/HWM`, `created_on` (hostname), `content_type`.
Key debugging discovery: the temp file must carry the source file's extension so
ffmpeg detects the correct muxer; `-map 0` is required to carry the VP9 alpha stream
on WebM files.

**VERSION_HWM:**  `#define VERSION_HWM "00117"` added to `version.hpp`. Embedded in
the `PATHMUX_LICENSE_NOTICE` canary string (`PathMux 2.0.0, HWM 00117`). Python
scripts carry matching `_PATHMUX_HWM = "00117"` constants.

**Output filenames include MID:**  `ftid` helper (`MID-TID` uppercase, `-` separator)
introduced in `ExtrasDialog::onQueue()` and `TripPropertiesDialog`. All generated
output paths now include the manifest ID: `pm_trip_XP-6Z_hud.webm`. Colon excluded
from filenames (invalid on Windows/macOS).

**Build/package version mismatch:**  `package-macos.sh` and `package-windows.ps1`
now have the same cache-vs-header version check that `package-linux.sh` already had.
All six scripts auto-reconfigure before packaging if the CMake cache is stale.

**macOS trip tile text fix:**  `m_startLabel`, `m_durationLabel`, and `m_detailLabel`
lacked explicit `color:` stylesheets. On macOS with dark mode or the native Qt theme,
the palette `WindowText` resolved to white against the white tile background — text
was invisible. Explicit colours added; `setEnabled(false)` on the detail label
replaced with `color: #888` in the stylesheet.

**Stage labels:**  "concat:Front" → "Front - join segments", "collage:4K" → "Collage
4K" etc. Case-mismatch bug fixed (camera names are capitalized; mapping checked
lowercase).

**SN high-water mark:** 00115 → 00117.

---

## 2026-05-09

### Session — Job Queue hardening, ExtrasDialog, remote monitor, collage fixes

**Focus:** A long day of live testing and bug-fixing across the job queue panel, the
tile-level workflow, and the collage build pipeline. Several architectural decisions
reversed (expandable tile panel abandoned for a dialog) and multiple crash sources
eliminated.

**TripPropertiesDialog — Outputs tab:**  The four-tab bottom bar (GPS / Map /
Dashboard / HUD) was collapsed into a single scrolled "Outputs" tab on the top bar.
The two-bar tab system was eliminated — now a single `m_topTabBar` with direct
`currentChanged → setCurrentIndex` wiring. All widget members and slot logic are
unchanged; the tabs are now stacked QGroupBoxes in a QScrollArea. Sync Values tab
stays in the top bar unchanged.

**TripTile Extras button → ExtrasDialog:**  The expandable tile panel was
implemented, tested, then abandoned. The tile height variation required non-trivial
layout changes in `layoutTiles()` (per-row variable heights), and the checkboxes
truncated at small zoom levels. Replaced with a lightweight modal dialog opened from
an "Extras…" button at the bottom of the tile card. Dialog uses a `tree -d` style
layout: GPS Extract (full row, red/green label) branching to ├─ Map / ├─ Dashboard /
└─ HUD, then Sync Cameras below. All items start unchecked; user selects what to
build and clicks Queue. GPS Extract auto-checks when any GPS-dependent item is
selected and GPS isn't yet extracted.

**SyncAnalysisJob:**  New job type that wraps `pm_sync_analyze.py`. Key discovery:
the script takes `MID:TID` as a positional argument and finds the manifest itself —
it does NOT accept `--manifest` or `--trip` flags. The initial implementation passed
both incorrectly (from the MapRenderJob pattern), causing argparse to reject every
invocation. Fixed to pass `"WT:1B --all-segments --write"`.

**QProcess thread-safety crash (main thread freeze):**  The root cause of the
repeated "both windows freeze and force quit required" bug was identified:
`MapRenderWorker::cancel()` called `m_proc->kill()` from the main thread, but
`m_proc` (a `QProcess`) is owned by and only safe to access from the worker thread.
When the main thread and worker thread both held Qt's internal QProcess mutex, the
result was a deadlock. Fix: `cancel()` now only sets the atomic flag; the worker's
own 100ms poll loop checks it and calls `proc.kill()` from the correct thread —
the same pattern `CollageWorker` was already using correctly.

**Use-after-free crash on X button (finished jobs):**  `m_worker` in MapRenderJob /
SyncAnalysisJob / CollageJob was nulled by `QThread::finished → worker->deleteLater()`,
but the job object itself outlived the worker. When the job destructor later ran
`m_worker->cancel()`, the pointer was dangling. Fix: the `QThread::finished`
lambda now calls `m_worker->deleteLater()` AND immediately sets `m_worker = nullptr`.
The destructor's `if (m_worker)` check then correctly skips the stale call.

**stageLog explosion on parallel camera concats:**  With `parallelConcats` enabled
(the GUI path), four camera threads emit progress signals concurrently. The main
thread interleaves their events, so the label alternates between "concat:Front" /
"concat:Rear" / etc. on nearly every event. The `CollageJob` handler emitted a
`stageLog` on every label change — producing hundreds of entries (one per progress
event) for a 42-minute trip. Fix: `stageLog` is now emitted only when `pct == 100`
(a stage actually completed). Result: one clean ✓ entry per camera/encode stage
instead of hundreds.

**Job queue destructor guard:**  All four job destructors (`GpsExtractJob`,
`MapRenderJob`, `SyncAnalysisJob`, `CollageJob`) now check `m_thread->isRunning()`
before calling `quit()`/`wait()`. For a completed job whose thread has already
exited, the check is false and the destructor returns immediately. Previously,
deleting N finished jobs simultaneously could cause N `wait()` calls, each
potentially blocking the main thread if the thread was in a brief teardown race.

**Remote job queue monitor:**  `pm_monitor.py` (new, `scripts/`) is a stdlib-only
Python HTTP server that serves a live job queue status page on port 8080. `pathmux-gui`
writes `/tmp/pathmux_jobs.json` via `JobQueueMonitor` (new, `gui/`) whenever the
queue changes (debounced 250ms). The status bar at the bottom of the Job Queue panel
now contains a pill-shaped red/green toggle that starts/stops the monitor, a live
job counter, and the URL when running. `pkill -f pm_monitor.py` is called before each
start to clean up any stale instance from a previous crashed session.

**Floating job queue fixes:**
- Blank detached window: `QDockWidget::setWidget(nullptr)` followed by
  `setParent(newParent)` left the panel explicitly hidden. Added `m_jobPanel->show()`.
- Dock ghost on job submission: `jobAdded` auto-show was not guarded; now only
  shows dock when not detached (`if (!m_jobFloatWin)`).
- Close = just close: eventFilter no longer re-shows the dock when the float
  window is closed by the user.

**Border consistency:**  `QWidget#jobFloatWin` added to the global `2px solid #5a5a5a`
border rule. `QFrame#paneFrame` border darkened from `#909090` to `#5a5a5a` to match.

**HUD resolution bug:**  `ExtrasDialog` was launching `pm_hud.py` with
`m_trip.videoProfile.width/height` (the per-camera stream dimensions, e.g. 1920×1080).
The HUD WebM was generated at 1920×1080 and overlaid at (0,0) on the 3840×2160
collage, covering exactly the top-left (front camera) quadrant. Fixed to hardcode
3840×2160, matching `TripPropertiesDialog`'s default spinbox values.

**CollageJob description includes basename:**  If the user sets a custom basename in
`TripBuildDialog`, it now appears in the job queue entry ("Collage — VP:8F  SwiftTruck"
instead of the raw date).

**Clear Finished button removed:**  Multiple implementations crashed or froze the
app. Individual X buttons on finished rows handle cleanup via the proven `dismissRow()`
path. The button was not worth the risk.

**X button behaviour:**  Running → `cancel()`; Queued / Done / Failed / Cancelled →
`dismissRow()` (immediate removal from queue and panel).

**Files added:** `gui/JobQueue.h`, `gui/JobQueue.cpp`, `gui/JobQueuePanel.h`,
`gui/JobQueuePanel.cpp`, `gui/ExtrasDialog.h`, `gui/ExtrasDialog.cpp`,
`gui/JobQueueMonitor.h`, `gui/JobQueueMonitor.cpp`, `scripts/pm_monitor.py`,
`.claude/commands/end_session.md`

**Files changed:** `gui/MainWindow.h`, `gui/MainWindow.cpp`, `gui/TripTile.h`,
`gui/TripTile.cpp`, `gui/TripGridPanel.h`, `gui/TripGridPanel.cpp`,
`gui/TripPropertiesDialog.h`, `gui/TripPropertiesDialog.cpp`, `gui/main.cpp`,
`CMakeLists.txt`

**Version/SN:** Unreleased (post-v1.9.10a). SN high-water mark bumped to 00113.
Build not yet cut — active testing in progress.

---

## 2026-05-08

### Session — Job Queue hardening, pane borders, sync script improvements

**Focus:** Making the Unified Job Queue Panel production-ready through a day of
live testing. A number of bugs surfaced and were fixed in sequence.

**Queue halt (root cause found):**  The `finished → tryStartNext` connection
was made lazily inside `tryStartNext()` itself — if anything interrupted the
path to that connect() call, the chain broke permanently. Fix: connect in
`enqueue()` unconditionally. Every job now has the connection from the moment
it enters the queue.

**Manifest not updated after queue render:** `MapRenderJob` (map/dashboard/HUD)
only wrote the output path to the manifest if the originating
`TripPropertiesDialog` was still open when the job finished. If the user closed
the dialog during a render, the file existed on disk but was absent from the
manifest — TripBuildDialog couldn't see it. Fix: `MapRenderJob::start()`
writes the path to the manifest directly in its `finished` handler, keyed by
script name (`pm_maprender.py` → `mapVideos`, etc.), before emitting `finished`.

**Segfault on GPS extract (Cobra/no-sync cameras):** `onRunSyncAnalysis()` was
called automatically after every successful GPS extraction. It dereferenced
`m_syncRunBtn` and `m_syncOutput` without null checks — both are `nullptr`
when the camera profile has no sync tab. Added early-return guard.

**QSizeGrip cursor over menu bar:** After adding QFrame wrapper panes, the
`QSizeGrip` (added for HiDPI resize convenience) migrated to the top-left of
the window, covering the File and View menu items with its diagonal resize
cursor and blocking clicks. Removed entirely — GNOME handles window resizing.

**Job Queue Panel improvements:**
- Title bar live counts: "Completed: N   —   Queued: N"
- × on finished rows dismisses/removes them; × on running still cancels
- Dot turns orange immediately when a job starts (was staying gray through
  the entire run)
- Collage rows grow a per-stage history below the main progress bar: each
  ffmpeg stage logs a ✓ or ✗ line as it completes, matching the BuildProgressDialog
  experience. Stage label changes in the CollageWorker progress signal drive this.

**Floating job queue window:** Multiple attempts to fix the floating dock
chrome (Qt::Window flags, setTitleBarWidget, QTimer deferral). Final working
approach: "Detach" button bypasses `setFloating()` entirely and creates a real
`Qt::Window` QWidget, reparents the panel into it, and hides the dock.
`eventFilter` on the window catches close and moves the panel back. This
guarantees OS decorations without fighting QDockWidget's internal flag management.

**Sync analysis output preservation:** `refreshSyncTab()` tore down the entire
sync widget on completion to rebuild the trim table, destroying the QPlainTextEdit
containing the script output. Now saves the text before rebuild and restores it
into the new widget.

**pm_sync_analyze.py — flexible reference camera:** The script hard-required a
left camera as the audio sync reference; Cobra (front + rear only) failed with
"Left camera not present." Reference is now auto-selected: left > front > rear >
right > first available. All-segments mode now prints verbose extraction and
correlation detail per segment (was silent during extraction), with a clean
summary table at the end.

**TripTile expand panel design recorded:** Shower thought captured in
`memory/shower_trip_tile_expand.md` — collapsible bottom half with per-item
queue checkboxes (GPS Extract, Sync, Map, Dashboard, HUD) plus "Queue Checked"
button; GPS/Map/Dashboard/HUD tabs in TripPropertiesDialog merge into a single
Outputs list tab.

**Files changed:** `gui/MainWindow.h`, `gui/MainWindow.cpp`, `gui/main.cpp`,
`gui/JobQueue.h`, `gui/JobQueue.cpp`, `gui/JobQueuePanel.h`,
`gui/JobQueuePanel.cpp`, `gui/TripPropertiesDialog.cpp`,
`scripts/pm_sync_analyze.py`

**Version/SN:** Unreleased post-v1.9.10a. SN high-water mark remains 00112.
Build not yet cut — session ended before final test cycle.

---

## 2026-02-25

### Session 1
**Focus:** Rebrand QuadEye → PathMux, project documentation, resync planning

**Decisions Made:**
- Name changed to PathMux — "Path" covers road/filesystem duality, "Mux" 
  accurately describes core output (multi-camera streams + GPS track combined 
  into unified presentation). Confirmed when noted that 3 camera feeds + moving 
  map is literally path multiplexing.
- Debug utilities adopt `pm_` prefix convention (e.g. `pm_tripdebug`)
- SN bumps on rename-only changes are incorrect per project policy — text 
  substitution is not a code change. Rebrand files should retain their 
  pre-rename SN values.
- Logo base: PM_logo2 (four road views, no compass) vs PM_logo3 (with compass).
  Compass retained — GPS integration is a core differentiator, compass earns 
  its place in the logo.
- Logo animation plan: four arrows (blue, orange/gold, green, red) orbiting the 
  sphere, passing behind dashcam housing at top. Arrows-only animation is the 
  right instinct — animating frame content simultaneously would be too busy.
- Static logo PNG used as dead-camera placeholder in collage generation. 
  Future swap to looping .ts/.mp4 is a file reference change only, no logic change.
- Single monolithic chat approach is impractical due to context window limits.
  Correct pattern: project knowledge base is ground truth, chat sessions are 
  working sessions. Significant decisions get written back to project docs.
- Session_Log.md created as detailed companion to CHANGELOG — captures the 
  why behind decisions, not just the what.

**Work Done:**
- Renamed binary, config dir, blacklist entry, all user-visible strings 
  across main.cpp, config_manager.cpp, version.hpp, debug_main.cpp, 
  Makefile, README.md
- Added `debug` Makefile target for `pm_tripdebug`
- SN added to debug_main.cpp (was missing entirely)
- Created ToDo.md with current bug list, feature queue, and waiting items
- Identified project knowledge base is significantly stale:
  - Still references QuadEye
  - SN high-water mark in project docs: 00009/00010
  - Actual current high-water mark: 00030
  - 8+ source files exist that project has never seen:
    gpx_export.cpp/.hpp, kml_prefs.cpp/.hpp, locations.cpp/.hpp,
    prefs.cpp/.hpp, video_build.cpp/.hpp, ui_helpers.hpp

**Correct SN State (rebrand files retain pre-rename values):**
- main.cpp — 00030 (per grep, not 00010 from rebrand)
- config_manager.cpp — 00030
- version.hpp — 00030
- debug_main.cpp — 00023
- Makefile — current value on disk
- ToDo.md — 00010 (new file, correct)
- Session_Log.md — 00010 (new file)

**Deferred to Next Session:**
- Full project resync: upload all current source files to project knowledge base
- Update pathmux_project_brief.md to reflect current reality (rename + new modules)
- GitHub repo rename from quadeye to pathmux
- Tarball extraction alongside current local source for file comparison
- Verify Makefile archive target resolves correctly after repo rename

**Open Questions:**
- Gap threshold: current `segdur + 30s` likely needs to be 30 minutes for 
  real-world fuel stop behavior. Pending testing.
- ExifTool 13.51+ release date unknown — GPS extraction implementation 
  is ready and waiting.

**Next Session Critical Path (in order):**
1. Upload all current .cpp/.hpp/Makefile to project knowledge base
2. Upload updated pathmux_project_brief.md
3. Upload current CHANGELOG.md and ROADMAP.md
4. Rename GitHub repo
5. Update local git remote URL
6. Verify build still works cleanly after rename
7. Then resume normal development

---

## 2026-02-28

### Session 1
**Focus:** pm_gpsinfo --scan-all-trips batch GPS lock scanner; workflow walkthrough

**Decisions Made:**
- `MID:TID` colon-separated syntax established as project-wide addressing convention
  for referencing a specific trip (e.g. `CQ:73`). Reserved for future batch manager.
  Colon chosen over slash to avoid ambiguity with filesystem paths.
- `TripSegment.front/rear/left/right` confirmed to store absolute paths in the
  manifest — not relative. Caught as a bug mid-session (all entries showed `!file`
  until the `sourcePath +` prepend was removed). Documented in CLAUDE.md.
- Housekeeping commits (CHANGELOG, ROADMAP, CLAUDE.md) kept separate from code
  commits — user's explicit preference, confirmed this session.
- Claude runs commands via Bash tool directly; user does not need external terminal
  for output review during a session.
- Session_Log lives in the source tree (`/z/dash/src/`), not in the memory dir.

**Work Done:**
- Added `--scan-all-trips` mode to `pm_gpsinfo`: reads all manifests, runs exiftool
  on first Front segment of each trip, reports seconds-to-GPS-lock in a table
- Fixed path bug: segment fields are absolute, not relative to sourcePath
- Changed progress output separator from `/` to `:` (`Scanning CQ:73`) for MID:TID
- Version bumped to 0.9.3, HWM to 00070
- CHANGELOG, ROADMAP, CLAUDE.md, Session_Log all updated (separate commits)
- Memory files updated: `pathmux.md` created in memory dir, `MEMORY.md` updated

**Real-World Results from --scan-all-trips:**
- 35 trips across multiple manifests scanned
- Most show `0s` lock — warm GPS starts (chip retained last position from prior use)
- `none` on two ~37s stub segments — genuine cold starts, no fix acquired
- Older manifests (pre-segdur) show `0s` segdur but GPS still scanned correctly

**Pending (carry forward):**
- Store GPS lock time back into manifest (`gpsLockSeconds` field per trip)
- `pm_gpsinfo MID:TID` direct addressing mode (no file path needed)
- `selectTrip()` unused `mode` parameter — `ExportMode /*mode*/`
- Man page updates (-G interactive flow, --validate, -t flags)
- GPS extraction to GeoJSON (architecture decided, code pending)

---

### Session 2
**Focus:** libpathmux.a library restructure completion; pm_gpsinfo enhancements;
v1.0 planning; utility suite scoping

**Decisions Made:**
- Tarball delivery workflow retired — VSCode + Claude Code + git push is the
  delivery mechanism. CLAUDE.md updated to reflect this; "cut the tarball" steps
  now end at `git push origin main`. No `.tgz`, no `present_files`.
- Session_Log file renamed from per-session files (`Session_Log_20260225.md`) to
  a single `Session_Log.md` with dated entries — more granular and avoids file
  proliferation. Renamed via `git mv` to preserve history.
- `pm_` prefix adopted as the project-wide naming convention for all standalone
  utility binaries. Consistent with `pm_gpsinfo`; signals a coherent suite.
- `pm_gpsexport` chosen over `pm_export` — the more specific name makes clear it
  exports the GPS track, not the trip footage. Making format a runtime flag
  (`--gpx`, `--kml`) eliminates the need for a separate `pm_convert` tool.
- `pm_probe` scoped as the camera compatibility/fingerprinting tool: single-file
  mode for per-segment inspection; `--card` mode fingerprints a full SD card root
  and produces a structured report suitable for GitHub issue submission to enable
  new camera support. Primary on-ramp for multi-brand contributions.
- v1.0 milestone: after resolving known bugs and TODO items, the project is
  functionally ready for v1.0. Hard dependency: license decision (GPL vs MIT)
  must be made and propagated to all pertinent source files before repo goes
  public. Repo currently private with one collaborator.

**Work Done:**
- **Library restructure completed (v0.9.4, HWM 00071):**
  - Fixed remaining build errors in `cli/find_trips.cpp` (`UI::haversineKm` →
    `haversineKm`, `UI::formatDistance` → `formatDistance` — 8 occurrences)
  - Fixed `-Wcomment` warning in `lib/platform.hpp` (backslash in comment)
  - Build verified clean; behavior confirmed against original on real footage
  - Merged `libpathmux-restructure` branch to `main` (fast-forward), removed worktree
  - Library layout: `lib/` (pathmuxlib.a, `namespace Pathmux`), `cli/` (front-end,
    `using namespace Pathmux`), `tools/` (pm_gpsinfo, debug_main)
  - `lib/platform.cpp/.hpp` — OS abstraction: `getHomePath`, `getConfigDir`,
    `getTerminalWidth`; Linux implemented; Windows/macOS branches documented
  - `lib/format_helpers.hpp` — pure math/format helpers, no POSIX deps, Qt6-safe
  - CMakeLists.txt rewritten: `pathmuxlib STATIC` with PUBLIC include propagation
- **pm_gpsinfo enhancements (post-0.9.4):**
  - Added `gpsLockSeconds` to `Trip` struct; serialized in config_manager.cpp
    (only written when `>= 0`; default `-1` on load)
  - `scanAllTrips()` refactored to use `ConfigManager::loadTripCache()` /
    `saveTripCache()` — raw JSON parsing eliminated; MD5 integrity automatic
  - Write-back only when `lockSec >= 0 && trip.gpsLockSeconds != r.lockSec`
  - `resolveMidTid()` added: resolves `MID:TID` address → absolute front segment
    path; normalizes O→0, I→1, L→1, uppercase; clear error messages per case
  - `--scan-all-trips` mutex updated to reject MID:TID args cleanly
- **PROPOSED_UTILS.md created:** Specifications for `pm_ls`, `pm_audit`,
  `pm_gpsexport`, `pm_probe` with usage, output format, implementation notes,
  and priority table
- CHANGELOG, ROADMAP, CLAUDE.md all updated; Session_Log renamed (`git mv`)

**Portability Discussion:**
- macOS: near-trivial — POSIX-compatible, `getConfigDir()` macOS branch just needs
  filling in and testing
- Windows: moderate — abstraction layer in place; remaining: `_popen` wrapper,
  `GetConsoleScreenBufferInfo`, path separator handling for manifest storage,
  `strptime` substitute, tool path detection
- pm_* tools are thin main() wrappers over library calls — port automatically
  once libpathmuxlib.a builds on the target platform

**Pending (carry forward):**
- Resolve all known bugs and TODO items (next session priority)
- License decision: GPL vs MIT — must be made before repo goes public; all
  source files will need license headers once decided
- ffprobe integration for accurate trip duration (high priority)
- GPS extraction to GeoJSON (architecture decided, ExifTool 13.51+ available)
- Man page updates (-G interactive flow, `--validate`, `-t` flags)
- `selectTrip()` unused `mode` parameter — `ExportMode /*mode*/`
- `trip_debug` → `pm_tripdebug` rename (low priority)
- pm_ls, pm_audit, pm_gpsexport, pm_probe — proposed, not yet implemented

---

## 2026-03-01

### Session 1
**Focus:** Bug list triage — items 1–3; v0.9.5a release

**Work Done:**
- **v0.9.5a (HWM 00072):** Three items from bug queue resolved.
  - `selectTrip()` unused `mode` parameter silenced with `ExportMode /*mode*/`
    in both declaration and definition. Retained in signature for future use.
  - Duplicate `// SN:` removed from top of `pm_gpsinfo.cpp` header block.
    Canonical SN location is bottom-of-file only; duplicate caused `sn-audit`
    to emit two rows for `pm_gpsinfo.cpp`.
  - `promptLine()` bare-Enter at "Output directory" prompt: verified correct —
    no `cin >>` mixing in the interactive GPS export path; `promptLine()` returns
    default on bare Enter as intended. No code change needed.
- CHANGELOG, Session_Log, CLAUDE.md updated; known issues list pruned.

**Pending (carry forward):**
- Man page updates (-G interactive flow, `--validate`, `-t` flags)
- GPS extraction to GeoJSON (architecture decided, code pending)
- ffprobe integration for accurate trip duration (high priority)
- `trip_debug` → `pm_tripdebug` rename (low priority)
- pm_ls, pm_audit, pm_gpsexport, pm_probe — proposed, not yet implemented
- License decision: GPL vs MIT — required before repo goes public

---

### Session 2 (Morning)
**Focus:** ID-based manifest filenames; scan-prompt; validation UX fix

**Work Done:**
- **v0.9.6c (SN 00077):** Added scan-prompt when `-s/--scan` targets a path that
  already has a manifest. Options: overwrite, delete-and-rescan, or quit.
- **v0.9.7a (SN 00078):** Switched manifest filenames from `pm_manifest_<sanitized_path>.json`
  to `pm_manifest_<id>.json`. The 2-char base36 ID is now immediately visible in a
  directory listing and consistent with MID:TID addressing.
  - `ensureManifestId()` — write-side: ensures an ID exists before constructing filename
  - `lookupManifestFilePath()` — read-only: resolves path from index; transparently
    migrates old sanitized-path filenames via `fs::rename()` on first access
  - `getManifestFilePath()`, `isCached()`, `loadTripCache()`, `updateManifestIndex()`
    all updated to use the new helpers
- **v0.9.7b (SN 00079):** Fixed `validateManifestIndex()` blocking an explicit
  `-s/--scan` with interactive prompts for every stale entry in the entire index.
  - Missing manifest entries now silently auto-pruned (one-line note, no prompt)
  - Interactive prompt reserved for md5-mismatch case only (file exists but externally modified)
  - `[R]` re-scan option removed (was a no-op)
  - Both validate functions now use `lookupManifestFilePath()` for accurate file resolution

**Decisions Made:**
- Stale manifest entries: move to `~/.config/pathmux/manifests_stale.json` (separate file,
  never read during normal operations) rather than silently discarding. Logged in ROADMAP.
- `--clear-cache` / `--clear-stale` UX rework scheduled as next session's first task
  before resuming the main bug/ToDo list.

**Pending (carry forward):**
- `--clear-cache` / `--clear-stale` UX rework (next session, first item)
- Implement `manifests_stale.json` archive for pruned entries
- Man page updates (-G interactive flow, `--validate`, `-t` flags)
- GPS extraction to GeoJSON (architecture decided, code pending)
- ffprobe integration for accurate trip duration (high priority)
- `trip_debug` → `pm_tripdebug` rename (low priority)
- License decision: GPL vs MIT — required before repo goes public

---

### Session 3 (Afternoon) — continued
**Focus:** `--clear-cache` / `--clear-stale` UX rework; stale manifest archive

**Work Done:**
- **v0.9.8 (SN 00079):** Full manifest management UX pass.
  - `manifests_stale.json` archive: stale entries pruned from live index are now
    appended to `~/.config/pathmux/manifests_stale.json` (write-only at runtime).
    Preserves id, path, manifestFile, lastScan, tripCount, note, and a `pruned` timestamp.
  - `--show-stale`: displays stale archive in a formatted table.
  - `--clear-stale [--force]`: wipes stale archive with confirmation; `--force` skips prompt.
  - New `Manifest management:` section in usage output; `--validate` moved there.
  - `--force` after `--clear-cache` is now order-independent (scanned from remaining argv).
  - Fixed two `std::cin >>` calls in `clearCache()` → `std::getline(std::cin >> std::ws, ...)`.
  - `validateManifestIndex()` stale message updated to say "archived" and cite `--show-stale`.

**Files changed:** `lib/config_manager.cpp`, `lib/config_manager.hpp`, `cli/main.cpp`

**v0.9.9 (same session, SN 00079):** Thumbnail detection complete.
- `TripSegment` gains `frontThumb`, `rearThumb`, `leftThumb`, `rightThumb`
  (absolute .jpg path or "" if absent; populated by `thumbFor()` helper at scan time)
- `Trip` gains 8 trip-level convenience fields: `firstFront/Rear/Left/RightThumb`
  and `lastFront/Rear/Left/RightThumb`
- `first*` uses segments[1] (cold-start avoidance); falls back to segments[0]
- All fields serialized in manifest JSON; backward-compatible on load
- ffprobe duration confirmed already implemented; ROADMAP updated
- Completes all Phase 1 Trip Detection & Caching roadmap items

**Pending (carry forward):**
- Man page updates (-G interactive flow, `--validate`, `-t` flags, new stale flags)
- GPS extraction to GeoJSON (architecture decided, code pending)
- `trip_debug` → `pm_tripdebug` rename (low priority)
- License decision: GPL vs MIT — required before repo goes public

---

## 2026-03-02

### Session 1 (Afternoon)
**Focus:** pm_probe; exiftool version policy; GitHub collaborator permissions

**Work Done:**
- **pm_probe (v0.9.10c, SN 00080):** New `tools/pm_probe.cpp` — camera compatibility profiler.
  - Single-file mode: `pm_probe <file.ts>` or `pm_probe MID:TID` — reports container,
    resolution, frame rate, pixel format, color space, duration, stream list, GPS method,
    first GPS fix (timestamp, lat, lon).
  - Card mode: `pm_probe --card <path>` — fingerprints full dashcam storage root; finds
    camera dirs, samples up to 5 segment durations, probes first segment of primary camera.
    Output formatted for pasting into a GitHub issue.
  - `--json` flag for all modes.
  - GPS first fix fields renamed `sample_*` → `first_*` (`first_lat`, `first_lon`,
    `first_timestamp`, `has_fix`) — clearer naming, not a statistical sample.
  - Man page: `man1/pm_probe.1` — "PathMux Suite - Camera Profiler".
  - Added to CMakeLists targets and install list; pathmux.1 SEE ALSO updated.

- **ExifTool version policy (all files):** Dropped all "13.51+" version requirements.
  PathMux no longer validates exiftool versions. If GPS extraction returns no data or
  corrupted/garbled data, user is directed to the exiftool maintainer. RPM Requires
  changed from `exiftool >= 13.51` to `exiftool`. Preemptive: user emailed Phil Harvey
  and invited him as GitHub collaborator.

- **Man page URL cleanup:** Removed inline `https://exiftool.org` URLs from man page
  prose (not standard practice). Replaced with "contact the ExifTool maintainer directly"
  and `SEE ALSO exiftool(1)`.

- **GitHub permissions:** Reviewed collaborator access. Personal repos only support
  Write or nothing for collaborators (no read-only option). Current collaborators:
  BiloxiGeek (admin), xplatform12/Chad (write), Phil Harvey invite pending.
  Decision: leave as-is, both are trusted.

**Files changed:** `tools/pm_probe.cpp` (new), `man1/pm_probe.1` (new),
`CMakeLists.txt`, `man1/pathmux.1`, `man1/pm_gpsinfo.1`, `man1/pm_gpsexport.1`,
`lib/gps_export.cpp`, `cli/gpx_export.cpp`, `tools/pm_gpsinfo.cpp`, `README.md`

**Pending (carry forward):**
- License decision: GPL vs MIT — next session; waiting on Phil Harvey response
- Man page updates (-G interactive flow, `--validate`, `-t` flags)
- GPS extraction to GeoJSON (architecture decided, code pending)
- Open bug: GPX/KML default output path should follow manifest dir, not global defaultExportDir
- CHANGELOG.md and CLAUDE.md not yet updated for v0.9.10 series

---

## 2026-03-04

### Session 1
**Focus:** Housekeeping — backfill docs, rename trip_debug, record CameraProfile architecture

**Work Done:**
- **CHANGELOG.md backfilled:** Entries for v0.9.10 through v0.9.10d were missing;
  all added from git log and Session_Log.
- **ROADMAP.md:** Corrected utility suite checkboxes (pm_gpsexport, pm_ls, pm_audit,
  pm_probe, pm_tripdebug all marked done). Added CameraProfile extraction TODO under
  Phase 1 Active Work. Added `Optional Camera Handling` and `User Support Model`
  sections to Multi-Brand Dashcam Support.
- **`trip_debug` → `pm_tripdebug` (v0.9.10e, SN 00081):**
  - `git mv tools/debug_main.cpp tools/pm_tripdebug.cpp`
  - All "trip_debug" strings in source updated to "pm_tripdebug"
  - CMake target and install target renamed; `pm_tripdebug` added to packaging install
  - `man1/pm_tripdebug.1` created — "PathMux Suite - Trip Detection Debugger"
  - `pathmux.1` SEE ALSO updated; `lib/version.hpp` suffix bumped to "e"
- **CameraProfile/StorageFormat architecture decision recorded** (from 2026-03-04
  planning session on claude.ai):
  - Camera format detection to be extracted from `trip_detection.cpp` into a
    separate library layer; TripDetection consumes it; rest of pipeline sees
    normalized output
  - Goal: field bug reports only require updating detection layer, not trip logic
  - `pm_probe --card` is the natural entry point; output formatted for GitHub issues
  - Optional cameras: must handle both empty-dir and absent-dir gracefully
  - Simulation plan: Front/Left/Right populated, Rear empty/absent
  - Cobra Drive HD dual-view card incoming as first non-D90 test case
  - Added to CLAUDE.md architecture section and ROADMAP Multi-Brand section

**Files changed:** `tools/pm_tripdebug.cpp` (renamed from debug_main.cpp),
`man1/pm_tripdebug.1` (new), `CMakeLists.txt`, `lib/version.hpp`, `man1/pathmux.1`,
`ROADMAP.md`, `CHANGELOG.md`, `CLAUDE.md`

**Pending (carry forward):**
- License decision: GPL vs MIT — waiting on Phil Harvey response
- Man page updates (-G interactive flow, `--validate`, `-t` flags)
- GPS extraction to GeoJSON (architecture decided, code pending)
- Open bug: GPX/KML default output path should follow manifest dir, not global defaultExportDir
- CameraProfile/StorageFormat layer implementation (architecture decided, code pending)

---

## 2026-03-05

### Session 1
**Focus:** Shower thoughts / planning; bug triage; man page; project infrastructure; Nutball-Labs org setup

**Shower Thoughts Recorded (ROADMAP):**
- **Smart Collage / Points of Interest:** When camera streams and moving map are
  ready, user assigns streams to collage quadrants, marks POI timestamps with
  captions and attached media. Collage opens at real-time then ramps up to timelapse
  between POIs; ramps down approaching each POI; normal/slo-mo during POI; ramps
  back up after. End-of-trip slows naturally.
- **Target-duration mode:** User specifies desired output length; system calculates
  required timelapse speed from `(total_source - POIs - ramps) / time_budget`.
  Warns if speed is infeasible.
- **Speed map preview:** Before rendering, present a plain-text timeline
  (`1x for 15s → 8x for 90s → 1x for 3m during incident → ...`) with total
  duration. User accepts or requests adjustments in a confirm/edit loop (CLI) or
  drag-and-drop timeline (GUI).
- **New dev machine incoming:** i7, 16 GB DDR5, RTX 4060, 1 TB NVMe. RTX 4060
  supports NVENC — ffmpeg `-c:v h264_nvenc` / `-hwaccel cuda`. Relevant to collage
  pipeline architecture. README will include real-world hardware benchmark section.
- **Community brag board:** GitHub issue template structured around a self-contained
  `buildHistory` JSON block (copy-paste from manifest, no reformatting). `userName`
  field included. BENCHMARKS.md maintained as leaderboard. Top 3 encode times
  published in the man page with each release.
- **Build timing telemetry:** `buildHistory` section in trip manifest JSON records
  per-camera concat time, collage build time, timelapse encode time. Supports
  multiple build records per trip. JSON block isolated for copy-paste submission.

**Bug Triage:**
- **GPX/KML default output path** — investigated; found already fixed in v0.9.6a
  (commit `417e901` message: "fix GPS output default path"). `runInteractive()` has
  writable-check logic: defaults to footage source dir if writable; offers three-
  option prompt otherwise. Cleared from CLAUDE.md known issues.
- **ROADMAP housekeeping noted but deferred:** duplicate `**What's done:**` header
  in GPS Data Extraction section; ExifTool runtime-check note contradicts current
  no-version-check policy. Carry forward.

**Man Page (`man1/pathmux.1`):**
- Expanded `-G` section from one line to a full interactive flow reference:
  trip picker commands (TripID, A, E, M, Q); action menu (G, X, K, J, Q);
  output directory logic (source-dir default, writable check, three-choice fallback).
- Added **BRAG BOARD** section — community encode timing leaderboard seeded with
  three fake-but-plausible entries (Nutball Labs #1: i7/RTX 4060/NVMe, 47-min trip,
  6m 22s 4K collage; two CPU-only entries for comparison). Points to `buildHistory`
  JSON block for submission workflow.

**Infrastructure:**
- `cmake/sn_audit.cmake` updated: glob now includes `*.md` files; regex updated
  from `^(//|#)` to `^(//|#|<!--)` to match HTML comment SN format.
- `<!-- SN: 00081 -->` added to all PathMux `.md` files (CHANGELOG, CLAUDE,
  ROADMAP, README, Session_Log, PROPOSED_UTILS, pathmux_project_brief,
  ROADMAP_MacOS, ROADMAP_WINDOWS). HTML comment — invisible when rendered.
- CLAUDE.md SN convention updated to document all three formats.
- Git remote updated: `git@github.com:BiloxiGeek/PathMux.git` →
  `git@github.com:Nutball-Labs/PathMux.git`.
- `~/.claude/settings.json`: `Read` added to permissions allow list —
  file reads auto-approve without user prompt.
- Session Rules section established in both project CLAUDE.md files.

**SRoute Project Setup** *(recorded here; full detail in SRoute Session_Log)*
- Nutball-Labs GitHub org confirmed (`Nutball-Labs`); both PathMux and SRoute
  repos live there.
- SRoute git repo initialized at `/z/sroute`; initial commit pushed.
- SRoute ROADMAP cleaned of PathMux dashcam content that had leaked in.

**Files Changed:** `man1/pathmux.1`, `cmake/sn_audit.cmake`, `CLAUDE.md`,
`CHANGELOG.md`, `ROADMAP.md`, `README.md`, `Session_Log.md`, `PROPOSED_UTILS.md`,
`pathmux_project_brief.md`, `ROADMAP_MacOS.md`, `ROADMAP_WINDOWS.md`

**Pending (carry forward):**
- License decision: GPL vs MIT — waiting on Phil Harvey response
- GPS extraction to GeoJSON (architecture decided, code pending)
- CameraProfile/StorageFormat implementation (architecture decided, code pending)
- ROADMAP housekeeping: duplicate `What's done:` header; stale ExifTool policy note
- CHANGELOG not yet updated for this session's changes
- CLI polish: `--format=[json,csv,xml]`, `--fields` filtering, `recordingProfile`,
  `extra_hw_frames`
- Man page header still says v0.9.10 — update when v0.9.11 is cut

---

## 2026-03-06

### Session 1
**Focus:** GPS lock diagnostic tooling; pm_findgpslock; gps_export quiet-mode fix; v0.9.10f

**Code Changes (v0.9.10f):**
- **`tools/pm_findgpslock.cpp`** — new standalone tool. Scans one or more `.ts` files
  via ExifTool; prints header + GPS samples up to the first fully-locked record
  (valid lat/lon AND year ≥ 2000). Pre-lock rows labelled `NO_POS`, `NO_TIME`,
  `NO_POS+NO_TIME`. Normal output is two lines per file. `--verbose` passes ExifTool
  stderr to terminal. Added to CMake build and install.
- **`lib/gps_export.cpp`** — ExifTool now called with `-q` in non-verbose mode,
  suppressing `[Minor] Tag 'Main:GPSDateTime' not defined` ANSI warnings that were
  polluting the terminal. Remaining stderr redirected to `/dev/null`.
  Added year < 2000 clock check: cold-start records with unsynchronized GPS clock
  (`1900:01:00`, `1970:01:01`) are now skipped in addition to zero lat/lon records.
  New manifest fields: `pre_position_lock_samples`, `pre_time_lock_samples` — count
  of skipped records before lock, stored per-trip after extraction.
- **`tools/pm_gpsexport.cpp`** — added `--dump` flag: prints all track points to
  stdout in a tabular format for quick diagnostic inspection. Progress dots moved
  from stdout to stderr so file paths remain cleanly on stdout for pipeline use.
- **`lib/version.hpp`** — bumped VERSION_SUFFIX to `"f"` (v0.9.10f).

**GPS Lock Research:**
- Ran `pm_findgpslock` against all February `.ts` files in `/z/srcdash/ex*/Front/`
  with filenames starting `0` (03:xx–09:xx range, early morning).
- Result: essentially universal GPS lock on sample 0 for that time range.
  Single exception: `20260225_035430F.ts` — 0 samples extracted (stub/corrupt file,
  present in both ex9 and ex10 from a duplicated SD card copy, not a real exception).
- **Conclusion withheld**: early-morning result is a narrow slice. 1900-date records
  previously observed while browsing suggest cold-start is real in the broader dataset.
  Full corpus scan launched via `nohup /z/srcdash/find_lock/run_findgpslock.sh`.
  Results to be analyzed in a follow-up session.
- `run_findgpslock.sh` written to `/z/srcdash/find_lock/` — iterates all
  `*/Front/*.ts` files, writes per-file output to `<stem>.findgpslock.txt`,
  skips already-processed files (safe to restart).

**Shower Thought Recorded (ROADMAP):**
- **Combo Compass/Speedometer Gauge Widget** — unified round gauge: compass rose
  center, speed on semicircular bar around perimeter; color bands (blue 0–25, green
  25–70, yellow 71–79, red 80–100 mph). Applies to Qt6 playback overlay and any
  CLI ASCII equivalent. Source: GitHub issue #4.

**Files Changed:** `tools/pm_findgpslock.cpp` (new), `lib/gps_export.cpp`,
`tools/pm_gpsexport.cpp`, `CMakeLists.txt`, `lib/version.hpp`, `ROADMAP.md`,
`CHANGELOG.md`, `Session_Log.md`

**Pending (carry forward):**
- GPS lock corpus scan in progress — analyze results when complete
- License decision: GPL vs MIT — waiting on Phil Harvey response
- Named locations: SSC Bldg 1007 (30.3679, -89.6117), 11 Oxford Dr Gulfport (30.4189, -89.0252)
- GPS extraction to GeoJSON (architecture decided, code pending)
- CameraProfile/StorageFormat implementation (architecture decided, code pending)
- Cobra CCDC4500 SD card expected ~2026-03-08 — GPS source TBD

---

## 2026-03-07

### Session 1
**Focus:** `pm_probe --wizard` UX polish; `<path>` display design; ROADMAP hardware-agnostic principle; v0.9.10g

**Code Changes (v0.9.10g — `tools/pm_probe.cpp`):**
- **`<path>` legend row** — `drawTable` now opens with `| <path> = <root> |` so the
  user knows what the placeholder resolves to for this run. All camera mapping lines
  display as `<path>/DirName/` rather than absolute or dirname-only values. Keeps
  profile data root-relative — portable if the mount point changes.
- **Camera remap UX redesign** — replaced numbered dir list with a typed subdirectory
  prompt. After picking F/B/L/R, user sees `<path>/ ` and types just the subdir name.
  Wizard validates: directory must exist under root and contain video files. On failure:
  stores the entry and sets a `[!] needs attention` attention flag visible in both the
  sub-menu and the main table. On success: clears any previous assignment for that
  dirname, clears the flag.
- **Pipe alignment fix** — `(not set — required)` em dash (3 UTF-8 bytes, 1-2 display
  columns) caused `wizRow()` byte-count padding to shift the right border 2 columns
  left. Replaced with `--`.
- **Attention flags** — `bool frontAttn/rearAttn/leftAttn/rightAttn` added to wizard
  state; `validateCamDir()` lambda checks existence + video content under root.

**Discovery:**
- D90 rear camera has no audio stream — confirmed in wizard output. Makes sense for
  an exterior-mounted camera. Per-camera audio blocks in the profile will correctly
  capture this absence for the collage layer.

**Design Decisions (ROADMAP):**
- **Hardware-agnostic defaults**: app ships with no active profile. On first run (no
  profile configured), both CLI and GUI display a visible warning directing the user
  to `pm_probe --wizard`. Default detection is a best-effort starting point, not a
  guarantee. Documented in ROADMAP.
- **`--prefs` profile item deferred**: adding a half-wired pref that silently does
  nothing would be a footgun. Held until CameraProfile C++ consumption layer exists.
- **Next critical step identified**: strip Pruveeo D90 hardcoding from
  `trip_detection.cpp`, replace with sane agnostic defaults, load active profile at
  scan time. This is the gate item that makes the wizard output actually useful.

**Files Changed:** `tools/pm_probe.cpp`, `ROADMAP.md`, `CHANGELOG.md`, `Session_Log.md`

**Pending (carry forward):**
- CameraProfile/StorageFormat extraction from `trip_detection.cpp` — next priority
- `pm_probe --wizard` live test on non-D90 hardware (Cobra CCDC4500 expected ~2026-03-08)
- GPS lock corpus scan results — analyze when complete
- License decision: GPL vs MIT — waiting on Phil Harvey response

---

## 2026-03-08

### Session 1 (Morning)
**Focus:** Windows/macOS portability pass; multi-platform build dir setup

**Work Done:**
- **Platform build dir rename (both projects):**
  - PathMux `build/` → `build-linux/`; `build-win/` and `build-macos/` created
  - SRoute same treatment (SRoute `build/` → `build-linux/`)
  - `.gitignore` updated in both repos to cover all three dirs
  - CLAUDE.md and README.md in both projects updated to reference `build-linux`

- **`lib/compat.hpp` (new, SN 00082):** Cross-platform shim header.
  - Defines `popen`/`pclose` → `_popen`/`_pclose` for MSVC
  - Defines `WEXITSTATUS(s)` as pass-through on Windows (pclose/system return
    exit code directly; no POSIX status word encoding)
  - No-op on Linux and macOS — safe to include everywhere
  - Added to all `.cpp` files that call `popen()`: `trip_detection.cpp`,
    `gps_export.cpp`, `config_manager.cpp`, `video_build.cpp`, `pm_audit.cpp`,
    `pm_probe.cpp`, `pm_gpsinfo.cpp`, `pm_findgpslock.cpp`

- **`lib/platform.cpp` (SN 00082):** Full three-way OS implementation:
  - Windows: `USERPROFILE` for home; `%APPDATA%\pathmux\` for config dir;
    `GetConsoleScreenBufferInfo` for terminal width
  - macOS: `HOME`; `~/Library/Application Support/pathmux/` for config dir;
    `TIOCGWINSZ` for terminal width (same as Linux)
  - Linux: unchanged behavior

- **`cli/ui_helpers.hpp` (SN 00082):** Three Windows fixes:
  - `#include <unistd.h>` replaced with `#ifdef _WIN32 / <io.h> + _access /
    #else / unistd.h` block; `X_OK` defined as `0` on Windows (existence check)
  - Path separator checks for `access()` and bare-name detection updated to
    handle both `/` and `\`
  - Tool existence check uses `where <name> >NUL 2>&1` on Windows vs
    `command -v <name> >/dev/null 2>&1` on Linux/macOS

- **`cli/video_build.cpp` (SN 00082):** `sys/wait.h` guarded with
  `#ifndef _WIN32`; `compat.hpp` added

- **`CMakeLists.txt` (SN 00082):** Compiler flags guarded:
  MSVC gets `/W3 /O2`; GCC/Clang get `-Wall -Wextra -O2`

**Linux build verified clean** after all changes. All binaries rebuilt successfully.

**macOS note:** No source changes required for macOS — fully POSIX-compatible.
The only behavioral difference is `getConfigDir()` now correctly returns
`~/Library/Application Support/pathmux/` on macOS instead of `~/.config/pathmux/`.

**Build workflow (Windows):** Windows machine runs VSCode and accesses the repo
over an NFS share — no clone needed, same source tree. CMake is configured and
run from Windows so `build-win/` gets a Windows-native CMakeCache.txt (Windows
paths to the NFS share). `build-linux/` retains its Linux cache. The two build
dirs coexist in the same source tree without interfering — each has its own
CMakeCache.txt with OS-appropriate paths. Same pattern used successfully for
the SRoute app mockup build on the Windows machine.

**Next step:** Afternoon session — open VSCode on Windows, cmake configure into
`build-win/`, build, smoke test a few CLI functions.

**Files Changed:** `lib/compat.hpp` (new), `lib/platform.cpp`, `cli/ui_helpers.hpp`,
`cli/video_build.cpp`, `CMakeLists.txt`, `lib/trip_detection.cpp`, `lib/gps_export.cpp`,
`lib/config_manager.cpp`, `tools/pm_audit.cpp`, `tools/pm_probe.cpp`,
`tools/pm_gpsinfo.cpp`, `tools/pm_findgpslock.cpp`, `.gitignore`

**Pending (carry forward):**
- Windows build and smoke test (afternoon session)
- CameraProfile/StorageFormat extraction from `trip_detection.cpp`
- GPS lock corpus scan results — analyze when complete
- License decision: GPL vs MIT — waiting on Phil Harvey response

---

## 2026-03-15

### Session 1
**Focus:** Host-specific config overlay; Linux/NVENC collage quality confirmation;
structured output (`--format`/`--fields`); GCC 14 build time; v0.9.11

**Code Changes (v0.9.11):**

**Host config overlay (`lib/config_manager.cpp/.hpp`, `lib/compat.hpp`):**
- `getShortHostname()` added to `lib/compat.hpp` — cross-platform, no Winsock init
  required (Windows: `GetComputerNameA`; POSIX: `gethostname` + strip domain)
- `ConfigManager` loads `pathmux_<hostname>.json` as an overlay on top of
  `pathmux.json`. Host fields applied after base load, before cfgState/logger eval.
  Host-specific: `encode.*`, `ffmpegPath`, `exiftoolPath`, `exiftoolOptions`,
  `defaultExportDir`, `tmpDir`, `logLevel`. Base-only: all trip/GPS/display prefs.
- `saveHostSettings()` writes only host-specific fields to host file
- `EncoderPrefsEditor` redirected to save via `saveHostSettings()` — encoder settings
  are now always host-specific

**`--hostprefs` menu (`cli/host_prefs.cpp/.hpp`):**
- New `HostPrefsEditor` class; interactive menu titled "Host Preferences (<hostname>)"
- Items: [A] FFmpeg path, [B] ExifTool path, [C] ExifTool options, [D] Default output
  dir, [E] Temp dir, [F] Log level, [G] Encoder settings (launches EncoderPrefsEditor)
- [S] saves via `config.applySettings(working); config.saveHostSettings()`

**Structured output (`lib/trip_format.hpp`, `cli/find_trips.cpp`, `tools/pm_ls.cpp`):**
- New `lib/trip_format.hpp` (header-only): `tripFieldVal`, `csvQuote`, `writeTripsCSV`,
  `writeTripsXML`, `defaultTripFields` — shared by both pathmux and pm_ls
- `pathmux -T --format=csv` / `--format=xml` / `--format=json` — `--format` is a
  modifier to `-T`, not standalone (pathmux alone does nothing)
- `pm_ls --format=csv` / `--format=xml` / `--format=json [MID] [MID:TID]` — scoped
  output; `--format=json` aliased to `--json`
- `--fields=<f1,f2,...>` selects output columns for csv/xml

**Video build fix (`cli/video_build.cpp`):**
- `buildCollage1080()` downscale: replaced `scale_cuda=1920:1080` (not universally
  compiled into ffmpeg) with CPU `scale=1920:1080,format=...,hwupload=extra_hw_frames=64`

**Build system (`CMakeLists.txt`):**
- ccache auto-detected via `find_program`, wired as `CMAKE_CXX_COMPILER_LAUNCHER`
- `json.hpp` added as PCH for `pathmuxlib` — GCC 14 template analysis amortized

**Discovery:**
- GCC 14 (Alma 10, nutball1) is 5–10× slower than GCC 11 (Alma 9.7, penny) on
  nlohmann/json template instantiation — not a code issue, compiler regression.
  ccache + PCH reduces subsequent builds to seconds.
- ccache is keyed on source content + compiler binary hash — GCC 11 and GCC 14 cache
  entries never collide. Each machine primes its own entries on first build.

**Linux/NVENC Collage Test (nutball1 / RTX5060 / Alma 10):**
- 4K collage confirmed clean — direct Roku Ultra play, no transcode trigger
- Visual quality confirmed via frame grab at 3:36 (Gulfport MS, Handsboro area,
  Magnolia St & Lorraine-Cowan Rd). 4-camera layout, GPS overlay visible.
- constqp QP 24: ~44 Mbps — chosen as default. QP 20/22/24 visually indistinguishable
  on 85" 4K display; QP 15 (~111 Mbps) triggered Plex transcode (Roku limit).
- Collage encode speed: ~1.89× realtime on 12-minute trip (4K output, GPU encode)
- concat stage ran at ~80× (fast copy) as expected

**Design Decisions:**
- `--format` in `pathmux` is a modifier, not standalone — pathmux does nothing without
  a mode flag; user correctly caught this during review
- `useImperial` removed from `formatDump()` signature — users choose `distance_km` or
  `distance_mi` explicitly; no ambiguity
- Host overlay "base-only" list is intentional — trip detection params, display units,
  and GPS settings should be consistent across machines working the same manifests

**Files Changed:**
`lib/compat.hpp`, `lib/config_manager.hpp`, `lib/config_manager.cpp`,
`lib/trip_format.hpp` (new), `lib/version.hpp`,
`cli/host_prefs.hpp` (new), `cli/host_prefs.cpp` (new),
`cli/find_trips.hpp`, `cli/find_trips.cpp`,
`cli/main.cpp`, `cli/prefs.cpp`, `cli/video_build.cpp`,
`tools/pm_ls.cpp`, `CMakeLists.txt`

**Pending (carry forward):**
- CameraProfile/StorageFormat extraction from `trip_detection.cpp` — next gate item
- `pm_probe --wizard` — trial scan deferred until CameraProfile layer exists
- Default encoder + HW profile system (CPU-safe default; community contribution model)
- README public-audience rewrite + LICENSE file + packaging audit
- Man page update for v0.9.11 (`--hostprefs`, `--format`, `--fields`)
- USB stick field test kit

---

## 2026-03-19

### Session 1
**Focus:** ffmpeg build progress tracking — live progress bar with ETA; Qt callback hook

**Code Changes (v0.9.11a):**

**`cli/video_build.cpp` / `cli/video_build.hpp`:**
- `runFfmpegWithProgress(cmd, label, totalDurationSecs)` — new method.  Uses a
  POSIX named pipe (`mkfifo`) and ffmpeg's `-progress <pipe>` to get machine-readable
  `out_time_us=` / `speed=` key-value updates. Parses these into a `\r`-overwritten
  progress bar: `  concat:Front      [=======>     ] 34%  ETA: 0:08`
- `drawProgressLine()` static helper — renders the bar; label padded to 16 chars,
  30-char fill bar, `NNN%  ETA: M:SS`.
- `ffprobeFromFfmpeg()` static helper — derives `ffprobe` path from `ffmpegPath` by
  replacing trailing `"ffmpeg"` with `"ffprobe"`. Used in `buildCollage1080`.
- `progressCallback` public member (`std::function<void(label, pct, etaSecs)>`) —
  Phase 2 Qt hook. When set, called instead of printing to terminal. Qt sets this to
  route updates to per-stage progress bar widgets.
- Four callers upgraded: `buildCameraFile` (`concat:<camera>`), `buildCollage4K`
  (`collage:4K`), `buildCollage1080` (`collage:1080p`), `buildCollage1080Direct`
  (`collage:1080p`). Internal steps (`buildPaddedInput`, audio extract) keep
  `runFfmpeg`.
- Falls back to `runFfmpeg` for: unknown duration, debug mode, Windows, pipe
  creation failure.
- Trip duration source: `durationFFProbed` if available, else `segDetectedDuration`.
  For 1080p-from-4K: `getFileDuration(source4K)` called before encode.

**Design notes:**
- Stage labels (`concat:Front`, `collage:4K`, `collage:1080p`) are the Phase 2
  blocker resolved: Qt can bind one progress bar per label.
- Concat bars zip fast (stream copy); collage bars move slowly (encode) — exactly
  the visual behaviour envisioned.
- Windows support deferred; named pipes work differently on Win32.

---

## 2026-03-20

### Session 1
**Focus:** macOS first run — compile verified, collage build debugged and confirmed working.
All three platforms (Linux, macOS, Windows) now build and produce collages.

**Platform news:**
- macOS compile confirmed clean (Apple Clang, Homebrew ffmpeg/cmake). First collage
  build on "Patsy's Air" (MacBook Air i5-8210Y, Intel UHD 617) succeeded.
- TV upgrade: office 32" Roku 1080p TV plays 4K collages correctly. Green screen on
  the previous TV was a display compatibility issue, not a PathMux codec problem.

**Code Changes (SN 00088):**

**`lib/config_manager.hpp` / `lib/config_manager.cpp`:**
- `reloadHostSettings()` added as public method — calls `loadHostOverlay()`.
  Allows interactive sessions to pick up `--encoderprefs` changes made in a
  separate invocation without requiring a restart.

**`cli/find_trips.cpp`:**
- `config.reloadHostSettings()` called before `videoBuilder.configureOptions()` in
  the interactive browser build path — host overlay always fresh at build time.

**`cli/video_build.hpp`:**
- `CollageOptions` struct gains `EncodeSettings encode` field.

**`cli/video_build.cpp`:**
- `config.reloadHostSettings()` called before `configureOptions()` in the
  `VideoBuilder` interactive build loop.
- `CollageOptions.encode` populated from `config.getEncodeSettings()` in
  `runCollageFromFiles()`.
- `vopts.encode = opts.encode` propagated in both 1080p-from-4K branches
  (`buildCollageFromSlots` path and direct path). Previously `vopts` was
  constructed with only `ffmpegPath`, causing 1080p downscale to fall back
  to default (QSV) encode settings regardless of host config.
- 4K collage failure now skips 1080p: both `buildCollageFromSlots` and
  `buildTrip` paths check return value of 4K build before attempting 1080p.
  Prints "Skipping 1080p — 4K collage failed." instead of attempting the
  downscale on a 0-byte or missing file.
- VideoToolbox `-q` fix: `h264_videotoolbox` and `hevc_videotoolbox` do not
  support `-q` (quality scale); replaced with `-b:v <quality>M` when encoder
  name contains `videotoolbox`. Applied in `buildCollage4K`,
  `buildCollage1080`, and `buildCollage1080Direct`.

**Diagnosed (not fixed this session):**
- Progress bar freezes at ~57-58% during per-camera concat on macOS/NFS.
  Root cause: ffmpeg stops emitting progress events while the moov atom is
  written via NFS seek-back (~97-98 KB for these files). Linux NFS client
  is faster; macOS NFS client holds write-back longer. Fix: animated
  "finalizing..." indicator when no progress arrives for >2 seconds. Deferred.
- `promptString()` hangs on bare Enter input — `std::cin >> std::ws` consumes
  the newline. Also: bare Enter returns the existing value, preventing field
  clear. Deferred.

**Decisions:**
- VideoToolbox quality values (collageQuality, downQuality) are now treated as
  Mbps bitrates when encoder is `*_videotoolbox`. Current values: 4K=20Mbps,
  1080p=22Mbps. 1080p is too high — adjust downQuality to 8–10 next session.
- macOS host profile "Patsy's Air": `h264_videotoolbox` (4K collage),
  `h264_videotoolbox` (1080p), `h264_videotoolbox` (norm); `yuv420p`.

---

## 2026-03-21

### Session 1 (Morning)
**Focus:** CameraProfile abstraction layer — strip D90 hardcoding from `trip_detection.cpp`,
enable multi-brand dashcam support. Performance testing on nutball1 RTX5060.

**Performance diagnostics:**
- Concat stall at ~88% on 24-min trip: confirmed expected behavior — ffmpeg moov atom
  write. `drawFinalizingLine()` spinner confirmed visible and working as designed.
- 4K collage encode: initially 45 min ETA at 5% — root cause: `-preset p7` in extra
  collage args. NVENC p7 = multi-pass max-quality, ~10x slower than p4.
  Fixed: both norm and collage extra args now `-preset p4 -rc constqp -qp 24`.
  After fix: ~13.5 min for 24-min 4K collage (~1.6x realtime), ~9 min for 1080p.

**Design decisions:**
- Camera identity: filename token is authoritative; directory structure is a scan hint
  (reduces search space). D90 uses both; Tesla/Cobra use filename token only.
  Flat-layout cameras still identifiable if directory is removed.
- Profile JSON: `scan_subdir` optional — absent = scan source root (flat layout).
  `filename_token` optional — absent = directory scan only.
- Regex group 1 = full timestamp string; group 2 (optional) = camera token.
  `timestampFormat` is strptime applied to group 1.
- `TripSegment`: named fields replaced with `cameras` and `thumbs` maps keyed by
  slot name (e.g. "front", "rear", "left_repeater"). Profile-agnostic.
- `Trip`: `firstThumbs`/`lastThumbs` maps replace 8 named thumbnail fields.
- `detectTrips()` signature: `CameraProfile` added as second param with
  `d90Default()` default — all existing callers unmodified.
- Manifest migration: `config_manager` detects old flat-key format on read,
  reshapes to new `cameras`/`thumbs`/`firstThumbs`/`lastThumbs` structure silently.
  No user action required.

**Code Changes (SN 00087):**

**New: `lib/camera_profile.hpp` / `lib/camera_profile.cpp`:**
- `CameraSlot`: name, displayName, filenameToken, scanSubdir, isPrimary
- `CameraProfile`: slots, filenameRegex, timestampFormat, containerExt,
  thumbnailMethod, gpsMethod, defaultLayout. Load/save JSON. `d90Default()`.
- `primarySlot()`, `slotByName()`, `isValid()` helpers.

**`lib/trip_detection.hpp`:**
- `TripSegment::front/rear/left/right/frontThumb/...` → `cameras` and `thumbs` maps
- `Trip::firstFrontThumb/...` (8 fields) → `firstThumbs`/`lastThumbs` maps
- `camPath(seg, slot)` and `camThumb(seg, slot)` inline helpers added
- `#include "camera_profile.hpp"` added; `<map>` added
- `detectTrips()` signature gains `const CameraProfile& profile = CameraProfile::d90Default()`

**`lib/trip_detection.cpp`:**
- `stringToTimestamp()` parameterized: now takes format string from profile
- `thumbFor()` parameterized: now takes thumbnailMethod from profile
- `scanDir` per-camera → `scanSlot` loop over `profile.slots`; supports flat and
  subdir layouts; optional filename token verification from group 2 capture
- `extractStartEndGps()` takes `primarySlot` string parameter
- All `seg.front/rear/left/right` → map operations throughout
- `closeTrip` lambda: `firstThumbs`/`lastThumbs` assigned from segment thumb maps

**`lib/config_manager.cpp`:**
- Write: segment cameras → `"cameras"` JSON object; thumbs → `"thumbs"` object;
  trip thumbnails → `"firstThumbs"`/`"lastThumbs"` objects
- Read: detects old vs new format, migrates D90 named fields to maps transparently
- `tripIdentityHash()`: `seg.front` → `camPath(seg, "front")`
- `selectValidationFiles()`: named field iteration → `seg.cameras` map iteration

**Updated consumers (mechanical):**
- `cli/find_trips.cpp`, `cli/video_build.cpp`, `cli/gpx_export.cpp`,
  `tools/pm_tripdebug.cpp`, `tools/pm_audit.cpp`, `tools/pm_gpsinfo.cpp`,
  `tools/pm_probe.cpp`, `cli/main.cpp`
- `video_build.cpp`: `collectSegments` member-pointer lambda → slot-name lambda;
  all audio if-else ladders → `camPath(seg, ac)`; `TripSegment::*` member pointers gone
- `gpx_export.cpp::buildStem()`: handles both old and new JSON segment format

**`CMakeLists.txt`:** `lib/camera_profile.cpp` added to pathmuxlib sources.

**Build:** Clean. One pre-existing warning in pm_gpsinfo.cpp (unused param). No new warnings.

**Files Changed:** `lib/camera_profile.hpp` (new), `lib/camera_profile.cpp` (new),
`lib/trip_detection.hpp`, `lib/trip_detection.cpp`, `lib/config_manager.cpp`,
`cli/main.cpp`, `cli/find_trips.cpp`, `cli/video_build.cpp`, `cli/gpx_export.cpp`,
`tools/pm_tripdebug.cpp`, `tools/pm_audit.cpp`, `tools/pm_gpsinfo.cpp`,
`tools/pm_probe.cpp`, `CMakeLists.txt`

**Pending (carry forward):**
- Wire profile loading from `~/.config/pathmux/profiles/` into callers
  (currently all callers use `d90Default()` explicitly)
- pm_probe trial scan — now unblocked
- Man page update: --hostprefs, --format, --fields (v0.9.11)
- README public-audience rewrite + LICENSE file

### Session 2 (Afternoon)
**Focus:** Wire profile loading from disk into `detectTrips()` callers — completing
the CameraProfile layer so active profile drives scanning.

**Code Changes (SN 00088):**

**`lib/config_manager.hpp`:**
- `#include "camera_profile.hpp"` added
- `activeProfileId = "pruveeo_d90"` added to `AppSettings`
- `getActiveProfileId()` accessor added
- `getCameraProfile() const` declared on `ConfigManager`

**`lib/config_manager.cpp`:**
- `#include "camera_profile.hpp"` added
- `loadSettings()`: reads `activeProfileId` from pathmux.json
- `saveSettings()`: writes `activeProfileId` to pathmux.json
- `getCameraProfile()` implemented: looks up
  `~/.config/pathmux/profiles/<activeProfileId>.json`, calls
  `CameraProfile::loadFromFile()` and validates. Falls back to `d90Default()`
  if file absent or profile fails `isValid()`. Prints warning on invalid profile.

**Callers updated (`CameraProfile::d90Default()` → `config.getCameraProfile()`):**
- `cli/main.cpp`
- `cli/find_trips.cpp` (both scan-path call sites)
- `tools/pm_tripdebug.cpp`

D90 hardcoding fully removed from all call sites. Profile is now read from disk;
new dashcam support requires only dropping a profile JSON into the profiles directory.

**Build:** Clean rebuild (corrupted .o from earlier partial build). One pre-existing
warning in pm_gpsinfo.cpp. No errors, no new warnings.

**SN HWM:** Bumped to 00088.

**Next session:**
- `pm_probe --wizard` trial scan for D90 and Cobra (both cameras in hand)

---

## 2026-03-25

### Session 1
**Focus:** Build phase timing in buildlog; buildlog fallback; brag board rework; v1.0.0

**Code Changes (v1.0.0 / SN 00089):**

**`cli/video_build.cpp`:**
- Added `#include "platform.hpp"` and `#include <chrono>`.
- `BuildTimings` struct: `concatSecs`, `collage4kSecs`, `collage1080Secs` (int, -1 = not run).
- `appendBuildLog()` now accepts `const BuildTimings&`; writes `concat_seconds`,
  `collage_4k_seconds`, `collage_1080p_seconds` as JSON fields (null if phase not run).
- Buildlog path resolution extracted to top of `appendBuildLog()`: probes source path
  writability via `std::ofstream` probe; falls back to `Platform::getConfigDir() +
  "pm_buildlog.json"` with a console notice if source is not writable.  Previously
  the entry was silently dropped on read-only source filesystems.
- Both `buildTrip()` and `run()` now wrap each phase (per-camera concat, 4K collage,
  1080p downscale) with `steady_clock` timers and pass a populated `BuildTimings` to
  `appendBuildLog()`.
- Slot-name API: `collectSegments` calls updated from member-pointer to slot-name
  strings (`"front"`, `"rear"`, etc.) to match CameraProfile refactor.

**Man Page (`man1/pathmux.1`):**
- BRAG BOARD section rewritten: placeholder entries removed; submission requirements
  locked in (daytime, ≥20 min, all four cameras, 4K collage); scoring formula defined
  (`footage_minutes / collage_4k_minutes`, higher is better); first real entry added
  (Nutball Labs, i7/RTX 4060/NVMe, 42m trip, 4m 15s concat, 6m 58s hevc_nvenc, score 6.04x).
  Submission workflow updated to reference `pm_buildlog.json` (with Windows fallback path noted).

**Decisions:**
- Brag board score = `footage_minutes / collage_4k_minutes` only.  1080p downscale excluded
  (it's a transcode of the already-built 4K file, not a measure of dashcam processing throughput).
- Nighttime trips excluded from submissions — less frame complexity = unfair encode advantage.
- All four camera slots required for a qualifying submission — eliminates camera-count variable.

**Pending (carry forward):**
- Buildlog incremental writes (in_progress → complete per phase) — see project memory
- Per-build log file naming: `pm_buildlog_TIMESTAMP_BASENAME.json`
- All JSON slots always present (null if not built)
- GPS extraction to GeoJSON
- pm_probe --wizard trial scan

---

---

## 2026-05-02

### Session 1
**Focus:** Camera sync drift analysis; Tier 1 fix for remaining build paths; pathmux-tl FrameStrip

**Discoveries:**
- Built `Drift_Examples.mp4` from a 42-minute commute; confirmed 2–3 s drift by 25 min
  (Swift truck section) and garage door section. Drift is visually obvious at that scale.
- Camera sync drift on D90 is a flat constant per trip (not accumulating). It is purely
  a start-time misalignment between cameras, not a rate difference. Confirmed via OSD
  timestamp analysis.

**Camera Sync Tier 1 fix (`cli/video_build.cpp`):**
- `probeSegmentDurations()` sync was already applied in `buildCollage4K` but was missing
  from `buildCameraFile()` and `buildAudioFile()`. Both paths now use explicit ffprobe
  durations for per-segment `-t` flags so concat output matches the collage timeline.

**pathmux-tl FrameStrip (`pathmux-tl/FrameStrip.cpp/.h`):**
- 13-cell filmstrip widget below the timeline; displays video frames at evenly spaced
  playback positions. Updates on media load and on timeline scrub.
- HH:MM:SS.FF frame-accurate timecode display in the info bar.
- Audio preservation in output MP4 (previously silent).
- Hardware acceleration auto-selected; `fps=` filter inserted before timelapse encode.
- Error surfacing for media load failures; `--debug` flag for verbose ffmpeg output.

**pm_hud.py fix:**
- Speed tape readout box top edge now aligned with H/2 (horizontal seam between upper
  and lower camera quadrants) instead of centered on the tape scale.

**Files Changed:** `cli/video_build.cpp`, `pathmux-tl/FrameStrip.cpp` (new),
`pathmux-tl/FrameStrip.h` (new), `pathmux-tl/MainWindow.cpp`, `pathmux-tl/MainWindow.h`,
`pathmux-tl/main.cpp`, `scripts/pm_hud.py`

---

## 2026-05-03

### Session 1
**Focus:** Camera sync Tier 2 — audio cross-correlation; new design

**Key findings:**
- VP:8N (Front-to-Right spread): 1.652 s
- VP:47 (Front-to-Right spread): 34.9 frames ≈ 1.4 s
- Offsets vary between trips — per-trip measurement is required. No single static offset
  is reliable.

**Design (confirmed and implemented):**
- `pm_sync_analyze.py --all-segments --write` performs scipy audio cross-correlation on
  each segment and writes results into the manifest `cameraSync` block.
- Left camera is the fixed cross-correlation reference (always shows 0.000 s in the table;
  the `syncCam` label field was removed from display — inferred from the 0.000 entry).
- Collage builder reads per-segment delay+hold values. Padding design:
  - `delay = maxTrim − segTrim` (pre-padded ghost frames)
  - `hold = segTrim` (held last frame at end)
  - `delay + hold = maxTrim` (constant per segment = lockstep across all cameras)
  - Ghost = subdued first frame (`eq brightness=-0.25:saturation=0.1`)
  - Audio: `adelay` + `apad` per segment
- Falls back to Tier 1 concat durations when no sync data is present.

**Code changes:**
- `CameraSync` struct added to `lib/trip_detection.hpp`; serialize/deserialize in
  `lib/config_manager.cpp`.
- `scripts/pm_sync_analyze.py` written from scratch.
- `gui/TripPropertiesDialog`: Sync Values tab added — per-segment trim table showing
  delay/hold for each camera; "Run Analysis" button invokes `pm_sync_analyze.py` with
  output streamed to a scrollable log panel.

**Files Changed:** `lib/trip_detection.hpp`, `lib/config_manager.cpp`,
`gui/TripPropertiesDialog.cpp`, `gui/TripPropertiesDialog.h`,
`scripts/pm_sync_analyze.py` (new)

---

## 2026-05-04

### Session 1
**Focus:** Dashboard layout system; overlay alpha + position; checkbox styling; v1.9.2 close-out

**Checkbox styling (`gui/main.cpp`):**
- Global QCheckBox stylesheet added to `main.cpp` — light gray indicator background,
  bright green checkmark SVG (`gui/resources/checkmark.svg`) when checked.
- Fixes invisible dark-on-dark checkboxes throughout the app (most visible in TripBuildDialog
  overlay and HUD enable rows).

**Dashboard overlay alpha (`gui/TripBuildDialog`, `cli/video_build.cpp`):**
- `.webm` files in the overlay combo now set `mapOverlayAlpha=true`. The ffmpeg command adds
  `-c:v libvpx-vp9` on input and `format=yuva420p` in the filter graph for both 4K and
  standard paths — transparent dashboard overlays composite correctly.

**Dashboard overlay position (`gui/TripBuildDialog`, `cli/video_build.hpp/.cpp`):**
- "Position" combo (Center / TL / TR / BL / BR) added to the overlay cell.
- `VideoOptions::overlayPosition` field added; ffmpeg `overlay=x:y` computed per selection
  in both code paths.

**Dashboard layout system (`scripts/pm_dashboard.py`):**
- Refactored: `--layout standard|quadrant-hud|<path.json>` argument.
- `load_layout()`, `resolve_anchor()`, element draw functions (`draw_compass_element`,
  `draw_speed_element`, `draw_weather_element`), `draw_element_backing()`.
- `_render_standard_panels()` for the unchanged standard preset.
- `_render_widget_layout()` for custom JSON layouts with time-window support on weather.
- Standard preset output is pixel-identical to before.

**TripPropertiesDialog Dashboard tab:**
- Layout picker combo (Standard / Quadrant HUD / Custom JSON…) with file-browse row for
  custom JSON. `--layout` wired into `onGenerateDashboard()` and `onBuildAll()`.

**pm_videos.cpp (`tools/pm_videos.cpp`):**
- New standalone CLI tool for batch map/dashboard/HUD generation.
- `--mid=XX` / `--tid=MM:TT` trip addressing; `--map`/`--dash`/`--hud`/`--all` flags.
- Updates the manifest on completion.

**Files Changed:** `gui/main.cpp`, `gui/resources/checkmark.svg` (new),
`gui/TripBuildDialog.cpp`, `gui/TripBuildDialog.h`,
`gui/TripPropertiesDialog.cpp`, `gui/TripPropertiesDialog.h`,
`cli/video_build.cpp`, `cli/video_build.hpp`,
`scripts/pm_dashboard.py`, `scripts/pm_maprender.py`,
`tools/pm_videos.cpp` (new), `lib/config_manager.cpp`, `lib/trip_detection.hpp`

**Version / SN:** Released as v1.9.9a (SN 00111). Committed and pushed.

---

## Session 2026-05-07 — Docs refresh + GUI polish (v1.9.10a / SN 00112)

### Documentation Refresh (loop task)

Full audit and update of all help files and man pages to reflect v1.9.10a state.
All changes made in-place; no push between individual file updates.

**README.md:**
- Added macOS packages (`.pkg`, `.tar.gz`) to Download section.
- Added `pathmux-tl` and `pm_videos` to binaries table.
- Merged Cobra CCDC4500 and Cobra GPS camera rows into one; added Prilotte row.
- New **Python Scripts** section listing `pm_maprender.py`, `pm_dashboard.py`,
  `pm_hud.py`, `pm_sync_analyze.py`, `gen_logo_morph.py` with explanation of
  why each is Python rather than C++.
- Project Status updated to reflect Phase 2 feature set including camera sync
  Tier 1+2 and `pathmux-tl`.

**ROADMAP.md:**
- Camera profile abstraction items marked `[x]` (was `[ ]`).
- `pm_probe --wizard` entry updated: confirmed on D90, Cobra CCDC4500, Cobra GPS, Prilotte.
- `pm_videos` added to tool list.
- Phase 2 heading now has an "Current Status" block summarising what is implemented.
- Cobra/Prilotte testing status updated to ✅ confirmed.
- Community section rewritten (removed stale pre-release blockers).
- License updated to GPL-3.0-or-later.
- New **v2.0.0 Gate Items** section (GPS+sync combined, Unified Job Queue, cameraSync schema fix).
- Footer updated to 2026-05-07 / v1.9.10a.

**man1/ — all existing pages updated; one new page created:**
- `pathmux.1`: version, camera sync Tier 1/2 pipeline, `scipy` added to Python deps,
  `pm_videos` added to SEE ALSO.
- `pm_gpsinfo.1`: version only.
- `pm_gpsexport.1`: version; fixed duplicate SN line.
- `pm_probe.1`: version; added confirmed-cameras list to DESCRIPTION.
- `pm_ls.1`: version; added `--format`/`--fields` to SYNOPSIS and OPTIONS;
  updated `gps_lock_sec` note to reflect auto-population on extraction.
- `pm_audit.1`: version only.
- `pm_findgpslock.1`: version; generalised from D90-only to LIGO-stream cameras;
  updated `gpsLockSeconds` auto-population note.
- `pm_tripdebug.1`: version; added camera profile note to DESCRIPTION.
- `pm_videos.1`: **new file** — full man page for new tool covering `--mid`, `--tid`,
  `--map`/`--dash`/`--hud`/`--all`, `--force`, `--dry-run`, script resolution,
  GPS auto-extraction behaviour, manifest update semantics.

**Source files — `--help` text audited:**
- `cli/main.cpp`: added missing `-t` (compact listing) to help text.
- All tool source files: help text reviewed and confirmed current; SNs bumped
  where content changed.

**Markdown docs:**
- `HW_Agnostic.md`: status updated to "Implemented (v1.9.x)".
- `Next.md`: added "superseded by build scripts" notice; corrected Qt version reference.
- `pathmux_project_brief.md`: added deprecation header (superseded by CLAUDE.md).
- `PROPOSED_UTILS.md`: added `pm_videos` and `pm_sync_analyze.py` entries.

**Feedback logged:** SN should only be bumped when actual file content changes,
not as a standalone housekeeping operation.

---

### GUI Polish (v1.9.10a)

**MID:TID in dialog title bars:**
- `TripTile` constructor gains `mid` parameter (passed from `TripGridPanel` via `entry.id`).
- `TripPropertiesDialog` constructor gains `mid` parameter; stores as `m_mid`.
- Title formats: "Trip Properties — CY:7E", "Build Trip — CY:7E",
  "Generating Map — CY:7E", "Generating Dashboard — CY:7E", "Generating HUD — CY:7E".
- `TripBuildDialog` derives MID from `manifest.id` (already available).

**TripPropertiesDialog tab focus highlight:**
- `kInactiveBarStyle` updated from color-only to also suppress the bottom indicator
  and background on the inactive bar's selected tab:
  `background: transparent; border-bottom-color: transparent`.
- Applies to both the `tabBarClicked` lambdas and the initial `m_botTabBar` state.

**TripBuildDialog action row cleanup:**
- Removed "Process Now" / "Add to Batch Queue" radio-button group (batch never shipped).
- Removed `m_btnNow`, `m_btnQueue` members and `QButtonGroup` include.
- New action row: Verbose checkbox (left) + expanding "Build Video ▶" button (right),
  blue styled, connected to `accept()`.
- `QDialogButtonBox` reduced to Cancel-only.
- `processNow()` simplified to always `return true`.

**Files Changed:** `gui/TripTile.h`, `gui/TripTile.cpp`,
`gui/TripGridPanel.cpp`, `gui/TripPropertiesDialog.h`,
`gui/TripPropertiesDialog.cpp`, `gui/TripBuildDialog.h`,
`gui/TripBuildDialog.cpp`, `CHANGELOG.md`, `Session_Log.md`

**Version / SN:** v1.9.10a (SN 00112). Ready to build and commit.

---

## 2026-05-16

### Session 2 — macOS packaging fix + cross-OS manifest data-loss bug (v2.2.0 / SN 00119)

**macOS package size — camclops-tl Frameworks symlink (`CMakeLists.txt`):**
- Observed: macOS tar.gz doubled from 36 MB (v2.1.0) to 69 MB (v2.2.0).
- Root cause: `camclops-tl.app` now gets full `macdeployqt` treatment, duplicating
  every Qt framework alongside `camclops-gui.app`'s copy (`libicudata.78.dylib`
  alone is 33 MB, appearing twice).
- Fix: after `macdeployqt` rewrites load paths in the `camclops-tl` binary, two
  extra CMake POST_BUILD commands replace the duplicate Frameworks directory with a
  relative symlink `→ ../../camclops-gui.app/Contents/Frameworks`. The binary's
  `@executable_path/../Frameworks/…` load paths follow the symlink correctly.
  `add_dependencies(camclops-tl camclops-gui)` ensures ordering. Symlink is preserved
  by tar and resolves correctly when both apps install as siblings in `/Applications`.

**Cross-OS manifest data-loss bug (`lib/config_manager.cpp`,
`gui/ScanProgressDialog.cpp`):**
- Bug: scanning a footage directory with an existing manifest (written on another
  OS/machine) wiped GPS extraction status, GPS coordinates, `gpsLockSeconds`,
  map/dash/HUD video paths, trip notes, and the GPS track from the manifest.
  Trip IDs and cameraSync were preserved, nothing else.
- Root cause: `saveTripCache` loaded existing trips to build `hashToId` and
  `existingSyncById`, but wrote each trip's JSON entirely from the fresh `detectTrips()`
  output, which cannot know about post-scan user-generated data.
- Fix: added `existingById` map (trip ID → existing Trip). After each trip's ID is
  assigned, a merge block restores all user-generated fields that `detectTrips()`
  did not set. `"unavailable"` (written by `detectTrips()` when `gpsMethod == "none"`,
  i.e. camera has no GPS hardware) is authoritative and cannot be overridden.
- Secondary fix: `ScanProgressDialog::startScan` now calls `getManifestFilePath()`
  before the profile check, forcing `ensureManifestId()` to adopt any unadopted
  manifest in the footage directory. Without this, the first cross-OS scan used
  the locally configured profile rather than the one embedded in the manifest.

**Files Changed:**
`CMakeLists.txt`, `lib/config_manager.cpp`, `gui/ScanProgressDialog.cpp`

**Version / SN:** v2.2.0 (SN 00119).

---

<!-- SN: 00122 -->
