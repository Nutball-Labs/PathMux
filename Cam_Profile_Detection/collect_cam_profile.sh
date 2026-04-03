#!/usr/bin/env bash
# =============================================================================
# PathMux Camera Profile Collection Script v1.0
#
# Walks you through gathering the diagnostic information needed to add
# detection support for a new dashcam model in PathMux.
#
# Saves everything to a single text file — attach it to a GitHub issue at:
# https://github.com/Nutball-Labs/PathMux/issues/new/choose
#
# Requires:  ffprobe (part of ffmpeg)  +  exiftool 13.51+
#   Linux:   sudo dnf install ffmpeg perl-Image-ExifTool
#   macOS:   brew install ffmpeg exiftool
# =============================================================================

SCRIPT_VERSION="1.1"

# ── Color helpers (degrade if not a tty) ─────────────────────────────────────
if [ -t 1 ] && command -v tput &>/dev/null && tput colors &>/dev/null 2>&1; then
    BOLD=$(tput bold);  GREEN=$(tput setaf 2); YELLOW=$(tput setaf 3)
    CYAN=$(tput setaf 6); RED=$(tput setaf 1); RESET=$(tput sgr0)
else
    BOLD=""; GREEN=""; YELLOW=""; CYAN=""; RED=""; RESET=""
fi

# ── Helpers ───────────────────────────────────────────────────────────────────
banner() {
    clear 2>/dev/null || true
    echo
    echo "${BOLD}${CYAN}============================================================${RESET}"
    echo "${BOLD}${CYAN}   PathMux Camera Profile Collection  v${SCRIPT_VERSION}${RESET}"
    echo "${BOLD}${CYAN}============================================================${RESET}"
    echo
    echo "  This script collects diagnostic info about your dashcam"
    echo "  and saves it to a single text file you can attach to a"
    echo "  GitHub issue so we can add support for your camera."
    echo
    echo "  ${CYAN}https://github.com/Nutball-Labs/PathMux/issues/new/choose${RESET}"
    echo
    echo "  Press Enter to continue, Ctrl-C to quit."
    read -r _
}

step()   { echo; echo "${BOLD}${GREEN}[ $1 ]${RESET}"; }
info()   { echo "  ${CYAN}$1${RESET}"; }
warn()   { echo "  ${YELLOW}WARNING: $1${RESET}"; }
ok()     { echo "  ${GREEN}OK  $1${RESET}"; }
prompt() { printf "  ${BOLD}%s: ${RESET}" "$1"; }

ask() {
    # ask <varname> <prompt> [default]
    local __var=$1 __prompt=$2 __default=${3:-}
    if [ -n "$__default" ]; then
        prompt "$__prompt [$__default]"
    else
        prompt "$__prompt"
    fi
    read -r __input
    if [ -z "$__input" ] && [ -n "$__default" ]; then
        __input="$__default"
    fi
    printf -v "$__var" '%s' "$__input"
}

divider() {
    echo "------------------------------------------------------------"
}

