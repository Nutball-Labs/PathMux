#!/usr/bin/env python3
"""
pm_hud.py — PathMux military-style HUD overlay renderer

Reads a GPS track from a PathMux manifest JSON and renders an animated
HUD (heads-up display) overlay as a transparent WebM/VP9 video for
compositing over collage footage.

Elements:
  Left edge  — vertical speed ladder tape (km/h)
  Right edge — vertical speed ladder tape (mph)
  Bottom     — horizontal heading tape (degrees / cardinal)

Usage:
    pm_hud.py --manifest <path> --trip <id> --output <path.webm>
              [--width 3840] [--height 2160] [--fps 30] [--render-fps 10]
              [--no-left-speed]   [--left-speed-width 160]  [--left-speed-height 1620]
              [--left-speed-x 0] [--left-speed-y 270]
              [--no-right-speed]  [--right-speed-width 160] [--right-speed-height 1620]
              [--right-speed-x AUTO] [--right-speed-y 270]
              [--no-heading]      [--heading-height 100]    [--heading-width AUTO]
              [--heading-x 0]    [--heading-y AUTO]
              [--color phosphor|amber] [--ffmpeg ffmpeg]

Requirements:
    pip install Pillow
"""

import argparse
import datetime
import json
import math
import os
import subprocess
import sys
import time

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: 'Pillow' not found. Install with: pip install Pillow", file=sys.stderr)
    sys.exit(1)


# ─────────────────────────────────────────────────────────────────────────────
# Color palettes
# ─────────────────────────────────────────────────────────────────────────────

_PALETTES = {
    "phosphor": {
        "bright":   (0,   255, 65,  255),
        "mid":      (0,   200, 50,  230),
        "dim":      (0,   140, 35,  200),
        "faint":    (0,   70,  18,  160),
        "readout":  (140, 255, 160, 255),
        "backing":  (0,   10,  3,   210),
        "box_fill": (0,   30,  8,   240),
        "north":    (255, 60,  60,  255),
    },
    "amber": {
        "bright":   (255, 200, 0,   255),
        "mid":      (220, 160, 0,   230),
        "dim":      (170, 115, 0,   200),
        "faint":    (100, 65,  0,   160),
        "readout":  (255, 230, 120, 255),
        "backing":  (10,  6,   0,   210),
        "box_fill": (30,  20,  0,   240),
        "north":    (255, 80,  80,  255),
    },
}


# ─────────────────────────────────────────────────────────────────────────────
# GPS helpers (build from manifest gpsTrack array)
# ─────────────────────────────────────────────────────────────────────────────

_R_KM = 6371.0


def _haversine_km(lat1, lon1, lat2, lon2):
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2
         + math.cos(math.radians(lat1))
         * math.cos(math.radians(lat2))
         * math.sin(dlon / 2) ** 2)
    return 2 * _R_KM * math.asin(math.sqrt(max(0.0, a)))


def build_points(gps_track, gps_lock_offset=0):
    if not gps_track:
        return []
    pts = []
    t0 = prev_lat = prev_lon = None
    odo_km = 0.0
    for raw in gps_track:
        ts_str = raw.get("timestamp", "")
        try:
            t = datetime.datetime.strptime(ts_str.rstrip("Z").strip(), "%Y:%m:%d %H:%M:%S")
        except ValueError:
            continue
        lat = float(raw.get("lat", 0.0))
        lon = float(raw.get("lon", 0.0))
        if lat == 0.0 and lon == 0.0:
            continue
        if t0 is None:
            t0 = t
        if prev_lat is not None:
            odo_km += _haversine_km(prev_lat, prev_lon, lat, lon)
        pts.append({
            "t":       (t - t0).total_seconds() + gps_lock_offset,
            "lat":     lat,
            "lon":     lon,
            "speed":   float(raw.get("speed",   0.0)),
            "heading": float(raw.get("heading", 0.0)),
        })
        prev_lat, prev_lon = lat, lon
    return pts


