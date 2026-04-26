#!/usr/bin/env bash
# release.sh — Upload PathMux packages to a GitHub release
# Run after the relevant package-*.sh / package-*.ps1 scripts have produced packages.
#
# Usage: ./scripts/release.sh --linux | --mac | --win | --all
#
# Requires: gh CLI authenticated (gh auth login)
#           Packages in packages/ at project root

set -euo pipefail

REPO="Nutball-Labs/PathMux"
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
PKG_DIR="$PROJ/packages"
VHP="$PROJ/lib/version.hpp"

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
usage() {
    echo "Usage: $(basename "$0") --linux | --mac | --win | --all"
    echo ""
    echo "  --linux   Upload Linux packages (RPM, DEB, TGZ)"
    echo "  --mac     Upload macOS packages (PKG, TGZ, ZIP)"
    echo "  --win     Upload Windows packages (ZIP, MSI)"
    echo "  --all     Upload all three platforms"
    exit 1
}

[[ $# -eq 0 ]] && usage

DO_LINUX=false; DO_MAC=false; DO_WIN=false
case "$1" in
    --linux) DO_LINUX=true ;;
    --mac)   DO_MAC=true   ;;
    --win)   DO_WIN=true   ;;
    --all)   DO_LINUX=true; DO_MAC=true; DO_WIN=true ;;
    *)       usage ;;
esac

# ---------------------------------------------------------------------------
# Parse current version from lib/version.hpp
# ---------------------------------------------------------------------------
MAJOR=$(grep -m1 '#define VERSION_MAJOR' "$VHP" | awk '{print $3}')
MINOR=$(grep -m1 '#define VERSION_MINOR' "$VHP" | awk '{print $3}')
PATCH=$(grep -m1 '#define VERSION_PATCH' "$VHP" | awk '{print $3}')
SUFFIX=$(grep -m1 '#define VERSION_SUFFIX' "$VHP" | grep -oP '(?<=")[^"]*' || true)
CURRENT="${MAJOR}.${MINOR}.${PATCH}${SUFFIX}"

echo "PathMux release uploader"
echo "Current version in lib/version.hpp: v${CURRENT}"
echo ""

# ---------------------------------------------------------------------------
# Ask: use current version or specify a different one?
# ---------------------------------------------------------------------------
read -rp "Use v${CURRENT} as the release tag? [Y/n] " ans
if [[ "${ans,,}" == "n" ]]; then
    read -rp "Enter version to release (e.g. 1.2.1): " VERSION
    VERSION="${VERSION#v}"
else
    VERSION="$CURRENT"
fi
TAG="v${VERSION}"

# ---------------------------------------------------------------------------
# Ask: set as latest release?
# ---------------------------------------------------------------------------
read -rp "Mark $TAG as the latest release? [Y/n] " ans_latest
MARK_LATEST=true
[[ "${ans_latest,,}" == "n" ]] && MARK_LATEST=false

# ---------------------------------------------------------------------------
# Collect packages for selected platforms
# ---------------------------------------------------------------------------
PKGS=()

if $DO_LINUX; then
    mapfile -t _linux < <(find "$PKG_DIR" -maxdepth 1 \( \
            -name "pathmux-${VERSION}-*.rpm"        \
        -o  -name "pathmux_${VERSION}_*.deb"        \
        -o  -name "pathmux-${VERSION}-Linux.tar.gz" \
        \) | sort)
    PKGS+=("${_linux[@]}")
fi

if $DO_MAC; then
    mapfile -t _mac < <(find "$PKG_DIR" -maxdepth 1 \( \
            -name "pathmux-${VERSION}-macOS.pkg"    \
        -o  -name "pathmux-${VERSION}-macOS.tar.gz" \
        -o  -name "pathmux-${VERSION}-macOS.zip"    \
        \) | sort)
    PKGS+=("${_mac[@]}")
fi

if $DO_WIN; then
    mapfile -t _win < <(find "$PKG_DIR" -maxdepth 1 \
            -name "pathmux-${VERSION}-win64.*"      \
        | grep -v '\.wixpdb$' | sort)
    PKGS+=("${_win[@]}")
fi

if [[ ${#PKGS[@]} -eq 0 ]]; then
    echo ""
    echo "ERROR: No packages found for version ${VERSION} in ${PKG_DIR}/"
    echo "Run the appropriate package-*.sh / package-*.ps1 script(s) first."
    exit 1
fi

echo ""
echo "Packages to upload:"
for p in "${PKGS[@]}"; do printf "  %s\n" "$(basename "$p")"; done
echo ""

# ---------------------------------------------------------------------------
# Create or update release
# ---------------------------------------------------------------------------
if gh release view "$TAG" --repo "$REPO" &>/dev/null; then
    echo "Release $TAG already exists — uploading assets (--clobber replaces duplicates)."
    gh release upload "$TAG" "${PKGS[@]}" --repo "$REPO" --clobber
    if $MARK_LATEST; then
        gh release edit "$TAG" --latest --repo "$REPO"
        echo "Marked $TAG as latest."
    fi
else
    echo "Creating release $TAG ..."
    LATEST_ARG=(); $MARK_LATEST && LATEST_ARG=(--latest)
    gh release create "$TAG" "${PKGS[@]}" \
        --repo "$REPO" \
        --title "PathMux ${TAG}" \
        --generate-notes \
        "${LATEST_ARG[@]}"
fi

echo ""
echo "Done. https://github.com/${REPO}/releases/tag/${TAG}"

# SN: 00095
