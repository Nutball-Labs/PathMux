#!/usr/bin/env python3
"""
clops_sync_analyze.py  —  Camera start-offset analyzer for a CamClops trip.

Usage: clops_sync_analyze.py MID:TID [--duration N] [--no-gps]
       clops_sync_analyze.py MID:TID --all-segments [--seg-duration N] [--write]

Two complementary methods:

  GPS stream  — exiftool extracts per-second GPS records from the first
                segment of each camera.  The GPS UTC timestamp of the first
                locked record, combined with the filename epoch, gives a
                coarse start-time per camera (±1s).  Subsequent records
                are checked to confirm the GPS/video rate is flat (no drift).

  Audio XCorr — mono 48 kHz PCM extracted from each camera's first segment
                and FFT cross-correlated against Left as the sync reference.
                Gives offset to the nearest audio sample (< 1 frame at 48 kHz).

Both methods report offsets.  Combined, they produce the leading trim in
frames to apply to each camera before a collage build.

--write (requires --all-segments):
  Writes per-segment cameraSync data into the trip's manifest JSON entry and
  generates ffmpeg concat text files + a ready-to-run test collage command.
"""

import sys
import os
import json
import platform
import subprocess
import argparse
import re
import tempfile
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

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

SAMPLE_RATE   = 48000
DEFAULT_DUR   = 60       # seconds of audio to cross-correlate
FPS           = 25.0
CAM_ORDER     = ("front", "rear", "left", "right")
REF_PRIORITY  = ("left", "front", "rear", "right")  # preference order for sync reference
GPS_FMT_FULL  = "%Y:%m:%d %H:%M:%S"  # exiftool GPSDateTime format


def pick_reference_cam(cams):
    """Return the best available camera to use as the cross-correlation reference."""
    for cam in REF_PRIORITY:
        if cam in cams and Path(cams[cam]).exists():
            return cam
    for cam in cams:
        if Path(cams[cam]).exists():
            return cam
    return None


# ── Manifest helpers ──────────────────────────────────────────────────────────

def config_dir():
    s = platform.system()
    if s == "Windows":
        appdata = os.environ.get("APPDATA")
        base = Path(appdata) if appdata else Path.home() / "AppData" / "Roaming"
        return base / "camclops"
    if s == "Darwin":
        return Path.home() / "Library" / "Application Support" / "camclops"
    return Path.home() / ".config" / "camclops"

def load_settings():
    """Return the CamClops settings dict (merged with host overlay if present).
    Mirrors ConfigManager: reads camclops.json then camclops_<hostname>.json."""
    cfg = {}
    p = config_dir() / "camclops.json"
    if p.exists():
        with open(p) as f:
            cfg = json.load(f)
    # Short hostname: strip domain suffix, matching getShortHostname() in compat.hpp
    node = platform.node()
    short_host = node.split(".")[0].lower()
    hp = config_dir() / f"camclops_{short_host}.json"
    if hp.exists():
        with open(hp) as f:
            cfg.update(json.load(f))
    return cfg

def resolve_tool(arg_val, settings_key, default):
    """Return tool path: explicit arg > settings.json > bare name."""
    if arg_val:
        return arg_val
    s = load_settings()
    v = s.get(settings_key, "")
    return v if v else default

def load_index():
    with open(config_dir() / "manifests.json") as f:
        return json.load(f)

def find_manifest_path(mid, index):
    for e in index.get("manifests", []):
        if e.get("id", "").upper() == mid.upper():
            p = e.get("manifest_file", "")
            if p and Path(p).exists():
                return p
    return None

def load_manifest(path):
    with open(path) as f:
        return json.load(f)

def find_trip(manifest, tid):
    for t in manifest.get("trips", []):
        if t.get("id", "").upper() == tid.upper():
            return t
    return None

def source_root(manifest):
    pm     = manifest.get("path_map", {})
    system = platform.system().lower()
    node   = platform.node().lower()
    key    = f"{system}_{node}"
    if key in pm:
        return pm[key]
    for k, v in pm.items():
        if system in k.lower():
            return v
    return next(iter(pm.values())) if pm else None

