#!/usr/bin/env python3
"""
pm_sync_analyze.py  —  Camera start-offset analyzer for a PathMux trip.

Usage: pm_sync_analyze.py MID:TID [--duration N] [--no-gps]
       pm_sync_analyze.py MID:TID --all-segments [--seg-duration N] [--write]

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
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

SAMPLE_RATE   = 48000
DEFAULT_DUR   = 60       # seconds of audio to cross-correlate
FPS           = 25.0
CAM_ORDER     = ("front", "rear", "left", "right")
GPS_FMT_FULL  = "%Y:%m:%d %H:%M:%S"  # exiftool GPSDateTime format


# ── Manifest helpers ──────────────────────────────────────────────────────────

def config_dir():
    return Path.home() / ".config" / "pathmux"

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

def extract_gps_stream(path):
    """
    Run exiftool on path and return a list of (gps_utc_epoch, lat, lon) for
    every GPS-locked record found in the embedded GPS stream.
    """
    cmd = [
        "exiftool", "-ee3",
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

def gps_analysis(path, file_ep):
    """
    Returns a dict with:
      gps_lock_s  — seconds from file start to first GPS-locked record
      start_utc   — estimated camera start UTC (= file_ep, confirmed by GPS)
      drift_s     — max deviation in GPS-record spacing from 1.000s/record
      n_records   — number of GPS records found
    Returns None if no GPS records found or exiftool not available.
    """
    try:
        records = extract_gps_stream(path)
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

def extract_audio(path, duration):
    """Return float32 mono 48 kHz PCM array, normalised to [-1, 1]."""
    cmd = [
        "ffmpeg", "-hide_banner", "-loglevel", "error",
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

def compute_audio_lags(cams, cam_order, duration, verbose=True):
    """
    Extract audio from each camera file and cross-correlate vs Left.
    Returns dict of {cam: lag_samples} with left=0, or None if Left fails.
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
            suffix = "  (reference)" if cam == "left" else ""
            print(f"  Extracting {cam} audio...{suffix} ", end="", flush=True)
        try:
            audio[cam] = extract_audio(path, duration)
            if verbose:
                print(f"{len(audio[cam]) / SAMPLE_RATE:.1f}s")
        except RuntimeError as e:
            if verbose:
                print(f"FAILED: {e}")

    ref = audio.get("left")
    if ref is None:
        return None

    lags = {"left": 0}
    for cam, sig in audio.items():
        if cam == "left":
            continue
        if verbose:
            print(f"  Correlating {cam} vs left...", end=" ", flush=True)
        lag = xcorr_lag(ref, sig)
        lags[cam] = lag
        if verbose:
            s, f = lag / SAMPLE_RATE, lag / SAMPLE_RATE * FPS
            if lag > 0:
                note = f"{abs(s):.3f}s ({abs(f):.1f}f) earlier than left"
            elif lag < 0:
                note = f"{abs(s):.3f}s ({abs(f):.1f}f) later than left"
            else:
                note = "exact match"
            print(note)
    return lags


def print_trim_summary(audio_lags, cam_order):
    """Print the trim summary table and plain-text trim list."""
    min_lag  = min(audio_lags.values())
    sync_cam = min(audio_lags, key=audio_lags.get)
    trims    = {cam: lag - min_lag for cam, lag in audio_lags.items()}

    print("─" * 60)
    print("TRIM SUMMARY")
    print("─" * 60)
    print(f"  {'Camera':<8}  {'Offset vs Left':>16}  {'Trim from start':>16}")
    print(f"  {'':8}  {'seconds / frames':>16}  {'frames / ms':>16}")
    print("  " + "─" * 44)

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


def gen_test_build(mid, tid, cam_order, all_lags, all_seg_cams):
    """
    Write per-camera ffmpeg concat text files (with inpoint directives) to
    /tmp and print a ready-to-run test collage command.
    """
    out_dir   = Path("/tmp")
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
        txt = out_dir / f"pm_sync_{tag}_{cam}.txt"
        txt.write_text("\n".join(lines) + "\n")
        concat_paths[cam] = str(txt)
        print(f"  {cam:<8}  {txt}")

    # ffmpeg 2×2 collage command
    output    = out_dir / f"pm_sync_{tag}.mp4"
    audio_idx = cam_order.index("left") if "left" in cam_order else 0
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

