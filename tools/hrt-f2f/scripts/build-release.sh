#!/usr/bin/env bash
# Build the merged hrt-tool release for the host Linux target and package it
# with SHA-512 and MD5 hashes.
#
#   tools/hrt-f2f/scripts/build-release.sh [VERSION] [OUTPUT_DIR]
#
# The F2F implementation is linked as a Go c-archive, so this script builds a
# native host binary rather than cross-compiling the old CGO_ENABLED=0
# hrt-f2f CLI. Windows and 32-bit targets need a separate toolchain and are
# not produced here.
set -euo pipefail

VERSION="${1:-0.1.0}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT_ROOT="${2:-$ROOT/dist}"
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
MACHINE="$(uname -m)"
case "$MACHINE" in
    x86_64) ARCH="amd64" ;;
    aarch64|arm64) ARCH="arm64" ;;
    i386|i686) ARCH="386" ;;
    *) ARCH="$MACHINE" ;;
esac
PKG_NAME="hrt-tool-$VERSION-$OS-$ARCH"
PKG_DIR="$OUT_ROOT/$PKG_NAME"
ARCHIVE="$OUT_ROOT/$PKG_NAME.tar.xz"
BUILD_DIR="$OUT_ROOT/.hrt-tool-build-$VERSION"

for tool in cmake go cargo ctest tar xz sha512sum md5sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-release: missing required tool: $tool" >&2
        exit 1
    }
done

if [ "$OS" != "linux" ]; then
    echo "build-release: the merged hrt-tool release is built for Linux only" >&2
    exit 1
fi

echo "build-release: version $VERSION"
echo "build-release: target  $OS/$ARCH"
echo "build-release: output  $OUT_ROOT"

rm -rf "$PKG_DIR" "$BUILD_DIR"
mkdir -p "$OUT_ROOT"

echo "build-release: running Go F2F tests"
(
    cd "$ROOT/tools/hrt-f2f"
    go test -count=1 ./...
)

echo "build-release: configuring and building hrt-tool"
cmake -S "$ROOT/tools/hrt-tool" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PKG_DIR" >/dev/null
cmake --build "$BUILD_DIR" --parallel

echo "build-release: running C/F2F tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "build-release: installing package"
cmake --install "$BUILD_DIR" >/dev/null
cp "$ROOT/LICENSE" "$PKG_DIR/LICENSE"
cp "$ROOT/tools/hrt-f2f/THIRD_PARTY_NOTICES.md" \
    "$PKG_DIR/THIRD_PARTY_NOTICES.md"

{
    echo "name: hrt-tool"
    echo "version: $VERSION"
    echo "commit: $(git -C "$ROOT" rev-parse HEAD)"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "go: $(go version)"
    echo "target: $OS/$ARCH"
    echo "note: F2F is linked as a Go c-archive; the host needs no Go toolchain"
} >"$PKG_DIR/BUILD_INFO.txt"

(
    cd "$PKG_DIR"
    sha512sum bin/hrt-tool >SHA512SUMS
    md5sum bin/hrt-tool >MD5SUMS
)

rm -rf "$BUILD_DIR"
echo "build-release: creating $ARCHIVE"
tar -C "$OUT_ROOT" -cJf "$ARCHIVE" "$PKG_NAME"

(
    cd "$OUT_ROOT"
    sha512sum "$(basename "$ARCHIVE")" >"$(basename "$ARCHIVE").sha512"
    md5sum "$(basename "$ARCHIVE")" >"$(basename "$ARCHIVE").md5"
)

echo
echo "build-release: done"
echo "  package: $PKG_DIR"
echo "  archive: $ARCHIVE"
echo "  sha512:  $ARCHIVE.sha512"
echo "  md5:     $ARCHIVE.md5"