def segment_cameras(trip, manifest, seg_idx=0):
    segs = trip.get("segments", [])
    if not segs or seg_idx >= len(segs):
        return {}
    root = source_root(manifest)
    out  = {}
    for cam, path in segs[seg_idx].get("cameras", {}).items():
        if os.path.isabs(path):
            out[cam] = path
        elif root:
            out[cam] = str(Path(root) / path)
    return out


# ── Filename epoch ────────────────────────────────────────────────────────────

def filename_epoch(path):
    """Parse YYYYMMDD_HHMMSS from filename → UTC epoch (float), or None."""
    m = re.search(r'(\d{8})_(\d{6})', Path(path).name)
    if not m:
        return None
    dt = datetime.strptime(m.group(1) + m.group(2), "%Y%m%d%H%M%S")
    return dt.replace(tzinfo=timezone.utc).timestamp()

def epoch_to_hms(ep):
    return datetime.fromtimestamp(ep, tz=timezone.utc).strftime("%H:%M:%S")


# ── GPS stream analysis ───────────────────────────────────────────────────────

def extract_gps_stream(path, exiftool_exe="exiftool"):
    """
    Run exiftool on path and return a list of (gps_utc_epoch, lat, lon) for
    every GPS-locked record found in the embedded GPS stream.
    """
    cmd = [
        exiftool_exe, "-ee3",
        "-p", "$GPSDateTime $GPSLatitude# $GPSLongitude#",
        str(path)
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    records = []
    for line in r.stdout.splitlines():
        parts = line.strip().split()
        if len(parts) < 4:
            continue
        try:
            gps_dt = datetime.strptime(parts[0] + " " + parts[1], GPS_FMT_FULL)
            lat = float(parts[2])
            lon = float(parts[3])
        except (ValueError, IndexError):
            continue
        if lat == 0.0 and lon == 0.0:
            continue
        records.append((gps_dt.replace(tzinfo=timezone.utc).timestamp(), lat, lon))
    return records

def gps_analysis(path, file_ep, exiftool_exe="exiftool"):
    """
    Returns a dict with:
      gps_lock_s  — seconds from file start to first GPS-locked record
      start_utc   — estimated camera start UTC (= file_ep, confirmed by GPS)
      drift_s     — max deviation in GPS-record spacing from 1.000s/record
      n_records   — number of GPS records found
    Returns None if no GPS records found or exiftool not available.
    """
    try:
        records = extract_gps_stream(path, exiftool_exe)
    except FileNotFoundError:
        return None   # exiftool not installed
    if not records:
        return None

    first_utc  = records[0][0]
    gps_lock_s = first_utc - file_ep if file_ep is not None else None

    # Drift check: GPS records should advance at exactly 1.000s each.
    intervals  = [records[i+1][0] - records[i][0] for i in range(len(records)-1)]
    drift_s    = max(abs(iv - 1.0) for iv in intervals) if intervals else 0.0

    return {
        "gps_lock_s": gps_lock_s,
        "start_utc":  file_ep,
        "drift_s":    drift_s,
        "n_records":  len(records),
    }


# ── Audio cross-correlation ───────────────────────────────────────────────────

def extract_audio(path, duration, ffmpeg_exe="ffmpeg"):
    """Return float32 mono 48 kHz PCM array, normalised to [-1, 1]."""
    cmd = [
        ffmpeg_exe, "-hide_banner", "-loglevel", "error",
        "-i", str(path), "-vn",
        "-ar", str(SAMPLE_RATE), "-ac", "1",
        "-t", str(duration),
        "-f", "s16le", "pipe:1",
    ]
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode != 0 or not r.stdout:
        raise RuntimeError(r.stderr.decode().strip())
    a = np.frombuffer(r.stdout, dtype=np.int16).astype(np.float32)
    peak = np.abs(a).max()
    if peak > 0:
        a /= peak
    return a

def xcorr_lag(ref, sig):
    """
    FFT cross-correlation of sig against ref.

    Returns lag in samples where correlation peaks:
      positive → sig started earlier than ref
                 (sig has content that predates ref's position 0)
      negative → sig started later than ref
    """
    n     = len(ref) + len(sig) - 1
    n_fft = 1 << (n - 1).bit_length()
    rf    = np.fft.rfft(ref, n=n_fft)
    sf    = np.fft.rfft(sig, n=n_fft)
    corr  = np.fft.irfft(np.conj(rf) * sf, n=n_fft)
    corr  = np.concatenate([corr[n_fft // 2:], corr[:n_fft // 2]])
    return int(np.argmax(corr)) - n_fft // 2


# ── Per-segment audio helper ──────────────────────────────────────────────────

def compute_audio_lags(cams, cam_order, duration, ref_cam, verbose=True, ffmpeg_exe="ffmpeg"):
    """
    Extract audio from each camera file and cross-correlate vs ref_cam.
    Returns dict of {cam: lag_samples} with ref_cam=0, or None if ref fails.
    verbose=True prints extraction and correlation progress.
    """
    audio = {}
    for cam in cam_order:
        path = cams.get(cam, "")
        if not Path(path).exists():
            if verbose:
                print(f"  {cam:<8}  file not found")
            continue
        if verbose:
            suffix = "  (reference)" if cam == ref_cam else ""
            print(f"  Extracting {cam} audio...{suffix} ", end="", flush=True)
        try:
            audio[cam] = extract_audio(path, duration, ffmpeg_exe)
            if verbose:
                print(f"{len(audio[cam]) / SAMPLE_RATE:.1f}s")
        except RuntimeError as e:
            if verbose:
                print(f"FAILED: {e}")

    ref = audio.get(ref_cam)
    if ref is None:
        return None

    lags = {ref_cam: 0}
    for cam, sig in audio.items():
        if cam == ref_cam:
            continue
        if verbose:
            print(f"  Correlating {cam} vs {ref_cam}...", end=" ", flush=True)
        lag = xcorr_lag(ref, sig)
        lags[cam] = lag
        if verbose:
            s, f = lag / SAMPLE_RATE, lag / SAMPLE_RATE * FPS
            if lag > 0:
                note = f"{abs(s):.3f}s ({abs(f):.1f}f) earlier than {ref_cam}"
            elif lag < 0:
                note = f"{abs(s):.3f}s ({abs(f):.1f}f) later than {ref_cam}"
            else:
                note = "exact match"
            print(note)
    return lags


def print_trim_summary(audio_lags, cam_order, ref_cam):
    """Print the trim summary table and plain-text trim list."""
    min_lag  = min(audio_lags.values())
    sync_cam = min(audio_lags, key=audio_lags.get)
    trims    = {cam: lag - min_lag for cam, lag in audio_lags.items()}

    print("-" * 60)
    print("TRIM SUMMARY")
    print("-" * 60)
    ref_hdr = f"Offset vs {ref_cam}"
    print(f"  {'Camera':<8}  {ref_hdr:>16}  {'Trim from start':>16}")
    print(f"  {'':8}  {'seconds / frames':>16}  {'frames / ms':>16}")
    print("  " + "-" * 44)

    for cam in cam_order:
        if cam not in audio_lags:
            continue
        off_s   = audio_lags[cam] / SAMPLE_RATE
        off_f   = off_s * FPS
        trim_f  = trims[cam] / SAMPLE_RATE * FPS
        trim_ms = trims[cam] / SAMPLE_RATE * 1000
        trim_str = f"{trim_f:>8.1f}f  {trim_ms:>5.0f}ms"
        print(f"  {cam:<8}  {off_s:>+9.3f}s  {off_f:>+5.1f}f  {trim_str}")

    print()
    print("Leading trims to apply:")
    for cam in cam_order:
        if cam not in trims:
            continue
        trim_s  = trims[cam] / SAMPLE_RATE
        trim_f  = round(trims[cam] / SAMPLE_RATE * FPS)
        trim_ms = trims[cam] / SAMPLE_RATE * 1000
        print(f"  {cam:<8}  {trim_s:.3f}s  ({trim_f} frames,  {trim_ms:.0f}ms)")


# ── Write & test-build helpers ────────────────────────────────────────────────

def seg_trims_from_lags(lags, cam_order):
    """
    Given a lag dict {cam: samples}, return {cam: trim_seconds} relative to
    the latest-starting camera (minimum lag = started last = no trim).
    """
    min_lag = min(lags.values())
    return {cam: (lags.get(cam, 0) - min_lag) / SAMPLE_RATE for cam in cam_order
            if cam in lags}


def write_sync_to_manifest(mpath, tid, cam_order, all_lags, all_seg_cams):
    """
    Write a cameraSync block into the trip's entry in the manifest JSON.
    all_lags:     list of {cam: lag_samples} dicts, one per segment.
    all_seg_cams: list of {cam: abs_path} dicts, one per segment (parallel to all_lags).
    segments dict is keyed by front camera filename timestamp ("YYYYMMDD_HHMMSS").
    Returns True on success.
    """
    with open(mpath) as f:
        mj = json.load(f)

    seg_trim_dict = {}
    sync_cams     = []
    spans         = []

    for lags, seg_cams in zip(all_lags, all_seg_cams):
        front_path = seg_cams.get("front", "")
        key = Path(front_path).name[:15] if front_path else ""
        if not key:
            continue
        min_lag  = min(lags.values())
        sync_cams.append(min(lags, key=lags.get))
        spans.append((max(lags.values()) - min_lag) / SAMPLE_RATE * FPS)
        seg_trim_dict[key] = seg_trims_from_lags(lags, cam_order)

    primary_sync = Counter(sync_cams).most_common(1)[0][0] if sync_cams else "right"
    span_mean    = sum(spans) / len(spans) if spans else 0.0
    span_var     = max(spans) - min(spans) if len(spans) > 1 else 0.0

    camera_sync = {
        "analyzedAt":    datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "syncCam":       primary_sync,
        "spanFrames":    round(span_mean, 2),
        "spanVariation": round(span_var, 2),
        "segments":      seg_trim_dict,
    }

    found = False
    for trip_json in mj.get("trips", []):
        if trip_json.get("id", "").upper() == tid.upper():
            trip_json["cameraSync"] = camera_sync
            found = True
            break

    if not found:
        print(f"  WARNING: trip {tid} not found in manifest — not written")
        return False

    with open(mpath, "w") as f:
        json.dump(mj, f, indent=2)

    print(f"  Written to manifest: {mpath}")
    print(f"  syncCam={primary_sync}  span={span_mean:.1f}f  variation={span_var:.1f}f"
          f"  segments={len(seg_trim_dict)}")
    return True


def gen_test_build(mid, tid, cam_order, all_lags, all_seg_cams, ref_cam):
    """
    Write per-camera ffmpeg concat text files (with inpoint directives) to
    /tmp and print a ready-to-run test collage command.
    """
    out_dir   = Path(tempfile.gettempdir())
    tag       = f"{mid}_{tid}"
    concat_paths = {}

    for cam in cam_order:
        lines = ["ffconcat version 1.0"]
        for lags, seg_cams in zip(all_lags, all_seg_cams):
            path = seg_cams.get(cam, "")
            if not path or not Path(path).exists():
                continue
            trim_s = seg_trims_from_lags(lags, cam_order).get(cam, 0.0)
            lines.append(f"file '{path}'")
            if trim_s > 0.0005:          # skip negligible trims (< 0.5ms)
                lines.append(f"inpoint {trim_s:.6f}")
        txt = out_dir / f"clops_sync_{tag}_{cam}.txt"
        txt.write_text("\n".join(lines) + "\n")
        concat_paths[cam] = str(txt)
        print(f"  {cam:<8}  {txt}")

    # ffmpeg 2×2 collage command
    output    = out_dir / f"clops_sync_{tag}.mp4"
    audio_idx = cam_order.index(ref_cam) if ref_cam in cam_order else 0
    n         = len(cam_order)

    # Grid layout: [0]TL [1]TR [2]BL [3]BR
    xpos = ["0", "960",   "0",   "960"]
    ypos = ["0",   "0", "540",   "540"]
    layout = "|".join(f"{xpos[i]}_{ypos[i]}" for i in range(n))

    fc_scales = "; ".join(f"[{i}:v]scale=960:540[q{i}]" for i in range(n))
    fc_stack  = "".join(f"[q{i}]" for i in range(n)) + \
                f"xstack=inputs={n}:layout={layout}[v]"
    fc        = fc_scales + "; " + fc_stack

    print()
    print("Test collage command:")
    print()
    parts = ["ffmpeg"]
    for cam in cam_order:
        parts.append(f"  -f concat -safe 0 -i {concat_paths[cam]}")
    parts.append(f"  -filter_complex '{fc}'")
    parts.append(f"  -map '[v]' -map {audio_idx}:a")
    parts.append(f"  -c:v libx264 -crf 20 -preset fast")
    parts.append(f"  -c:a aac -b:a 192k")
    parts.append(f"  {output}")
    print(" \\\n".join(parts))
    print()
    print(f"Output: {output}")


# ── Main ──────────────────────────────────────────────────────────────────────

def _cleanup_stale_tmp():
    """Remove leftover sync concat files from killed/cancelled prior runs."""
    import glob, shutil
    tmp = tempfile.gettempdir()
    for pat in ("clops_sync_*.txt", "pm_sync_*.txt"):
        for f in glob.glob(os.path.join(tmp, pat)):
            try:
                os.unlink(f)
            except OSError:
                pass


def main():
    _setup_log()
    _cleanup_stale_tmp()
    ap = argparse.ArgumentParser(
        description="Analyze camera start offsets for a CamClops trip."
    )
    ap.add_argument("trip", metavar="MID:TID",
                    help="trip address, e.g. CQ:73")
    ap.add_argument("--duration", type=float, default=DEFAULT_DUR, metavar="N",
                    help=f"seconds of audio to cross-correlate in single-segment mode (default: {DEFAULT_DUR})")
    ap.add_argument("--no-gps", action="store_true",
                    help="skip GPS stream analysis (single-segment mode only)")
    ap.add_argument("--all-segments", action="store_true",
                    help="analyze every segment in the trip and show how offsets evolve "
                         "across segment boundaries; GPS analysis is skipped")
    ap.add_argument("--seg-duration", type=float, default=15.0, metavar="N",
                    help="seconds of audio per segment in --all-segments mode (default: 15)")
    ap.add_argument("--write", action="store_true",
                    help="write cameraSync data to manifest JSON and generate ffmpeg "
                         "concat files + test collage command (requires --all-segments)")
    ap.add_argument("--ffmpeg",    default="", metavar="PATH",
                    help="path to ffmpeg executable (default: ffmpegPath from settings, then system PATH)")
    ap.add_argument("--exiftool",  default="", metavar="PATH",
                    help="path to exiftool executable (default: exiftoolPath from settings, then system PATH)")
    args = ap.parse_args()

    if ":" not in args.trip:
        ap.error("Trip must be specified as MID:TID")

    if args.write and not args.all_segments:
        ap.error("--write requires --all-segments")

    ffmpeg_exe   = resolve_tool(args.ffmpeg,   "ffmpegPath",   "ffmpeg")
    exiftool_exe = resolve_tool(args.exiftool, "exiftoolPath", "exiftool")

    mid, tid = args.trip.upper().split(":", 1)

    try:
        index = load_index()
    except FileNotFoundError:
        sys.exit(f"Error: manifests.json not found in {config_dir()}")

    mpath = find_manifest_path(mid, index)
    if not mpath:
        sys.exit(f"Error: manifest {mid} not found")

    manifest = load_manifest(mpath)
    trip     = find_trip(manifest, tid)
    if not trip:
        sys.exit(f"Error: trip {tid} not found in manifest {mid}")

    # Build cam_order from segment 0
    cams0 = segment_cameras(trip, manifest, 0)
    if not cams0:
        sys.exit("Error: no camera files found in first segment")

    cam_order = [c for c in CAM_ORDER if c in cams0] + \
                [c for c in cams0 if c not in CAM_ORDER]
    ref_cam   = pick_reference_cam(cams0)
    if not ref_cam:
        sys.exit("Error: no readable camera files found in first segment")
    other_cams = [c for c in cam_order if c != ref_cam]

    print(f"Trip {mid}:{tid} - camera sync analysis")
    print()

    # ════════════════════════════════════════════════════════════════════════
    # --all-segments: scan every segment and show offset evolution
    # ════════════════════════════════════════════════════════════════════════
    if args.all_segments:
        segs = trip.get("segments", [])
        n    = len(segs)
        print(f"All-segments mode - {n} segments  ({args.seg_duration:.0f}s audio each)")
        print(f"Offsets shown in frames vs {ref_cam}  (positive = started earlier than {ref_cam})")
        print()

        col_w = 10
        hdr   = f"  {'Seg':>3}  {f'{ref_cam} start':>10}" + \
                "".join(f"  {c:>{col_w}}" for c in other_cams) + \
                f"  {'span':>8}"
        sep   = "  " + "-" * (len(hdr) - 2)

        all_lags     = []   # collected for --write
        all_seg_cams = []   # collected for --write
        prev_lags    = None

        for idx in range(n):
            cams = segment_cameras(trip, manifest, idx)
            seg_ref = pick_reference_cam(cams) if cams else None
            if not seg_ref:
                print(f"  {idx:>3}  (no readable camera files)")
                continue

            ref_ep  = filename_epoch(cams.get(ref_cam) or cams.get(seg_ref, ""))
            ref_ts  = epoch_to_hms(ref_ep) if ref_ep else "?"

            # Print extraction details first so they're retained in the output,
            # then print the summary row for this segment.
            print(f"\nSegment {idx}  ({ref_cam} {ref_ts})")
            lags = compute_audio_lags(cams, cam_order, args.seg_duration, ref_cam, verbose=True, ffmpeg_exe=ffmpeg_exe)
            if lags is None:
                print(f"  audio extraction failed")
                continue

            frames = {c: lags.get(c, 0) / SAMPLE_RATE * FPS for c in other_cams}
            span_f = (max(lags.values()) - min(lags.values())) / SAMPLE_RATE * FPS

            row = f"  {idx:>3}  {ref_ts:>10}"
            for c in other_cams:
                row += f"  {frames[c]:>+{col_w}.1f}f"
            row += f"  {span_f:>7.1f}f"

            if prev_lags is not None:
                deltas    = [abs(lags.get(c, 0) - prev_lags.get(c, 0)) / SAMPLE_RATE * FPS
                             for c in other_cams]
                max_delta = max(deltas)
                if max_delta > 1.0:
                    row += f"  << shift {max_delta:.1f}f"

            print(row, flush=True)
            all_lags.append(lags)
            all_seg_cams.append(cams)
            prev_lags = lags

        # Summary table — all segments in one place at the end.
        if all_lags:
            print()
            print("-" * 4 + " Summary " + "-" * (len(hdr) - 11))
            print(hdr)
            print(sep)
            for seg_idx, (lags, seg_cams) in enumerate(zip(all_lags, all_seg_cams)):
                ref_path = seg_cams.get(ref_cam, "")
                ep = filename_epoch(ref_path) if ref_path else None
                ts = epoch_to_hms(ep) if ep else "?"
                frames_s = {c: lags.get(c, 0) / SAMPLE_RATE * FPS for c in other_cams}
                span_fs  = (max(lags.values()) - min(lags.values())) / SAMPLE_RATE * FPS
                r = f"  {seg_idx:>3}  {ts:>10}"
                for c in other_cams:
                    r += f"  {frames_s[c]:>+{col_w}.1f}f"
                r += f"  {span_fs:>7.1f}f"
                print(r)

            spans     = [(max(lg.values()) - min(lg.values())) / SAMPLE_RATE * FPS
                         for lg in all_lags]
            span_min  = min(spans)
            span_max  = max(spans)
            span_mean = sum(spans) / len(spans)
            span_var  = span_max - span_min
            print()
            print(f"Span:  min {span_min:.1f}f  max {span_max:.1f}f  "
                  f"mean {span_mean:.1f}f  variation {span_var:.1f}f")
            if span_var <= 3.0:
                print("Stable — segment-0 trim is representative for the full trip.")
            elif span_var <= 6.0:
                print(f"Minor variation ({span_var:.1f}f) — segment-0 trim is a good "
                      f"approximation; per-segment data improves accuracy.")
            else:
                print(f"Significant variation ({span_var:.1f}f) — per-segment correction recommended.")

        if args.write and all_lags:
            print()
            print("Writing to manifest...")
            write_sync_to_manifest(mpath, tid, cam_order, all_lags, all_seg_cams)
            print()
            print("Generating concat files...")
            gen_test_build(mid, tid, cam_order, all_lags, all_seg_cams, ref_cam)
        return

    # ════════════════════════════════════════════════════════════════════════
    # Single-segment mode (default): full analysis of segment 0
    # ════════════════════════════════════════════════════════════════════════
    cams = cams0

    # ── Filename epochs ───────────────────────────────────────────────────────
    file_eps = {}
    for cam in cam_order:
        ep = filename_epoch(cams[cam])
        file_eps[cam] = ep

    ref_ep = file_eps.get(ref_cam)

    print("Filename timestamps (coarse ±1s)")
    print(f"  {'Camera':<8}  {'Start UTC':>12}  {f'vs {ref_cam}':>10}")
    print("  " + "─" * 34)
    for cam in cam_order:
        ep = file_eps.get(cam)
        if ep is None:
            print(f"  {cam:<8}  {'unknown':>12}  {'?':>10}")
            continue
        diff     = ep - ref_ep if ref_ep is not None else None
        diff_str = f"{diff:>+9.0f}s" if diff is not None else "?"
        print(f"  {cam:<8}  {epoch_to_hms(ep):>12}  {diff_str:>10}")
    print()

    # ── GPS stream analysis ───────────────────────────────────────────────────
    gps_results = {}
    if not args.no_gps:
        print("GPS stream analysis (exiftool)")
        for cam in cam_order:
            path = cams.get(cam, "")
            if not Path(path).exists():
                print(f"  {cam:<8}  file not found")
                continue
            print(f"  {cam:<8}  ", end="", flush=True)
            ep  = file_eps.get(cam)
            res = gps_analysis(path, ep, exiftool_exe=exiftool_exe)
            if res is None:
                print("no GPS records (or exiftool unavailable)")
            else:
                gps_results[cam] = res
                lock_str  = f"lock@{res['gps_lock_s']:.0f}s" if res["gps_lock_s"] is not None else "lock@?s"
                drift_str = f"drift span {res['drift_s']:.3f}s" if res["n_records"] > 1 else "single record"
                print(f"{res['n_records']} records  {lock_str}  {drift_str}")
        print()

    # ── Audio cross-correlation ───────────────────────────────────────────────
    print(f"Audio cross-correlation  ({args.duration:.0f}s window, 48 kHz)")
    audio_lags = compute_audio_lags(cams, cam_order, args.duration, ref_cam, verbose=True, ffmpeg_exe=ffmpeg_exe)
    if audio_lags is None:
        sys.exit(f"Error: failed to extract {ref_cam} audio")
    print()

    # Cross-check audio offsets against filename epochs (fixed sign convention)
    for cam in other_cams:
        if ref_ep and file_eps.get(cam):
            fn_diff    = file_eps[cam] - ref_ep
            audio_diff = audio_lags.get(cam, 0) / SAMPLE_RATE
            delta      = abs(fn_diff + audio_diff)
            if delta > 1.0:
                print(f"  WARNING {cam}: filename says {fn_diff:+.0f}s vs {ref_cam}, "
                      f"audio says {-audio_diff:+.3f}s vs {ref_cam}  (Δ {delta:.3f}s)")

    print_trim_summary(audio_lags, cam_order, ref_cam)


if __name__ == "__main__":
    main()

# SN: 00122
