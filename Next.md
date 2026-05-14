# CamClops — Windows & macOS Build + Package Process Reference

> **Note:** Version numbers in this document reflect the v1.1.0 build session (2026-04-04).
> For current builds, see `scripts/build-linux.sh`, `scripts/build-macos.sh`, and
> `scripts/build-windows.ps1` which handle version-aware packaging automatically.
> Current version: **v1.9.10a**.

Follow these steps in order on each platform. The source is already
cross-platform — these are packaging/deployment steps only.

**Status (2026-05-14, v2.0.0 pending):** Builds running on all three platforms.
Version bump (VERSION_MAJOR=2, VERSION_MINOR=0, VERSION_PATCH=0) pending clean
build verification; packages to follow.

**Status (2026-04-04, v1.1.0 reference build):**
- ✅ Linux packages built (RPM, DEB, tar.gz)
- ✅ macOS packages built (tar.gz, zip) — Qt 6.11.0 via Homebrew on penny
- ✅ Windows packages built — Qt 6.10.2 MinGW on nutball1 (not MSVC — see Notes)
- ✅ GitHub release v1.1.0 published — https://github.com/Nutball-Labs/CamClops/releases/tag/v1.1.0

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
cd C:\Users\iceberg\Nutball-Labs\camclops
.\run-build.ps1
```

What `run-build.ps1` does:
1. Loads MSVC environment (`vcvars64.bat`)
2. `cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release`
3. `cmake --build build-win`
4. **`windeployqt --release --no-translations build-win\camclops-gui.exe`**
   — copies Qt6 DLLs, platform plugin, etc. next to the exe
5. `cmake --build build-win --target package` → `packages\camclops-1.1.0-win64.zip`
6. `cmake --build build-win --target msi`    → `packages\camclops-1.1.0-win64.msi`

### Verify
- Double-click `camclops-gui.exe` from `build-win\` — should launch without needing Qt installed
- Check taskbar: icon should appear (embedded via `gui/resources/camclops.rc`)
- Test `camclops --version` from a new CMD window → `1.1.0`
- Install `camclops-1.1.0-win64.msi`, verify both CLI tools and GUI install to `Program Files\CamClops\`

### Upload packages
```
packages\camclops-1.1.0-win64.zip
packages\camclops-1.1.0-win64.msi
```

---

## macOS

### Prerequisites (one-time)
- Xcode Command Line Tools: `xcode-select --install`
- Qt 6.11.x: `brew install qt6`  (or Qt Online Installer)
- cmake: `brew install cmake`

### Build
```bash
cd ~/path/to/camclops
cmake -B build-mac -DCMAKE_BUILD_TYPE=Release
cmake --build build-mac
```

### Deploy Qt frameworks into the app bundle
```bash
# macdeployqt bundles Qt frameworks so the .app runs without Qt installed.
# Path may vary — check `brew --prefix qt6`
$(brew --prefix qt6)/bin/macdeployqt build-mac/camclops-gui.app -verbose=1
```

### (Optional) App icon
The bundle's `Info.plist` references `camclops.icns`. To generate it from the
existing PNGs on a Mac:
```bash
mkdir camclops.iconset
for sz in 16 32 64 128 256 512; do
    cp gui/resources/camclops_${sz}.png camclops.iconset/icon_${sz}x${sz}.png
done
iconutil -c icns camclops.iconset -o build-mac/camclops-gui.app/Contents/Resources/camclops.icns
```

### Package
```bash
# TGZ + ZIP (CPack)
cmake --build build-mac --target package
# Output: packages/camclops-1.1.0-macOS.tar.gz
#         packages/camclops-1.1.0-macOS.zip
```

### Verify
- `open build-mac/camclops-gui.app` — should launch, show icon in Dock
- `build-mac/camclops --version` → `1.1.0`
- Confirm Dock icon renders correctly at multiple sizes

### Upload packages
```
packages/camclops-1.1.0-macOS.tar.gz
packages/camclops-1.1.0-macOS.zip
```

---

## GitHub Release (do this after both platforms are packaged)

Linux and macOS packages are built and in `packages/`. Once Windows is done, gather all seven files:

```
packages/camclops-1.1.0-1.x86_64.rpm
packages/camclops_1.1.0_amd64.deb
packages/camclops-1.1.0-Linux.tar.gz
packages/camclops-1.1.0-win64.msi
packages/camclops-1.1.0-win64.zip
packages/camclops-1.1.0-macOS.tar.gz
packages/camclops-1.1.0-macOS.zip
```

Create the release from penny or nutball1 (SSH key needed):

```bash
gh release create v1.1.0 \
    packages/camclops-1.1.0-1.x86_64.rpm \
    packages/camclops_1.1.0_amd64.deb \
    packages/camclops-1.1.0-Linux.tar.gz \
    packages/camclops-1.1.0-win64.msi \
    packages/camclops-1.1.0-win64.zip \
    packages/camclops-1.1.0-macOS.tar.gz \
    packages/camclops-1.1.0-macOS.zip \
    --repo Nutball-Labs/CamClops \
    --title "CamClops v1.1.0" \
    --notes "Qt6 GUI first release. Includes camclops-gui for Linux, Windows, and macOS." \
    --latest
```

Or use the GitHub web UI: Releases → Draft new release → tag v1.1.0 → upload all files.

---

## Notes

- `windeployqt` version in `run-build.ps1` is hardcoded to Qt 6.6.2 path —
  adjust `$windeployqt` if you've upgraded Qt.
- macOS `.icns` file is optional; the Dock icon will still render from the PNG
  resources embedded via Qt, but Finder and Mission Control look better with `.icns`.
- The MSI `C_camclops_gui` component assumes `windeployqt` has already been run
  and all Qt DLLs are present in `build-win\`. Qt DLL harvesting into the MSI
  is a future improvement — for now the ZIP is the canonical Windows distribution.
<!-- SN: 00112 -->
