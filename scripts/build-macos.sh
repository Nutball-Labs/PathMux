#!/usr/bin/env bash
# build-macos.sh — Full compile for PathMux on macOS (Homebrew Qt6)
# Run directly from any directory; locates project root relative to this script.
#
# Usage:
#   ./scripts/build-macos.sh           — configure (if needed) + build
#   ./scripts/build-macos.sh --clean   — wipe build dir first, then configure + build
#
# Build dir: <project-root>/build-mac
# Requires:  Xcode Command Line Tools, cmake, Qt6 via Homebrew
#   xcode-select --install
#   brew install cmake qt6

set -euo pipefail
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$PROJ/build-mac"

# Locate Qt6 via Homebrew (handles Apple Silicon and Intel paths)
if command -v brew &>/dev/null; then
    QT_PREFIX="$(brew --prefix qt6 2>/dev/null || brew --prefix qt@6 2>/dev/null || true)"
fi
if [[ -z "${QT_PREFIX:-}" ]]; then
    QT_PREFIX="/usr/local/opt/qt6"
fi

if [[ "${1:-}" == "--clean" ]]; then
    echo "--- Cleaning build directory ---"
    rm -rf "$BUILD"
fi

if [[ ! -d "$BUILD" ]]; then
    echo "--- Configuring (build-mac) ---"
    echo "    Qt6 prefix: $QT_PREFIX"
    cmake -S "$PROJ" -B "$BUILD" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_PREFIX"
    echo ""
fi

echo "--- Building ---"
cmake --build "$BUILD" --parallel "$(sysctl -n hw.logicalcpu)"

# Remove AppleDouble ._* sidecar files that macOS creates on NFS/SMB mounts.
# macdeployqt trips on them (treats them as dylibs), breaking codesign.
APP="$BUILD/pathmux-gui.app"
if [[ -d "$APP" ]]; then
    echo "--- Cleaning AppleDouble sidecars from .app bundle ---"
    dot_clean -m "$APP"
fi

echo ""
echo "Done. Binaries in $BUILD/"

# SN: 00095
