# Hardware-Agnostic Camera Profile Redesign

**Status:** Implemented — `layoutMode`, `scanRootCandidates`, `sharedScanDir` in production; D90, Cobra CCDC4500, Cobra GPS, and Prilotte all confirmed working. Wizard updated to emit new format.
**Context session:** 2026-04-23
**Completed:** v1.9.x

---

## Why This Is Needed

The current `CameraSlot` struct uses `scanSubdir` and `filenameToken` as a single pair of
fields to describe two fundamentally different identification strategies:

| Strategy | Example cameras | How camera is identified |
|---|---|---|
| Subdir-per-camera | Pruveeo D90, Prelotte | Which subdirectory the file is in |
| Flat with token | Cobra, Cobra GPS | A token captured from the filename |

These two strategies cannot share the same fields cleanly. The result:

- D90 has `filenameToken = "_F"` etc., but the regex has only one capture group —
  the tokens were **never actually tested**. This caused the `countSlotFiles` bug that
  broke D90 auto-detection entirely.
- Prelotte needed `scanSubdir = "DCIM/DCIMA"` baked in, rather than expressing the
  DCIM prefix as a property of where footage roots tend to live.
- Cobra footage in `DCIM/100_DSC/` fails detection because the profile hardcodes `100_DSC`.
- Adding any new camera type forces editing scan logic, not just writing a profile JSON.

---

## New Design

### Two explicit layout modes

**`"subdir"`** — camera identity comes from which directory the file is in.
Each slot has its own `subDir` under the detected scan root.
`filenameToken` is empty and unused for these slots.
Examples: D90 (Front/, Rear/, Left/, Right/), Prelotte (DCIMA/, DCIMC/).

**`"flat"`** — camera identity comes from a token captured from the filename.
All slots share one directory (`sharedScanDir`).
`filenameToken` is required and non-empty for each slot.
Examples: Cobra GPS (CAM1/CAM2 in 100_DSC/), Cobra plain (CAM1 only).

### `scanRootCandidates` — kills the DCIM-prefix problem

A profile-level ordered list of subpaths to try under the user-provided source root.
Detection and scanning both iterate the list and use the first candidate that contains
matching content.

```json
"scan_root_candidates": ["", "DCIM"]
```

- `""` means the source root itself.
- `"DCIM"` means `<source_root>/DCIM/`.
- Order matters — put the most common layout first.

For D90: `[""]` — footage always at the source root (SD card root = source root).
For Cobra GPS: `["", "DCIM"]` — some setups copy the 100_DSC dir directly; others
  copy the full card including DCIM parent.
For Prelotte: `["DCIM", ""]` — DCIM is the standard layout; try root as fallback.

### `sharedScanDir` — flat mode only

The single directory (relative to the detected scan root) that all cameras live in.
Currently this is redundantly duplicated into every slot's `scanSubdir`.

```json
"shared_scan_dir": "100_DSC"
```

For subdir mode this field is empty/absent.

---

## Updated Structs

### `CameraSlot` (lib/camera_profile.hpp)

```cpp
struct CameraSlot {
    std::string name;           // canonical: "front", "rear", "left", "right"
    std::string displayName;    // human-readable label
    bool        isPrimary = false;

    // Subdir mode: directory name under detected scan root (e.g. "Front", "DCIMA").
    // Flat mode:   empty — all cameras share CameraProfile::sharedScanDir.
    std::string subDir;

    // Flat mode:   token value in the regex's tokenCaptureGroup (e.g. "CAM1", "CAM2").
    // Subdir mode: empty — camera identity comes from the directory, not the filename.
    std::string filenameToken;
};
```

Remove the old `scanSubdir` field entirely. `subDir` is its replacement (subdir mode only).

### `CameraProfile` (lib/camera_profile.hpp)

Add:

```cpp
// "subdir" — each camera in its own subdirectory.
// "flat"   — all cameras in sharedScanDir, distinguished by filenameToken.
std::string layoutMode = "subdir";

// Ordered list of subpaths under the source root to probe.
// First one that contains primary-camera files wins.
// Use {""} for footage directly at the source root,
// {"DCIM"} for under DCIM/, {"", "DCIM"} to try both.
std::vector<std::string> scanRootCandidates = {""};

// Flat mode only: the shared directory containing all camera files.
// Relative to the detected scan root (e.g. "100_DSC").
// Empty for subdir mode.
std::string sharedScanDir;
```

