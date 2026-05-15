# CamClops — Supported Cameras

CamClops uses JSON camera profiles to describe how each dashcam model lays
out its footage on the SD card: directory structure, filename format, GPS
extraction method, and which camera slots are present. The table below lists
cameras that have been tested and confirmed to work with the built-in profiles.
Each entry shows the SD card layout CamClops expects, how GPS data is
extracted (if supported), and the confirmation status.

| Camera | Layout | GPS | Status |
|---|---|---|---|
| Pruveeo D90 360° | `Front/` `Rear/` `Left/` `Right/` subdirs, `.ts` | LIGOGPSINFO via ExifTool | ✅ Confirmed |
| Cobra CCDC4500 / GPS | `DCIM/100_DSC/`, CAM1+CAM2, `.MOV`/`.3GP` | gps0 atom via ExifTool (GPS models) | ✅ Confirmed |
| Prilotte | `DCIMA/` + `DCIMC/` subdirs, `.AVI` MJPEG | None (mtime timestamps) | ✅ Confirmed (no GPS) |

**Don't see your camera?** See [Adding Camera Support](#adding-camera-support) below.

---

## Adding Camera Support

### Try the wizard first

```bash
./clops_probe --wizard /path/to/sdcard
```

The wizard handles subdirectory layouts (one folder per camera) and flat
layouts (all cameras in one directory, distinguished by filename token).
It detects timestamp format, GPS method, and thumbnail handling
automatically where possible.

### If the wizard doesn't produce a working profile

Open a GitHub issue so we can build a profile without needing the hardware
in hand. There are two ways to collect the diagnostic info we need:

**Option A — Collection scripts (easiest, no CamClops required)**

Download the [`Cam_Profile_Detection/`](https://github.com/Nutball-Labs/CamClops/tree/main/Cam_Profile_Detection)
folder, run the script for your OS, and attach the generated `.txt` file to
the issue:

- **Linux / macOS:** `./collect_cam_profile.sh`
- **Windows:** double-click `collect_cam_profile.bat`

The script walks you through each step, runs `ffprobe` and `exiftool`
automatically, and saves everything to a single file.

**Option B — clops_probe (if CamClops is already installed)**

```bash
./clops_probe --card /path/to/sdcard --json > camera_report.json
```

Attach `camera_report.json` to the issue.

---

Open issues at **https://github.com/Nutball-Labs/CamClops/issues** and include
the camera make and model in the title.

<!-- SN: 00118 -->
