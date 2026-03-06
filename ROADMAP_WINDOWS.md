# PathMux — Windows Port Checklist

Tracks what must be done to build and run PathMux (CLI and Qt6 GUI) on Windows.
Overall development direction lives in ROADMAP.md. This file covers platform-specific
porting blockers only.

---

## Build Pathway Options

| Pathway | Effort | Produces | Notes |
|---|---|---|---|
| WSL2 | None | Linux binary in WSL2 | Not a real port; for CLI testing only |
| MinGW-w64 cross-compile (from Linux) | Medium | Native `.exe` | Good for CI/release automation |
| MSYS2/MinGW on Windows | Medium | Native `.exe` | Good for Windows dev workflow |
| MSVC (Visual Studio) | High | Native `.exe` | Best compatibility; needed for Store/enterprise |

---

## Pathway 1: WSL2 (No Code Changes)

- [ ] Install WSL2 + AlmaLinux 9 (or Rocky) on Windows machine
- [ ] Install `cmake g++ ffmpeg exiftool` inside WSL2
- [ ] Clone repo and build as normal
- [ ] Verify CLI works against footage on `/mnt/c/...` Windows paths
- [ ] Document WSL2 drive-path mapping for users

**Limitation:** Requires WSL2 install; not suitable for end-user distribution.

---

## Pathway 2: MinGW-w64 Cross-Compile (from Alma Linux)

### Toolchain Setup
- [ ] `dnf install mingw64-gcc-c++ mingw64-cmake`
- [ ] Verify `x86_64-w64-mingw32-g++` supports C++17 and `std::filesystem`
- [ ] Create `cmake/toolchain-mingw64.cmake` with `CMAKE_SYSTEM_NAME Windows` and compiler paths
- [ ] Add static runtime link flags (`-static -lstdc++ -lwinpthread`) for dependency-free `.exe`

### Code Changes Required
- [ ] **`localtime_r` → `localtime_s`** — reversed parameter order on Windows.
  Wrap in `#ifdef _WIN32` guard in any file that calls it. Already replaced throughout
  in v0.9.6b but guard needs adding for cross-compile.
- [ ] **Terminal width** (`lib/platform.cpp`) — implement Windows stub using
  `GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)`
- [ ] **Home/config paths** (`lib/platform.cpp`) — implement Windows stubs:
  - `getHomePath()` → `%USERPROFILE%`
  - `getConfigDir()` → `%APPDATA%\pathmux\`
- [ ] **`popen`/`pclose`** — alias to `_popen`/`_pclose` on Windows; add thin wrapper
  or `#ifdef` in platform layer (used for ffprobe and exiftool subprocess calls)
- [ ] **`access()`** — replace or alias to `_access()` on Windows; prefer
  `std::filesystem::exists()` where feasible
- [ ] **Newline stripping** — strip `\r` from lines read back from `popen` pipes
  (Windows subprocess output includes `\r\n`)
- [ ] **Path separators** — `std::filesystem::path` handles this transparently; verify
  no hardcoded `/`-split string parsing on manifest paths

### External Dependencies
- [ ] `ffprobe.exe` — static Windows build from ffmpeg.org or via Chocolatey/Scoop;
  must be in `%PATH%`
- [ ] `exiftool.exe` — standalone Win32 `.exe` from exiftool.org; must be in `%PATH%`
- [ ] Add platform-aware startup check: if tool not found, print Windows-specific
  install instructions (not Linux `dnf install` instructions)

### Testing
- [ ] `pathmux.exe --version` runs on Windows 10/11
- [ ] `-s <path>` scan against footage on a Windows drive path
- [ ] Manifest written to `%APPDATA%\pathmux\` correctly
- [ ] Terminal box drawing renders in Windows Terminal and cmd.exe
- [ ] `pm_gpsinfo.exe` operates correctly
- [ ] Paths with spaces and Unicode characters handled without crash

---

## Pathway 3: MSYS2/MinGW on Windows

Same code changes as Pathway 2. Differences are environment setup only.

- [ ] Document required MSYS2 packages:
  `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make`
- [ ] Build must use MinGW64 shell, not MSYS shell (to produce native PE `.exe`)
- [ ] Verify output `.exe` runs outside MSYS2 (no MSYS2 DLL dependencies if statically linked)

---

## Pathway 4: MSVC (Visual Studio) — Long-Term

- [ ] Verify CMakeLists.txt generates valid VS solution: `cmake -G "Visual Studio 17 2022"`
- [ ] Resolve MSVC-specific warnings (`/W4` level)
- [ ] Replace `#pragma GCC diagnostic` with `#pragma warning` where needed
- [ ] MSVC does not support `popen` by name — must use `_popen` or a compatibility shim
- [ ] All other code changes from Pathway 2 apply

---

## Phase 2: Qt6 GUI on Windows

Qt6 is fully supported on Windows; most GUI code requires no changes.

- [ ] Install Qt6 via Qt Online Installer or `winget install Qt.Qt.6`
  (or in MSYS2: `pacman -S mingw-w64-x86_64-qt6-base`)
- [ ] Verify Qt6 CMake integration resolves on Windows
- [ ] Qt6 natively provides:
  - Native Win32 window chrome and file dialogs
  - Dark mode follows Windows theme
  - `QStandardPaths::AppDataLocation` returns correct `%APPDATA%\pathmux\` path
- [ ] Build and launch `pathmux-gui` on Windows 10/11

### Distribution
- [ ] Evaluate installer: NSIS (open source), WiX (Microsoft), or Qt Installer Framework
- [ ] Bundle or document ffmpeg and ExifTool prerequisites with their licenses
- [ ] Add Start Menu shortcut and `.quadeye` file association (future archive format)
- [ ] Code signing: self-signed triggers SmartScreen warnings; EV cert needed for clean distribution

---

## Open Questions

- **ExifTool bundling:** Standalone `.exe` is Perl compiled to Win32. Verify redistribution
  is permitted under Perl Artistic License before bundling in installer.
- **Manifest colocated write:** `pm_manifest_<id>.json` written alongside footage.
  Verify write permission detection works on NTFS (different model from POSIX `access()`).
- **Drive-letter paths in manifests:** Verify `std::filesystem` comparison and
  manifest index lookups handle `C:\...` paths without false mismatches.
- **Code signing cost:** EV certificates run $200–400/year. Decision deferred until
  distribution is planned.

---

*Last updated: 2026-03-01*
*Status: Planning — no Windows build attempted yet*

<!-- SN: 00081 -->
