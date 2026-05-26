# CamClops V3.x Roadmap

## Trip Collage Creation — Three-Stage Workflow

Trip collage creation will be restructured into a three-stage process.
Each stage will occupy its own tab in the main GUI. `camclops-tl` will be
absorbed into the GUI as Stage 3, eliminating the separate application.

---

### Stage One — Trip Selection (Current View)

The existing manifest browser and trip tile grid. Not much will change here.
User browses manifests, reviews trip tiles, and selects the trip(s) to work
with. From here the user preps the trip:

- Extract GPS
- Run sync analysis
- Build extras as needed (HUD / map / dashboard)

Once prep is complete the user selects "Move this trip to Stage 2."
The concat process for cameras happens at this transition point, getting
everything ready for the editor.

---

### Stage Two — Stream Editing

The trip arrives in Stage 2 ready for the core editing process. The interface
presents the collage layout (similar to the v2 TripBuildDialog) with a
timeline scrubber along the bottom, similar to the camclops-tl timeline.

#### Six Independent Element Slots

The collage has six independently controllable elements:

| Slot | Available Sources |
|---|---|
| Four quadrants (TL / TR / BL / BR) | Cameras, map, dashboard, external clip, logo morph, black/none |
| Center overlay | Map, dashboard, external clip, logo morph, none |
| HUD | HUD style options only (current, SpaceX telemetry, off, …) |

HUD is never available as a source for any other slot. No other source type
appears in the HUD slot menu. New HUD styles are added to the HUD menu as
they are implemented; the slot model is extensible without structural change.

#### Timeline Editing

The user scrubs the timeline to find the desired frame and marks a keyframe
for a slot change. At any keyframe, one or more slots can be reassigned to
a different source. Unchanged slots carry forward implicitly — only changed
slots need an entry in the keyframe.

#### Full-Screen Insertions

At any point the user can pause the collage entirely and insert an external
source full-screen (pre-roll, post-roll, or mid-collage jump cut). The
collage is suspended for the duration of the insertion; all six elements are
replaced. When the insertion ends, each element resumes whatever it was
showing. Full-screen insertions are independent of per-slot swaps and are
tracked separately in the config.

External sources in full-screen insertions play at their own speed with no
synchronization to the dashcam timeline required. If an external source is
too long or needs editing, that is handled in Stage 3.

#### Transitions

Every slot swap and every insertion entry/exit point has an independently
configurable transition:

- **Hard cut** — immediate switch, no transition frames
- **Fade out / fade in** — source fades to black, destination fades in from black (or logo morph)
- **Cross fade** — source dissolves directly into destination

Fade duration is configurable per transition point. The transition into an
insertion and the transition back out to the collage are set independently —
a user might want a hard cut in but a 2-second cross fade back to dashcam.

#### Render Architecture — Layered Compositing

The render does not rewrite the filter graph at each keyframe. Instead, all
sources are loaded as inputs upfront and composited in a single static
ffmpeg filter graph. Each source is positioned for its slot (crop, scale,
overlay coordinates) and tagged with `enable='between(t,start,end)'`
expressions that control visibility over time. ffmpeg handles all switching
internally.

The editing config (JSON) therefore describes **time ranges**, not swap
events:

```
front camera → top-left quadrant: 0s – 847s
map          → top-left quadrant: 847s – 1203s
```

This model keeps the filter graph structure static and makes the config
generator straightforward to implement. The tradeoff is that all sources
must be open simultaneously as ffmpeg inputs; this is an operational
constraint, not a fundamental blocker.

Transitions are opacity ramps on the same `enable` time windows.

#### Edit Config File

All editing decisions are saved to a JSON config file
(`clops_edit_<MID>-<TID>.json`) alongside the manifest. The config
describes:

- All source files needed for the render
- Starting slot assignments
- Keyframes: timestamp, which slots changed, new source for each changed slot
- Full-screen insertions: timestamp, source file, in-point in source, exit transition
- Per-transition type and duration

The config can be saved and reloaded for later adjustment before rendering.
Submitting the trip to the job queue generates the final collage from the
saved config using the existing job queue infrastructure.

---

### Stage Three — Timelapse Compression

Essentially the same functionality as the current `camclops-tl`. The
resulting collage from Stage 2 (or any standalone video) can be loaded here.
The user sets timelapse marks along the timeline; those marks are saved to a
config file (as camclops-tl does now). The timelapse job is submitted to the
existing job queue.

---

### Workflow in Version 3.x

- Start app
- Run through any manifest scanning needed
- Find and select the trip to be worked on
- Prep the trip
  - Extract GPS
  - Run sync analysis
  - Build extras as needed (HUD / map / dashboard)
- Move trip to Stage 2 — Editing
  - Scrub timeline, assign sources to slots at keyframes
  - Add full-screen insertions (pre-roll, post-roll, jump cuts) as needed
  - Configure transitions per swap point
  - Save editing choices to JSON config
- Submit to job queue for rendering (same queue as v2)
- Optionally move result to Stage 3
  - Set timelapse marks along the timeline
  - Save marks to config file
  - Submit timelapse job to queue
- Enjoy a professionally edited and optionally timelapse-compressed collage

---

### Milestone Sketch

Rather than shipping v3 as a single deliverable, a staged approach reduces
risk and keeps releases flowing:

| Milestone | Scope |
|---|---|
| v3.0 | Absorb `camclops-tl` as Stage 3 tab; no other changes |
| v3.1 | JSON edit config schema; static (no-timeline) Stage 2 — save/load slot assignments, submit to queue |
| v3.2 | Timeline widget; keyframe editing; per-slot swap UI |
| v3.3 | Transitions (hard cut, fade, cross fade) |
| v3.4 | Full-screen insertions |

New HUD styles (SpaceX telemetry variant etc.) are independent of the v3
milestone track and can ship at any point.

<!-- SN: 00121 -->
