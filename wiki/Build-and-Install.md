# Build and Install

## Requirements

| Component | Version |
| --- | --- |
| C compiler | C11 |
| C++ compiler | C++17 |
| CMake | 3.16 or newer |
| OpenSSL | 3.0 or newer (`EVP_MAC`, HKDF controls) |
| libcurl | any supported stable release |
| nlohmann/json | header-only |
| Rust | stable toolchain (printer) |
| Go | 1.23 or newer (F2F c-archive) |
| pkg-config | for dependency discovery |

`hrt-cold` needs only Go.

### Debian / Ubuntu

```sh
sudo apt-get install -y cmake pkg-config libssl-dev libcurl4-openssl-dev \
  nlohmann-json3-dev build-essential
```

### Gentoo

```sh
sudo emerge -av dev-libs/openssl net-misc/curl dev-cpp/nlohmann_json \
  dev-lang/rust dev-lang/go cmake
```

## Build and test

```sh
cmake -S tools/hrt-tool -B tools/hrt-tool/build
cmake --build tools/hrt-tool/build --parallel
ctest --test-dir tools/hrt-tool/build --output-on-failure
```

The Rust printer is built by CMake with `cargo build --release --offline`. The
Go F2F library is built as a `c-archive` by the same build.

For a build without Go:

```sh
cmake -S tools/hrt-tool -B build-nof2f -DHRT_TOOL_ENABLE_F2F=OFF
cmake --build build-nof2f --parallel
```

With `HRT_TOOL_ENABLE_F2F=OFF`, `hrt-tool f2f ...` returns
`F2F_DISABLED`/exit `14`.

## Install and update

```sh
sudo cmake --install tools/hrt-tool/build
# or the source-tree wrapper (PREFIX defaults to /usr/local)
sudo make -C tools/hrt-tool install
sudo make -C tools/hrt-tool update
```

`make install` configures, builds, and installs `hrt-tool` plus its docs.
`make update` rebuilds and reinstalls using the cached prefix in
`build/CMakeCache.txt`; it does not fetch source, reconfigure, or touch the
vault.

CMake options:

| Option | Default | Meaning |
| --- | --- | --- |
| `HRT_TOOL_VERSION` | `0.1.1` | Version injected into the tool, core, printer, and F2F. |
| `HRT_TOOL_ENABLE_F2F` | `ON` | Link the Go F2F c-archive. |
| `HRT_TOOL_ENABLE_WARNINGS` | `ON` | Enable compiler warnings. |
| `HRT_TOOL_ENABLE_WERROR` | `OFF` | Treat warnings as errors. |
| `HRT_TOOL_ENABLE_SANITIZERS` | `OFF` | ASan + UBSan (GCC/Clang). |

## hrt-cold

```sh
cd tools/hrt-cold
go build -trimpath -o hrt-cold .
go test ./...
```

## Native scaffold

The `hrtcold` C ABI scaffold is built separately from the repository root:

```sh
./configure --build-dir=build --enable-werror
cmake --build build
(cd build && ctest --output-on-failure)
```

See [`docs/native/BUILD.md`](https://github.com/xunxunProjects/Oyama-s-HRT-Tracker-Tools/blob/main/docs/native/BUILD.md).

## Cross-compilation

See [Cross Compilation](Cross-Compilation) for the four-target release matrix
and the required MinGW/32-bit packages.

## Canonical sources

- `tools/hrt-tool/README.md`
- `docs/native/BUILD.md`
- `CONTRIBUTING.md`
