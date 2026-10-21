# Cross Compilation

The release matrix is:

| Target | Toolchain | Status |
| --- | --- | --- |
| `linux/amd64` | host GCC, OpenSSL, libcurl | native, tested |
| `linux/386` | GCC `-m32`, 32-bit OpenSSL/libcurl | needs 32-bit libraries |
| `windows/amd64` | MinGW-w64 x86_64 + Rust `x86_64-pc-windows-gnu` | needs cross OpenSSL/libcurl |
| `windows/386` | MinGW-w64 i686 + Rust `i686-pc-windows-gnu` | needs i686 cross toolchain and libraries |

A target is listed as available only after its binary is built, `hrt-tool
version` reports the expected version, and the package checksums exist.

## Rust targets

```sh
rustup target add i686-unknown-linux-gnu x86_64-pc-windows-gnu i686-pc-windows-gnu
```

## Linux/386

Install 32-bit OpenSSL and libcurl on Gentoo:

```sh
sudo env ABI_X86="32 64" emerge -1av dev-libs/openssl net-misc/curl
```

The build then uses `-m32` and the 32-bit pkg-config/library paths. On
Debian/Ubuntu, enable the `i386` architecture and install the multiarch
`libssl-dev:i386`/`libcurl4-openssl-dev:i386` packages instead.

## Windows with MinGW

The MinGW compiler and Rust GNU targets are required. Static OpenSSL/libcurl
avoid shipping DLLs in the archive.

Gentoo cross packages:

```sh
echo 'cross-x86_64-w64-mingw32/dev-libs/openssl static-libs' | sudo tee -a /etc/portage/package.use/cross-x86_64-w64-mingw32
echo 'cross-x86_64-w64-mingw32/net-misc/curl static-libs -quic -http3' | sudo tee -a /etc/portage/package.use/cross-x86_64-w64-mingw32
sudo x86_64-w64-mingw32-emerge -1av dev-libs/openssl net-misc/curl

sudo crossdev -t i686-w64-mingw32
echo 'cross-i686-w64-mingw32/dev-libs/openssl static-libs' | sudo tee -a /etc/portage/package.use/cross-i686-w64-mingw32
echo 'cross-i686-w64-mingw32/net-misc/curl static-libs -quic -http3' | sudo tee -a /etc/portage/package.use/cross-i686-w64-mingw32
sudo i686-w64-mingw32-emerge -1av dev-libs/openssl net-misc/curl
```

Windows link requirements are added by the toolchain files: `ws2_32`,
`crypt32`, `bcrypt`, `ntdll`, and `userenv`. Go's c-archive also needs
`winmm`; Rust's static library needs the native libraries reported by:

```sh
cargo rustc --release --target x86_64-pc-windows-gnu -- --print native-static-libs
```

## Build commands

Once the dependencies and toolchain files are installed:

```sh
tools/hrt-tool/scripts/build-release.sh 0.1.1 dist --target linux/amd64
tools/hrt-tool/scripts/build-release.sh 0.1.1 dist --target linux/386
tools/hrt-tool/scripts/build-release.sh 0.1.1 dist --target windows/amd64
tools/hrt-tool/scripts/build-release.sh 0.1.1 dist --target windows/386
```

Cross builds configure with `-DBUILD_TESTING=OFF`; run the host test suite on
`linux/amd64` first.

## Verify

```sh
file dist/hrt-tool-0.1.1-*/bin/hrt-tool*
# ELF 64-bit, ELF 32-bit, PE32+ x86-64, PE32 i386

wine dist/hrt-tool-0.1.1-windows-amd64/bin/hrt-tool.exe version
wine dist/hrt-tool-0.1.1-windows-386/bin/hrt-tool.exe version
dist/hrt-tool-0.1.1-linux-386/bin/hrt-tool version
```

## Fallbacks

- If the Gentoo MinGW sysroot conflicts with `dev-util/mingw64-toolchain`,
  build static OpenSSL/curl into a user prefix, or use the MSYS2 jobs in
  GitHub Actions (`windows-latest` with `mingw-w64-*-openssl` and
  `mingw-w64-*-curl`).
- If a target still cannot be built, do not publish it. Report the exact
  missing library or link error and mark the target as pending.

## Canonical sources

- `tools/hrt-tool/CMakeLists.txt`
- `cmake/toolchains/`
- `tools/hrt-tool/scripts/build-release.sh`
