# PathMux — macOS Port Checklist

Tracks what must be done to build and run PathMux (CLI and Qt6 GUI) on macOS.
Overall development direction lives in ROADMAP.md. This file covers platform-specific
porting blockers only.

---

## Good News First

macOS is POSIX/BSD, which means the porting lift is substantially smaller than Windows.
Most POSIX APIs used by PathMux exist on macOS unchanged:

- `localtime_r` — available (BSD POSIX)
- `ioctl(TIOCGWINSZ)` / terminal width — available
- `popen`/`pclose` — available
- `access()` / `stat()` — available
- `getenv("HOME")` — works
- `std::filesystem` — available (requires macOS 10.15 Catalina or later with Apple Clang)

The primary differences are config directory convention, Apple Silicon, and distribution.

---

## Build Pathway Options

| Pathway | Effort | Notes |
|---|---|---|
| Native Xcode/Apple Clang | Low | clang is the system compiler; no Xcode IDE required |
| Homebrew GCC | Low | `brew install gcc` gives g++13+; drop-in replacement |
| Cross-compile from Linux | High | `osxcross` toolchain; fragile; not recommended |

**Recommended:** Native build on a macOS machine using Apple Clang (comes with
Xcode Command Line Tools) or Homebrew GCC. CMake works identically.

---

## Toolchain Setup

- [ ] Install Xcode Command Line Tools: `xcode-select --install`
  (provides `clang++`, `cmake` if installed via Homebrew, `make`)
- [ ] Install Homebrew: [brew.sh](https://brew.sh)
- [ ] Install dependencies: `brew install cmake ffmpeg exiftool`
- [ ] Verify `clang++ --std=c++17` works: `clang++ --version` should show Apple Clang 14+
  (Xcode 14 = macOS Ventura; Xcode 15 = macOS Sonoma)
- [ ] Verify `std::filesystem` available: requires macOS 10.15+ and deployment target set accordingly

### CMake Deployment Target
- [ ] Set minimum macOS deployment target to avoid `std::filesystem` runtime errors on
  older systems. Add to CMakeLists.txt:
  ```cmake
  if(APPLE)
      set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
  endif()
  ```
- [ ] Decide on Intel-only vs Universal Binary (Intel + Apple Silicon) — see Apple Silicon section below

---

## Code Changes Required

### Config Directory
- [x] `lib/platform.cpp` `getConfigDir()` — macOS path implemented.
  Returns `~/Library/Application Support/pathmux/`. ✓

### Platform Detection Macro
- [x] `__APPLE__` used throughout `compat.hpp` and `platform.cpp`. ✓

### VideoToolbox Encoder Support (discovered during macOS testing)
- [x] `compat.hpp`: `WEXITSTATUS` rvalue fix for Apple's `sys/wait.h` (which requires
  lvalue; replaced with portable `(((s) >> 8) & 0xff)` form). ✓
- [x] `video_build.cpp`: VideoToolbox encoders reject `-q`; replaced with `-b:v <quality>M`
  when encoder name contains `videotoolbox`. ✓
- [x] Host config `pathmux_Patsys-Air.json` created with VideoToolbox encoder profile. ✓

### No Other Changes Required
- All POSIX calls used by PathMux work unchanged on macOS. ✓

---

## Apple Silicon (arm64)

macOS has run on Apple Silicon (M1/M2/M3/M4) since 2020. Most code compiles
for arm64 without changes. C++17 is fully supported.

- [ ] Verify clean build for `arm64` target: `cmake -DCMAKE_OSX_ARCHITECTURES=arm64 ..`
- [ ] Verify clean build for `x86_64` (Intel) target: `cmake -DCMAKE_OSX_ARCHITECTURES=x86_64 ..`
- [ ] Optionally build Universal Binary (runs natively on both):
  `cmake -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" ..`
  Note: Universal Binary requires all linked libraries (ffmpeg, Qt6) to also be universal
  or the link step fails. Homebrew provides universal bottles for most packages.
- [ ] Test on both architectures if hardware is available; GitHub Actions macOS runners
  now include both intel and arm64 environments.

---

## External Dependencies

All installable via Homebrew:

- [ ] `brew install ffmpeg` — provides `ffprobe` in `/usr/local/bin` (Intel) or
  `/opt/homebrew/bin` (Apple Silicon). Verify `ffprobe` is in `$PATH`.
