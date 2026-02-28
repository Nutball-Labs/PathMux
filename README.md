# QuadEye Dashcam Manager

QuadEye is a lightweight C++ tool designed to organize and manage multi-camera dashcam footage. It automatically groups related video segments into "Trips" and provides both human-readable trees and machine-readable JSON manifests.

## Key Features
- **Smart Path Discovery**: Just run `./quadeye`. It checks your last used folder and looks for mounted SD cards automatically.
- **Fast Caching**: Scans once and saves a manifest in `~/.config/quadeye/` for instant subsequent loads.
- **Multi-Camera Support**: Automatically aligns Front, Rear, Left, and Right camera segments by timestamp.
- **Ignore Hidden**: Automatically ignores hidden dot files (`.` files).

## Quick Start
1. **Compile**: `make`
2. **Run**: `./quadeye` (Auto-detects media)
3. **List Trips**: `./quadeye -t /path/to/videos`
4. **Full Tree**: `./quadeye -T`

## Output Examples
`quadeye -t` gives you a summary of trip IDs, durations, and start times.
`quadeye -T` expands that view to show every file associated with each camera in that trip.