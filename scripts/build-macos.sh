#!/usr/bin/env bash
# build-macos.sh — Full compile for CamClops on macOS (Homebrew Qt6)
# Run directly from any directory; locates project root relative to this script.
#
# Usage:
#   ./scripts/build-macos.sh           — configure + build
#   ./scripts/build-macos.sh --clean   — wipe build dir first, then configure + build
#
# Build dir: <project-root>/build-mac
# Requires:  Xcode Command Line Tools, cmake, Qt6 via Homebrew
#   xcode-select --install
#   brew install cmake qt               # monolithic Qt6 (includes multimedia)
#   — or modular install —
#   brew install cmake qtbase qttools qtmultimedia
#
# camclops-tl (timelapse editor) requires Qt6 Multimedia.  If it is skipped
# during configure, ensure qtmultimedia is installed and re-run with --clean.

set -euo pipefail
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$PROJ/build-mac"

# Locate Qt6 via Homebrew.  Homebrew Qt6 comes in two forms:
#   Monolithic: brew install qt           → brew --prefix qt
#   Modular:    brew install qtbase ...   → brew --prefix qtbase
# Try all known formula names.
if command -v brew &>/dev/null; then
    QT_PREFIX="$(brew --prefix qt 2>/dev/null \
              || brew --prefix qtbase 2>/dev/null \
              || brew --prefix qt6 2>/dev/null \
              || brew --prefix qt@6 2>/dev/null \
              || true)"
fi
if [[ -z "${QT_PREFIX:-}" ]]; then
    echo "WARNING: Qt6 not found via Homebrew — cmake will search system paths."
    echo "         If the build fails, run: brew install qt"
    echo "         For camclops-tl (timelapse editor), also: brew install qtmultimedia"
    QT_PREFIX=""
fi

# ── Version banner ────────────────────────────────────────────────────────────
VHP="$PROJ/lib/version.hpp"
MAJOR=$(grep -m1 '#define VERSION_MAJOR'  "$VHP" | awk '{print $3}' | tr -d '\r')
MINOR=$(grep -m1 '#define VERSION_MINOR'  "$VHP" | awk '{print $3}' | tr -d '\r')
PATCH=$(grep -m1 '#define VERSION_PATCH'  "$VHP" | awk '{print $3}' | tr -d '\r')
SUFFIX=$(grep -m1 '#define VERSION_SUFFIX' "$VHP" | sed 's/.*"\([^"]*\)".*/\1/' | tr -d '\r' || true)
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

# Always run cmake configure so that newly-installed Qt components (e.g.
# QtMultimedia for camclops-tl) are detected even when build-mac already exists.
echo "--- Configuring (build-mac) ---"
if [[ -n "$QT_PREFIX" ]]; then
    echo "    Qt6 prefix: $QT_PREFIX"
fi
cmake -S "$PROJ" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    ${QT_PREFIX:+-DCMAKE_PREFIX_PATH="$QT_PREFIX"}
echo ""

echo "--- Building ---"
# Pre-clean AppleDouble ._* sidecars left by macdeployqt on previous NFS builds.
# These confuse macdeployqt's otool pass and cause spurious codesign failures.
[[ -d "$BUILD" ]] && find "$BUILD" -name "._*" -delete 2>/dev/null || true
cmake --build "$BUILD" --parallel "$(sysctl -n hw.logicalcpu)"

echo "--- Removing AppleDouble sidecars (post-build) ---"
find "$BUILD" -name "._*" -delete 2>/dev/null || true

# macOS creates ._* AppleDouble sidecar files on any NFS mount (unavoidable —
# it's how macOS stores resource-fork metadata on non-APFS filesystems).
# macdeployqt copies Qt frameworks to the .app bundle on the NFS share, which
# immediately spawns ._* files that confuse codesign.  dot_clean can't help
# here because it requires listxattr which NFS denies.
#
# Fix: delete ._* files with find (no xattr needed), then re-sign ad-hoc.
# Ad-hoc signing (identity "-") is sufficient for local use and packaging.
for APP in "$BUILD/camclops-gui.app" "$BUILD/camclops-tl.app"; do
    [[ -d "$APP" ]] || continue
    echo "--- Removing AppleDouble sidecars: $(basename "$APP") ---"
    find "$APP" -name "._*" -delete

    # Sign inside-out: dylibs → frameworks → app.
    # --deep fails when macdeployqt left a framework in a partial state (NFS race),
    # so we sign components individually first.
    echo "--- Ad-hoc signing: $(basename "$APP") ---"
    find "$APP/Contents" \( -name "*.dylib" -o -name "*.so" \) \
        -exec codesign --force --sign - {} \; 2>/dev/null || true
    find "$APP/Contents/Frameworks" -maxdepth 1 -name "*.framework" \
        -exec codesign --force --sign - {} \; 2>/dev/null || true
    codesign --force --sign - "$APP" 2>/dev/null \
        && echo "    Signed OK." \
        || echo "    NOTE: $(basename "$APP") unsigned — right-click > Open on first launch."
done

echo ""
echo "Done. Binaries in $BUILD/"

# SN: 00112
