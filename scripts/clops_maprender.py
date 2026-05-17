#!/usr/bin/env python3
"""
clops_maprender.py — CamClops moving map renderer

Reads a GPS track from a CamClops manifest JSON and renders a north-up,
speed-adaptive moving map as an MP4 video suitable for use as a collage quadrant.

The viewport follows the current position (north-up).  The displayed geographic
area widens at highway speed and narrows at city speed so there is always a
useful amount of context on screen.  The trail shows the last --trail-seconds
of travelled path and drops off behind the vehicle as the trip progresses.

Tiles are fetched from OpenStreetMap and cached in ~/.cache/camclops/tiles so
that re-renders of the same area do not re-download tiles.

OSM tile usage policy: https://operations.osmfoundation.org/policies/tiles/
Attribution required: (c) OpenStreetMap contributors

Usage:
    clops_maprender.py --manifest <path> --trip <id> --output <path.mp4>
                    [--width 960] [--height 540] [--fps 30]
                    [--render-fps 10] [--trail-seconds 60]
                    [--ffmpeg ffmpeg] [--cache-dir ~/.cache/camclops/tiles]

--render-fps controls how many unique frames are rendered per second; ffmpeg
duplicates them to reach --fps.  --render-fps 10 gives a 3x speedup with
barely noticeable quality loss on a map display.

Requirements:
    pip install requests Pillow
"""

import argparse
import bisect
import datetime
import json
import math
import os
import socket
import subprocess
import sys
import time
from io import BytesIO

_CAMCLOPS_VERSION = "1.101.2"
_CAMCLOPS_HWM     = "00117"

def _setup_log():
    import datetime
    log_dir = os.path.join(os.path.expanduser("~"), ".config", "camclops", "logs")
    os.makedirs(log_dir, exist_ok=True)
    script   = os.path.splitext(os.path.basename(__file__))[0]
    log_path = os.path.join(log_dir, script + ".log")
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
        def fileno(self):   return self._s.fileno()
        def isatty(self):   return False
        @property
        def encoding(self): return self._s.encoding
    sys.stderr = _Tee(sys.__stderr__, log_fh)
    print(f"  Log: {log_path}", file=sys.stderr)
    return log_fh

