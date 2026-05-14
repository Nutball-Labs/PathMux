#!/usr/bin/env bash
# package-macos.sh — Produce macOS .pkg installer and TGZ/ZIP archives via CPack/pkgbuild
# Assumes build-macos.sh has already been run successfully.
#
# Usage:
#   ./scripts/package-macos.sh
#
# Output: packages/ at project root
#   camclops-X.Y.Z-macOS.pkg      (pkgbuild installer)
#   camclops-X.Y.Z-macOS.tar.gz   (CPack TGZ)
#   camclops-X.Y.Z-macOS.zip      (CPack ZIP)
#
# Requires: Xcode Command Line Tools (pkgbuild), cmake

set -euo pipefail
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$PROJ/build-mac"

VHP="$PROJ/lib/version.hpp"
MAJOR=$(grep -m1 '#define VERSION_MAJOR'  "$VHP" | awk '{print $3}' | tr -d '\r')
MINOR=$(grep -m1 '#define VERSION_MINOR'  "$VHP" | awk '{print $3}' | tr -d '\r')
PATCH=$(grep -m1 '#define VERSION_PATCH'  "$VHP" | awk '{print $3}' | tr -d '\r')
SUFFIX=$(grep -m1 '#define VERSION_SUFFIX' "$VHP" | sed 's/.*"\([^"]*\)".*/\1/' | tr -d '\r' || true)
VERSION="${MAJOR}.${MINOR}.${PATCH}${SUFFIX}"
INNER="  Packaging CamClops version ${VERSION}  —  macOS  "
BORDER=$(printf '%*s' $(( ${#INNER} + 2 )) | tr ' ' '*')
echo "$BORDER"
echo "*${INNER}*"
echo "$BORDER"
echo ""

# Remove AppleDouble ._* sidecars before packaging.  dot_clean fails on NFS
# (requires listxattr); use find instead.  Also fix any directory permissions
# that macdeployqt set incorrectly on the NFS mount (manifests as CPack
# "Permission denied" on Versions/A/Resources inside Qt framework bundles).
for APP in "$BUILD/camclops-gui.app" "$BUILD/camclops-tl.app"; do
    [[ -d "$APP" ]] || continue
    find "$APP" -name "._*" -delete
    chmod -R a+rX "$APP"
done

if [[ -f "$BUILD/CMakeCache.txt" ]]; then
    CACHED=$(grep -s 'CAMCLOPS_VERSION:STRING=' "$BUILD/CMakeCache.txt" | cut -d= -f2 | tr -d '\r' || true)
    if [[ "$CACHED" != "$VERSION" ]]; then
        echo "--- Version changed ($CACHED → $VERSION), reconfiguring ---"
        cmake -S "$PROJ" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
        echo ""
    fi
fi

echo "=== Packaging TGZ + ZIP (CPack) ==="
(cd "$BUILD" && cpack)

echo ""
echo "=== Packaging .pkg (pkgbuild) ==="
cmake --build "$BUILD" --target pkg

echo ""
echo "Packages:"
ls -lh "$PROJ/packages"/camclops-*macOS* 2>/dev/null \
    | awk '{print "  "$NF, "("$5")"}'

# SN: 00117
