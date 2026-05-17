#!/usr/bin/env python3
"""
clops_hud.py — CamClops military-style HUD overlay renderer

Reads a GPS track from a CamClops manifest JSON and renders an animated
HUD (heads-up display) overlay as a transparent WebM/VP9 video for
compositing over collage footage.

Elements:
  Left edge  — vertical speed ladder tape (km/h)
  Right edge — vertical speed ladder tape (mph)
  Bottom     — horizontal heading tape (degrees / cardinal)

Usage:
    clops_hud.py --manifest <path> --trip <id> --output <path.webm>
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
import multiprocessing
import socket
import subprocess
import sys
import tempfile
import time

_CAMCLOPS_VERSION = "1.101.2"
_CAMCLOPS_HWM     = "00117"

# ---------------------------------------------------------------------------
# Error log — tees sys.stderr to ~/.config/camclops/logs/<script>.log
# so GUI-launched runs leave a readable trace even when the GUI hides stderr.
# ---------------------------------------------------------------------------
def _setup_log():
    import datetime
    log_dir = os.path.join(os.path.expanduser("~"), ".config", "camclops", "logs")
    os.makedirs(log_dir, exist_ok=True)
    script   = os.path.splitext(os.path.basename(__file__))[0]
    log_path = os.path.join(log_dir, script + ".log")

    # Rotate: if > 1 MB keep last 512 KB so the file stays manageable
    try:
        if os.path.getsize(log_path) > 1_048_576:
            with open(log_path, "rb") as fh:
                fh.seek(-524_288, 2)
                tail = fh.read()
            with open(log_path, "wb") as fh:
                fh.write(tail)
    except OSError:
        pass

    log_fh = open(log_path, "a", buffering=1, encoding="utf-8", errors="replace")
    ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    log_fh.write(f"\n=== {script}  {ts}  {' '.join(sys.argv[1:])} ===\n")

    class _Tee:
        def __init__(self, stream, fh):
            self._s, self._f = stream, fh
        def write(self, data):
            self._s.write(data)
            try:   self._f.write(data)
            except Exception: pass
        def flush(self):
            self._s.flush()
            try:   self._f.flush()
            except Exception: pass
        def fileno(self):     return self._s.fileno()
        def isatty(self):     return False
        @property
        def encoding(self):   return self._s.encoding

    sys.stderr = _Tee(sys.__stderr__, log_fh)
    print(f"  Log: {log_path}", file=sys.stderr)
    return log_fh

def _brand_output(path: str, content_type: str, ffmpeg: str) -> None:
    base, ext = os.path.splitext(path)
    tmp = base + ".clops_brand" + ext   # preserve extension so ffmpeg picks correct muxer
    host = socket.gethostname().split('.')[0]
    ret = subprocess.run(
        [ffmpeg, "-v", "quiet", "-y", "-i", path,
         "-map", "0",              # copy all streams (incl. VP9 alpha track)
         "-c", "copy",
         "-metadata", "vendor=Nutball Labs",
         "-metadata", "product=CamClops",
         "-metadata", f"version={_CAMCLOPS_VERSION}, HWM {_CAMCLOPS_HWM}",
         "-metadata", f"created_on={host}",
         "-metadata", f"content_type={content_type}",
         tmp],
        capture_output=True
    )
    if ret.returncode == 0:
        os.replace(tmp, path)
    else:
        err = ret.stderr.decode("utf-8", errors="replace").strip()
        print(f"  [brand] ffmpeg exit {ret.returncode}: {err}", file=sys.stderr)
        try: os.remove(tmp)
        except OSError: pass

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


def compress_gaps(points, gap_threshold=10):
    """Compress large time gaps from archived segments. See clops_maprender.py."""
    if len(points) < 2:
        return points
    runs, run = [], [points[0]]
    for i in range(1, len(points)):
        if points[i]["t"] - points[i-1]["t"] > gap_threshold:
            runs.append(run)
            run = [points[i]]
        else:
            run.append(points[i])
    runs.append(run)
    if len(runs) == 1:
        return points
    remapped, t_cursor = [], runs[0][0]["t"]
    for r in runs:
        r_t0 = r[0]["t"]
        for pt in r:
            remapped.append({**pt, "t": t_cursor + (pt["t"] - r_t0)})
        t_cursor += (r[-1]["t"] - r[0]["t"]) + 1
    print(f"  Gap compression: {len(runs)} GPS run(s), "
          f"{points[-1]['t']:.0f}s → {remapped[-1]['t']:.0f}s",
          file=sys.stderr, flush=True)
    return remapped


def compute_derived_headings(points, min_move_m=2.0):
    """
    Replace GPS track headings with bearings computed from consecutive
    lat/lon positions. The D90's GPSTrack field freezes at the last
    road heading when speed drops below the chip's update threshold
    (~10 km/h). Position-derived bearing is accurate at any speed
    as long as the vehicle moved at least min_move_m between records.
    When movement is below the threshold (stopped / GPS jitter), the
    last valid bearing is held so the rose stays stable.
    """
    if len(points) < 2:
        return points

    def _bearing(lat1, lon1, lat2, lon2):
        lat1, lon1, lat2, lon2 = (math.radians(v) for v in (lat1, lon1, lat2, lon2))
        dlon = lon2 - lon1
        x = math.sin(dlon) * math.cos(lat2)
        y = math.cos(lat1) * math.sin(lat2) - math.sin(lat1) * math.cos(lat2) * math.cos(dlon)
        return (math.degrees(math.atan2(x, y)) + 360) % 360

    def _dist_m(lat1, lon1, lat2, lon2):
        R = 6_371_000.0
        dlat = math.radians(lat2 - lat1)
        dlon = math.radians(lon2 - lon1)
        a = (math.sin(dlat / 2) ** 2
             + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2))
             * math.sin(dlon / 2) ** 2)
        return 2 * R * math.asin(math.sqrt(min(a, 1.0)))

    result = [dict(p) for p in points]
    last_hdg = points[0].get("heading", 0.0)
    for i in range(len(points) - 1):
        d = _dist_m(points[i]["lat"], points[i]["lon"],
                    points[i + 1]["lat"], points[i + 1]["lon"])
        if d >= min_move_m:
            last_hdg = _bearing(points[i]["lat"], points[i]["lon"],
                                points[i + 1]["lat"], points[i + 1]["lon"])
        result[i]["heading"] = last_hdg
    result[-1]["heading"] = last_hdg
    return result


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
_hud_frame_counter = None   # set by main() before Pool forks; inherited via fork




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

        lbl_col = pal["mid"] if is_major else pal["dim"]
        if side == "left":
            draw.line([(scale_x - tlen, y), (scale_x, y)], fill=tcol, width=tw_)
            if is_major or is_minor:
                lbl = str(V)
                lw2, lh2 = _twh(draw, lbl, fonts["tiny"])
                lx = scale_x - tlen - lw2 - 4
                ly = y - lh2 // 2
                if lx >= tx and ty <= ly and ly + lh2 <= ty + th:
                    _hud_text(draw, (lx, ly), lbl, lbl_col, fonts["tiny"])
        else:
            draw.line([(scale_x, y), (scale_x + tlen, y)], fill=tcol, width=tw_)
            if is_major or is_minor:
                lbl = str(V)
                lw2, lh2 = _twh(draw, lbl, fonts["tiny"])
                lx = scale_x + tlen + 4
                ly = y - lh2 // 2
                if lx + lw2 <= tx + tw and ty <= ly and ly + lh2 <= ty + th:
                    _hud_text(draw, (lx, ly), lbl, lbl_col, fonts["tiny"])

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
# Multiprocessing helpers
# ─────────────────────────────────────────────────────────────────────────────

def _make_fonts(font_sizes):
    """Create PIL font objects from a sizes dict. Called in main and in each worker."""
    return {name: find_font(size) for name, size in font_sizes.items()}


def _render_chunk(job):
    """
    Worker: render a contiguous frame range to a temp WebM file.
    All arguments must be picklable — no PIL objects.
    Returns (returncode, tmp_path).
    """
    # Die when parent dies — prevents orphaned workers after GUI cancel/close.
    try:
        import ctypes as _ct, signal as _sig
        _ct.CDLL("libc.so.6", use_errno=True).prctl(1, _sig.SIGTERM)
    except Exception:
        pass

    (frame_start, frame_end, rfps, ofps, W, H,
     pts, cfg, pal, font_sizes, ffmpeg_exe, tmp_path, counter) = job

    fonts     = _make_fonts(font_sizes)
    frame_buf = bytearray(W * H * 4)

    # Each worker runs its own single-threaded VP9 encoder; parallelism
    # comes from N worker processes rather than intra-encoder threading.
    ff_cmd = [
        ffmpeg_exe, "-y",
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
        "-threads",    "1",
        tmp_path,
    ]
    proc = subprocess.Popen(ff_cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    for fi in range(frame_start, frame_end):
        pt = interp_point(pts, fi / rfps)
        render_hud_frame(W, H, pt, cfg, pal, fonts, frame_buf)
        try:
            proc.stdin.write(frame_buf)
        except BrokenPipeError:
            break
        if counter is not None:
            counter.value += 1
    proc.stdin.close()
    ff_stderr = proc.stderr.read().decode(errors="replace")
    proc.wait()
    # Only pass stderr back on failure — discard on success to keep output clean.
    err_tail = ""
    if proc.returncode != 0:
        lines = [l for l in ff_stderr.splitlines() if l.strip()]
        err_tail = "\n    ".join(lines[-6:]) if lines else "(no output)"
    return (proc.returncode, tmp_path, err_tail)


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def _cleanup_stale_tmp(prefix="clops_hud_"):
    """Remove leftover temp dirs from killed/cancelled prior runs."""
    import glob, shutil
    for d in glob.glob(os.path.join(tempfile.gettempdir(), prefix + "*")):
        if os.path.isdir(d):
            try:
                shutil.rmtree(d)
            except OSError:
                pass


def main():
    _setup_log()
    _cleanup_stale_tmp()
    parser = argparse.ArgumentParser(
        description="CamClops military-style HUD overlay renderer — GPS track → WebM"
    )
    parser.add_argument("--manifest",  required=True, help="CamClops manifest JSON path")
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
    parser.add_argument("--workers",   type=int, default=0,
                        help="Parallel render worker processes (0=auto uses all CPU cores, 1=serial)")

    # Left speed tape
    parser.add_argument("--no-left-speed",     action="store_true")
    parser.add_argument("--left-speed-width",  type=int, default=0, help="Tape width in px (0=auto)")
    parser.add_argument("--left-speed-height", type=int, default=0, help="Tape height in px (0=auto)")
    parser.add_argument("--left-speed-x",      type=int, default=0)
    parser.add_argument("--left-speed-y",      type=int, default=-1,
                        help="Top y of tape (-1=auto: readout box top at H/2)")

    # Right speed tape
    parser.add_argument("--no-right-speed",     action="store_true")
    parser.add_argument("--right-speed-width",  type=int, default=0)
    parser.add_argument("--right-speed-height", type=int, default=0)
    parser.add_argument("--right-speed-x",      type=int, default=-1,
                        help="Left x of tape (-1=auto right edge)")
    parser.add_argument("--right-speed-y",      type=int, default=-1,
                        help="Top y of tape (-1=auto: readout box top at H/2)")

    # Heading tape
    parser.add_argument("--no-heading",     action="store_true")
    parser.add_argument("--heading-height", type=int, default=0)
    parser.add_argument("--heading-width",  type=int, default=0)
    parser.add_argument("--heading-x",      type=int, default=-1,
                        help="Compass center X in pixels (-1 = frame centre)")
    parser.add_argument("--heading-y",      type=int, default=-1,
                        help="Compass center Y in pixels (-1 = auto)")
    parser.add_argument("--heading-crop",   type=int, default=20,
                        help="Percent of ring diameter below frame edge (0-50, default 20)")

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

    lock_secs = trip.get("gpsLockSeconds")
    if not isinstance(lock_secs, (int, float)) or lock_secs < 0:
        # gpsLockSeconds absent or invalid — derive from start_epoch + first GPS timestamp.
        start_epoch = trip.get("start_epoch", 0)
        if start_epoch and gps_track:
            first_ts = gps_track[0].get("timestamp", "")
            try:
                import calendar as _cal
                gt = datetime.datetime.strptime(first_ts[:19], "%Y:%m:%d %H:%M:%S")
                derived = _cal.timegm(gt.timetuple()) - int(start_epoch)
                lock_secs = derived if 0 <= derived <= 300 else 0
            except Exception:
                lock_secs = 0
        else:
            lock_secs = 0

    print(f"  Trip {args.trip}: {len(gps_track)} GPS samples  "
          f"(GPS lock offset: {lock_secs}s)", file=sys.stderr)

    pts       = build_points(gps_track, gps_lock_offset=lock_secs)
    pts       = compress_gaps(pts)
    pts       = compute_derived_headings(pts)
    total_dur = pts[-1]["t"]
    rfps      = args.render_fps
    ofps      = args.fps
    n_frames  = int(total_dur * rfps) + 1

    print(f"  Duration: {total_dur:.0f}s  →  {n_frames} frames "
          f"at {rfps}fps render / {ofps}fps output", file=sys.stderr)

    if args.workers > 0:
        n_workers = args.workers
    else:
        # Each worker allocates a W×H×4 frame buffer plus a libvpx-vp9 ffmpeg
        # instance (~6× frame size for encoder state at 4K).  Cap automatically
        # so total VP9 memory stays under ~512 MB; also use physical-core
        # estimate (logical // 2) since HT adds little for CPU-bound VP9 work.
        frame_mb = max(1, W * H * 4 // (1024 * 1024))
        per_worker_mb = frame_mb + max(64, frame_mb * 6)
        n_cpu = max(1, multiprocessing.cpu_count() // 2)
        n_workers = min(n_cpu, max(1, 512 // per_worker_mb))
    n_workers = max(1, min(n_workers, n_frames))

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
    # Sink the rose so heading_crop% of the diameter hangs below the frame edge.
    # compass_cy = H - compass_r + 2*compass_r*crop//100
    crop = max(0, min(50, args.heading_crop))
    compass_cy = args.heading_y if args.heading_y >= 0 else H - compass_r + compass_r * 2 * crop // 100

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
    font_sizes  = {
        "micro":  max(10, int(15 * fscale * fs)),   # unit labels + intercardinal labels
        "tiny":   max(20, int(30 * fscale * fs)),   # tick labels
        "small":  max(26, int(46 * fscale * fs)),   # compass cardinal labels
        "medium": max(48, min(medium_raw, medium_cap)),  # speed & heading readouts
    }
    fonts = _make_fonts(font_sizes)

    # Recompute tape y so the readout box top aligns with the horizontal seam
    # between the upper and lower quadrant pairs (H // 2).  The box height
    # depends on the rendered font metrics, so we measure it with a 1×1 probe
    # rather than guessing from the font size.  Only applies when --left/right-
    # speed-y were not explicitly set (i.e. still at their auto-positioned value).
    if args.left_speed_y < 0 or args.right_speed_y < 0:
        _probe_img  = Image.new("RGBA", (1, 1))
        _probe_draw = ImageDraw.Draw(_probe_img)
        _, _rh = _twh(_probe_draw, "000", fonts["medium"])
        _box_h  = _rh + max(10, ls_h // 80)
        _box_cy = H // 2 + _box_h // 2   # center_y that puts box top at H//2
        if args.left_speed_y < 0:
            ls_y = _box_cy - ls_h // 2
            cfg["left_speed"]["y"] = ls_y
        if args.right_speed_y < 0:
            rs_y = _box_cy - rs_h // 2
            cfg["right_speed"]["y"] = rs_y

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

    # --- Render ---
    print(f"  Rendering {n_frames} frames"
          + (f" across {n_workers} workers" if n_workers > 1 else "")
          + f" -> {args.output}", file=sys.stderr, flush=True)
    t_start = time.monotonic()
    # Emit 0% now so the progress bar activates as soon as setup is done.
    print(f"  0%  [0:00 / --:--]  starting", file=sys.stderr, flush=True)

    if n_workers == 1:
        # Serial path: pipe frames directly to a single ffmpeg process.
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
        try:
            proc = subprocess.Popen(ff_cmd, stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
        except FileNotFoundError:
            print(f"Error: ffmpeg not found at '{args.ffmpeg}'", file=sys.stderr)
            sys.exit(1)

        frame_buf    = bytearray(W * H * 4)
        frame_mv     = memoryview(frame_buf)
        prev_pct     = -1
        last_print_t = t_start

        for fi in range(n_frames):
            t_sec = fi / rfps
            pt    = interp_point(pts, t_sec)
            render_hud_frame(W, H, pt, cfg, pal, fonts, frame_buf)
            try:
                proc.stdin.write(frame_mv)
            except BrokenPipeError:
                break

            pct = int(fi / n_frames * 100)
            now = time.monotonic()
            if (pct != prev_pct or (now - last_print_t) >= 5.0) and (now - last_print_t) >= 0.1:
                elapsed = now - t_start
                fps_act = (fi + 1) / elapsed if elapsed > 0 else 0
                eta     = (n_frames - fi) / fps_act if fps_act > 0 else 0
                el_str  = f"{int(elapsed // 60)}:{int(elapsed % 60):02d}"
                eta_str = f"{int(eta    // 60)}:{int(eta    % 60):02d}"
                mult    = fps_act / rfps if rfps > 0 else 0
                print(f"  {pct}%  [{el_str} / {eta_str}]  {mult:.1f}x",
                      file=sys.stderr, flush=True)
                prev_pct     = pct
                last_print_t = now

        print(f"  100%  Done.", file=sys.stderr)
        proc.stdin.close()
        proc.wait()

        if proc.returncode != 0:
            print(f"Error: ffmpeg exited with code {proc.returncode}", file=sys.stderr)
            sys.exit(proc.returncode)

    else:
        # Parallel path: N workers each render a frame chunk to a temp WebM,
        # then ffmpeg concat -c copy stitches the chunks into the final output.
        chunk_size = (n_frames + n_workers - 1) // n_workers
        tmp_dir    = tempfile.mkdtemp(prefix="clops_hud_")
        tmp_files  = []
        list_file  = None
        # Shared counter passed in each job tuple so it works with both fork
        # (Linux) and spawn (Windows).  multiprocessing.Value is pickle-safe and
        # the default lock makes concurrent increments from spawned workers safe.
        try:
            jobs = []
            for i in range(n_workers):
                f_start = i * chunk_size
                f_end   = min(f_start + chunk_size, n_frames)
                if f_start >= n_frames:
                    break
                tmp_path = os.path.join(tmp_dir, f"chunk_{i:04d}.webm")
                tmp_files.append(tmp_path)
                # counter placeholder — filled in after Manager is created below
                jobs.append((f_start, f_end, rfps, ofps, W, H,
                             pts, cfg, pal, font_sizes, args.ffmpeg, tmp_path, None))

            n_chunks   = len(jobs)
            any_failed = False

            # Prefer fork on Linux for lower startup cost; fall back to spawn
            # on Windows.
            try:
                _ctx = multiprocessing.get_context("fork")
            except ValueError:
                _ctx = multiprocessing.get_context("spawn")

            # Manager.Value is a picklable proxy — safe to pass through the
            # Pool's internal queue regardless of start method.
            # multiprocessing.Value is NOT picklable and raises
            # "Synchronized objects should only be shared between processes
            # through inheritance" when passed via apply_async.
            with multiprocessing.Manager() as _mgr:
                counter = _mgr.Value('i', 0)
                jobs = [(*j[:-1], counter) for j in jobs]

                with _ctx.Pool(processes=n_chunks) as pool:
                    async_results = [pool.apply_async(_render_chunk, (job,))
                                     for job in jobs]
                    last_print = t_start

                    while True:
                        now     = time.monotonic()
                        elapsed = now - t_start
                        if (now - last_print) >= 3.0:
                            done   = counter.value
                            pct    = min(99, done * 100 // n_frames) if n_frames > 0 else 0
                            el_str = f"{int(elapsed // 60)}:{int(elapsed % 60):02d}"
                            line   = f"  {pct}%  ({done}/{n_frames} frames)  {el_str} elapsed\n"
                            os.write(2, line.encode())
                            last_print = now
                        if all(r.ready() for r in async_results):
                            break
                        time.sleep(1.0)

                for r in async_results:
                    try:
                        rc, chunk_path, err_tail = r.get()
                        if rc != 0:
                            any_failed = True
                            print(f"\n  Error: chunk failed (code {rc}): {chunk_path}",
                                  file=sys.stderr)
                            if err_tail:
                                print(f"    ffmpeg: {err_tail}", file=sys.stderr)
                    except Exception as exc:
                        any_failed = True
                        print(f"\n  Error: worker exception: {exc}", file=sys.stderr)

            print(file=sys.stderr)
            if any_failed:
                print("Error: one or more render chunks failed.", file=sys.stderr)
                sys.exit(1)

            list_file = os.path.join(tmp_dir, "concat.txt")
            with open(list_file, "w") as lf:
                for p in tmp_files:
                    lf.write(f"file '{p}'\n")

            print(f"  Concatenating {n_chunks} chunks...", file=sys.stderr, flush=True)
            r = subprocess.run(
                [args.ffmpeg, "-y", "-f", "concat", "-safe", "0",
                 "-i", list_file, "-c", "copy", args.output],
                stderr=subprocess.DEVNULL,
            )
            if r.returncode != 0:
                print("Error: ffmpeg concat failed.", file=sys.stderr)
                sys.exit(1)

        finally:
            for p in tmp_files:
                try:
                    os.unlink(p)
                except OSError:
                    pass
            if list_file:
                try:
                    os.unlink(list_file)
                except OSError:
                    pass
            try:
                os.rmdir(tmp_dir)
            except OSError:
                pass

    total_elapsed = time.monotonic() - t_start
    print(f"  Rendered in {total_elapsed:.1f}s", file=sys.stderr)
    _brand_output(args.output, "hud", args.ffmpeg)


if __name__ == "__main__":
    main()
# SN: 00119
