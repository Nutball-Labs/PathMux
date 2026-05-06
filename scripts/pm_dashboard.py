#!/usr/bin/env python3
"""
pm_dashboard.py — PathMux instrument dashboard renderer

Reads a GPS track from a PathMux manifest JSON and renders an animated
instrument panel (compass/heading, speedometer, trip odometer, weather)
as an MP4 video for use as a camera slot replacement in a collage, or as
a WebM with alpha channel for overlay compositing.

Usage:
    pm_dashboard.py --manifest <path> --trip <id> --output <path.mp4>
                    [--width 960] [--height 540] [--fps 30]
                    [--render-fps 10] [--units metric|imperial]
                    [--transparent] [--layout standard|quadrant-hud|<path.json>]
                    [--ffmpeg ffmpeg]

--transparent renders with a semi-transparent dark background and writes
WebM (VP9 + alpha).  Output path should end in .webm; the extension is
fixed automatically if it does not.

--layout selects the instrument layout:
    standard       3-panel row: compass | speed | weather  (default)
    quadrant-hud   Compact widgets at corners: compass top-right,
                   speed bottom-left, weather center (first 30 s)
    <path.json>    Custom layout JSON file

--render-fps controls unique frames rendered per second; ffmpeg duplicates
them to reach --fps.  --render-fps 10 gives a 3x speedup with no visible
quality loss on a non-map display.

Requirements:
    pip install Pillow
    pip install requests   (optional — for weather lookup; skipped gracefully
                            if absent or if the network is unavailable)
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
    print("Error: 'Pillow' module not found.  Install with:  pip install Pillow",
          file=sys.stderr)
    sys.exit(1)

try:
    import requests as _requests
    _HAS_REQUESTS = True
except ImportError:
    _HAS_REQUESTS = False


# ────────────────────────────────────────────────────────────────────────────
# GPS helpers
# ────────────────────────────────────────────────────────────────────────────

_R_KM = 6371.0


def _haversine_km(lat1, lon1, lat2, lon2):
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2
         + math.cos(math.radians(lat1))
         * math.cos(math.radians(lat2))
         * math.sin(dlon / 2) ** 2)
    return 2 * _R_KM * math.asin(math.sqrt(max(0.0, a)))


def _parse_ts(ts):
    return datetime.datetime.strptime(ts.rstrip("Z").strip(), "%Y:%m:%d %H:%M:%S")


def build_points(gps_track, gps_lock_offset=0):
    """
    Convert raw gpsTrack list to an interpolation table.
    Accumulated odometer (km) is computed from successive haversine distances.
    gps_lock_offset aligns T=0 with video start (same convention as pm_maprender.py).
    """
    if not gps_track:
        return []
    t0        = _parse_ts(gps_track[0]["timestamp"])
    pts       = []
    odo_km    = 0.0
    prev_lat  = prev_lon = None
    for raw in gps_track:
        t   = _parse_ts(raw["timestamp"])
        lat = raw["lat"]
        lon = raw["lon"]
        if prev_lat is not None:
            odo_km += _haversine_km(prev_lat, prev_lon, lat, lon)
        pts.append({
            "t":       (t - t0).total_seconds() + gps_lock_offset,
            "lat":     lat,
            "lon":     lon,
            "speed":   raw.get("speed",   0.0),   # km/h
            "heading": raw.get("heading", 0.0),   # degrees, 0=North, clockwise
            "accelX":  raw.get("accelX",  0.0),
            "accelY":  raw.get("accelY",  0.0),
            "accelZ":  raw.get("accelZ",  0.0),
            "odo_km":  odo_km,
        })
        prev_lat, prev_lon = lat, lon
    return pts


def compress_gaps(points, gap_threshold=10):
    """Compress large time gaps from archived segments. See pm_maprender.py."""
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
    """Position-derived bearing replaces GPS track heading. See pm_hud.py."""
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
    """Return a linearly interpolated point dict at elapsed time t_sec."""
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
    for k in ("lat", "lon", "speed", "heading", "accelX", "accelY", "accelZ", "odo_km"):
        a_v = pts[lo].get(k, 0.0)
        b_v = pts[hi].get(k, 0.0)
        result[k] = a_v + (b_v - a_v) * alpha
    return result


# ────────────────────────────────────────────────────────────────────────────
# Font loading
# ────────────────────────────────────────────────────────────────────────────

_font_warned = False


def find_font(size):
    global _font_warned
    candidates = [
        # Alma / RHEL actual paths
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        # Debian / Ubuntu / legacy paths
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        # macOS
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        # Windows
        "C:\\Windows\\Fonts\\arial.ttf",
    ]
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                pass
    if not _font_warned:
        print("  Warning: no TrueType font found — text may render small."
              "  Install dejavu-fonts-ttf or liberation-fonts.",
              file=sys.stderr)
        _font_warned = True
    try:
        return ImageFont.load_default(size=size)   # Pillow 10+
    except TypeError:
        return ImageFont.load_default()


def _twh(draw, text, font):
    """Return (width, height) of rendered text; compatible with Pillow 9 and 10+."""
    try:
        b = draw.textbbox((0, 0), text, font=font, anchor="lt")
        return b[2] - b[0], b[3] - b[1]
    except (AttributeError, TypeError):
        return draw.textsize(text, font=font)  # type: ignore[attr-defined]


def _text(draw, xy, text, fill, font, stroke=2):
    """draw.text with dark outline for readability on any background."""
    draw.text(xy, text, fill=fill, font=font, anchor="lt",
              stroke_width=stroke, stroke_fill="#000000")


# ────────────────────────────────────────────────────────────────────────────
# Weather via Open-Meteo archive API (free, no API key required)
# ────────────────────────────────────────────────────────────────────────────

_WMO = {
    0: "Clear sky",      1: "Mainly clear",    2: "Partly cloudy",  3: "Overcast",
    45: "Foggy",         48: "Icy fog",
    51: "Light drizzle", 53: "Drizzle",        55: "Heavy drizzle",
    61: "Light rain",    63: "Rain",            65: "Heavy rain",
    71: "Light snow",    73: "Snow",            75: "Heavy snow",    77: "Snow grains",
    80: "Rain showers",  81: "Showers",         82: "Heavy showers",
    85: "Snow showers",  86: "Heavy snow showers",
    95: "Thunderstorm",  96: "T-storm + hail",  99: "T-storm + heavy hail",
}


def fetch_weather(lat, lon, dt):
    """
    Return (wmo_code, description, temp_c) for the hour containing dt, or None.
    Uses Open-Meteo archive API — completely free, no registration required.
    """
    if not _HAS_REQUESTS:
        print("  Weather skipped: 'requests' not installed  (pip install requests)",
              file=sys.stderr)
        return None
    try:
        date = dt.strftime("%Y-%m-%d")
        url  = (
            "https://archive-api.open-meteo.com/v1/archive"
            f"?latitude={lat:.4f}&longitude={lon:.4f}"
            f"&start_date={date}&end_date={date}"
            "&hourly=weather_code,temperature_2m&timezone=UTC"
        )
        r = _requests.get(url, timeout=12)
        r.raise_for_status()
        h     = r.json().get("hourly", {})
        times = h.get("time", [])
        codes = h.get("weather_code", [])
        temps = h.get("temperature_2m", [])
        # Match on exact hour
        target = dt.strftime("%Y-%m-%dT%H:00")
        for i, t in enumerate(times):
            if t == target:
                code = int(codes[i]) if i < len(codes) else 0
                temp = temps[i]      if i < len(temps)  else None
                return code, _WMO.get(code, f"Code {code}"), temp
        # Fallback: index by hour-of-day
        if codes and dt.hour < len(codes):
            code = int(codes[dt.hour])
            temp = temps[dt.hour] if temps and dt.hour < len(temps) else None
            return code, _WMO.get(code, f"Code {code}"), temp
    except Exception as e:
        print(f"\n  Weather unavailable: {e}", file=sys.stderr)
    return None


# ────────────────────────────────────────────────────────────────────────────
# Weather icon drawing — Pillow shapes only, no external image files
# ────────────────────────────────────────────────────────────────────────────

_SUN   = "#FFEE00"
_CLOUD = "#C0CCD8"
_RAIN  = "#44AAFF"
_SNOW  = "#D0E8FF"
_BOLT  = "#FFEE00"
_FOG   = "#90A0B0"


def _draw_sun(draw, cx, cy, r, color=_SUN):
    draw.ellipse([(cx - r, cy - r), (cx + r, cy + r)], fill=color)
    ray = max(int(r * 0.45), 6)
    lw  = max(2, r // 14)
    for deg in range(0, 360, 45):
        a  = math.radians(deg)
        x1 = cx + (r + 4) * math.cos(a)
        y1 = cy + (r + 4) * math.sin(a)
        x2 = cx + (r + 4 + ray) * math.cos(a)
        y2 = cy + (r + 4 + ray) * math.sin(a)
        draw.line([(x1, y1), (x2, y2)], fill=color, width=lw)


def _draw_cloud(draw, cx, cy, r, color=_CLOUD):
    # Body
    draw.ellipse([(cx - r,        cy - int(r * 0.3)),
                  (cx + int(r * 0.35), cy + int(r * 0.5))], fill=color)
    # Left puff
    draw.ellipse([(cx - int(r * 0.7), cy - int(r * 0.75)),
                  (cx + int(r * 0.1), cy + int(r * 0.1))],  fill=color)
    # Top-right puff
    draw.ellipse([(cx - int(r * 0.1), cy - r),
                  (cx + int(r * 0.9), cy + int(r * 0.1))],  fill=color)
    # Right shoulder
    draw.ellipse([(cx,                cy - int(r * 0.4)),
                  (cx + r,            cy + int(r * 0.5))],   fill=color)


def _draw_rain_lines(draw, cx, cy, r, color=_RAIN):
    n       = max(3, r // 12)
    spacing = r * 2.0 / (n + 1)
    length  = int(r * 0.55)
    lw      = max(2, r // 18)
    for i in range(n):
        rx = int(cx - r + spacing * (i + 1))
        ry = int(cy + int(r * 0.35))
        draw.line([(rx, ry), (rx - int(length * 0.3), ry + length)],
                  fill=color, width=lw)


def _draw_snow_dots(draw, cx, cy, r, color=_SNOW):
    dr = max(3, r // 12)
    for i in range(6):
        a  = math.radians(i * 60)
        sx = int(cx + r * 0.55 * math.cos(a))
        sy = int(cy + int(r * 0.5) + int(r * 0.35 * math.sin(a)))
        draw.ellipse([(sx - dr, sy - dr), (sx + dr, sy + dr)], fill=color)


def draw_weather_icon(draw, cx, cy, r, wmo_code):
    """Draw a simple, icon-like weather symbol centred at (cx, cy) with radius r."""
    r = int(r)
    if wmo_code is None:
        return

    if wmo_code == 0:                                          # Clear sky — sun
        _draw_sun(draw, cx, cy, int(r * 0.52))

    elif wmo_code in (1, 2):                                   # Partly cloudy
        _draw_sun(draw, cx - r // 3, cy - r // 4, int(r * 0.36))
        _draw_cloud(draw, cx + r // 5, cy + r // 5, int(r * 0.46))

    elif wmo_code == 3:                                        # Overcast
        _draw_cloud(draw, cx, cy, int(r * 0.55))

    elif wmo_code in (45, 48):                                 # Fog
        for i in range(4):
            yl = int(cy - r * 0.25 + i * r * 0.2)
            draw.line(
                [(cx - int(r * 0.58), yl), (cx + int(r * 0.58), yl)],
                fill=_FOG, width=max(3, r // 14))

    elif wmo_code in (51, 53, 55, 61, 63, 65, 80, 81, 82):   # Rain / drizzle
        _draw_cloud(draw, cx, cy - r // 5, int(r * 0.48))
        _draw_rain_lines(draw, cx, cy + r // 8, r)

    elif wmo_code in (71, 73, 75, 77, 85, 86):                # Snow
        _draw_cloud(draw, cx, cy - r // 5, int(r * 0.48))
        _draw_snow_dots(draw, cx, cy + r // 8, r)

    elif wmo_code in (95, 96, 99):                             # Thunderstorm
        _draw_cloud(draw, cx, cy - r // 4, int(r * 0.46))
        bx, by = cx, int(cy + r * 0.08)
        draw.polygon([
            (bx,                by),
            (bx + r // 4,       by + r // 3),
            (bx - r // 14,      by + r // 3),
            (bx + r // 4 + 2,   by + int(r * 0.7)),
        ], fill=_BOLT)

    else:                                                      # Unknown WMO code
        font = ImageFont.load_default()
        draw.text((cx - 5, cy - 8), "?", fill="#808090", font=font)


def _cardinal(deg):
    dirs = ["N","NNE","NE","ENE","E","ESE","SE","SSE",
            "S","SSW","SW","WSW","W","WNW","NW","NNW"]
    return dirs[int(round(deg / 22.5)) % 16]


# ────────────────────────────────────────────────────────────────────────────
# Palette
# ────────────────────────────────────────────────────────────────────────────

_BG  = "#0A0C12"      # solid background (opaque mode)
_DIV = "#3A3F58"      # panel dividers
_WHT = "#FFFFFF"      # primary text
_DIM = "#B0BAD4"      # secondary labels
_RED = "#FF2244"      # north indicator, needle tail
_GRN = "#00FF88"      # odometer (neon green)
_ACC = "#00CCFF"      # accent cyan: heading readout, compass cardinals
_YLW = "#FFE030"      # speed digits
_PANEL_ALPHA = 160    # panel background alpha in transparent mode (0–255)


# ────────────────────────────────────────────────────────────────────────────
# Layout system
# ────────────────────────────────────────────────────────────────────────────

# Built-in layout definitions.
# For widget layouts, element sizes are fractions of frame height (≤1.0).
_BUILTIN_LAYOUTS = {
    "standard": {
        "_preset": "standard",
    },
    "quadrant-hud": {
        "_preset": "widget",
        "compass": {"anchor": "top-right",   "size": 0.24},
        "speed":   {"anchor": "bottom-left", "size": 0.24},
        "weather": {"anchor": "center",      "size": 0.18, "end": 30},
    },
}


def load_layout(name_or_path):
    """Load a layout by built-in name or JSON file path. Falls back to standard."""
    if not name_or_path or name_or_path == "standard":
        return _BUILTIN_LAYOUTS["standard"]
    if name_or_path in _BUILTIN_LAYOUTS:
        return _BUILTIN_LAYOUTS[name_or_path]
    try:
        with open(name_or_path) as f:
            data = json.load(f)
        if "_preset" not in data:
            data["_preset"] = "widget"
        return data
    except Exception as e:
        print(f"  Warning: cannot load layout '{name_or_path}': {e}  (using standard)",
              file=sys.stderr)
        return _BUILTIN_LAYOUTS["standard"]


def resolve_anchor(anchor, x_override, y_override, W, H, size, margin=16):
    """
    Return (cx, cy) center of the element bounding box.
    size = element radius in pixels; used to keep the element inside the frame.
    """
    half = int(size)
    pad  = margin + half
    named = {
        "top-left":      (pad,      pad),
        "top-right":     (W - pad,  pad),
        "bottom-left":   (pad,      H - pad),
        "bottom-right":  (W - pad,  H - pad),
        "center":        (W // 2,   H // 2),
        "top-center":    (W // 2,   pad),
        "bottom-center": (W // 2,   H - pad),
        "left-center":   (pad,      H // 2),
        "right-center":  (W - pad,  H // 2),
    }
    if anchor and anchor in named:
        return named[anchor]
    return (x_override or W // 2), (y_override or H // 2)


# ────────────────────────────────────────────────────────────────────────────
# Widget element draw functions
# ────────────────────────────────────────────────────────────────────────────

def draw_element_backing(draw, cx, cy, size, transparent):
    """Semi-transparent rounded-rect backing behind a widget element."""
    pad = max(8, int(size * 0.12))
    box = [(cx - size - pad, cy - size - pad),
           (cx + size + pad, cy + size + pad)]
    r = min(16, size // 4)
    try:
        if transparent:
            draw.rounded_rectangle(box, radius=r, fill=(6, 8, 16, _PANEL_ALPHA))
        else:
            draw.rounded_rectangle(box, radius=r, fill=(20, 24, 38),
                                   outline=_DIV, width=1)
    except AttributeError:
        # Pillow < 8.2 fallback
        if transparent:
            draw.rectangle(box, fill=(6, 8, 16, _PANEL_ALPHA))
        else:
            draw.rectangle(box, fill=(20, 24, 38), outline=_DIV)


def draw_compass_element(draw, cx, cy, CR, pt, fonts):
    """Draw compass rose centered at (cx, cy) with ring radius CR."""
    # Scale fonts to ring size (CR ≈ 90 at default 960×540 standard layout)
    scale = CR / 90.0
    tiny   = find_font(max(9,  int(14 * scale)))
    small  = find_font(max(12, int(20 * scale)))
    medium = find_font(max(14, int(28 * scale)))

    # Outer ring
    draw.ellipse([(cx - CR, cy - CR), (cx + CR, cy + CR)], outline=_ACC, width=2)

    # Tick marks every 10°, major every 30°
    for deg in range(0, 360, 10):
        major = (deg % 30 == 0)
        a     = math.radians(deg - 90)
        r_out = CR
        r_in  = CR - (max(6, CR // 8) if major else max(3, CR // 16))
        color = "#6A7890" if major else "#454858"
        draw.line(
            [(int(cx + r_out * math.cos(a)), int(cy + r_out * math.sin(a))),
             (int(cx + r_in  * math.cos(a)), int(cy + r_in  * math.sin(a)))],
            fill=color, width=(2 if major else 1))

    # Cardinal labels N / E / S / W inside the ring
    LR = CR - max(12, CR // 5)
    for lbl, deg in [("N", 0), ("E", 90), ("S", 180), ("W", 270)]:
        a  = math.radians(deg - 90)
        lx = int(cx + LR * math.cos(a))
        ly = int(cy + LR * math.sin(a))
        tw, th = _twh(draw, lbl, small)
        col = _RED if lbl == "N" else _ACC
        _text(draw, (lx - tw // 2, ly - th // 2), lbl, col, small, stroke=2)

    # Heading needle
    heading = pt.get("heading", 0.0)
    ang     = math.radians(heading - 90)
    NL = int(CR * 0.75)
    TL = int(CR * 0.28)
    nw = max(3, int(CR * 0.07))
    tip_x  = int(cx + NL * math.cos(ang))
    tip_y  = int(cy + NL * math.sin(ang))
    tail_x = int(cx - TL * math.cos(ang))
    tail_y = int(cy - TL * math.sin(ang))
    draw.line([(cx, cy), (tip_x,  tip_y)],  fill=_WHT, width=nw)
    draw.line([(cx, cy), (tail_x, tail_y)], fill=_RED, width=nw)
    cd = max(4, int(CR * 0.08))
    draw.ellipse([(cx - cd, cy - cd), (cx + cd, cy + cd)], fill=_WHT)

    # Heading readout below ring
    hdg_txt = f"{int(heading + 0.5) % 360}°  {_cardinal(heading)}"
    tw, th  = _twh(draw, hdg_txt, medium)
    _text(draw, (cx - tw // 2, cy + CR + max(4, CR // 12)), hdg_txt, _ACC, medium, stroke=2)


def draw_speed_element(draw, cx, cy, size, speed_kmh, odo_km, units, fonts):
    """
    Draw speed + odometer block.
    cx, cy = center of element; size = half-height of the rendering box.
    Fonts are scaled to fit within size.
    """
    fscale = size / 160.0   # 160px = canonical half-height at 960×540 standard
    huge_f  = find_font(max(28, int(80 * fscale)))
    med_f   = find_font(max(12, int(28 * fscale)))
    small_f = find_font(max(10, int(18 * fscale)))

    spd_mph = speed_kmh * 0.621371
    if units == "imperial":
        pri_val, pri_lbl = spd_mph,   "mph"
        sec_val, sec_lbl = speed_kmh, "km/h"
    else:
        pri_val, pri_lbl = speed_kmh, "km/h"
        sec_val, sec_lbl = spd_mph,   "mph"

    pri_str = str(int(round(pri_val)))
    sec_str = str(int(round(sec_val)))
    odo_mi  = odo_km * 0.621371
    odo_str = f"Trip: {odo_km:.1f} km / {odo_mi:.1f} mi"

    psw, psh = _twh(draw, pri_str, huge_f)
    plw, plh = _twh(draw, pri_lbl, med_f)
    ssw, ssh = _twh(draw, sec_str, huge_f)
    slw, slh = _twh(draw, sec_lbl, med_f)
    ow,  oh  = _twh(draw, odo_str, small_f)

    top_y = cy - size
    bot_y = cy + size
    odo_y = bot_y - oh - max(4, int(size * 0.05))
    GA = max(2, int(psh * 0.08))
    GB = max(6, int(psh * 0.18))
    GC = max(2, int(ssh * 0.08))
    block_h = psh + GA + plh + GB + ssh + GC + slh
    y = top_y + max(0, (odo_y - top_y - block_h) // 2)

    _text(draw, (cx - psw // 2, y), pri_str, _YLW, huge_f, stroke=3); y += psh + GA
    _text(draw, (cx - plw // 2, y), pri_lbl, _DIM, med_f,  stroke=1); y += plh + GB
    _text(draw, (cx - ssw // 2, y), sec_str, _WHT, huge_f, stroke=2); y += ssh + GC
    _text(draw, (cx - slw // 2, y), sec_lbl, _DIM, med_f,  stroke=1)
    _text(draw, (cx - ow  // 2, odo_y), odo_str, _GRN, small_f, stroke=2)


def draw_weather_element(draw, cx, cy, IR, weather, fonts):
    """Draw weather icon + text centered at (cx, cy), icon radius = IR."""
    fscale = IR / 108.0   # 108px = canonical icon radius at 960×540 standard
    small_f = find_font(max(10, int(22 * fscale)))

    if not weather:
        mw, mh = _twh(draw, "No weather data", small_f)
        _text(draw, (cx - mw // 2, cy - mh // 2), "No weather data", _DIM, small_f, stroke=1)
        return

    wmo_code, description, temp_c = weather
    ICY = cy - IR // 4    # push icon above center so text fits below
    draw_weather_icon(draw, cx, ICY, IR, wmo_code)

    words  = description.split()
    lines  = [" ".join(words[i:i + 2]) for i in range(0, len(words), 2)]
    desc_y = ICY + IR + max(8, IR // 10)
    for line in lines:
        lw2, lh = _twh(draw, line, small_f)
        _text(draw, (cx - lw2 // 2, desc_y), line, _WHT, small_f, stroke=2)
        desc_y += lh + 4

    if temp_c is not None:
        temp_f = temp_c * 9 / 5 + 32
        tstr   = f"{temp_c:.0f}°C / {temp_f:.0f}°F"
        tw2, _ = _twh(draw, tstr, small_f)
        _text(draw, (cx - tw2 // 2, desc_y + 4), tstr, _DIM, small_f, stroke=2)


# ────────────────────────────────────────────────────────────────────────────
# Frame renderers
# ────────────────────────────────────────────────────────────────────────────

def _render_standard_panels(draw, W, H, pt, speed_kmh, odo_km, weather, fonts, units):
    """Render the classic 3-panel dashboard layout onto an existing draw context."""
    PW  = W // 3

    # Panel dividers
    draw.line([(PW,     0), (PW,     H)], fill=_DIV, width=2)
    draw.line([(PW * 2, 0), (PW * 2, H)], fill=_DIV, width=2)

    # ── Panel 1: Compass ─────────────────────────────────────────────────────
    _text(draw, (6, 6), "HEADING", _DIM, fonts["tiny"], stroke=1)

    CCX = PW // 2
    CCY = H // 2 - 10
    CR  = min(PW, H) // 3

    draw.ellipse(
        [(CCX - CR, CCY - CR), (CCX + CR, CCY + CR)],
        outline=_ACC, width=2)

    for deg in range(0, 360, 10):
        major = (deg % 30 == 0)
        a     = math.radians(deg - 90)
        r_out = CR
        r_in  = CR - (12 if major else 5)
        color = "#6A7890" if major else "#454858"
        draw.line(
            [(int(CCX + r_out * math.cos(a)), int(CCY + r_out * math.sin(a))),
             (int(CCX + r_in  * math.cos(a)), int(CCY + r_in  * math.sin(a)))],
            fill=color, width=(2 if major else 1))

    LR = CR - 22
    for lbl, deg in [("N", 0), ("E", 90), ("S", 180), ("W", 270)]:
        a  = math.radians(deg - 90)
        lx = int(CCX + LR * math.cos(a))
        ly = int(CCY + LR * math.sin(a))
        tw, th = _twh(draw, lbl, fonts["small"])
        col = _RED if lbl == "N" else _ACC
        _text(draw, (lx - tw // 2, ly - th // 2), lbl, col, fonts["small"], stroke=2)

    heading = pt.get("heading", 0.0)
    ang     = math.radians(heading - 90)
    NL = int(CR * 0.75)
    TL = int(CR * 0.28)
    nw = max(3, int(CR * 0.07))
    tip_x  = int(CCX + NL * math.cos(ang))
    tip_y  = int(CCY + NL * math.sin(ang))
    tail_x = int(CCX - TL * math.cos(ang))
    tail_y = int(CCY - TL * math.sin(ang))
    draw.line([(CCX, CCY), (tip_x,  tip_y)],  fill=_WHT, width=nw)
    draw.line([(CCX, CCY), (tail_x, tail_y)], fill=_RED, width=nw)
    cd = max(5, int(CR * 0.09))
    draw.ellipse([(CCX - cd, CCY - cd), (CCX + cd, CCY + cd)], fill=_WHT)

    hdg_txt = f"{int(heading + 0.5) % 360}°  {_cardinal(heading)}"
    tw, th  = _twh(draw, hdg_txt, fonts["medium"])
    _text(draw, (CCX - tw // 2, CCY + CR + 14), hdg_txt, _ACC, fonts["medium"], stroke=2)

    # ── Panel 2: Speed ────────────────────────────────────────────────────────
    _text(draw, (PW + 6, 6), "SPEED", _DIM, fonts["tiny"], stroke=1)

    SCX     = PW + PW // 2
    spd_mph = speed_kmh * 0.621371

    if units == "imperial":
        pri_val, pri_lbl = spd_mph,    "mph"
        sec_val, sec_lbl = speed_kmh,  "km/h"
    else:
        pri_val, pri_lbl = speed_kmh,  "km/h"
        sec_val, sec_lbl = spd_mph,    "mph"

    pri_str = str(int(round(pri_val)))
    sec_str = str(int(round(sec_val)))

    psw, psh = _twh(draw, pri_str, fonts["huge"])
    plw, plh = _twh(draw, pri_lbl, fonts["medium"])
    ssw, ssh = _twh(draw, sec_str, fonts["huge"])
    slw, slh = _twh(draw, sec_lbl, fonts["medium"])
    odo_mi   = odo_km * 0.621371
    odo_str  = f"Trip: {odo_km:.1f} km / {odo_mi:.1f} mi"
    ow, oh   = _twh(draw, odo_str, fonts["small"])

    HDR_BOT = max(20, int(H * 0.06))
    ODO_PAD = max(10, int(H * 0.07))
    odo_y   = H - ODO_PAD - oh
    GA = max(4,  int(psh * 0.08))
    GB = max(12, int(psh * 0.22))
    GC = max(4,  int(ssh * 0.08))
    block_h = psh + GA + plh + GB + ssh + GC + slh
    avail   = odo_y - max(4, int(H * 0.02)) - HDR_BOT
    y       = HDR_BOT + max(0, (avail - block_h) // 2)

    _text(draw, (SCX - psw // 2, y), pri_str, _YLW, fonts["huge"],   stroke=3); y += psh + GA
    _text(draw, (SCX - plw // 2, y), pri_lbl, _DIM, fonts["medium"], stroke=1); y += plh + GB
    _text(draw, (SCX - ssw // 2, y), sec_str, _WHT, fonts["huge"],   stroke=2); y += ssh + GC
    _text(draw, (SCX - slw // 2, y), sec_lbl, _DIM, fonts["medium"], stroke=1)
    _text(draw, (SCX - ow  // 2, odo_y), odo_str, _GRN, fonts["small"], stroke=2)

    # ── Panel 3: Weather ─────────────────────────────────────────────────────
    _text(draw, (PW * 2 + 6, 6), "CONDITIONS", _DIM, fonts["tiny"], stroke=1)

    WCX = PW * 2 + PW // 2
    IR  = min(PW // 2 - 16, H // 5)

    if weather:
        wmo_code, description, temp_c = weather
        ICY = max(20, int(H * 0.06)) + IR
        draw_weather_icon(draw, WCX, ICY, IR, wmo_code)

        words  = description.split()
        lines  = [" ".join(words[i:i + 2]) for i in range(0, len(words), 2)]
        desc_y = ICY + IR + 16
        for line in lines:
            lw2, lh = _twh(draw, line, fonts["small"])
            _text(draw, (WCX - lw2 // 2, desc_y), line, _WHT, fonts["small"], stroke=2)
            desc_y += lh + 4

        if temp_c is not None:
            temp_f = temp_c * 9 / 5 + 32
            tstr   = f"{temp_c:.0f}°C / {temp_f:.0f}°F"
            tw2, _ = _twh(draw, tstr, fonts["small"])
            _text(draw, (WCX - tw2 // 2, desc_y + 8), tstr, _DIM, fonts["small"], stroke=2)
    else:
        for i, msg in enumerate(["No weather", "data available"]):
            mw2, mh2 = _twh(draw, msg, fonts["small"])
            _text(draw,
                  (WCX - mw2 // 2, H // 2 + (i - 1) * (mh2 + 4)),
                  msg, _DIM, fonts["small"], stroke=1)


def _render_widget_layout(draw, W, H, pt, speed_kmh, odo_km, weather, fonts, units,
                          layout, t_sec, transparent):
    """Render elements from a widget layout dict."""
    for elem_id, spec in layout.items():
        if elem_id.startswith("_"):
            continue

        # Time window
        start = spec.get("start") or 0
        end   = spec.get("end")
        if t_sec < start:
            continue
        if end is not None and t_sec >= end:
            continue

        # Size: fraction of H if ≤1.0, else raw pixels scaled by fscale
        raw_size = spec.get("size", 0.24)
        size = int(raw_size * H if raw_size <= 1.0 else raw_size * (H / 540.0))
        size = max(20, size)

        cx, cy = resolve_anchor(
            spec.get("anchor"), spec.get("x"), spec.get("y"), W, H, size)

        draw_element_backing(draw, cx, cy, size, transparent)

        if elem_id == "compass":
            draw_compass_element(draw, cx, cy, size, pt, fonts)
        elif elem_id == "speed":
            draw_speed_element(draw, cx, cy, size, speed_kmh, odo_km, units, fonts)
        elif elem_id == "weather":
            draw_weather_element(draw, cx, cy, max(20, size // 2), weather, fonts)


def render_frame(W, H, pt, speed_kmh, odo_km, weather, fonts,
                 units="imperial", transparent=False, layout=None, t_sec=0.0):
    """
    Render one dashboard frame.
    layout: dict from load_layout(); None or standard → classic 3-panel.
    t_sec:  elapsed video time (used for element time-window checks).
    """
    preset = (layout or {}).get("_preset", "standard")

    if transparent:
        img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        if preset == "standard":
            ImageDraw.Draw(img).rectangle(
                [(0, 0), (W - 1, H - 1)], fill=(6, 8, 16, _PANEL_ALPHA))
        # widget layout: no full-frame backing; each element draws its own
    else:
        img = Image.new("RGB", (W, H), _BG)

    draw = ImageDraw.Draw(img)

    if preset == "standard":
        _render_standard_panels(draw, W, H, pt, speed_kmh, odo_km, weather, fonts, units)
    else:
        _render_widget_layout(draw, W, H, pt, speed_kmh, odo_km, weather, fonts, units,
                              layout, t_sec, transparent)

    return img


# ────────────────────────────────────────────────────────────────────────────
# Main
# ────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="PathMux instrument dashboard renderer — GPS track → MP4 / WebM"
    )
    parser.add_argument("--manifest",    required=True,
                        help="PathMux manifest JSON path")
    parser.add_argument("--trip",        required=True,
                        help="Trip ID (e.g. G1)")
    parser.add_argument("--output",      required=True,
                        help="Output path (.mp4 or .webm)")
    parser.add_argument("--width",       type=int, default=960)
    parser.add_argument("--height",      type=int, default=540)
    parser.add_argument("--fps",         type=int, default=30,
                        help="Output framerate (default 30)")
    parser.add_argument("--render-fps",  type=int, default=None,
                        help="Unique frames rendered/sec; ffmpeg fills to --fps.  "
                             "Default: same as --fps.  10 gives 3x speedup.")
    parser.add_argument("--units", default="metric",
                        choices=["metric", "imperial"])
    parser.add_argument("--transparent", action="store_true",
                        help="Semi-transparent dark background; output as WebM/VP9 "
                             "with alpha channel.  Output extension forced to .webm.")
    parser.add_argument("--layout", default="standard",
                        help="Layout preset (standard, quadrant-hud) or path to JSON "
                             "layout file.  Default: standard (3-panel row).")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    args = parser.parse_args()

    # Transparent mode requires WebM container (VP9 alpha)
    if args.transparent and not args.output.lower().endswith(".webm"):
        base  = os.path.splitext(args.output)[0]
        args.output = base + ".webm"
        print(f"  --transparent: output changed to {args.output} (VP9 alpha)",
              file=sys.stderr)

    layout = load_layout(args.layout)
    print(f"  Layout: {args.layout} (preset: {layout.get('_preset', 'standard')})",
          file=sys.stderr)

    # --- Load manifest ---
    try:
        with open(args.manifest) as f:
            manifest = json.load(f)
    except Exception as e:
        print(f"Error: Cannot read manifest: {e}", file=sys.stderr)
        sys.exit(1)

    trip = next(
        (t for t in manifest.get("trips", [])
         if str(t.get("id", "")).upper() == args.trip.upper()),
        None)
    if trip is None:
        print(f"Error: Trip '{args.trip}' not found in manifest.", file=sys.stderr)
        sys.exit(1)

    gps_track = trip.get("gpsTrack", [])
    if len(gps_track) < 2:
        print("Error: Trip has no GPS track data (run GPS extraction first).",
              file=sys.stderr)
        sys.exit(1)

    lock_secs = trip.get("gpsLockSeconds")
    if not isinstance(lock_secs, (int, float)) or lock_secs < 0:
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

    pts        = build_points(gps_track, gps_lock_offset=lock_secs)
    pts        = compress_gaps(pts)
    pts        = compute_derived_headings(pts)
    total_dur  = pts[-1]["t"]
    render_fps = args.render_fps or args.fps
    output_fps = args.fps
    n_frames   = int(total_dur * render_fps) + 1

    print(f"  Duration: {total_dur:.0f}s  →  {n_frames} frames "
          f"at {render_fps}fps render / {output_fps}fps output", file=sys.stderr)

    # --- Weather (fetched once, shown throughout the video) ---
    weather   = None
    start_lat = trip.get("startLat", 0.0)
    start_lon = trip.get("startLon", 0.0)
    if start_lat or start_lon:
        date_s = trip.get("date",       "")
        time_s = trip.get("start_time", "00:00:00")
        try:
            trip_dt = datetime.datetime.strptime(
                f"{date_s} {time_s}", "%Y-%m-%d %H:%M:%S")
            print(f"  Fetching weather for {date_s} {time_s} at "
                  f"{start_lat:.4f},{start_lon:.4f}...",
                  file=sys.stderr, flush=True)
            weather = fetch_weather(start_lat, start_lon, trip_dt)
            if weather:
                code, desc, temp = weather
                print(f"  Weather: {desc}"
                      + (f"  {temp:.0f}°C" if temp is not None else ""),
                      file=sys.stderr)
            else:
                print("  Weather: unavailable", file=sys.stderr)
        except ValueError:
            pass

    # --- Fonts — scale with frame height so layout holds at any resolution ---
    fscale = args.height / 540.0
    fonts = {
        "tiny":   find_font(max(12, int(round(18 * fscale)))),
        "small":  find_font(max(16, int(round(26 * fscale)))),
        "medium": find_font(max(22, int(round(36 * fscale)))),
        "huge":   find_font(max(44, int(round(110 * fscale)))),
    }

    # --- ffmpeg pipe ---
    if args.transparent:
        # VP9 WebM with alpha channel
        ff_cmd = [
            args.ffmpeg, "-y",
            "-f",       "rawvideo",
            "-vcodec",  "rawvideo",
            "-s",       f"{args.width}x{args.height}",
            "-pix_fmt", "rgba",
            "-r",       str(render_fps),
            "-i",       "pipe:0",
            "-r",       str(output_fps),
            "-c:v",     "libvpx-vp9",
            "-pix_fmt", "yuva420p",
            "-b:v",     "0",
            "-crf",     "30",
            "-speed",   "4",
            args.output,
        ]
    else:
        # H.264 MP4 (no alpha)
        ff_cmd = [
            args.ffmpeg, "-y",
            "-f",       "rawvideo",
            "-vcodec",  "rawvideo",
            "-s",       f"{args.width}x{args.height}",
            "-pix_fmt", "rgb24",
            "-r",       str(render_fps),
            "-i",       "pipe:0",
            "-r",       str(output_fps),
            "-c:v",     "libx264",
            "-preset",  "fast",
            "-crf",     "18",
            "-pix_fmt", "yuv420p",
            args.output,
        ]

    print(f"  Rendering {n_frames} frames → {args.output}",
          file=sys.stderr, flush=True)
    t_start = time.monotonic()

    try:
        proc = subprocess.Popen(
            ff_cmd, stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        print(f"Error: ffmpeg not found at '{args.ffmpeg}'", file=sys.stderr)
        sys.exit(1)

    prev_pct = -1
    for fi in range(n_frames):
        t_sec = fi / render_fps
        pt    = interp_point(pts, t_sec)

        frame = render_frame(
            args.width, args.height,
            pt, pt["speed"], pt["odo_km"], weather, fonts,
            units=args.units, transparent=args.transparent,
            layout=layout, t_sec=t_sec)
        try:
            proc.stdin.write(frame.tobytes())
        except BrokenPipeError:
            break

        pct = int(fi / n_frames * 100)
        if pct != prev_pct:
            elapsed  = time.monotonic() - t_start
            fps_act  = (fi + 1) / elapsed if elapsed > 0 else 0
            eta      = (n_frames - fi) / fps_act if fps_act > 0 else 0
            el_str   = f"{int(elapsed // 60)}:{int(elapsed % 60):02d}"
            eta_str  = f"{int(eta    // 60)}:{int(eta    % 60):02d}"
            mult     = fps_act / render_fps if render_fps > 0 else 0
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
# SN: 00111
