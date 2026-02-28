# PathMux Session Log

Running log of development sessions. Captures decisions, reasoning, and context
that CHANGELOG and ROADMAP don't cover. One entry per working session.

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
