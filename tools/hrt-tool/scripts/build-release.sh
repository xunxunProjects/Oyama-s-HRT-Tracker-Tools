#!/usr/bin/env bash
# Build hrt-tool release packages with SHA-512 and MD5 checksums.
#
#   tools/hrt-tool/scripts/build-release.sh [VERSION] [OUTPUT_DIR] [--target TARGET]
#
# TARGET is linux/amd64 (default), linux/386, windows/amd64, windows/386, or
# all. Cross targets require the toolchains and libraries described in the
# wiki Cross Compilation page. The script never creates .sig files; it prints
# the files the release owner should sign.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
VERSION=""
OUT_ROOT=""
TARGET=""

usage() {
    sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --target)
            [ $# -ge 2 ] || { echo "build-release: --target needs a value" >&2; exit 2; }
            TARGET="$2"
            shift 2
            ;;
        --target=*)
            TARGET="${1#*=}"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --*)
            echo "build-release: unknown option: $1" >&2
            exit 2
            ;;
        *)
            if [ -z "$VERSION" ]; then
                VERSION="$1"
            elif [ -z "$OUT_ROOT" ]; then
                OUT_ROOT="$1"
            else
                echo "build-release: unexpected argument: $1" >&2
                exit 2
            fi
            shift
            ;;
    esac
done

VERSION="${VERSION:-0.1.1}"
OUT_ROOT="${OUT_ROOT:-$ROOT/dist}"
TARGET="${TARGET:-linux/amd64}"

for tool in cmake go cargo rustc ctest tar xz sha512sum md5sum git; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-release: missing required tool: $tool" >&2
        exit 1
    }
done

if [ "$(uname -s)" != "Linux" ]; then
    echo "build-release: release builds require a Linux host" >&2
    exit 1
fi

mkdir -p "$OUT_ROOT"

# Print the target list when asked for all.
targets() {
    if [ "$1" = "all" ]; then
        printf '%s\n' linux/amd64 linux/386 windows/amd64 windows/386
    else
        printf '%s\n' "$1"
    fi
}

target_fields() {
    local target="$1"
    case "$target" in
        linux/amd64) OS=linux; ARCH=amd64 ;;
        linux/386) OS=linux; ARCH=386 ;;
        windows/amd64) OS=windows; ARCH=amd64 ;;
        windows/386) OS=windows; ARCH=386 ;;
        *)
            echo "build-release: unknown target: $target" >&2
            exit 2
            ;;
    esac
}

check_version_output() {
    local output="$1" label="$2"
    if ! printf '%s\n' "$output" | grep -q "hrt-tool $VERSION"; then
        echo "build-release: $label reports an unexpected version:" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi
}