# ── Tool detection ────────────────────────────────────────────────────────────
check_tools() {
    step "Checking required tools"
    HAS_FFPROBE=0; HAS_EXIFTOOL=0

    if command -v ffprobe &>/dev/null; then
        FFPROBE_VER=$(ffprobe -version 2>&1 | head -1)
        ok "ffprobe   — $FFPROBE_VER"
        HAS_FFPROBE=1
    else
        warn "ffprobe not found — install ffmpeg to get it."
        warn "  Linux:  sudo dnf install ffmpeg   (RPM Fusion)"
        warn "  macOS:  brew install ffmpeg"
    fi

    if command -v exiftool &>/dev/null; then
        ET_VER=$(exiftool -ver 2>/dev/null)
        ok "exiftool  — v${ET_VER}"
        HAS_EXIFTOOL=1
        # Warn if older than 13.51 (LIGO GPS support threshold)
        ET_MAJOR=$(echo "$ET_VER" | cut -d. -f1)
        ET_MINOR=$(echo "$ET_VER" | cut -d. -f2)
        if [ "$ET_MAJOR" -lt 13 ] || { [ "$ET_MAJOR" -eq 13 ] && [ "$ET_MINOR" -lt 51 ]; }; then
            warn "exiftool $ET_VER is older than 13.51 — LIGO GPS extraction may not work."
            warn "  Consider upgrading: https://exiftool.org"
        fi
    else
        warn "exiftool not found."
        warn "  Linux:  sudo dnf install perl-Image-ExifTool"
        warn "  macOS:  brew install exiftool"
    fi

    if [ $HAS_FFPROBE -eq 0 ] || [ $HAS_EXIFTOOL -eq 0 ]; then
        echo
        warn "One or more tools are missing. The report will be incomplete."
        ask CONTINUE "Continue anyway? [y/N]" "N"
        case "$CONTINUE" in
            [yY]*) ;;
            *) echo "Exiting."; exit 0 ;;
        esac
    fi
}

# ── Camera basics ─────────────────────────────────────────────────────────────
collect_camera_info() {
    step "Camera information"
    info "Enter the details printed on the box or shown in the camera menu."
    echo

    ask CAM_MODEL    "Camera make and model (e.g. Vantrue N4 Pro)"
    ask CAM_FIRMWARE "Firmware version (check camera Settings menu)" "unknown"
    ask CAM_COUNT    "Number of camera channels / lenses" "1"

    step "GPS"
    info "  1) Yes — GPS module built in"
    info "  2) Yes — external GPS receiver plugged in"
    info "  3) No GPS"
    info "  4) Unknown"
    ask GPS_CHOICE "Enter 1-4" "4"
    case "$GPS_CHOICE" in
        1) GPS_PRESENT="Yes — built-in GPS" ;;
        2) GPS_PRESENT="Yes — external GPS receiver" ;;
        3) GPS_PRESENT="No GPS" ;;
        *) GPS_PRESENT="Unknown" ;;
    esac
}

# ── SD card layout ────────────────────────────────────────────────────────────
collect_sd_layout() {
    step "SD card location"
    info "Enter the full path to the root of the SD card — the top-level"
    info "directory that contains folders like DCIM/ or Front/ or video/."
    echo

    while true; do
        ask SD_PATH "SD card path (e.g. /media/user/DASHCAM)"
        SD_PATH="${SD_PATH%/}"   # strip trailing slash
        if [ -d "$SD_PATH" ]; then
            break
        else
            warn "'$SD_PATH' is not a directory. Try again."
        fi
    done

    # Directory tree — full depth, directories only
    if command -v tree &>/dev/null; then
        info "Running:  tree -d \"$SD_PATH\""
        TREE_OUTPUT=$(tree -d "$SD_PATH" 2>&1)
        ok "Directory tree captured."
    else
        TREE_OUTPUT="[tree not installed — install with: sudo dnf install tree  OR  brew install tree]"
        warn "tree not installed — directory tree will be absent from report."
    fi

    # Per-directory file listings, 3 levels deep, 25 files each
    info "Building per-folder file listings (up to 25 files per folder, 3 levels deep)..."
    local LS_TMPFILE
    LS_TMPFILE=$(mktemp)

    # Top-level directory
    echo "$SD_PATH:" >> "$LS_TMPFILE"
    ls -l "$SD_PATH" 2>&1 >> "$LS_TMPFILE"

    # Each subdirectory, sorted, up to 3 levels deep
    while IFS= read -r DIR; do
        echo >> "$LS_TMPFILE"
        echo "$DIR:" >> "$LS_TMPFILE"
        ls -l "$DIR" 2>&1 | head -25 >> "$LS_TMPFILE"
    done < <(find "$SD_PATH" -mindepth 1 -maxdepth 3 -type d 2>/dev/null | sort)

    LS_OUTPUT=$(cat "$LS_TMPFILE")
    rm -f "$LS_TMPFILE"
    ok "File listings captured ($(echo "$LS_OUTPUT" | wc -l) lines)."
}

