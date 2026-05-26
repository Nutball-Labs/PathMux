# CamClops V3.x Roadmap

## Overview

v3.0 restructures the GUI around a four-tab pipeline that mirrors the natural
production workflow: find a trip, review it, build the collage, optionally
compress it into a timelapse. The `camclops-tl` timelapse editor is absorbed
as the final tab — no separate application.

### Binary renames (v2.9.0 prerequisite)

| Old name | New name | Notes |
|---|---|---|
| `camclops-gui` | `camclops` | The GUI becomes THE app |
| `camclops` | `camclops-cli` | CLI tools remain available |
| `camclops-tl` | — | Absorbed into the GUI as the Timelapse tab |

Most users will need only the `camclops` package. `camclops-cli` becomes a
separate optional package for scripting and fleet use.

---

## The Four-Tab Pipeline

The GUI tab bar is kept in strict left-to-right pipeline order at all times:

```
[ Manifest... ] [ Trip... ] [ Collage... ] [ Timelapse... ]
```

Any number of tabs of any type can be open simultaneously. New tabs are
inserted at the right edge of their group (`insertTab()`, not `addTab()`),
so the pipeline order is always preserved regardless of how many are open.

---

### Tab 1 — Manifest

The existing manifest browser and trip tile grid, largely unchanged. Each
footage source (SD card, NAS share, local copy) gets its own Manifest tab.
Multiple Manifest tabs can be open simultaneously.

From a trip tile the user can:
- Right-click → **Open Trip** to open a Trip tab
- Use the Extras button to queue GPS extraction, sync analysis, map/dash/HUD renders

---

### Tab 2 — Trip

Opened by right-clicking any trip tile. Replaces `TripPropertiesDialog` —
the trip detail view is promoted from a dialog to a full tab.

- Full segment list and per-camera file tables
- Extras status indicators (GPS / Map / Dashboard / HUD) — update live as
  Job Queue finishes them
- "Send to Queue" buttons for each extra
- **Open in Collage** button → opens a Collage tab pre-loaded with this trip

Tab label: `Trip MID:TID` (e.g. `Trip CQ:73`)

---

### Tab 3 — Collage

Opened from a Trip tab. Replaces `TripBuildDialog` — the build interface is
promoted from a dialog to a full tab.

The trip is pre-loaded. Extras (map, dashboard, HUD) appear automatically
as the Job Queue delivers them — no manual refresh needed. Job Queue progress
is visible inline via the docked/detached panel.

#### Six Independent Element Slots

| Slot | Available sources |
|---|---|
| Four quadrants (TL / TR / BL / BR) | Camera, map, dashboard, external clip, logo morph, black/none |
| Center overlay | Map, dashboard, external clip, logo morph, none |
| HUD | HUD style options only (current style, SpaceX telemetry, off, …) |

HUD is never available as a source for any other slot. New HUD styles are
added to the HUD menu independently of the tab structure.

#### Timeline Editing (v3.2+)

The user scrubs the trip timeline to find the desired frame and sets a
keyframe for a slot change. At any keyframe, one or more slots can be
reassigned to a different source. Unchanged slots carry forward implicitly.

#### Full-Screen Insertions (v3.4+)

At any keyframe the collage can be suspended and an external source inserted
full-screen — pre-roll, post-roll, or mid-collage jump cut. Entry and exit
transitions are configured independently.

#### Transitions (v3.3+)

Every slot swap and every insertion entry/exit point has a configurable
transition:

- **Hard cut** — immediate switch, no transition frames
- **Fade out / fade in** — source fades to black, destination fades in
- **Cross fade** — source dissolves directly into destination

Fade duration is configurable per transition point.

#### Render Architecture — Layered Compositing

All sources are loaded as inputs upfront and composited in a single static
ffmpeg filter graph. Each source is tagged with `enable='between(t,start,end)'`
expressions that control visibility over time. The filter graph structure is
static — ffmpeg handles all switching internally.

The editing config therefore describes **time ranges**, not swap events:

```
front camera → top-left quadrant: 0s – 847s
map          → top-left quadrant: 847s – 1203s
```

#### Edit Config File

All editing decisions are saved to `clops_edit_<MID>-<TID>.json` alongside
the manifest:

- All source files needed for the render
- Starting slot assignments
- Keyframes: timestamp, which slots changed, new source per changed slot
- Full-screen insertions: timestamp, source file, in-point, exit transition
- Per-transition type and duration

The config can be saved and reloaded for later adjustment before rendering.
Submitting the trip to the Job Queue generates the final collage from the
saved config.

When a collage finishes: **Open in Timelapse** button appears, opening a
Timelapse tab with that collage pre-loaded.

Tab label: `Collage MID:TID` (e.g. `Collage CQ:73`)

---

### Tab 4 — Timelapse

Opened from a Collage tab via **Open in Timelapse**, or standalone via file
open. Contains the full `camclops-tl` interface: timeline widget, mark list,
frame strip, transport controls.

- Collage MP4 pre-loaded; marks file auto-loaded if present (`.cltl.json`)
- Unsaved marks: close prompts to confirm discard
- Submits timelapse job to the existing Job Queue

Tab label: `TL MID:TID` or filename (e.g. `TL CQ:73`)

---

## Milestone Track

Rather than shipping v3 as a single deliverable, staged milestones keep
releases flowing and reduce risk.

| Milestone | Scope |
|---|---|
| v3.0 | Tab pipeline shell; absorb `camclops-tl` as Timelapse tab; binary renames |
| v3.1 | JSON edit config schema; static (no timeline) Collage tab — save/load slot assignments, submit to queue |
| v3.2 | Timeline widget; keyframe editing; per-slot swap UI |
| v3.3 | Transitions (hard cut, fade, cross fade) |
| v3.4 | Full-screen insertions |

New HUD styles (SpaceX telemetry variant, etc.) are independent of the v3
milestone track and can ship at any point.

<!-- SN: 00122 -->