build_target() {
    local target="$1"
    local os arch pkg_name pkg_dir archive build_dir toolchain=""
    local -a cmake_args=()
    local -a build_env=()
    local bin_name="hrt-tool"
    local ext=""

    target_fields "$target"
    os="$OS"
    arch="$ARCH"
    pkg_name="hrt-tool-$VERSION-$os-$arch"
    pkg_dir="$OUT_ROOT/$pkg_name"
    archive="$OUT_ROOT/$pkg_name.tar.xz"
    build_dir="$OUT_ROOT/.hrt-tool-build-$VERSION-$os-$arch"

    echo
    echo "build-release: $target"
    echo "build-release: version $VERSION"
    echo "build-release: output  $pkg_dir"

    rm -rf "$pkg_dir" "$build_dir"

    case "$target" in
        linux/amd64)
            ;;
        linux/386)
            toolchain="$ROOT/cmake/toolchains/linux-i686-multilib.cmake"
            cmake_args+=(
                -DHRT_RUST_TARGET=i686-unknown-linux-gnu
                -DHRT_GOOS=linux -DHRT_GOARCH=386
                -DHRT_GO_CGO_CFLAGS=-m32 -DHRT_GO_CGO_LDFLAGS=-m32
                -DBUILD_TESTING=OFF)
            build_env+=(RUSTFLAGS="-C link-arg=-m32")
            ;;
        windows/amd64)
            toolchain="$ROOT/cmake/toolchains/windows-amd64-mingw.cmake"
            cmake_args+=(
                -DHRT_RUST_TARGET=x86_64-pc-windows-gnu
                -DHRT_GOOS=windows -DHRT_GOARCH=amd64
                -DHRT_GO_CC=x86_64-w64-mingw32-gcc
                -DHRT_GO_CXX=x86_64-w64-mingw32-g++
                -DBUILD_TESTING=OFF)
            ext=".exe"
            ;;
        windows/386)
            toolchain="$ROOT/cmake/toolchains/windows-386-mingw.cmake"
            cmake_args+=(
                -DHRT_RUST_TARGET=i686-pc-windows-gnu
                -DHRT_GOOS=windows -DHRT_GOARCH=386
                -DHRT_GO_CC=i686-w64-mingw32-gcc
                -DHRT_GO_CXX=i686-w64-mingw32-g++
                -DBUILD_TESTING=OFF)
            ext=".exe"
            ;;
    esac
    if [ -n "$toolchain" ]; then
        cmake_args+=("-DCMAKE_TOOLCHAIN_FILE=$toolchain")
    fi

    if [ "$target" = "linux/amd64" ]; then
        echo "build-release: running Go F2F tests"
        (cd "$ROOT/tools/hrt-f2f" && go test -count=1 ./...)
    fi

    echo "build-release: configuring"
    if [ ${#cmake_args[@]} -gt 0 ]; then
        cmake -S "$ROOT/tools/hrt-tool" -B "$build_dir" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$pkg_dir" \
            -DHRT_TOOL_VERSION="$VERSION" \
            "${cmake_args[@]}" >/dev/null
    else
        cmake -S "$ROOT/tools/hrt-tool" -B "$build_dir" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$pkg_dir" \
            -DHRT_TOOL_VERSION="$VERSION" >/dev/null
    fi

    echo "build-release: building"
    if [ ${#build_env[@]} -gt 0 ]; then
        env "${build_env[@]}" cmake --build "$build_dir" --parallel
    else
        cmake --build "$build_dir" --parallel
    fi

    if [ "$target" = "linux/amd64" ]; then
        echo "build-release: running C/F2F tests"
        ctest --test-dir "$build_dir" --output-on-failure
    fi

    echo "build-release: installing"
    cmake --install "$build_dir" >/dev/null
    cp "$ROOT/LICENSE" "$pkg_dir/LICENSE"
    cp "$ROOT/tools/hrt-f2f/THIRD_PARTY_NOTICES.md" \
        "$pkg_dir/THIRD_PARTY_NOTICES.md"

    {
        echo "name: hrt-tool"
        echo "version: $VERSION"
        echo "target: $os/$arch"
        echo "commit: $(git -C "$ROOT" rev-parse HEAD)"
        echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "go: $(go version)"
        echo "rust: $(rustc --version)"
        echo "note: F2F is linked as a Go c-archive; the host needs no Go toolchain"
    } >"$pkg_dir/BUILD_INFO.txt"

    local binary="$pkg_dir/bin/$bin_name$ext"
    [ -f "$binary" ] || {
        echo "build-release: install did not produce $binary" >&2
        exit 1
    }

    echo "build-release: checking version"
    local output=""
    case "$os" in
        linux)
            if output="$("$binary" version 2>&1)"; then
                check_version_output "$output" "$target"
            elif [ "$arch" = "386" ]; then
                echo "build-release: warning: built, not executed (32-bit runtime unavailable)"
            else
                echo "build-release: cannot execute $binary" >&2
                exit 1
            fi
            ;;
        windows)
            if command -v wine >/dev/null 2>&1; then
                if output="$(WINEDEBUG=-all wine "$binary" version 2>&1)"; then
                    check_version_output "$output" "$target"
                else
                    echo "build-release: warning: built, not executed under Wine"
                fi
            else
                echo "build-release: warning: built, not executed (wine missing)"
            fi
            ;;
    esac

    (
        cd "$pkg_dir"
        sha512sum "bin/$bin_name$ext" >SHA512SUMS
        md5sum "bin/$bin_name$ext" >MD5SUMS
    )

    rm -rf "$build_dir"
    echo "build-release: creating $archive"
    tar -C "$OUT_ROOT" -cJf "$archive" "$pkg_name"
    (
        cd "$OUT_ROOT"
        sha512sum "$(basename "$archive")" >"$(basename "$archive").sha512"
        md5sum "$(basename "$archive")" >"$(basename "$archive").md5"
    )
    echo "build-release: done: $pkg_name"
}

for target in $(targets "$TARGET"); do
    build_target "$target"
done

echo
echo "build-release: artifacts are in $OUT_ROOT"
echo "build-release: sign these files later with gpg; this script does not create .sig files:"
find "$OUT_ROOT" -maxdepth 1 -name "hrt-tool-$VERSION-*.tar.xz" -print | sort