def main():
    ap = argparse.ArgumentParser(
        description="Analyze camera start offsets for a PathMux trip."
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
    args = ap.parse_args()

    if ":" not in args.trip:
        ap.error("Trip must be specified as MID:TID")

    if args.write and not args.all_segments:
        ap.error("--write requires --all-segments")

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
    if "left" not in cams0:
        sys.exit("Error: Left camera not present — required as audio sync reference")

    cam_order  = [c for c in CAM_ORDER if c in cams0] + \
                 [c for c in cams0 if c not in CAM_ORDER]
    other_cams = [c for c in cam_order if c != "left"]

    print(f"Trip {mid}:{tid}  —  camera sync analysis")
    print()

    # ════════════════════════════════════════════════════════════════════════
    # --all-segments: scan every segment and show offset evolution
    # ════════════════════════════════════════════════════════════════════════
    if args.all_segments:
        segs = trip.get("segments", [])
        n    = len(segs)
        print(f"All-segments mode  —  {n} segments  ({args.seg_duration:.0f}s audio each)")
        print(f"Offsets shown in frames vs Left  (positive = started earlier than Left)")
        print()

        col_w = 10
        hdr   = f"  {'Seg':>3}  {'Left start':>10}" + \
                "".join(f"  {c:>{col_w}}" for c in other_cams) + \
                f"  {'span':>8}"
        print(hdr)
        print("  " + "─" * (len(hdr) - 2))

        all_lags     = []   # collected for --write
        all_seg_cams = []   # collected for --write
        prev_lags    = None

        for idx in range(n):
            cams = segment_cameras(trip, manifest, idx)
            if not cams or "left" not in cams:
                print(f"  {idx:>3}  (no Left camera)")
                continue

            left_ep  = filename_epoch(cams.get("left", ""))
            left_ts  = epoch_to_hms(left_ep) if left_ep else "        ?"

            lags = compute_audio_lags(cams, cam_order, args.seg_duration, verbose=False)
            if lags is None:
                print(f"  {idx:>3}  {left_ts}  (audio extraction failed)")
                continue

            frames = {c: lags.get(c, 0) / SAMPLE_RATE * FPS for c in other_cams}
            span_f = (max(lags.values()) - min(lags.values())) / SAMPLE_RATE * FPS

            row = f"  {idx:>3}  {left_ts:>10}"
            for c in other_cams:
                row += f"  {frames[c]:>+{col_w}.1f}f"
            row += f"  {span_f:>7.1f}f"

            if prev_lags is not None:
                deltas    = [abs(lags.get(c, 0) - prev_lags.get(c, 0)) / SAMPLE_RATE * FPS
                             for c in other_cams]
                max_delta = max(deltas)
                if max_delta > 1.0:
                    row += f"  ← shift {max_delta:.1f}f"

            print(row, flush=True)
            all_lags.append(lags)
            all_seg_cams.append(cams)
            prev_lags = lags

        if all_lags:
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
            gen_test_build(mid, tid, cam_order, all_lags, all_seg_cams)
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

    left_ep = file_eps.get("left")

    print("Filename timestamps (coarse ±1s)")
    print(f"  {'Camera':<8}  {'Start UTC':>12}  {'vs Left':>10}")
    print("  " + "─" * 34)
    for cam in cam_order:
        ep = file_eps.get(cam)
        if ep is None:
            print(f"  {cam:<8}  {'unknown':>12}  {'?':>10}")
            continue
        diff     = ep - left_ep if left_ep is not None else None
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
            res = gps_analysis(path, ep)
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
    audio_lags = compute_audio_lags(cams, cam_order, args.duration, verbose=True)
    if audio_lags is None:
        sys.exit("Error: failed to extract Left audio")
    print()

    # Cross-check audio offsets against filename epochs (fixed sign convention)
    for cam in other_cams:
        if left_ep and file_eps.get(cam):
            fn_diff    = file_eps[cam] - left_ep
            audio_diff = audio_lags.get(cam, 0) / SAMPLE_RATE
            delta      = abs(fn_diff + audio_diff)
            if delta > 1.0:
                print(f"  WARNING {cam}: filename says {fn_diff:+.0f}s vs left, "
                      f"audio says {-audio_diff:+.3f}s vs left  (Δ {delta:.3f}s)")

    print_trim_summary(audio_lags, cam_order)


if __name__ == "__main__":
    main()

# SN: 00111