def interp_point(pts, t_sec):
    if t_sec <= pts[0]["t"]:
        return dict(pts[0])
    if t_sec >= pts[-1]["t"]:
        return dict(pts[-1])
    lo, hi = 0, len(pts) - 1
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if pts[mid]["t"] <= t_sec:
            lo = mid
        else:
            hi = mid
    alpha = (t_sec - pts[lo]["t"]) / (pts[hi]["t"] - pts[lo]["t"])
    result = {"t": t_sec}
    for k in ("lat", "lon", "speed", "heading"):
        result[k] = pts[lo].get(k, 0.0) + (pts[hi].get(k, 0.0) - pts[lo].get(k, 0.0)) * alpha
    # Heading wraparound interpolation
    h0, h1 = pts[lo].get("heading", 0.0), pts[hi].get("heading", 0.0)
    diff = ((h1 - h0 + 180) % 360) - 180
    result["heading"] = (h0 + alpha * diff) % 360
    return result


# ─────────────────────────────────────────────────────────────────────────────
# Font loading
# ─────────────────────────────────────────────────────────────────────────────

_font_warned = False


def find_font(size):
    global _font_warned
    candidates = [
        # Alma / RHEL / CentOS actual paths
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        # Legacy / alternate paths
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                pass
    if not _font_warned:
        print("  Warning: no TrueType font found; using PIL bitmap default (layout may vary)",
              file=sys.stderr)
        _font_warned = True
    return ImageFont.load_default()


def _twh(draw, text, font):
    try:
        b = draw.textbbox((0, 0), text, font=font, anchor="lt")
        return b[2] - b[0], b[3] - b[1]
    except Exception:
        return draw.textsize(text, font=font)  # type: ignore[attr-defined]


def _hud_text(draw, xy, text, fill, font, outline=(0, 0, 0, 200), stroke=1):
    """Draw text with a dark outline for readability over camera footage."""
    draw.text(xy, text, fill=outline, font=font, anchor="lt",
              stroke_width=stroke, stroke_fill=outline)
    draw.text(xy, text, fill=fill, font=font, anchor="lt")


# ─────────────────────────────────────────────────────────────────────────────
# Speed tape (vertical ladder)
# ─────────────────────────────────────────────────────────────────────────────

_CARDINALS = {0: "N", 45: "NE", 90: "E", 135: "SE",
              180: "S", 225: "SW", 270: "W", 315: "NW"}


def draw_speed_tape(img, draw, tx, ty, tw, th, speed_kmh, unit, side, pal, fonts,
                    line_scale=1.0, visible_range=80.0):
    """
    Transparent vertical speed ladder tape — no backing strip.
    Tick marks with small labels float over footage; current speed in a
    side-mounted box with a pointer line to the scale.
    """
    if unit == "MPH":
        speed_val = speed_kmh * 0.621371
        max_val   = 140
    else:
        speed_val = speed_kmh
        max_val   = 220

    speed_val  = max(0.0, speed_val)
    center_y   = ty + th // 2
    visible    = max(20.0, float(visible_range))
    pix_pu     = th / visible
    step_major = 20
    step_minor = 10
    step_sub   = 5

    lw_line  = max(2, round(max(2, tw // 80)  * line_scale))  # min 2px — visible on 4K display
    lw_major = max(2, round(max(2, tw // 60)  * line_scale))
    lw_minor = max(1, round(max(1, tw // 90)  * line_scale))

    # Vertical scale line on the inner edge (right for left tape, left for right)
    scale_x = tx + tw - 1 if side == "left" else tx
    draw.line([(scale_x, ty), (scale_x, ty + th)], fill=pal["mid"], width=lw_line)

    # Tick marks and labels
    v_lo = math.floor((speed_val - visible / 2 - step_major) / step_sub) * step_sub
    v_hi = math.ceil( (speed_val + visible / 2 + step_major) / step_sub) * step_sub + step_sub

    tick_major = tw * 42 // 100
    tick_minor = tw * 26 // 100
    tick_sub   = tw * 10 // 100

    for V in range(int(v_lo), int(v_hi) + 1, step_sub):
        if V < 0 or V > max_val:
            continue
        y = int(center_y - (V - speed_val) * pix_pu)
        if y < ty or y >= ty + th:
            continue

        is_major = (V % step_major == 0)
        is_minor = (V % step_minor == 0) and not is_major

        if is_major:
            tlen, tcol, tw_ = tick_major, pal["bright"], lw_major
        elif is_minor:
            tlen, tcol, tw_ = tick_minor, pal["mid"],    lw_minor
        else:
            tlen, tcol, tw_ = tick_sub,   pal["faint"],  1

        if side == "left":
            draw.line([(scale_x - tlen, y), (scale_x, y)], fill=tcol, width=tw_)
            if is_major:
                lbl = str(V)
                lw2, lh2 = _twh(draw, lbl, fonts["tiny"])
                lx = scale_x - tlen - lw2 - 4
                ly = y - lh2 // 2
                if lx >= tx and ty <= ly and ly + lh2 <= ty + th:
                    _hud_text(draw, (lx, ly), lbl, pal["dim"], fonts["tiny"])
        else:
            draw.line([(scale_x, y), (scale_x + tlen, y)], fill=tcol, width=tw_)
            if is_major:
                lbl = str(V)
                lw2, lh2 = _twh(draw, lbl, fonts["tiny"])
                lx = scale_x + tlen + 4
                ly = y - lh2 // 2
                if lx + lw2 <= tx + tw and ty <= ly and ly + lh2 <= ty + th:
                    _hud_text(draw, (lx, ly), lbl, pal["dim"], fonts["tiny"])

    # Unit label at top of tape
    uw, uh = _twh(draw, unit, fonts["micro"])
    ux = scale_x - uw - 4 if side == "left" else scale_x + 4
    _hud_text(draw, (ux, ty + 4), unit, pal["mid"], fonts["micro"])

    # Full-width readout box at center — semi-transparent backing, large centered text.
    # The box spans the full tape width so font size is limited only by tape height.
    val_str  = f"{int(round(speed_val))}"
    rw, rh   = _twh(draw, "000", fonts["medium"])  # size against widest value
    vw, vh   = _twh(draw, val_str, fonts["medium"])
    box_h    = rh + max(10, th // 80)
    box_y1   = center_y - box_h // 2
    box_y2   = center_y + box_h // 2
    draw.rectangle([(tx, box_y1), (tx + tw - 1, box_y2)], fill=pal["box_fill"])
    draw.rectangle([(tx, box_y1), (tx + tw - 1, box_y2)], outline=pal["bright"], width=lw_major)
    _hud_text(draw, (tx + (tw - vw) // 2, center_y - vh // 2),
              val_str, pal["readout"], fonts["medium"])

    # Pointer chevron on the inner edge at center_y
    chv = max(8, tw // 14)
    if side == "left":
        pts = [(tx + tw, center_y),
               (tx + tw - chv, center_y - chv * 3 // 4),
               (tx + tw - chv, center_y + chv * 3 // 4)]
    else:
        pts = [(tx, center_y),
               (tx + chv, center_y - chv * 3 // 4),
               (tx + chv, center_y + chv * 3 // 4)]
    draw.polygon(pts, fill=pal["bright"])


# ─────────────────────────────────────────────────────────────────────────────
# Compass rose (circular, no backing)
# ─────────────────────────────────────────────────────────────────────────────

def draw_compass_rose(img, draw, cx, cy, radius, heading_deg, pal, fonts, line_scale=1.0):
    """
    Circular compass rose.  The ring rotates so the current heading is always
    at 12 o'clock; a fixed pointer triangle marks it.  No backing — fully
    transparent over camera footage.
    cx, cy: centre in the strip's local coordinate space.
    radius: outer radius of the ring in pixels.
    """
    heading_deg = heading_deg % 360

    lw_ring  = max(1, round(max(1, radius // 80)  * line_scale))
    lw_card  = max(2, round(max(3, radius // 40)  * line_scale))
    lw_maj   = max(1, round(max(2, radius // 55)  * line_scale))
    lw_min   = max(1, round(max(1, radius // 80)  * line_scale))

    # Outer ring
    draw.ellipse([(cx - radius, cy - radius), (cx + radius, cy + radius)],
                 outline=pal["mid"], width=lw_ring)

    # Tick marks every 5°
    for D in range(0, 360, 5):
        delta = (D - heading_deg) % 360
        if delta > 180: delta -= 360
        angle    = math.radians(delta - 90)   # 0° heading → top of circle
        cos_a    = math.cos(angle)
        sin_a    = math.sin(angle)

        is_card    = (D % 90  == 0)
        is_intercd = (D % 45  == 0) and not is_card
        is_major   = (D % 10  == 0) and not is_card and not is_intercd

        if is_card:
            inner_r, tw_, col = radius * 68 // 100, lw_card, pal["bright"]
        elif is_intercd:
            inner_r, tw_, col = radius * 74 // 100, lw_maj,  pal["mid"]
        elif is_major:
            inner_r, tw_, col = radius * 82 // 100, lw_min + 1, pal["dim"]
        else:
            inner_r, tw_, col = radius * 88 // 100, lw_min,  pal["faint"]

        ox, oy = cx + radius * cos_a, cy + radius * sin_a
        ix, iy = cx + inner_r * cos_a, cy + inner_r * sin_a
        draw.line([(ix, iy), (ox, oy)], fill=col, width=tw_)

        # Cardinal and intercardinal labels inside the ring
        label_map = {0: "N", 45: "NE", 90: "E", 135: "SE",
                     180: "S", 225: "SW", 270: "W", 315: "NW"}
        if D in label_map:
            lbl    = label_map[D]
            font   = fonts["small"] if is_card else fonts["micro"]
            lbl_r  = radius * 55 // 100 if is_card else radius * 62 // 100
            lx_f   = cx + lbl_r * cos_a
            ly_f   = cy + lbl_r * sin_a
            lw2, lh2 = _twh(draw, lbl, font)
            color  = pal["north"] if D == 0 else (pal["bright"] if is_card else pal["mid"])
            _hud_text(draw, (int(lx_f - lw2 // 2), int(ly_f - lh2 // 2)), lbl, color, font)

    # Fixed pointer at 12 o'clock — triangle pointing inward from the top of the ring
    ptr_h = max(10, radius // 9)
    ptr_w = max(6,  radius // 14)
    pts_ptr = [(cx,            cy - radius + ptr_h),
               (cx - ptr_w,   cy - radius - 2),
               (cx + ptr_w,   cy - radius - 2)]
    draw.polygon(pts_ptr, fill=pal["bright"])

    # Heading readout box above the ring (outside the circle, above pointer)
    hdg_str  = f"{int(round(heading_deg)):03d}°"
    hw2, hh2 = _twh(draw, hdg_str, fonts["medium"])
    bpad     = max(4, radius // 30)
    bx       = cx - hw2 // 2 - bpad
    by       = cy - radius - ptr_h - hh2 - bpad - 6
    bw       = hw2 + bpad * 2
    bh       = hh2 + bpad
    draw.rectangle([(bx, by), (bx + bw - 1, by + bh - 1)], fill=pal["box_fill"])
    draw.rectangle([(bx, by), (bx + bw - 1, by + bh - 1)], outline=pal["bright"], width=lw_card)
    _hud_text(draw, (bx + bpad, by + bpad // 2), hdg_str, pal["readout"], fonts["medium"])


# ─────────────────────────────────────────────────────────────────────────────
# Strip-based frame renderer
#
# Instead of allocating a fresh W×H RGBA image (32 MB at 4K) every frame,
# we render each element into a small strip image and blit it into a
# pre-allocated bytearray frame buffer that is reused across frames.
# Only the element bounding boxes are cleared + redrawn each frame;
# the rest of the buffer stays zero (transparent).
# ─────────────────────────────────────────────────────────────────────────────

def _clear_strip(buf, W, x, y, w, h):
    """Zero-fill a rectangular region in the RGBA frame bytearray."""
    row_bytes = w * 4
    zero_row  = b'\x00' * row_bytes
    for row in range(h):
        off = ((y + row) * W + x) * 4
        buf[off : off + row_bytes] = zero_row


def _blit_strip(buf, W, img, x, y):
    """Copy an RGBA strip image into the frame bytearray at (x, y)."""
    iw, ih    = img.size
    raw       = img.tobytes()
    row_bytes = iw * 4
    if x == 0 and iw == W:
        # Full-width strip: single contiguous write.
        off = y * W * 4
        buf[off : off + ih * W * 4] = raw
    else:
        for row in range(ih):
            off = ((y + row) * W + x) * 4
            buf[off : off + row_bytes] = raw[row * row_bytes : (row + 1) * row_bytes]


def render_hud_frame(W, H, pt, cfg, pal, fonts, frame_buf):
    """
    Render one HUD frame into the pre-allocated bytearray frame_buf (W×H×4 bytes).
    Only element bounding boxes are cleared and redrawn; transparent regions are
    left at zero from initialization.
    """
    speed_kmh   = max(0.0, float(pt.get("speed",   0.0)))
    heading_deg = float(pt.get("heading", 0.0)) % 360
    ls          = cfg.get("line_scale",    1.0)
    vis         = cfg.get("visible_range", 80.0)

    if cfg["left_speed"]["enabled"]:
        c = cfg["left_speed"]
        _clear_strip(frame_buf, W, c["x"], c["y"], c["w"], c["h"])
        strip = Image.new("RGBA", (c["w"], c["h"]), (0, 0, 0, 0))
        draw_speed_tape(strip, ImageDraw.Draw(strip), 0, 0, c["w"], c["h"],
                        speed_kmh, "KPH", "left", pal, fonts, ls, vis)
        _blit_strip(frame_buf, W, strip, c["x"], c["y"])

    if cfg["right_speed"]["enabled"]:
        c = cfg["right_speed"]
        _clear_strip(frame_buf, W, c["x"], c["y"], c["w"], c["h"])
        strip = Image.new("RGBA", (c["w"], c["h"]), (0, 0, 0, 0))
        draw_speed_tape(strip, ImageDraw.Draw(strip), 0, 0, c["w"], c["h"],
                        speed_kmh, "MPH", "right", pal, fonts, ls, vis)
        _blit_strip(frame_buf, W, strip, c["x"], c["y"])

    if cfg["heading"]["enabled"]:
        c = cfg["heading"]
        _clear_strip(frame_buf, W, c["x"], c["y"], c["w"], c["h"])
        strip = Image.new("RGBA", (c["w"], c["h"]), (0, 0, 0, 0))
        # cx/cy stored as frame-absolute; convert to strip-local for drawing
        draw_compass_rose(strip, ImageDraw.Draw(strip),
                          c["cx"] - c["x"], c["cy"] - c["y"], c["r"],
                          heading_deg, pal, fonts, ls)
        _blit_strip(frame_buf, W, strip, c["x"], c["y"])


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="PathMux military-style HUD overlay renderer — GPS track → WebM"
    )
    parser.add_argument("--manifest",  required=True, help="PathMux manifest JSON path")
    parser.add_argument("--trip",      required=True, help="Trip ID (e.g. G1)")
    parser.add_argument("--output",    required=True, help="Output path (.webm)")
    parser.add_argument("--width",     type=int, default=3840, help="Frame width (default 3840)")
    parser.add_argument("--height",    type=int, default=2160, help="Frame height (default 2160)")
    parser.add_argument("--fps",       type=int, default=30,   help="Output framerate")
    parser.add_argument("--render-fps", type=int, default=5,
                        help="Unique frames per second (ffmpeg fills to --fps; 5 is the default "
                             "since HUD instruments change slowly — increase for smoother animation)")
    parser.add_argument("--tape-width",    type=int, default=0,
                        help="Speed tape width in px for both left and right tapes (0=auto)")
    parser.add_argument("--visible-range", type=int, default=100,
                        help="Speed units visible in full tape height (default 100; smaller = zoomed in)")
    parser.add_argument("--color",     default="phosphor", choices=["phosphor", "amber"],
                        help="Named palette (overridden by --color-hex if provided)")
    parser.add_argument("--color-hex", default="",
                        help="Element color as CSS hex, e.g. #00ff41.  Overrides --color.")
    parser.add_argument("--font-scale", type=float, default=1.0,
                        help="Multiplier applied to all font sizes (default 1.0)")
    parser.add_argument("--line-scale", type=float, default=1.0,
                        help="Multiplier applied to all line/tick widths (default 1.0)")
    parser.add_argument("--ffmpeg",    default="ffmpeg")

    # Left speed tape
    parser.add_argument("--no-left-speed",     action="store_true")
    parser.add_argument("--left-speed-width",  type=int, default=0, help="Tape width in px (0=auto)")
    parser.add_argument("--left-speed-height", type=int, default=0, help="Tape height in px (0=auto)")
    parser.add_argument("--left-speed-x",      type=int, default=0)
    parser.add_argument("--left-speed-y",      type=int, default=-1,
                        help="Top y of tape (-1=auto center)")

    # Right speed tape
    parser.add_argument("--no-right-speed",     action="store_true")
    parser.add_argument("--right-speed-width",  type=int, default=0)
    parser.add_argument("--right-speed-height", type=int, default=0)
    parser.add_argument("--right-speed-x",      type=int, default=-1,
                        help="Left x of tape (-1=auto right edge)")
    parser.add_argument("--right-speed-y",      type=int, default=-1)

    # Heading tape
    parser.add_argument("--no-heading",     action="store_true")
    parser.add_argument("--heading-height", type=int, default=0)
    parser.add_argument("--heading-width",  type=int, default=0)
    parser.add_argument("--heading-x",      type=int, default=-1,
                        help="Compass center X in pixels (-1 = frame centre)")
    parser.add_argument("--heading-y",      type=int, default=-1,
                        help="Top y of tape (-1=auto bottom edge)")

    args = parser.parse_args()

    W, H = args.width, args.height

    # Force .webm extension
    if not args.output.lower().endswith(".webm"):
        args.output = os.path.splitext(args.output)[0] + ".webm"
        print(f"  Output changed to {args.output} (VP9 alpha requires WebM)", file=sys.stderr)

    # --- Load manifest ---
    try:
        with open(args.manifest) as f:
            manifest = json.load(f)
    except Exception as e:
        print(f"Error: cannot read manifest: {e}", file=sys.stderr)
        sys.exit(1)

    trip = next(
        (t for t in manifest.get("trips", [])
         if str(t.get("id", "")).upper() == args.trip.upper()),
        None)
    if trip is None:
        print(f"Error: trip '{args.trip}' not found in manifest.", file=sys.stderr)
        sys.exit(1)

    gps_track = trip.get("gpsTrack", [])
    if len(gps_track) < 2:
        print("Error: trip has no GPS track data — run GPS extraction first.", file=sys.stderr)
        sys.exit(1)

    lock_secs = trip.get("gpsLockSeconds", 0)
    if not isinstance(lock_secs, (int, float)) or lock_secs < 0:
        lock_secs = 0

    print(f"  Trip {args.trip}: {len(gps_track)} GPS samples  "
          f"(GPS lock offset: {lock_secs}s)", file=sys.stderr)

    pts       = build_points(gps_track, gps_lock_offset=lock_secs)
    total_dur = pts[-1]["t"]
    rfps      = args.render_fps
    ofps      = args.fps
    n_frames  = int(total_dur * rfps) + 1

    print(f"  Duration: {total_dur:.0f}s  →  {n_frames} frames "
          f"at {rfps}fps render / {ofps}fps output", file=sys.stderr)

    # --- Element sizes (auto-fill from frame dimensions) ---
    default_tape_w = args.tape_width if args.tape_width > 0 else max(80, W * 160 // 3840)
    default_tape_h = max(400, H * 3 // 4)
    default_hdg_h  = max(120, H // 15)
    default_hdg_w  = W

    ls_w = args.left_speed_width  or default_tape_w
    ls_h = args.left_speed_height or default_tape_h
    ls_x = args.left_speed_x
    ls_y = args.left_speed_y if args.left_speed_y >= 0 else (H - ls_h) // 2

    rs_w = args.right_speed_width  or default_tape_w
    rs_h = args.right_speed_height or default_tape_h
    rs_x = args.right_speed_x if args.right_speed_x >= 0 else W - rs_w
    rs_y = args.right_speed_y if args.right_speed_y >= 0 else (H - rs_h) // 2

    # Compass rose geometry.  --heading-height is repurposed as radius;
    # --heading-x/y as the frame-absolute centre of the rose.
    compass_r  = args.heading_height or max(150, H // 8)   # ~25% of frame height at 4K
    compass_cx = args.heading_x if args.heading_x >= 0 else W // 2
    overhead   = compass_r // 2 + max(20, compass_r // 8)   # heading box + pointer above ring
    margin     = max(16, compass_r // 10)
    # Default: sink the compass so the bottom 1/5 of the ring is off-screen.
    # compass_cy + compass_r = H + (2*compass_r)/5  →  compass_cy = H - compass_r*3//5
    compass_cy = args.heading_y if args.heading_y >= 0 else H - compass_r * 3 // 5

    # Bounding box for the compass strip (includes the heading readout above the ring).
    # Clamp bottom to frame height so the blit stays within frame_buf.
    cstrip_x = max(0, compass_cx - compass_r - margin)
    cstrip_y = max(0, compass_cy - compass_r - overhead)
    cstrip_w = min(W - cstrip_x, 2 * (compass_r + margin))
    cstrip_h = min(H - cstrip_y, compass_cy + compass_r + margin - cstrip_y)

    cfg = {
        "left_speed":  {"enabled": not args.no_left_speed,
                        "x": ls_x, "y": ls_y, "w": ls_w, "h": ls_h},
        "right_speed": {"enabled": not args.no_right_speed,
                        "x": rs_x, "y": rs_y, "w": rs_w, "h": rs_h},
        "heading":     {"enabled": not args.no_heading,
                        "cx": compass_cx, "cy": compass_cy, "r": compass_r,
                        "x": cstrip_x, "y": cstrip_y, "w": cstrip_w, "h": cstrip_h},
    }

    # --- Fonts (scale from frame height) ---
    # Sized to be readable when the full-frame HUD WebM is composited over 4K footage.
    # medium is capped so the 3-digit readout fits within the tape strip width.
    fscale = H / 2160.0
    fs     = args.font_scale
    # Tick labels small (matching reference photo proportions);
    # cardinal labels medium; readout ~3× tick label height.
    # medium is capped so 3-digit "000" fits in the tape width with padding.
    # At 160px tape: cap = 160*62//100 = 99px; at 4K fscale=1.0 raw=80px → uses 80px.
    tape_ref    = ls_w if cfg["left_speed"]["enabled"] else \
                  rs_w if cfg["right_speed"]["enabled"] else default_tape_w
    medium_raw  = int(80 * fscale * fs)
    medium_cap  = tape_ref * 62 // 100      # 3-digit number + minimal padding must fit
    fonts       = {
        "micro":  find_font(max(10, int(15 * fscale * fs))),   # unit labels + intercardinal labels
        "tiny":   find_font(max(20, int(30 * fscale * fs))),   # tick labels
        "small":  find_font(max(26, int(46 * fscale * fs))),   # compass cardinal labels
        "medium": find_font(max(48, min(medium_raw, medium_cap))),  # speed & heading readouts
    }

    cfg["line_scale"]    = args.line_scale
    cfg["visible_range"] = float(args.visible_range)

    # --color-hex overrides the named palette with a custom hex color.
    hex_color = args.color_hex.strip()
    if hex_color and hex_color.startswith('#') and len(hex_color) in (4, 7):
        def _hex_to_rgba(h, a=255):
            h = h.lstrip('#')
            if len(h) == 3: h = ''.join(c*2 for c in h)
            r, g, b = int(h[0:2],16), int(h[2:4],16), int(h[4:6],16)
            return (r, g, b, a)
        base = _hex_to_rgba(hex_color)
        dim  = tuple(max(0, int(c * 0.65)) if i < 3 else 220 for i, c in enumerate(base))
        fnt  = tuple(min(255, int(c * 1.3)) if i < 3 else 255 for i, c in enumerate(base))
        pal = {
            "bright":   base,
            "mid":      dim,
            "dim":      tuple(max(0, int(c * 0.45)) if i < 3 else 200 for i, c in enumerate(base)),
            "faint":    tuple(max(0, int(c * 0.28)) if i < 3 else 160 for i, c in enumerate(base)),
            "readout":  fnt,
            "backing":  (0, 8, 2, 210),
            "box_fill": (0, 20, 5, 240),
            "north":    (255, 60, 60, 255),
        }
    else:
        pal = _PALETTES[args.color]

    # --- ffmpeg pipe (VP9 WebM with alpha) ---
    # -speed 8 + -row-mt 1: fastest VP9 preset with row-parallel threading.
    # -crf 40: HUD elements are simple geometry; high CRF is visually lossless here.
    # -tile-columns 2: allows VP9 to split the frame across multiple threads.
    ff_cmd = [
        args.ffmpeg, "-y",
        "-f",       "rawvideo",
        "-vcodec",  "rawvideo",
        "-s",       f"{W}x{H}",
        "-pix_fmt", "rgba",
        "-r",       str(rfps),
        "-i",       "pipe:0",
        "-r",       str(ofps),
        "-c:v",        "libvpx-vp9",
        "-pix_fmt",    "yuva420p",
        "-b:v",        "0",
        "-crf",        "40",
        "-speed",      "8",
        "-row-mt",     "1",
        "-tile-columns", "2",
        args.output,
    ]

    print(f"  Rendering {n_frames} frames → {args.output}", file=sys.stderr, flush=True)
    t_start = time.monotonic()

    try:
        proc = subprocess.Popen(ff_cmd, stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        print(f"Error: ffmpeg not found at '{args.ffmpeg}'", file=sys.stderr)
        sys.exit(1)

    # Pre-allocate once; render_hud_frame modifies element regions in-place.
    # Using memoryview avoids a buffer copy on each write().
    frame_buf = bytearray(W * H * 4)
    frame_mv  = memoryview(frame_buf)

    prev_pct = -1
    for fi in range(n_frames):
        t_sec = fi / rfps
        pt    = interp_point(pts, t_sec)
        render_hud_frame(W, H, pt, cfg, pal, fonts, frame_buf)

        try:
            proc.stdin.write(frame_mv)
        except BrokenPipeError:
            break

        pct = int(fi / n_frames * 100)
        if pct != prev_pct:
            elapsed = time.monotonic() - t_start
            fps_act = (fi + 1) / elapsed if elapsed > 0 else 0
            eta     = (n_frames - fi) / fps_act if fps_act > 0 else 0
            el_str  = f"{int(elapsed // 60)}:{int(elapsed % 60):02d}"
            eta_str = f"{int(eta    // 60)}:{int(eta    % 60):02d}"
            mult    = fps_act / rfps if rfps > 0 else 0
            print(f"\r  {pct}%  [{el_str} / {eta_str}]  {mult:.1f}x",
                  end="", file=sys.stderr, flush=True)
            prev_pct = pct

    print(f"\r  100%  Done.{' ' * 30}", file=sys.stderr)
    proc.stdin.close()
    proc.wait()

    if proc.returncode != 0:
        print(f"Error: ffmpeg exited with code {proc.returncode}", file=sys.stderr)
        sys.exit(proc.returncode)

    total_elapsed = time.monotonic() - t_start
    print(f"  Rendered in {total_elapsed:.1f}s", file=sys.stderr)


if __name__ == "__main__":
    main()
# SN: 00104
