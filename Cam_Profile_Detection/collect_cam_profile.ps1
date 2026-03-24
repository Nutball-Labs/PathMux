#Requires -Version 5.1
# =============================================================================
# PathMux Camera Profile Collection Script v1.0  (Windows / PowerShell)
#
# Walks you through gathering the diagnostic information needed to add
# detection support for a new dashcam model in PathMux.
#
# Saves everything to a single text file — attach it to a GitHub issue at:
# https://github.com/Nutball-Labs/PathMux/issues/new/choose
#
# Requires:  ffprobe (part of ffmpeg)  +  exiftool
#   Download ffmpeg:   https://ffmpeg.org/download.html
#   Download exiftool: https://exiftool.org
#   Add both to your PATH, or place them in the same folder as this script.
# =============================================================================

$ScriptVersion = "1.0"
$ScriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Path

# ── Color helpers ─────────────────────────────────────────────────────────────
function Write-Step   { param($msg) Write-Host; Write-Host "[ $msg ]" -ForegroundColor Green -NoNewline; Write-Host }
function Write-Info   { param($msg) Write-Host "  $msg" -ForegroundColor Cyan }
function Write-Warn   { param($msg) Write-Host "  WARNING: $msg" -ForegroundColor Yellow }
function Write-Ok     { param($msg) Write-Host "  OK  $msg" -ForegroundColor Green }
function Write-Divider{ Write-Host "------------------------------------------------------------" }

function Ask {
    param([string]$Prompt, [string]$Default = "")
    if ($Default) {
        Write-Host "  $Prompt [$Default]: " -NoNewline -ForegroundColor White
    } else {
        Write-Host "  ${Prompt}: " -NoNewline -ForegroundColor White
    }
    $input = Read-Host
    if ([string]::IsNullOrWhiteSpace($input) -and $Default) { return $Default }
    return $input.Trim()
}

# Locate an executable: check PATH first, then the script directory.
function Find-Tool {
    param([string]$Name)
    $found = Get-Command $Name -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    $local = Join-Path $ScriptDir "$Name.exe"
    if (Test-Path $local) { return $local }
    return $null
}

# Run an external command, capture stdout+stderr, return as string.
function Run-Tool {
    param([string]$Exe, [string[]]$Args)
    try {
        $out = & $Exe @Args 2>&1
        return ($out -join "`n")
    } catch {
        return "[ERROR running ${Exe}: $_]"
    }
}

# ── Banner ────────────────────────────────────────────────────────────────────
function Show-Banner {
    Clear-Host
    Write-Host
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "   PathMux Camera Profile Collection  v$ScriptVersion" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host
    Write-Host "  This script collects diagnostic info about your dashcam"
    Write-Host "  and saves it to a single text file you can attach to a"
    Write-Host "  GitHub issue so we can add support for your camera."
    Write-Host
    Write-Host "  https://github.com/Nutball-Labs/PathMux/issues/new/choose" -ForegroundColor Cyan
    Write-Host
    Read-Host "  Press Enter to continue"
}

# ── Tool detection ────────────────────────────────────────────────────────────
function Check-Tools {
    Write-Step "Checking required tools"

    $script:FfprobePath  = Find-Tool "ffprobe"
    $script:ExiftoolPath = Find-Tool "exiftool"
    $script:HasFfprobe   = $false
    $script:HasExiftool  = $false

    if ($script:FfprobePath) {
        $ver = (& $script:FfprobePath -version 2>&1 | Select-Object -First 1)
        Write-Ok "ffprobe   — $ver"
        $script:HasFfprobe = $true
    } else {
        Write-Warn "ffprobe not found."
        Write-Warn "  Download: https://ffmpeg.org/download.html"
        Write-Warn "  Add ffprobe.exe to your PATH, or place it next to this script."
    }

    if ($script:ExiftoolPath) {
        $ver = (& $script:ExiftoolPath -ver 2>&1 | Select-Object -First 1).Trim()
        Write-Ok "exiftool  — v$ver"
        $script:HasExiftool = $true
        # Warn if older than 13.51
        $parts = $ver -split '\.'
        if ($parts.Count -ge 2) {
            $major = [int]$parts[0]; $minor = [int]$parts[1]
            if ($major -lt 13 -or ($major -eq 13 -and $minor -lt 51)) {
                Write-Warn "exiftool $ver is older than 13.51 — LIGO GPS extraction may not work."
                Write-Warn "  Upgrade from: https://exiftool.org"
            }
        }
    } else {
        Write-Warn "exiftool not found."
        Write-Warn "  Download: https://exiftool.org"
        Write-Warn "  Add exiftool.exe to your PATH, or place it next to this script."
    }

    if (-not $script:HasFfprobe -or -not $script:HasExiftool) {
        Write-Host
        Write-Warn "One or more tools are missing. The report will be incomplete."
        $cont = Ask "Continue anyway? [y/N]" "N"
        if ($cont -notmatch '^[yY]') { Write-Host "Exiting."; exit 0 }
    }
}

