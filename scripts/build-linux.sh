#!/usr/bin/env bash
# build-linux.sh — Full compile for CamClops on Linux (Alma/RHEL/Fedora)
# Run directly from any directory; locates project root relative to this script.
#
# Usage:
#   ./scripts/build-linux.sh           — configure (if needed) + build
#   ./scripts/build-linux.sh --clean   — wipe build dir first, then configure + build
#
# Build dir: <project-root>/build-linux
# Requires:  cmake, g++ (C++17), Qt6 dev packages (for GUI target)
#   sudo dnf install cmake gcc-c++ qt6-qtbase-devel

set -euo pipefail
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$PROJ/build-linux"

# ── Version banner ────────────────────────────────────────────────────────────
VHP="$PROJ/lib/version.hpp"
MAJOR=$(grep -m1 '#define VERSION_MAJOR'  "$VHP" | awk '{print $3}' | tr -d '\r')
MINOR=$(grep -m1 '#define VERSION_MINOR'  "$VHP" | awk '{print $3}' | tr -d '\r')
PATCH=$(grep -m1 '#define VERSION_PATCH'  "$VHP" | awk '{print $3}' | tr -d '\r')
SUFFIX=$(grep -m1 '#define VERSION_SUFFIX' "$VHP" | grep -oP '(?<=")[^"]*' | tr -d '\r' || true)
VERSION="${MAJOR}.${MINOR}.${PATCH}${SUFFIX}"
INNER="  Building CamClops version ${VERSION}  "
BORDER=$(printf '%*s' $(( ${#INNER} + 2 )) | tr ' ' '*')
echo "$BORDER"
echo "*${INNER}*"
echo "$BORDER"
echo ""

if [[ "${1:-}" == "--clean" ]]; then
    echo "--- Cleaning build directory ---"
    rm -rf "$BUILD"
fi

if [[ ! -d "$BUILD" ]]; then
    echo "--- Configuring (build-linux) ---"
    cmake -S "$PROJ" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
    echo ""
elif [[ -f "$BUILD/CMakeCache.txt" ]]; then
    CACHED=$(grep -s 'CAMCLOPS_VERSION:STRING=' "$BUILD/CMakeCache.txt" | cut -d= -f2 | tr -d '\r' || true)
    if [[ "$CACHED" != "$VERSION" ]]; then
        echo "--- Version changed ($CACHED → $VERSION), reconfiguring ---"
        cmake -S "$PROJ" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
        echo ""
    fi
fi

echo "--- Building ---"
cmake --build "$BUILD" --parallel "$(nproc)"

echo ""
echo "Done. Binaries in $BUILD/"

# SN: 00117