Keep all existing fields (`filenameRegex`, `timestampFormat`, `timestampSource`,
`timestampTimezone`, `timestampCaptureGroup`, `tokenCaptureGroup`, `containerExt`,
`thumbnailMethod`, `gpsMethod`, `defaultLayout`).

---

## Updated Built-in Profiles

### Pruveeo D90

```cpp
p.layoutMode           = "subdir";
p.scanRootCandidates   = {""};
p.sharedScanDir        = "";
p.filenameRegex        = R"((\d{8}_\d{6})[A-Za-z]\.[tT][sS])";
p.timestampFormat      = "%Y%m%d_%H%M%S";
p.timestampTimezone    = "utc";
p.thumbnailMethod      = "ths_sidecar";
p.gpsMethod            = "exiftool_ligogps";

// Slots — subDir is the directory name; filenameToken is empty (not used in subdir mode).
// front: subDir="Front", isPrimary=true
// rear:  subDir="Rear"
// left:  subDir="Left"
// right: subDir="Right"
```

### Cobra GPS

```cpp
p.layoutMode           = "flat";
p.scanRootCandidates   = {"", "DCIM"};
p.sharedScanDir        = "100_DSC";
p.filenameRegex        = R"((\d{8}_\d{4})_(CAM[12])_VID\.[mM][oO][vV])";
p.timestampSource      = "exiftool_metadata";
p.tokenCaptureGroup    = 2;
p.thumbnailMethod      = "none";
p.gpsMethod            = "exiftool_gps0";

// Slots — subDir is empty; filenameToken identifies each camera.
// front: filenameToken="CAM1", isPrimary=true
// rear:  filenameToken="CAM2"
```

### Cobra plain (single camera)

```cpp
p.layoutMode           = "flat";
p.scanRootCandidates   = {"", "DCIM"};
p.sharedScanDir        = "100_DSC";
p.filenameRegex        = R"((\d{8}_\d{4})_(CAM[12])_VID\.[mM][oO][vV])";
p.timestampSource      = "exiftool_metadata";
p.tokenCaptureGroup    = 2;
p.thumbnailMethod      = "none";
p.gpsMethod            = "none";

// Slots — single camera.
// front: filenameToken="CAM1", isPrimary=true
```

Note: Keep Cobra GPS before plain Cobra in `getBuiltinProfiles()` — the GPS model
scores higher when CAM2 files are present, so detection auto-promotes correctly.

### Prelotte

```cpp
p.layoutMode           = "subdir";
p.scanRootCandidates   = {"DCIM", ""};   // DCIM first — that's the standard layout
p.sharedScanDir        = "";
p.filenameRegex        = R"(MOV([ABC])(\d{4})\.avi)";
p.timestampSource      = "mtime";
p.timestampCaptureGroup = 2;
p.tokenCaptureGroup    = 1;

// Slots — subDir is the directory name; filenameToken is empty in subdir mode.
// front: subDir="DCIMA", isPrimary=true
// rear:  subDir="DCIMC"
```

Note: With `scanRootCandidates = {"DCIM", ""}`, the scan root for Prelotte resolves to
`<source>/DCIM/`, and then `subDir = "DCIMA"` gives `<source>/DCIM/DCIMA/`. This is
cleaner than baking `DCIM/DCIMA` into the subDir field.

---

## New Scan Logic

### Helper: `findScanRoot()`

New function in `trip_detection.cpp` (or a shared utility). Tries each candidate in
order; returns the first root under which primary-camera files exist.

```
findScanRoot(sourcePath, profile):
    primarySlot = first slot where isPrimary == true

    for candidate in profile.scanRootCandidates:
        root = candidate.empty() ? sourcePath : sourcePath + "/" + candidate

        if profile.layoutMode == "subdir":
            probeDir = root + "/" + primarySlot.subDir

        else: // "flat"
            probeDir = root + "/" + profile.sharedScanDir

        if probeDir exists AND contains at least one file matching filenameRegex:
            (for flat: also matching primarySlot.filenameToken in tokenCaptureGroup)
            return root

    return ""   // no match found
```

`findScanRoot()` is called once at the start of `detectTrips()`. If it returns empty,
return an empty trip vector immediately.

### `scanSlot()` — subdir mode

```
scanSlot_Subdir(slot, scanRoot, regex, timestampSource, ...):
    scanDir = scanRoot + "/" + slot.subDir
    if not exists(scanDir): return   // slot absent — OK for optional cameras

    for file in directory_iterator(scanDir):
        if not regex_search(filename, regex): continue
        epoch = parse_timestamp(file, profile)
        if epoch <= 0: continue
        slotFiles[slot.name][epoch] = file.abs_path
```