# ── Select a sample video file ────────────────────────────────────────────────
find_video_file() {
    step "Select a sample video file"
    info "We need one video file from the primary (front-facing) camera."
    info "Searching for video files on the SD card..."
    echo

    VIDEO_FILE=""
    FOUND_FILES=$(find "$SD_PATH" -maxdepth 6 \
        \( -iname "*.ts" -o -iname "*.mp4" -o -iname "*.mov" \
           -o -iname "*.avi" -o -iname "*.mkv" \) \
        2>/dev/null | sort | head -30)

    if [ -n "$FOUND_FILES" ]; then
        echo "  Found video files (up to 30 shown):"
        echo
        N=1
        while IFS= read -r F; do
            printf "   %3d)  %s\n" "$N" "$F"
            N=$((N+1))
        done <<< "$FOUND_FILES"
        echo
        ask SELECTION "Enter a number to select, or paste the full path"
        if [[ "$SELECTION" =~ ^[0-9]+$ ]]; then
            VIDEO_FILE=$(echo "$FOUND_FILES" | sed -n "${SELECTION}p")
        else
            VIDEO_FILE="$SELECTION"
        fi
    else
        warn "No video files found automatically."
        ask VIDEO_FILE "Enter the full path to a primary-camera video file"
    fi

    if [ -n "$VIDEO_FILE" ] && [ ! -f "$VIDEO_FILE" ]; then
        warn "File not found: $VIDEO_FILE"
        warn "ffprobe and exiftool sections will be skipped."
        VIDEO_FILE=""
    fi

    if [ -n "$VIDEO_FILE" ]; then
        ok "Selected: $VIDEO_FILE"
    fi
}

# ── Thumbnail auto-detection ──────────────────────────────────────────────────
probe_thumbnails() {
    THUMB_INFO=""   # empty = no confident result; collect_user_notes will ask

    [ -z "$VIDEO_FILE" ] && return

    step "Thumbnail auto-detection"

    local vid_dir vid_stem jpg_ths jpg_same any_jpg
    vid_dir=$(dirname "$VIDEO_FILE")
    vid_stem=$(basename "${VIDEO_FILE%.*}")
    jpg_ths="$vid_dir/${vid_stem}_ths.jpg"
    jpg_same="$vid_dir/${vid_stem}.jpg"

    if [ -f "$jpg_ths" ]; then
        THUMB_INFO="Yes — _ths suffix (auto-detected)"
        ok "Detected: _ths suffix  (${vid_stem}_ths.jpg)"
    elif [ -f "$jpg_same" ]; then
        THUMB_INFO="Yes — same base name, .jpg extension (auto-detected)"
        ok "Detected: same base name  (${vid_stem}.jpg)"
    else
        any_jpg=$(find "$vid_dir" -maxdepth 1 -iname '*.jpg' 2>/dev/null | head -1)
        if [ -n "$any_jpg" ]; then
            THUMB_INFO="Yes — other pattern: $(basename "$any_jpg")"
            info "JPG files found but no standard pattern matched."
            info "  Example: $(basename "$any_jpg")"
        else
            info "No .jpg thumbnails found alongside the selected video."
        fi
    fi
}

# ── ffprobe ───────────────────────────────────────────────────────────────────
run_ffprobe() {
    step "ffprobe — stream and format info"
    if [ -z "$VIDEO_FILE" ]; then
        warn "Skipped — no video file selected."
        FFPROBE_OUTPUT="[SKIPPED — no video file selected]"
        return
    fi
    if [ $HAS_FFPROBE -eq 0 ]; then
        warn "Skipped — ffprobe not installed."
        FFPROBE_OUTPUT="[SKIPPED — ffprobe not installed]"
        return
    fi

    info "Running:  ffprobe -v quiet -show_streams -show_format -of json"
    FFPROBE_OUTPUT=$(ffprobe -v quiet -show_streams -show_format -of json \
        "$VIDEO_FILE" 2>&1)
    ok "ffprobe complete."
}

