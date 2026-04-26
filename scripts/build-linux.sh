#!/usr/bin/env bash
# build-linux.sh — Full compile for PathMux on Linux (Alma/RHEL/Fedora)
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

if [[ "${1:-}" == "--clean" ]]; then
    echo "--- Cleaning build directory ---"
    rm -rf "$BUILD"
fi

if [[ ! -d "$BUILD" ]]; then
    echo "--- Configuring (build-linux) ---"
    cmake -S "$PROJ" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
    echo ""
fi

echo "--- Building ---"
cmake --build "$BUILD" --parallel "$(nproc)"

echo ""
echo "Done. Binaries in $BUILD/"

# SN: 00095
