#!/usr/bin/env bash
# package-macos.sh — Produce macOS .pkg installer and TGZ/ZIP archives via CPack/pkgbuild
# Assumes build-macos.sh has already been run successfully.
#
# Usage:
#   ./scripts/package-macos.sh
#
# Output: packages/ at project root
#   pathmux-X.Y.Z-macOS.pkg      (pkgbuild installer)
#   pathmux-X.Y.Z-macOS.tar.gz   (CPack TGZ)
#   pathmux-X.Y.Z-macOS.zip      (CPack ZIP)
#
# Requires: Xcode Command Line Tools (pkgbuild), cmake

set -euo pipefail
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$PROJ/build-mac"

# Remove AppleDouble ._* sidecars before packaging.  dot_clean fails on NFS
# (requires listxattr); use find instead.  Also fix any directory permissions
# that macdeployqt set incorrectly on the NFS mount (manifests as CPack
# "Permission denied" on Versions/A/Resources inside Qt framework bundles).
for APP in "$BUILD/pathmux-gui.app" "$BUILD/pathmux-tl.app"; do
    [[ -d "$APP" ]] || continue
    find "$APP" -name "._*" -delete
    chmod -R a+rX "$APP"
done

echo "=== Packaging TGZ + ZIP (CPack) ==="
(cd "$BUILD" && cpack)

echo ""
echo "=== Packaging .pkg (pkgbuild) ==="
cmake --build "$BUILD" --target pkg

echo ""
echo "Packages:"
ls -lh "$PROJ/packages"/pathmux-*macOS* 2>/dev/null \
    | awk '{print "  "$NF, "("$5")"}'

# SN: 00106
