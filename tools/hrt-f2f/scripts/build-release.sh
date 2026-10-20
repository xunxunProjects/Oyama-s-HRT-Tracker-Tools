#!/usr/bin/env bash
# Build the four released hrt-f2f binaries, test the module, and package the
# tar.xz with SHA-512 and MD5 hashes.
#
#   tools/hrt-f2f/scripts/build-release.sh [VERSION] [OUTPUT_DIR]
#
# Defaults: VERSION=0.1.0, OUTPUT_DIR=<repo>/dist
#
# The Go CLI is pure Go, so cross-compilation uses GOOS/GOARCH with
# CGO_ENABLED=0.
set -euo pipefail

VERSION="${1:-0.1.0}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
TOOL_DIR="$ROOT/tools/hrt-f2f"
OUT_ROOT="${2:-$ROOT/dist}"
PKG_NAME="hrt-f2f-$VERSION"
PKG_DIR="$OUT_ROOT/$PKG_NAME"
ARCHIVE="$OUT_ROOT/$PKG_NAME-multiarch.tar.xz"

for tool in go tar xz sha512sum md5sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-release: missing required tool: $tool" >&2
        exit 1
    }
done

echo "build-release: version $VERSION"
echo "build-release: tool dir $TOOL_DIR"
echo "build-release: output   $OUT_ROOT"

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/bin" "$OUT_ROOT"

echo "build-release: running Go tests"
(
    cd "$TOOL_DIR"
    go test -count=1 ./...
)

# Linux first, then Windows; 64-bit before 32-bit.
TARGETS=(
    "linux amd64"
    "windows amd64"
    "linux 386"
    "windows 386"
)

for spec in "${TARGETS[@]}"; do
    os="${spec% *}"
    arch="${spec#* }"
    name="hrt-f2f-$os-$arch"
    if [ "$os" = "windows" ]; then
        name="$name.exe"
    fi
    echo "build-release: building $os/$arch -> bin/$name"
    (
        cd "$TOOL_DIR"
        CGO_ENABLED=0 GOOS="$os" GOARCH="$arch" \
            go build -trimpath -buildvcs=false \
            -ldflags "-s -w -X main.buildVersion=$VERSION" \
            -o "$PKG_DIR/bin/$name" .
    )
done

cp "$TOOL_DIR/README.md" "$PKG_DIR/README.md"
cp "$TOOL_DIR/THIRD_PARTY_NOTICES.md" "$PKG_DIR/THIRD_PARTY_NOTICES.md"
cp "$ROOT/LICENSE" "$PKG_DIR/LICENSE"

{
    echo "name: hrt-f2f"
    echo "version: $VERSION"
    echo "commit: $(git -C "$ROOT" rev-parse HEAD)"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "go: $(go version)"
    echo "targets:"
    printf '  %s\n' "${TARGETS[@]}"
    echo "note: binaries are built with CGO_ENABLED=0 and need no runtime toolchain"
} >"$PKG_DIR/BUILD_INFO.txt"

(
    cd "$PKG_DIR"
    sha512sum bin/* >SHA512SUMS
    md5sum bin/* >MD5SUMS
)

echo "build-release: creating $ARCHIVE"
tar -C "$OUT_ROOT" -cJf "$ARCHIVE" "$PKG_NAME"

(
    cd "$OUT_ROOT"
    sha512sum "$(basename "$ARCHIVE")" >"$(basename "$ARCHIVE").sha512"
    md5sum "$(basename "$ARCHIVE")" >"$(basename "$ARCHIVE").md5"
)

echo
echo "build-release: done"
echo "  archive: $ARCHIVE"
echo "  sha512:  $ARCHIVE.sha512"
echo "  md5:     $ARCHIVE.md5"
echo
cat "$ARCHIVE.sha512"
cat "$ARCHIVE.md5"