# ── exiftool ──────────────────────────────────────────────────────────────────
run_exiftool() {
    step "exiftool — metadata and GPS extraction"
    if [ -z "$VIDEO_FILE" ]; then
        warn "Skipped — no video file selected."
        EXIFTOOL_OUTPUT="[SKIPPED — no video file selected]"
        GPS_RESULT="[SKIPPED]"
        return
    fi
    if [ $HAS_EXIFTOOL -eq 0 ]; then
        warn "Skipped — exiftool not installed."
        EXIFTOOL_OUTPUT="[SKIPPED — exiftool not installed]"
        GPS_RESULT="[SKIPPED]"
        return
    fi

    info "Running:  exiftool -G1 -s (first 80 lines)"
    EXIFTOOL_OUTPUT=$(exiftool -G1 -s "$VIDEO_FILE" 2>&1 | head -80)
    ok "exiftool metadata captured."

    info "GPS check A — format-string extraction..."
    GPS_A=$(exiftool -ee3 -p \
        '$GPSDateTime $GPSLatitude# $GPSLongitude# $GPSAltitude# $GPSSpeed# $GPSTrack#' \
        "$VIDEO_FILE" 2>/dev/null | head -10 || true)

    info "GPS check B — tag name scan for GPS/LIGO/NMEA groups..."
    GPS_B=$(exiftool -G1 -s "$VIDEO_FILE" 2>/dev/null \
        | grep -i 'gps\|ligo\|nmea\|location' || true)

    if [ -z "$GPS_A" ] && [ -z "$GPS_B" ]; then
        GPS_RESULT="[No GPS data detected by either extraction method]"
        info "No GPS data detected."
    else
        GPS_RESULT="Option A — format-string extraction:
${GPS_A:-[no output]}

Option B — GPS/LIGO/NMEA tag scan:
${GPS_B:-[no output]}"
        ok "GPS data found."
    fi
}

# ── Timestamp and thumbnail notes ─────────────────────────────────────────────
collect_user_notes() {
    step "Timestamp timezone"
    info "Are the filename timestamps in UTC or your local time zone?"
    info "Examples: 'UTC', 'US/Eastern (UTC-5)', 'UTC+9', 'unknown'"
    ask TIMESTAMP_TZ "Timestamp timezone" "unknown"

    step "Thumbnail / preview images"
    if [ -n "$THUMB_INFO" ]; then
        info "Auto-detected: $THUMB_INFO"
        local THUMB_CONFIRM
        ask THUMB_CONFIRM "Accept? [Y/n]" "Y"
        case "$THUMB_CONFIRM" in
            [nN]*) THUMB_INFO="" ;;
        esac
    fi
    if [ -z "$THUMB_INFO" ]; then
        info "Does the SD card contain .jpg preview images alongside the video files?"
        info "  1) Yes — same base name, .jpg extension  (e.g. clip001.jpg)"
        info "  2) Yes — _ths suffix                     (e.g. clip001_ths.jpg)"
        info "  3) Yes — other pattern"
        info "  4) No thumbnails"
        info "  5) Unknown"
        ask THUMB_CHOICE "Enter 1-5" "5"
        case "$THUMB_CHOICE" in
            1) THUMB_INFO="Yes — same base name, .jpg extension" ;;
            2) THUMB_INFO="Yes — _ths suffix" ;;
            3) ask THUMB_INFO "Describe the thumbnail filename pattern" ;;
            4) THUMB_INFO="No thumbnails" ;;
            *) THUMB_INFO="Unknown" ;;
        esac
    fi

    step "Segment / loop recording"
    ask SEG_MINUTES "Segment length in minutes (check camera settings)" "unknown"

    step "Additional notes  (press Enter on a blank line when done)"
    info "Anything else: known quirks, irregular naming, PathMux errors seen, etc."
    EXTRA_NOTES=""
    while IFS= read -r LINE; do
        [ -z "$LINE" ] && break
        EXTRA_NOTES="${EXTRA_NOTES}${LINE}