def _brand_output(path: str, content_type: str, ffmpeg: str) -> None:
    base, ext = os.path.splitext(path)
    tmp = base + ".clops_brand" + ext
    host = socket.gethostname().split('.')[0]
    ret = subprocess.run(
        [ffmpeg, "-v", "quiet", "-y", "-i", path,
         "-map", "0", "-c", "copy",
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
        try: os.remove(tmp)
        except OSError: pass

try:
    import requests
except ImportError:
    print("Error: 'requests' module not found. Install with: pip install requests", file=sys.stderr)
    sys.exit(1)

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: 'Pillow' module not found. Install with: pip install Pillow", file=sys.stderr)
    sys.exit(1)

_font_warned = False


def find_font(size):
    global _font_warned
    candidates = [
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
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
        print("  Warning: no TrueType font found — text may render small."
              "  Install dejavu-fonts-ttf or liberation-fonts.",
              file=sys.stderr)
        _font_warned = True
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


# ---------------------------------------------------------------------------
# OSM tile math
# ---------------------------------------------------------------------------

TILE_SIZE = 256

def lat_lon_to_tile_float(lat, lon, zoom):
    """Return fractional tile coordinates for a lat/lon."""
    n = 2 ** zoom
    x = (lon + 180) / 360 * n
    y = (1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n
    return x, y

def lat_lon_to_tile(lat, lon, zoom):
    x, y = lat_lon_to_tile_float(lat, lon, zoom)
    return int(x), int(y)

def fetch_tile(tx, ty, zoom, cache_dir, session):
    """Fetch one OSM tile, returning a PIL Image (RGB).  Cached to disk."""
    cache_path = os.path.join(cache_dir, f"{zoom}_{tx}_{ty}.png")
    if os.path.exists(cache_path):
        try:
            return Image.open(cache_path).convert("RGB")
        except Exception:
            pass  # corrupted cache — re-fetch

    url = f"https://tile.openstreetmap.org/{zoom}/{tx}/{ty}.png"
    headers = {
        "User-Agent": "CamClops/1.2 dashcam-gps-renderer (https://github.com/Nutball-Labs/CamClops)"
    }
    try:
        resp = session.get(url, headers=headers, timeout=15)
        resp.raise_for_status()
        img = Image.open(BytesIO(resp.content)).convert("RGB")
        img.save(cache_path)
        return img
    except Exception as e:
        print(f"\n  Warning: tile {zoom}/{tx}/{ty} failed: {e}", file=sys.stderr)
        return Image.new("RGB", (TILE_SIZE, TILE_SIZE), (200, 200, 200))


def stitch_tiles(x_min, x_max, y_min, y_max, zoom, cache_dir, session):
    """Fetch and stitch a rectangular tile grid into one large PIL Image."""
    cols = x_max - x_min + 1
    rows = y_max - y_min + 1
    total = cols * rows
    print(f"  Fetching {total} map tile(s) ({cols}x{rows}) at zoom {zoom}...",
          file=sys.stderr, flush=True)

    canvas = Image.new("RGB", (cols * TILE_SIZE, rows * TILE_SIZE))
    fetched = 0
    for yi, ty in enumerate(range(y_min, y_max + 1)):
        for xi, tx in enumerate(range(x_min, x_max + 1)):
            tile = fetch_tile(tx, ty, zoom, cache_dir, session)
            canvas.paste(tile, (xi * TILE_SIZE, yi * TILE_SIZE))
            fetched += 1
            if fetched % 10 == 0 or fetched == total:
                print(f"\r    {fetched}/{total} tiles", end="", file=sys.stderr, flush=True)
    print(file=sys.stderr)
    return canvas


# ---------------------------------------------------------------------------
# Coordinate projection (Mercator) onto the stitched canvas
# ---------------------------------------------------------------------------

def to_canvas_px(lat, lon, x_min_tile, y_min_tile, zoom):
    """Return pixel coordinates on the stitched canvas for a lat/lon."""
    fx, fy = lat_lon_to_tile_float(lat, lon, zoom)
    px = (fx - x_min_tile) * TILE_SIZE
    py = (fy - y_min_tile) * TILE_SIZE
    return px, py


# ---------------------------------------------------------------------------
# Speed → viewport radius
# ---------------------------------------------------------------------------

def speed_to_viewport_km(speed_kmh):
    """
    Return the geographic half-height of the viewport in km based on speed.
    Slow / stopped: ~0.25 km  (tight city view)
    Highway:        ~1.8 km   (wide context)
    """
    MIN_SPEED,  MAX_SPEED  = 0.0, 120.0
    MIN_RADIUS, MAX_RADIUS = 0.25, 1.8
    t = max(0.0, min(1.0, speed_kmh / MAX_SPEED))
    # Ease-in curve so the zoom feels less mechanical
    t = t * t * (3 - 2 * t)
    return MIN_RADIUS + (MAX_RADIUS - MIN_RADIUS) * t


def km_to_canvas_px(km, zoom, lat):
    """Convert km to canvas pixels at this zoom level and latitude."""
    # Pixel size at equator for this zoom; shrinks with cos(lat) as tiles stretch
    meters_per_px = 40075_000 * math.cos(math.radians(lat)) / (2 ** zoom * TILE_SIZE)
    return (km * 1000) / meters_per_px


# ---------------------------------------------------------------------------
# GPS interpolation
# ---------------------------------------------------------------------------

def parse_gps_timestamp(ts):
    """Parse 'YYYY:MM:DD HH:MM:SS[Z]' → datetime (UTC, tz-naive)."""
    return datetime.datetime.strptime(ts.rstrip("Z").strip(), "%Y:%m:%d %H:%M:%S")


def build_points(gps_track, gps_lock_offset=0):
    """
    Convert raw gpsTrack list to list of dicts with elapsed seconds.

    gps_lock_offset — seconds between trip video start and the first GPS
    record (i.e. gpsLockSeconds from the manifest).  Shifts all elapsed
    times forward so t=0 is trip-video-start, not GPS-lock-time.  Without
    this the map runs ahead of the video by exactly the lock delay.
    """
    if not gps_track:
        return []
    t0 = parse_gps_timestamp(gps_track[0]["timestamp"])
    pts = []
    for raw in gps_track:
        t = parse_gps_timestamp(raw["timestamp"])
        pts.append({
            "t":     (t - t0).total_seconds() + gps_lock_offset,
            "lat":   raw["lat"],
            "lon":   raw["lon"],
            "speed": raw.get("speed", 0.0),
        })
    return pts


def compress_gaps(points, gap_threshold=10):
    """
    Compress large time gaps out of a GPS point list.
    Gaps arise when trip segments have been archived — the remaining
    segments span the original trip timeline but only cover a fraction
    of it.  Without compression the render duration equals the full
    original span rather than the actual covered footage duration.
    Each contiguous run of points is remapped so runs follow each other
    with a 1-second join; total_duration becomes the sum of run lengths.
    """
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


def interp(points, t_sec):
    """
    Linear interpolation of lat, lon, speed at elapsed time t_sec.
    Returns (lat, lon, speed_kmh).
    """
    if t_sec <= points[0]["t"]:
        p = points[0]
        return p["lat"], p["lon"], p["speed"]
    if t_sec >= points[-1]["t"]:
        p = points[-1]
        return p["lat"], p["lon"], p["speed"]

    # Binary search
    lo, hi = 0, len(points) - 1
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if points[mid]["t"] <= t_sec:
            lo = mid
        else:
            hi = mid

    alpha = (t_sec - points[lo]["t"]) / (points[hi]["t"] - points[lo]["t"])
    def lerp(a, b): return a + (b - a) * alpha
    return (
        lerp(points[lo]["lat"],   points[hi]["lat"]),
        lerp(points[lo]["lon"],   points[hi]["lon"]),
        lerp(points[lo]["speed"], points[hi]["speed"]),
    )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    _setup_log()
    parser = argparse.ArgumentParser(
        description="CamClops moving map renderer — GPS track → MP4"
    )
    parser.add_argument("--manifest",      required=True,  help="Path to CamClops manifest JSON")
    parser.add_argument("--trip",          required=True,  help="Trip ID (e.g. G1)")
    parser.add_argument("--output",        required=True,  help="Output MP4 path")
    parser.add_argument("--width",         type=int, default=960,  help="Frame width (default 960)")
    parser.add_argument("--height",        type=int, default=540,  help="Frame height (default 540)")
    parser.add_argument("--fps",           type=int, default=30,   help="Output FPS (default 30)")
    parser.add_argument("--render-fps",    type=int, default=None, help="Render FPS (default: same as --fps). Lower = faster encode, ffmpeg duplicates frames to reach --fps.")
    parser.add_argument("--trail-seconds",   type=int,   default=60,   help="(legacy, unused)")
    parser.add_argument("--start-zoom-secs",type=float, default=8.0,  help="Seconds to hold tight zoom after movement starts (default 8)")
    parser.add_argument("--start-zoom-km",  type=float, default=0.3,  help="Viewport radius in km during start zoom (default 0.3)")
    parser.add_argument("--end-zoom-secs",  type=float, default=8.0,  help="Seconds to hold tight zoom after movement ends (default 8)")
    parser.add_argument("--end-zoom-km",    type=float, default=0.3,  help="Viewport radius in km during end zoom (default 0.3)")
    parser.add_argument("--ffmpeg",         default="ffmpeg",         help="Path to ffmpeg binary")
    parser.add_argument("--cache-dir",      default=None,             help="Tile cache directory")
    args = parser.parse_args()

    # --- Load manifest ---
    try:
        with open(args.manifest) as f:
            manifest = json.load(f)
    except Exception as e:
        print(f"Error: Cannot read manifest: {e}", file=sys.stderr)
        sys.exit(1)

    trip = None
    for t in manifest.get("trips", []):
        if str(t.get("id", "")).upper() == args.trip.upper():
            trip = t
            break
    if trip is None:
        print(f"Error: Trip '{args.trip}' not found in manifest.", file=sys.stderr)
        sys.exit(1)

    gps_track = trip.get("gpsTrack", [])
    if len(gps_track) < 2:
        print("Error: Trip has no GPS track data (run GPS extraction first).", file=sys.stderr)
        sys.exit(1)

    # gpsLockSeconds: how many seconds after video start before first GPS record.
    # -1 means not scanned; treat as 0 (no offset applied).
    gps_lock_secs = trip.get("gpsLockSeconds")
    if not isinstance(gps_lock_secs, (int, float)) or gps_lock_secs < 0:
        start_epoch = trip.get("start_epoch", 0)
        if start_epoch and gps_track:
            first_ts = gps_track[0].get("timestamp", "")
            try:
                import calendar as _cal
                gt = datetime.datetime.strptime(first_ts[:19], "%Y:%m:%d %H:%M:%S")
                derived = _cal.timegm(gt.timetuple()) - int(start_epoch)
                gps_lock_secs = derived if 0 <= derived <= 300 else 0
            except Exception:
                gps_lock_secs = 0
        else:
            gps_lock_secs = 0

    print(f"  Trip {args.trip}: {len(gps_track)} GPS samples  "
          f"(GPS lock offset: {gps_lock_secs}s)", file=sys.stderr)

    # --- Build interpolation table ---
    points = build_points(gps_track, gps_lock_offset=gps_lock_secs)
    points = compress_gaps(points)
    total_duration = points[-1]["t"]

    render_fps   = args.render_fps if args.render_fps else args.fps
    output_fps   = args.fps
    total_frames = int(total_duration * render_fps) + 1

    print(f"  Duration: {total_duration:.0f}s  →  {total_frames} frames "
          f"at {render_fps}fps render / {output_fps}fps output",
          file=sys.stderr)

    # --- Detect movement start / end for start+end zoom windows ---
    MOVE_THRESHOLD_KMH = 8.0
    t_move_start = next(
        (p["t"] for p in points if p["speed"] >= MOVE_THRESHOLD_KMH),
        0.0
    )
    t_move_end = next(
        (p["t"] for p in reversed(points) if p["speed"] >= MOVE_THRESHOLD_KMH),
        total_duration
    )
    print(f"  Movement: {t_move_start:.0f}s → {t_move_end:.0f}s  "
          f"(start zoom {args.start_zoom_secs:.0f}s/{args.start_zoom_km}km  "
          f"end zoom {args.end_zoom_secs:.0f}s/{args.end_zoom_km}km)",
          file=sys.stderr)

    # --- Choose zoom level from 85th-percentile speed ---
    # --- Bounding box + 40% margin ---
    lats = [p["lat"] for p in points]
    lons = [p["lon"] for p in points]
    lat_span = max(lats) - min(lats)
    lon_span = max(lons) - min(lons)
    margin_factor = 0.4
    lat_margin = max(lat_span * margin_factor, 0.005)  # at least ~500m
    lon_margin = max(lon_span * margin_factor, 0.008)

    # Choose the highest zoom level whose tile count fits within budget.
    # Always try zoom 16 first — finer tiles mean less pixelation when the
    # viewport is tight at slow speeds.  Drop one level at a time until the
    # tile count is manageable.
    TILE_BUDGET = 800
    for zoom in [16, 15, 14, 13]:
        x_min_t, y_min_t = lat_lon_to_tile(max(lats) + lat_margin, min(lons) - lon_margin, zoom)
        x_max_t, y_max_t = lat_lon_to_tile(min(lats) - lat_margin, max(lons) + lon_margin, zoom)
        tile_count = (x_max_t - x_min_t + 1) * (y_max_t - y_min_t + 1)
        if tile_count <= TILE_BUDGET:
            break
        print(f"  {tile_count} tiles at zoom {zoom} exceeds budget — trying zoom {zoom-1}",
              file=sys.stderr)

    print(f"  Zoom {zoom}: {tile_count} tiles", file=sys.stderr)

    # --- Fetch and stitch tiles ---
    cache_dir = args.cache_dir or os.path.join(
        os.path.expanduser("~"), ".cache", "camclops", "tiles"
    )
    os.makedirs(cache_dir, exist_ok=True)

    session = requests.Session()
    base_map = stitch_tiles(x_min_t, x_max_t, y_min_t, y_max_t, zoom, cache_dir, session)
    bw, bh = base_map.size

    # --- Precompute track pixel positions on canvas ---
    track_px = [
        to_canvas_px(p["lat"], p["lon"], x_min_t, y_min_t, zoom)
        for p in points
    ]

    # --- Start ffmpeg subprocess ---
    ffmpeg_cmd = [
        args.ffmpeg, "-y",
        "-f", "rawvideo",
        "-vcodec", "rawvideo",
        "-s", f"{args.width}x{args.height}",
        "-pix_fmt", "rgb24",
        "-r", str(render_fps),      # input rate = render rate
        "-i", "pipe:0",
        "-r", str(output_fps),      # output rate: ffmpeg duplicates frames if render_fps < output_fps
        "-c:v", "libx264",
        "-preset", "fast",
        "-crf", "18",
        "-pix_fmt", "yuv420p",
        args.output,
    ]

    print(f"  Rendering {total_frames} frames → {args.output}", file=sys.stderr, flush=True)
    render_start = time.monotonic()

    try:
        proc = subprocess.Popen(
            ffmpeg_cmd, stdin=subprocess.PIPE, stderr=subprocess.DEVNULL
        )
    except FileNotFoundError:
        print(f"Error: ffmpeg not found at '{args.ffmpeg}'", file=sys.stderr)
        sys.exit(1)

    aspect = args.width / args.height

    # Precompute time index for binary search — avoids O(n) track scan per frame
    point_times = [p["t"] for p in points]

    for frame_num in range(total_frames):
        t_sec = frame_num / render_fps
        lat, lon, speed = interp(points, t_sec)

        # Viewport radius: speed-adaptive baseline, blended toward tight zoom
        # at trip start (after first movement) and trip end (last N seconds of trip).
        # ease(t) smooths the transition with a cosine curve.
        def ease(t): return 0.5 - 0.5 * math.cos(math.pi * max(0.0, min(1.0, t)))

        speed_km = speed_to_viewport_km(speed)

        # Start zoom: hold tight from t=0 until vehicle first moves, then ease
        # out over start_zoom_secs.  This avoids a jarring jump at t_move_start.
        start_blend = 0.0
        if args.start_zoom_secs > 0:
            if t_sec < t_move_start:
                start_blend = 1.0  # hold tight until movement begins
            else:
                dt = t_sec - t_move_start
                start_blend = ease(1.0 - dt / args.start_zoom_secs)  # 1→0 over window

        # End zoom: tight for last end_zoom_secs of the recording.
        # Measured from total_duration backward — fires at the actual end of
        # the trip regardless of when the vehicle last moved.
        end_blend = 0.0
        t_end_window = total_duration - args.end_zoom_secs
        if args.end_zoom_secs > 0 and t_sec >= t_end_window:
            dt = t_sec - t_end_window
            end_blend = ease(dt / args.end_zoom_secs)  # 0→1 over window

        if start_blend >= end_blend:
            blend    = start_blend
            tight_km = args.start_zoom_km
        else:
            blend    = end_blend
            tight_km = args.end_zoom_km

        # Speed-adaptive path keeps the upscaling floor (no pixelation).
        # Tight zoom bypasses the floor — user explicitly requested this and
        # brief pixelation during the start/end window is acceptable.
        speed_h_px = max(km_to_canvas_px(speed_km, zoom, lat), args.height / 2)
        tight_h_px = km_to_canvas_px(tight_km, zoom, lat)
        half_h_px  = speed_h_px * (1.0 - blend) + tight_h_px * blend
        half_w_px  = half_h_px * aspect

        # Center of current position on canvas
        cx, cy = to_canvas_px(lat, lon, x_min_t, y_min_t, zoom)

        # Crop box (may extend outside canvas — PIL fills black)
        left   = cx - half_w_px
        top    = cy - half_h_px
        right  = cx + half_w_px
        bottom = cy + half_h_px

        # Scale factor from canvas pixels to frame pixels (for trail coords)
        crop_w = right - left
        crop_h = bottom - top
        sx = args.width  / crop_w if crop_w > 0 else 1
        sy = args.height / crop_h if crop_h > 0 else 1

        # Crop directly from base map — PIL fills black for out-of-bounds coords
        frame_img = base_map.crop((int(left), int(top), int(right), int(bottom))) \
                             .resize((args.width, args.height), Image.BILINEAR)

        draw = ImageDraw.Draw(frame_img)

        # Full trip track: green ahead of current position, red behind.
        # idx_cur splits past (red) from future (green).
        idx_cur = bisect.bisect_right(point_times, t_sec)

        def to_screen(i):
            px, py = track_px[i]
            return ((px - left) * sx, (py - top) * sy)

        past   = [to_screen(i) for i in range(min(idx_cur + 1, len(points)))]
        future = [to_screen(i) for i in range(max(idx_cur - 1, 0), len(points))]

        if len(past)   >= 2:
            draw.line(past,   fill=(220, 50,  50),  width=6)  # red — where we've been
        if len(future) >= 2:
            draw.line(future, fill=(50,  200, 50),  width=6)  # green — where we're going

        # Position dot (current location)
        dot_x = args.width  // 2
        dot_y = args.height // 2
        r = 7
        draw.ellipse(
            [dot_x - r, dot_y - r, dot_x + r, dot_y + r],
            fill=(220, 50, 50), outline=(255, 255, 255), width=2
        )

        # OSM attribution (bottom-right corner, small)
        draw.text((args.width - 5, args.height - 5),
                  "(c) OpenStreetMap contributors",
                  fill=(80, 80, 80), anchor="rb", font=find_font(12))

        try:
            proc.stdin.write(frame_img.tobytes())
        except BrokenPipeError:
            print("\nError: ffmpeg pipe broken — ffmpeg may have exited early.", file=sys.stderr)
            sys.exit(1)

        if frame_num % render_fps == 0 or frame_num == total_frames - 1:
            pct = int(frame_num / max(total_frames - 1, 1) * 100)
            elapsed_s = int(t_sec)
            wall = time.monotonic() - render_start
            speed_x = f"{t_sec / wall:.1f}x" if wall > 0.5 else "..."
            print(f"\r  {pct:3d}%  [{elapsed_s//60}:{elapsed_s%60:02d} / "
                  f"{int(total_duration)//60}:{int(total_duration)%60:02d}]"
                  f"  {speed_x}",
                  end="", file=sys.stderr, flush=True)

    proc.stdin.close()
    ret = proc.wait()
    print(file=sys.stderr)

    if ret != 0:
        print(f"Error: ffmpeg exited with code {ret}", file=sys.stderr)
        sys.exit(1)

    print(f"  Done: {args.output}", file=sys.stderr)
    _brand_output(args.output, "map", args.ffmpeg)


if __name__ == "__main__":
    main()

# SN: 00117
