#!/usr/bin/env bash
# package-linux.sh — Produce RPM, DEB, TGZ, and SRPM packages
# Assumes build-linux.sh has already been run successfully.
#
# Usage:
#   ./scripts/package-linux.sh
#
# Output: packages/ at project root
#   camclops-X.Y.Z-1.x86_64.rpm
#   camclops_X.Y.Z_amd64.deb
#   camclops-X.Y.Z-Linux.tar.gz
#   camclops-X.Y.Z-1.src.rpm
#
# Requires: cmake, rpm-build (for RPM and SRPM), dpkg-deb (for DEB), git
#   sudo dnf install rpm-build git

set -euo pipefail
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$PROJ/build-linux"

VHP="$PROJ/lib/version.hpp"
MAJOR=$(grep -m1 '#define VERSION_MAJOR'  "$VHP" | awk '{print $3}' | tr -d '\r')
MINOR=$(grep -m1 '#define VERSION_MINOR'  "$VHP" | awk '{print $3}' | tr -d '\r')
PATCH=$(grep -m1 '#define VERSION_PATCH'  "$VHP" | awk '{print $3}' | tr -d '\r')
SUFFIX=$(grep -m1 '#define VERSION_SUFFIX' "$VHP" | grep -oP '(?<=")[^"]*' | tr -d '\r' || true)
VERSION="${MAJOR}.${MINOR}.${PATCH}${SUFFIX}"
INNER="  Packaging CamClops version ${VERSION}  —  Linux  "
BORDER=$(printf '%*s' $(( ${#INNER} + 2 )) | tr ' ' '*')
echo "$BORDER"
echo "*${INNER}*"
echo "$BORDER"
echo ""

if [[ -f "$BUILD/CMakeCache.txt" ]]; then
    CACHED=$(grep -s 'CAMCLOPS_VERSION:STRING=' "$BUILD/CMakeCache.txt" | cut -d= -f2 | tr -d '\r' || true)
    if [[ "$CACHED" != "$VERSION" ]]; then
        echo "--- Version changed ($CACHED → $VERSION), reconfiguring ---"
        cmake -S "$PROJ" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
        echo ""
    fi
fi

echo "=== Packaging (RPM, DEB, TGZ) ==="
echo "    Started: $(date '+%H:%M:%S')"
echo "    Build dir: $BUILD"
echo "    (RPM build can take several minutes — rpmbuild output appears below)"
echo ""
(cd "$BUILD" && cpack -V)
echo ""
echo "    Finished: $(date '+%H:%M:%S')"

echo ""
echo "=== Source RPM (SRPM) ==="
if command -v rpmbuild &>/dev/null && command -v git &>/dev/null; then
    SRPM_TMP="$(mktemp -d)"
    trap 'rm -rf "$SRPM_TMP"' EXIT

    # Build source tarball from git HEAD
    TARNAME="camclops-${VERSION}"
    TARBALL="$SRPM_TMP/${TARNAME}.tar.gz"
    echo "    Creating source tarball: ${TARNAME}.tar.gz"
    git -C "$PROJ" archive --format=tar.gz --prefix="${TARNAME}/" HEAD \
        > "$TARBALL"

    # Process the spec template
    SPECFILE="$SRPM_TMP/camclops.spec"
    CHANGELOG_DATE=$(date '+%a %b %d %Y')
    sed -e "s/@VERSION@/${VERSION}/g" \
        -e "s/@CHANGELOG_DATE@/${CHANGELOG_DATE}/g" \
        "$PROJ/packaging/camclops.spec.in" > "$SPECFILE"

    # Set up rpmbuild directory tree in temp
    mkdir -p "$SRPM_TMP"/{SOURCES,SPECS,SRPMS,BUILD,RPMS}
    cp "$TARBALL" "$SRPM_TMP/SOURCES/"

    rpmbuild -bs "$SPECFILE" \
        --define "_topdir $SRPM_TMP" \
        --define "_sourcedir $SRPM_TMP/SOURCES" \
        --define "_srcrpmdir $PROJ/packages"

    echo "    Finished: $(date '+%H:%M:%S')"
else
    echo "    Skipped (rpmbuild or git not found)"
fi

echo ""
echo "Packages:"
ls -lh "$PROJ/packages"/camclops-*.rpm \
        "$PROJ/packages"/camclops-*.src.rpm \
        "$PROJ/packages"/camclops_*.deb \
        "$PROJ/packages"/camclops-*-Linux.tar.gz 2>/dev/null \
    | awk '{print "  "$NF, "("$5")"}'

# SN: 00122