"
    done
}

# ── Assemble the report file ──────────────────────────────────────────────────
write_report() {
    step "Writing report file"

    SAFE_MODEL=$(printf '%s' "$CAM_MODEL" | tr ' /\\:' '____' | tr -dc '[:alnum:]_-')
    DATESTAMP=$(date '+%Y%m%d_%H%M%S')
    OUTPUT_FILE="pathmux_cam_profile_${SAFE_MODEL}_${DATESTAMP}.txt"

    {
        echo "============================================================"
        echo "  PathMux Camera Fingerprint Report"
        echo "  Script:    collect_cam_profile.sh v${SCRIPT_VERSION}"
        echo "  Generated: $(date '+%Y-%m-%d %H:%M:%S %Z')"
        echo "  Host:      $(uname -sr 2>/dev/null || echo 'unknown')"
        echo "============================================================"
        echo
        divider
        echo "CAMERA INFO"
        divider
        echo "Make / Model      : $CAM_MODEL"
        echo "Firmware          : $CAM_FIRMWARE"
        echo "Camera channels   : $CAM_COUNT"
        echo "GPS present       : $GPS_PRESENT"
        echo "Timestamp TZ      : $TIMESTAMP_TZ"
        echo "Thumbnails        : $THUMB_INFO"
        echo "Segment length    : $SEG_MINUTES min"
        echo "Sample video file : ${VIDEO_FILE:-[none selected]}"
        echo
        divider
        echo "DIRECTORY TREE  (tree -d, full depth)"
        divider
        echo "$TREE_OUTPUT"
        echo
        divider
        echo "SD CARD LAYOUT  (ls -l per folder, up to 25 files each, 3 levels deep)"
        divider
        echo "$LS_OUTPUT"
        echo
        divider
        echo "FFPROBE OUTPUT  (streams + format, JSON)"
        divider
        echo "$FFPROBE_OUTPUT"
        echo
        divider
        echo "EXIFTOOL OUTPUT  (first 80 lines)"
        divider
        echo "$EXIFTOOL_OUTPUT"
        echo
        divider
        echo "GPS EXTRACTION CHECK"
        divider
        printf '%s\n' "$GPS_RESULT"
        echo
        divider
        echo "EXTRA NOTES"
        divider
        printf '%s' "${EXTRA_NOTES:-[none]}"
        echo
        echo
        echo "============================================================"
        echo "  Attach this file to a GitHub issue:"
        echo "  https://github.com/Nutball-Labs/PathMux/issues/new/choose"
        echo "============================================================"
    } > "$OUTPUT_FILE"

    echo
    echo "${BOLD}${GREEN}============================================================${RESET}"
    echo "${BOLD}  Report saved:  $OUTPUT_FILE${RESET}"
    echo "${BOLD}${GREEN}============================================================${RESET}"
    echo
    echo "  1. Open: https://github.com/Nutball-Labs/PathMux/issues/new/choose"
    echo "  2. Choose 'Camera Fingerprint — New Dashcam Support'"
    echo "  3. Fill in the short text fields, then attach this file"
    echo
}

# ── Main ──────────────────────────────────────────────────────────────────────
main() {
    banner
    check_tools
    collect_camera_info
    collect_sd_layout
    find_video_file
    probe_thumbnails
    run_ffprobe
    run_exiftool
    collect_user_notes
    write_report
}

main "$@"

# SN: 00090