No token filtering in subdir mode — the directory IS the camera filter.

### `scanSlot()` — flat mode

```
scanSlot_Flat(slot, scanRoot, sharedScanDir, regex, tokenCaptureGroup, ...):
    scanDir = scanRoot + "/" + sharedScanDir

    for file in directory_iterator(scanDir):
        match = regex_search(filename, regex)
        if not match: continue

        // Token filter — required in flat mode.
        if not slot.filenameToken.empty():
            if match.size() <= tokenCaptureGroup: continue
            if match[tokenCaptureGroup] != slot.filenameToken: continue

        epoch = parse_timestamp(file, profile)
        if epoch <= 0: continue
        slotFiles[slot.name][epoch] = file.abs_path
```

### `thumbFor()` — unchanged

The thumbnail lookup is already mode-agnostic (just strips the extension). No change needed.

---

## New Detection Logic (profile_detector.cpp)

Replace `countSlotFiles()` with a mode-aware version:

```
countPrimaryFiles(sourcePath, profile):
    primarySlot = first slot where isPrimary == true

    for candidate in profile.scanRootCandidates:
        root = candidate.empty() ? sourcePath : sourcePath + "/" + candidate

        if profile.layoutMode == "subdir":
            scanDir = root + "/" + primarySlot.subDir
        else:
            scanDir = root + "/" + profile.sharedScanDir

        if not exists(scanDir): continue

        count = 0
        for file in scanDir:
            match = regex_search(file, pattern)
            if not match: continue
            if layoutMode == "flat" and !primarySlot.filenameToken.empty():
                if match.size() <= tokenCaptureGroup: continue
                if match[tokenCaptureGroup] != primarySlot.filenameToken: continue
            ++count

        if count > 0:
            return {root: root, count: count}   // found — stop trying candidates

    return {root: "", count: 0}
```

Then in `detectProfile()`:

```
for each profile:
    result = countPrimaryFiles(path, profile)
    if result.count == 0: continue

    score = 60
    for each secondary slot:
        n = countSlotFiles(result.root, slot, profile)
        score += n > 0 ? 20 : -5
    score = max(0, score)
    // ... update best
```

---

## JSON Schema (lib/camera_profile.cpp)

### New fields to read in `loadFromFile()`

```cpp
p.layoutMode           = j.value("layout_mode",         "subdir");
p.sharedScanDir        = j.value("shared_scan_dir",      "");

if (j.contains("scan_root_candidates") && j["scan_root_candidates"].is_array()) {
    for (const auto& c : j["scan_root_candidates"])
        if (c.is_string()) p.scanRootCandidates.push_back(c.get<std::string>());
}
if (p.scanRootCandidates.empty()) p.scanRootCandidates = {""};
```

For slots, read `sub_dir` instead of `scan_subdir`:

```cpp
s.subDir        = js.value("sub_dir",        "");
s.filenameToken = js.value("filename_token", "");
```

### Migration from old format (in `loadFromFile()`)

Old files have `"scan_subdir"` per slot, no `"layout_mode"`.

```cpp
// Migration: old "scan_subdir" per slot → new "sub_dir"
if (s.subDir.empty())
    s.subDir = js.value("scan_subdir", "");
```

Detecting old flat-layout profiles (all slots share same `scan_subdir`):
After loading slots, if `layoutMode` was not present in JSON and all non-empty `subDir`
values are identical across slots, set `layoutMode = "flat"` and
`sharedScanDir = that common value`, clear `subDir` on all slots.

### New fields to write in `saveToFile()`

```cpp
j["layout_mode"]           = layoutMode;
j["shared_scan_dir"]       = sharedScanDir;
json rootArr = json::array();
for (const auto& c : scanRootCandidates) rootArr.push_back(c);
j["scan_root_candidates"]  = rootArr;
```

For slots, write `sub_dir` (not `scan_subdir`):
```cpp
js["sub_dir"]        = s.subDir;
js["filename_token"] = s.filenameToken;
```

---

## User Profile Files — Current State

The following files in `~/.config/camclops/profiles/` use stale formats and will need
to be regenerated by running `clops_probe --wizard` after the new code is in place:

