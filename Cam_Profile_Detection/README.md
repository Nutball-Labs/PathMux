# PathMux — Camera Profile Collection Kit

Run this tool to generate a diagnostic report about your dashcam.
Attach the output file to a GitHub issue and we can add detection support
for your camera without needing the hardware in hand.

**Open an issue:** https://github.com/Nutball-Labs/PathMux/issues/new/choose

---

## What you need before running

| Tool | Why | How to get it |
|---|---|---|
| **ffprobe** | Reads video stream info from your footage | Part of [ffmpeg](https://ffmpeg.org/download.html) |
| **exiftool 13.51+** | Reads metadata and GPS streams | [exiftool.org](https://exiftool.org) · Linux: `dnf install perl-Image-ExifTool` · macOS: `brew install exiftool` |

The script will warn you if either tool is missing and let you continue
anyway — but the report will be incomplete without them.

---

## How to run

### Linux / macOS

1. Open a terminal in this folder
2. Make the script executable (first time only):
   ```
   chmod +x collect_cam_profile.sh
   ```
3. Run it:
   ```
   ./collect_cam_profile.sh
   ```
4. Follow the prompts. The script will find video files on your SD card
   automatically.
5. Attach the generated `pathmux_cam_profile_*.txt` file to a GitHub issue.

### Windows

1. Download this folder (or clone the repo)
2. Insert your dashcam SD card
3. Double-click **`collect_cam_profile.bat`**
   - PowerShell must be available (built into Windows 10 and 11)
   - If ffprobe.exe or exiftool.exe are not in your PATH, place copies of
     them in the same folder as these scripts and they will be found
     automatically
4. Follow the on-screen prompts
5. Attach the generated `pathmux_cam_profile_*.txt` file to a GitHub issue

---

## What the script collects

| Section | Content |
|---|---|
| Camera info | Make, model, firmware, channel count, GPS presence |
| Directory tree | Full-depth directory structure (`tree -d` / `tree /A`) — no depth cap |
| SD card layout | `ls -l` / `dir` per folder, up to 25 files each, 3 levels deep |
| ffprobe output | Full JSON — codec names, container format, data streams, duration |
| exiftool output | First 80 lines with group tags — critical for GPS method detection |
| GPS check | Two extraction attempts: format-string and tag-name scan |
| Timestamp notes | UTC vs. local time zone (needed for trip detection accuracy) |
| Thumbnail info | Sidecar image naming pattern (auto-detected where possible) |
| Extra notes | Anything else you want to add |

The report is a plain text file. Nothing is sent anywhere automatically —
you attach it to the GitHub issue yourself.

---

## Privacy

GPS coordinates appear in the exiftool and ffprobe output sections.
If you prefer not to share them publicly, open the generated `.txt` file
in any text editor and replace the coordinate numbers with `XX.XXXXX`
before attaching. Keep all field names and structure intact.

<!-- SN: 00089 -->
