# PathMux v1.1.0 — Windows & macOS Build + Package Checklist

Follow these steps in order on each platform. The source is already
cross-platform — these are packaging/deployment steps only.

**Status (2026-04-04):**
- ✅ Linux packages built (RPM, DEB, tar.gz)
- ✅ macOS packages built (tar.gz, zip) — Qt 6.11.0 via Homebrew on penny
- ❌ Windows packages — still needed (run on nutball1)
- ❌ GitHub release — waiting on Windows packages

---

## WINDOWS (nutball1 — Windows NVMe)

### Prerequisites (one-time)
- Qt 6.10.2 MinGW via Qt Online Installer — MinGW 13.1.0 64-bit component
  - Installed at: `C:\Qt\6.10.2\mingw_64\` and `C:\Qt\Tools\mingw1310_64\`
  - `run-build.ps1` is configured for these paths; adjust if Qt version differs
- WiX 6 (for MSI): `dotnet tool install --global wix`
- CMake: bundled with Qt at `C:\Qt\Tools\CMake_64\bin\cmake.exe`
- Ninja: bundled with Qt at `C:\Qt\Tools\Ninja\ninja.exe`

### Build & Package
```powershell
cd C:\Users\iceberg\Nutball-Labs\pathmux
.\run-build.ps1
```

What `run-build.ps1` does:
1. Loads MSVC environment (`vcvars64.bat`)
2. `cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release`
3. `cmake --build build-win`
4. **`windeployqt --release --no-translations build-win\pathmux-gui.exe`**
   — copies Qt6 DLLs, platform plugin, etc. next to the exe
5. `cmake --build build-win --target package` → `packages\pathmux-1.1.0-win64.zip`
6. `cmake --build build-win --target msi`    → `packages\pathmux-1.1.0-win64.msi`

### Verify
- Double-click `pathmux-gui.exe` from `build-win\` — should launch without needing Qt installed
- Check taskbar: icon should appear (embedded via `gui/resources/pathmux.rc`)
- Test `pathmux --version` from a new CMD window → `1.1.0`
- Install `pathmux-1.1.0-win64.msi`, verify both CLI tools and GUI install to `Program Files\PathMux\`

### Upload packages
```
packages\pathmux-1.1.0-win64.zip
packages\pathmux-1.1.0-win64.msi
```

---

## macOS

### Prerequisites (one-time)
- Xcode Command Line Tools: `xcode-select --install`
- Qt 6.6.x: `brew install qt6`  (or Qt Online Installer)
- cmake: `brew install cmake`

### Build
```bash
cd ~/path/to/pathmux
cmake -B build-mac -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac
```

### Deploy Qt frameworks into the app bundle
```bash
# macdeployqt bundles Qt frameworks so the .app runs without Qt installed.
# Path may vary — check `brew --prefix qt6`
$(brew --prefix qt6)/bin/macdeployqt build-mac/pathmux-gui.app -verbose=1
```

### (Optional) App icon
The bundle's `Info.plist` references `pathmux.icns`. To generate it from the
existing PNGs on a Mac:
```bash
mkdir pathmux.iconset
for sz in 16 32 64 128 256 512; do
    cp gui/resources/pathmux_${sz}.png pathmux.iconset/icon_${sz}x${sz}.png
done
iconutil -c icns pathmux.iconset -o build-mac/pathmux-gui.app/Contents/Resources/pathmux.icns
```

### Package
```bash
# TGZ + ZIP (CPack)
cmake --build build-mac --target package
# Output: packages/pathmux-1.1.0-macOS.tar.gz
#         packages/pathmux-1.1.0-macOS.zip
```

### Verify
- `open build-mac/pathmux-gui.app` — should launch, show icon in Dock
- `build-mac/pathmux --version` → `1.1.0`
- Confirm Dock icon renders correctly at multiple sizes

### Upload packages
```
packages/pathmux-1.1.0-macOS.tar.gz
packages/pathmux-1.1.0-macOS.zip
```

---

## GitHub Release (do this after both platforms are packaged)

Linux and macOS packages are built and in `packages/`. Once Windows is done, gather all seven files:

```
packages/pathmux-1.1.0-1.x86_64.rpm
packages/pathmux_1.1.0_amd64.deb
packages/pathmux-1.1.0-Linux.tar.gz
packages/pathmux-1.1.0-win64.msi
packages/pathmux-1.1.0-win64.zip
packages/pathmux-1.1.0-macOS.tar.gz
packages/pathmux-1.1.0-macOS.zip
```

Create the release from penny or nutball1 (SSH key needed):

```bash
gh release create v1.1.0 \
    packages/pathmux-1.1.0-1.x86_64.rpm \
    packages/pathmux_1.1.0_amd64.deb \
    packages/pathmux-1.1.0-Linux.tar.gz \
    packages/pathmux-1.1.0-win64.msi \
    packages/pathmux-1.1.0-win64.zip \
    packages/pathmux-1.1.0-macOS.tar.gz \
    packages/pathmux-1.1.0-macOS.zip \
    --repo Nutball-Labs/PathMux \
    --title "PathMux v1.1.0" \
    --notes "Qt6 GUI first release. Includes pathmux-gui for Linux, Windows, and macOS." \
    --latest
```

Or use the GitHub web UI: Releases → Draft new release → tag v1.1.0 → upload all files.

---

## Notes

- `windeployqt` version in `run-build.ps1` is hardcoded to Qt 6.6.2 path —
  adjust `$windeployqt` if you've upgraded Qt.
- macOS `.icns` file is optional; the Dock icon will still render from the PNG
  resources embedded via Qt, but Finder and Mission Control look better with `.icns`.
- The MSI `C_pathmux_gui` component assumes `windeployqt` has already been run
  and all Qt DLLs are present in `build-win\`. Qt DLL harvesting into the MSI
  is a future improvement — for now the ZIP is the canonical Windows distribution.
<!-- SN: 00092 -->