| File | Problem |
|---|---|
| `d90.json` | Uses `"slots"` key (now handled by backward-compat), no `layout_mode`, `thumbnail_method: "none"` (wrong for D90) |
| `d90_flat.json` | `"slots"` key, `thumbnail_method: "none"` |
| `pruveeo_d90_flat.json` | `"slots"` key, `thumbnail_method: "none"` |
| `pruveeo_d90_test.json` | `"slots"` key |
| `cobra.json` | `"slots"` key |
| `cobra_gps.json` | `"slots"` key |
| `new_pruveeo_d90.json` | Old format entirely (`"cameras"` / `"detection"` keys) — not loadable |
| `new_pruveeo_d90_4_channel.json` | Same old format |
| `pruveeo_d90_360.json` | Same old format |

The backward-compat `"slots"` fallback added in this session handles the `"slots"` key.
The truly old-format files (`"cameras"` / `"detection"`) will continue to fail `isValid()`.
That is acceptable — they predate the `CameraProfile` struct entirely.

---

## clops_probe Wizard Updates (tools/clops_probe.cpp)

The wizard runs on an SD card or footage directory and saves a profile JSON.
It needs to:

1. **Detect layout mode**: check if cameras are in separate subdirs or share one dir.
   - Enumerate first-level subdirs; if multiple contain matching video files → subdir mode.
   - If one dir contains files with distinct filename tokens → flat mode.

2. **Detect scan root candidates**: probe whether footage is directly in the provided
   path or under a `DCIM/` subdirectory. Emit whichever was found first; optionally
   add the other as a fallback candidate.

3. **Emit new JSON format**: `layout_mode`, `scan_root_candidates`, `shared_scan_dir`,
   slots with `sub_dir` or `filename_token` (not both, not `scan_subdir`).

---

## Files That Need Changes

| File | Change |
|---|---|
| `lib/camera_profile.hpp` | Update `CameraSlot` (rename `scanSubdir`→`subDir`); add `layoutMode`, `scanRootCandidates`, `sharedScanDir` to `CameraProfile` |
| `lib/camera_profile.cpp` | `loadFromFile` (new fields + migration); `saveToFile` (new fields); all four `*Default()` functions; `getBuiltinProfiles()` (order unchanged) |
| `lib/profile_detector.cpp` | Replace `countSlotFiles()` with mode-aware version; update `detectProfile()` to call new helper |
| `lib/trip_detection.cpp` | Add `findScanRoot()`; split `scanSlot` into subdir/flat paths; call `findScanRoot()` at top of `detectTrips()` |
| `tools/clops_probe.cpp` | Wizard output format; layout mode detection; scan root candidate detection |
| `lib/camera_profile.hpp` SN | Bump to next HWM |
| `lib/camera_profile.cpp` SN | Bump to next HWM |
| `lib/profile_detector.cpp` SN | Bump to next HWM |
| `lib/trip_detection.cpp` SN | Bump to next HWM |
| `tools/clops_probe.cpp` SN | Bump to next HWM |

GUI files (`ScanProgressDialog`, `MainWindow`, `SettingsDialog`) need no changes —
they interact with profiles only through `ConfigManager` and the profile ID string.

CLI files (`cli/main.cpp`, `cli/prefs.cpp`) need no changes for the same reason.

---

## Test Paths After Implementation

| Path | Expected detection | Notes |
|---|---|---|
| `/z/srcdash/ex01` | Pruveeo D90 | scanRoot="" (footage at root) |
| `/z/srcdash/ex04` | Pruveeo D90 | Same layout, currently no manifest |
| `/z/srcdash/Cobra_GPS` | Cobra GPS | scanRoot="" (100_DSC at root) |
| `/z/srcdash/Cobra` | Cobra GPS | scanRoot="DCIM" (footage in DCIM/100_DSC/) |
| `/z/srcdash/Prelotte` | Prelotte | scanRoot="DCIM" (footage in DCIM/DCIMA/) |

`/z/srcdash/find_lock` contains `.findgpslock.txt` files, not video — no profile
should match; detection should return no match cleanly.

---

## Implementation Order

Start here to minimize churn:

1. Update `CameraSlot` and `CameraProfile` structs (`camera_profile.hpp`)
2. Update `loadFromFile` / `saveToFile` with migration (`camera_profile.cpp`)
3. Update all four `*Default()` built-in profiles (`camera_profile.cpp`)
4. Add `findScanRoot()` to `trip_detection.cpp`; rewrite `scanSlot` with mode split
5. Rewrite `countSlotFiles` / `detectProfile` in `profile_detector.cpp`
6. Build and verify all test paths detect and scan correctly
7. Update `clops_probe` wizard to emit new format
8. Bump SNs on all changed files; commit

<!-- SN: 00112 -->