- [ ] `brew install exiftool` — installs ExifTool 13.x (Homebrew tracks upstream
  releases more closely than EPEL; verify version meets 13.51+ requirement).
- [ ] Add macOS-specific install instructions to error messages if tools not found in `$PATH`.
  `brew install ffmpeg exiftool` is the one-line answer for most macOS users.

---

## Testing

- [x] `pathmux --version` runs on macOS (tested on Ventura/Sonoma, MacBook Air i5-8210Y) ✓
- [x] `-s <path>` scan against footage on NFS-mounted volume ✓
- [x] Manifest written to `~/Library/Application Support/pathmux/` correctly ✓
- [x] Terminal box drawing renders correctly in iTerm2 ✓
- [x] Full collage build (4K + 1080p) via VideoToolbox confirmed working ✓
- [ ] `pm_gpsinfo` operates correctly — not yet tested on macOS
- [ ] Test on Apple Silicon hardware (or arm64 GitHub Actions runner)
- [ ] Test paths with spaces (common on macOS, e.g., `/Volumes/My Dashcam/`)

---

## Phase 2: Qt6 GUI on macOS

Qt6 is fully supported on macOS and handles all platform differences natively.

- [ ] `brew install qt@6` — installs Qt6 and CMake integration
- [ ] Set `CMAKE_PREFIX_PATH` to Homebrew Qt6 path:
  `export CMAKE_PREFIX_PATH=$(brew --prefix qt@6)`
- [ ] Qt6 natively provides on macOS:
  - Native Aqua window chrome (traffic light buttons, title bar)
  - macOS file picker dialog
  - Dark mode follows system setting (macOS 10.14+ Mojave)
  - `QStandardPaths::AppDataLocation` returns `~/Library/Application Support/pathmux/`
    automatically — consistent with `Platform::getConfigDir()` macOS implementation
  - Keyboard shortcuts follow macOS convention (Cmd instead of Ctrl)
- [ ] Build and launch `pathmux-gui.app` on macOS 13+

### Distribution — `.app` Bundle and `.dmg`
- [ ] Use `macdeployqt` to bundle Qt6 frameworks into `pathmux-gui.app`
  (avoids dependency on system Qt, which may not match the build version)
- [ ] Bundle `ffprobe` and `exiftool` inside the `.app` bundle, or document as
  Homebrew prerequisites in the README
- [ ] Create `.dmg` installer:
  - CMake/CPack can generate a `.dmg` via `cpack -G DragNDrop`
  - Or use `create-dmg` tool (Homebrew): `brew install create-dmg`
- [ ] **Code signing and notarization** — required for Gatekeeper to allow launch
  without user override on macOS 10.15+:
  - [ ] Enroll in Apple Developer Program ($99/year) to get a signing certificate
  - [ ] Sign `.app` bundle: `codesign --deep --sign "Developer ID Application: ..." pathmux-gui.app`
  - [ ] Notarize with Apple: `xcrun notarytool submit pathmux-gui.zip --wait`
  - [ ] Staple notarization ticket: `xcrun stapler staple pathmux-gui.app`
  - Without notarization: users see "unidentified developer" dialog; can bypass with
    right-click → Open, but this is a barrier for non-technical users
- [ ] Add `.quadeye` file association in `Info.plist` for future archive format

---

## Open Questions

- **Minimum macOS version:** 10.15 (Catalina) is the floor for `std::filesystem` with
  Apple Clang. macOS 12 Monterey may be a more realistic minimum given current hardware.
  Decide before cutting any macOS release.
- **Universal Binary vs separate Intel/ARM builds:** Universal binary is cleaner for
  users but requires all deps (Qt6, ffmpeg) to be universal. Homebrew supports this.
  Separate builds are simpler to produce. Decide when macOS build is ready.
- **Homebrew ExifTool version:** Homebrew updates packages faster than EPEL.
  As of 2026 Homebrew should have ExifTool >= 13.51. Verify at build time.
- **Apple Developer Program:** $99/year is the cost for code signing.
  Required for frictionless distribution. Decision deferred until GUI distribution is planned.

---

*Last updated: 2026-03-20*
*Status: CLI fully working — collage pipeline confirmed on Intel macOS via VideoToolbox*

<!-- SN: 00081 -->