# ── Camera basics ─────────────────────────────────────────────────────────────
function Collect-CameraInfo {
    Write-Step "Camera information"
    Write-Info "Enter the details printed on the box or shown in the camera menu."
    Write-Host

    $script:CamModel    = Ask "Camera make and model (e.g. Vantrue N4 Pro)"
    $script:CamFirmware = Ask "Firmware version (check camera Settings menu)" "unknown"
    $script:CamCount    = Ask "Number of camera channels / lenses" "1"

    Write-Step "GPS"
    Write-Info "  1) Yes — GPS module built in"
    Write-Info "  2) Yes — external GPS receiver plugged in"
    Write-Info "  3) No GPS"
    Write-Info "  4) Unknown"
    $gpsChoice = Ask "Enter 1-4" "4"
    $script:GpsPresent = switch ($gpsChoice) {
        "1" { "Yes — built-in GPS" }
        "2" { "Yes — external GPS receiver" }
        "3" { "No GPS" }
        default { "Unknown" }
    }
}

# ── SD card layout ────────────────────────────────────────────────────────────
function Collect-SdLayout {
    Write-Step "SD card location"
    Write-Info "Enter the drive letter or full path to the root of your SD card."
    Write-Info "This is the top-level directory containing folders like DCIM\ or Front\."
    Write-Host

    while ($true) {
        $script:SdPath = Ask "SD card path (e.g. D:\ or E:\DASHCAM)"
        $script:SdPath = $script:SdPath.TrimEnd('\','/')
        if (Test-Path $script:SdPath -PathType Container) { break }
        Write-Warn "'$($script:SdPath)' is not a valid directory. Try again."
    }

    Write-Info "Running:  Get-ChildItem -Recurse (first 150 entries)"
    try {
        $items = Get-ChildItem -Recurse -Force -ErrorAction SilentlyContinue $script:SdPath |
                 Select-Object -First 150 |
                 ForEach-Object {
                     $size = if ($_.PSIsContainer) { "<DIR>" } else { "{0,12} bytes" -f $_.Length }
                     "{0,-12}  {1}" -f $size, $_.FullName
                 }
        $script:LsOutput = $items -join "`n"
        Write-Ok "Directory listing captured ($($items.Count) entries)."
    } catch {
        $script:LsOutput = "[ERROR collecting directory listing: $_]"
        Write-Warn "Directory listing failed: $_"
    }
}

# ── Select a sample video file ────────────────────────────────────────────────
function Find-VideoFile {
    Write-Step "Select a sample video file"
    Write-Info "We need one video file from the primary (front-facing) camera."
    Write-Info "Searching for video files on the SD card..."
    Write-Host

    $script:VideoFile = $null
    $extensions = @("*.ts","*.mp4","*.mov","*.avi","*.mkv","*.MP4","*.MOV","*.TS")
    $foundFiles = @()
    foreach ($ext in $extensions) {
        $foundFiles += Get-ChildItem -Recurse -Force -ErrorAction SilentlyContinue `
                           -Path $script:SdPath -Filter $ext |
                       Select-Object -First 30
        if ($foundFiles.Count -ge 30) { break }
    }
    $foundFiles = $foundFiles | Select-Object -First 30 | Sort-Object FullName

    if ($foundFiles.Count -gt 0) {
        Write-Host "  Found video files (up to 30 shown):"
        Write-Host
        for ($i = 0; $i -lt $foundFiles.Count; $i++) {
            Write-Host ("   {0,3})  {1}" -f ($i+1), $foundFiles[$i].FullName)
        }
        Write-Host
        $sel = Ask "Enter a number to select, or paste the full path"
        if ($sel -match '^\d+$') {
            $idx = [int]$sel - 1
            if ($idx -ge 0 -and $idx -lt $foundFiles.Count) {
                $script:VideoFile = $foundFiles[$idx].FullName
            } else {
                Write-Warn "Number out of range."
            }
        } else {
            $script:VideoFile = $sel
        }
    } else {
        Write-Warn "No video files found automatically."
        $script:VideoFile = Ask "Enter the full path to a primary-camera video file"
    }

    if ($script:VideoFile -and -not (Test-Path $script:VideoFile -PathType Leaf)) {
        Write-Warn "File not found: $($script:VideoFile)"
        Write-Warn "ffprobe and exiftool sections will be skipped."
        $script:VideoFile = $null
    }
    if ($script:VideoFile) { Write-Ok "Selected: $($script:VideoFile)" }
}

# ── ffprobe ───────────────────────────────────────────────────────────────────
function Run-Ffprobe {
    Write-Step "ffprobe — stream and format info"
    if (-not $script:VideoFile) {
        Write-Warn "Skipped — no video file selected."
        $script:FfprobeOutput = "[SKIPPED — no video file selected]"
        return
    }
    if (-not $script:HasFfprobe) {
        Write-Warn "Skipped — ffprobe not installed."
        $script:FfprobeOutput = "[SKIPPED — ffprobe not installed]"
        return
    }
    Write-Info "Running:  ffprobe -v quiet -show_streams -show_format -of json"
    $script:FfprobeOutput = Run-Tool $script:FfprobePath @(
        "-v","quiet","-show_streams","-show_format","-of","json",$script:VideoFile)
    Write-Ok "ffprobe complete."
}

# ── exiftool ──────────────────────────────────────────────────────────────────
function Run-Exiftool {
    Write-Step "exiftool — metadata and GPS extraction"
    if (-not $script:VideoFile) {
        Write-Warn "Skipped — no video file selected."
        $script:ExiftoolOutput = "[SKIPPED — no video file selected]"
        $script:GpsResult      = "[SKIPPED]"
        return
    }
    if (-not $script:HasExiftool) {
        Write-Warn "Skipped — exiftool not installed."
        $script:ExiftoolOutput = "[SKIPPED — exiftool not installed]"
        $script:GpsResult      = "[SKIPPED]"
        return
    }

    Write-Info "Running:  exiftool -G1 -s (first 80 lines)"
    $rawEt = Run-Tool $script:ExiftoolPath @("-G1","-s",$script:VideoFile)
    $script:ExiftoolOutput = ($rawEt -split "`n" | Select-Object -First 80) -join "`n"
    Write-Ok "exiftool metadata captured."

    Write-Info "GPS check A — format-string extraction..."
    $gpsA = Run-Tool $script:ExiftoolPath @(
        "-ee3","-p",
        '$GPSDateTime $GPSLatitude# $GPSLongitude# $GPSAltitude# $GPSSpeed# $GPSTrack#',
        $script:VideoFile)
    $gpsALines = ($gpsA -split "`n" | Select-Object -First 10) -join "`n"

    Write-Info "GPS check B — tag name scan for GPS/LIGO/NMEA groups..."
    $rawAll = Run-Tool $script:ExiftoolPath @("-G1","-s",$script:VideoFile)
    $gpsB = ($rawAll -split "`n" | Where-Object { $_ -match 'gps|ligo|nmea|location' }) -join "`n"

    if ([string]::IsNullOrWhiteSpace($gpsALines) -and [string]::IsNullOrWhiteSpace($gpsB)) {
        $script:GpsResult = "[No GPS data detected by either extraction method]"
        Write-Info "No GPS data detected."
    } else {
        $script:GpsResult = "Option A — format-string extraction:`n$gpsALines`n`nOption B — GPS/LIGO/NMEA tag scan:`n$gpsB"
        Write-Ok "GPS data found."
    }
}

# ── User notes ────────────────────────────────────────────────────────────────
function Collect-UserNotes {
    Write-Step "Timestamp timezone"
    Write-Info "Are the filename timestamps in UTC or your local time zone?"
    Write-Info "Examples: 'UTC', 'US/Eastern (UTC-5)', 'UTC+9', 'unknown'"
    $script:TimestampTz = Ask "Timestamp timezone" "unknown"

    Write-Step "Thumbnail / preview images"
    Write-Info "Does the SD card contain .jpg preview images alongside the video files?"
    Write-Info "  1) Yes — same base name, .jpg extension (e.g. clip001.jpg)"
    Write-Info "  2) Yes — _ths suffix      (e.g. clip001_ths.jpg)"
    Write-Info "  3) Yes — other pattern"
    Write-Info "  4) No thumbnails"
    Write-Info "  5) Unknown"
    $thumbChoice = Ask "Enter 1-5" "5"
    $script:ThumbInfo = switch ($thumbChoice) {
        "1" { "Yes — same name, .jpg extension" }
        "2" { "Yes — _ths suffix" }
        "3" { Ask "Describe the thumbnail filename pattern" }
        "4" { "No thumbnails" }
        default { "Unknown" }
    }

    Write-Step "Segment / loop recording"
    $script:SegMinutes = Ask "Segment length in minutes (check camera settings)" "unknown"

    Write-Step "Additional notes"
    Write-Info "Anything else: known quirks, irregular naming, PathMux errors seen, etc."
    Write-Info "Press Enter on a blank line when done."
    $lines = @()
    while ($true) {
        $line = Read-Host "  "
        if ([string]::IsNullOrEmpty($line)) { break }
        $lines += $line
    }
    $script:ExtraNotes = if ($lines.Count -gt 0) { $lines -join "`n" } else { "[none]" }
}

# ── Write the report ──────────────────────────────────────────────────────────
function Write-Report {
    Write-Step "Writing report file"

    $safeModel  = $script:CamModel -replace '[\\/:*?"<>| ]','_'
    $datestamp  = Get-Date -Format "yyyyMMdd_HHmmss"
    $outputFile = "pathmux_cam_profile_${safeModel}_${datestamp}.txt"
    $outputPath = Join-Path (Get-Location) $outputFile

    $divider = "------------------------------------------------------------"
    $osInfo  = [System.Environment]::OSVersion.VersionString

    $report = @"
============================================================
  PathMux Camera Fingerprint Report
  Script:    collect_cam_profile.ps1 v$ScriptVersion
  Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
  Host:      $osInfo
============================================================

$divider
CAMERA INFO
$divider
Make / Model      : $($script:CamModel)
Firmware          : $($script:CamFirmware)
Camera channels   : $($script:CamCount)
GPS present       : $($script:GpsPresent)
Timestamp TZ      : $($script:TimestampTz)
Thumbnails        : $($script:ThumbInfo)
Segment length    : $($script:SegMinutes) min
Sample video file : $(if ($script:VideoFile) { $script:VideoFile } else { "[none selected]" })

$divider
SD CARD LAYOUT  (Get-ChildItem -Recurse, first 150 entries)
$divider
$($script:LsOutput)

$divider
FFPROBE OUTPUT  (streams + format, JSON)
$divider
$($script:FfprobeOutput)

$divider
EXIFTOOL OUTPUT  (first 80 lines)
$divider
$($script:ExiftoolOutput)

$divider
GPS EXTRACTION CHECK
$divider
$($script:GpsResult)

$divider
EXTRA NOTES
$divider
$($script:ExtraNotes)

============================================================
  Attach this file to a GitHub issue:
  https://github.com/Nutball-Labs/PathMux/issues/new/choose
============================================================
"@

    $report | Out-File -FilePath $outputPath -Encoding UTF8

    Write-Host
    Write-Host "============================================================" -ForegroundColor Green
    Write-Host "  Report saved:  $outputFile" -ForegroundColor White
    Write-Host "============================================================" -ForegroundColor Green
    Write-Host
    Write-Host "  1. Open: https://github.com/Nutball-Labs/PathMux/issues/new/choose"
    Write-Host "  2. Choose 'Camera Fingerprint — New Dashcam Support'"
    Write-Host "  3. Fill in the short text fields, then attach this file"
    Write-Host
    Write-Host "  Press Enter to exit."
    Read-Host | Out-Null
}

# ── Main ──────────────────────────────────────────────────────────────────────
Show-Banner
Check-Tools
Collect-CameraInfo
Collect-SdLayout
Find-VideoFile
Run-Ffprobe
Run-Exiftool
Collect-UserNotes
Write-Report

# SN: 00089
