# Supported Platforms and Build Environment

[中文](zh-CN/build-and-platforms.md)

## Support matrix

| Component | Status | Notes |
|---|---|---|
| Linux host, x86-64 | Primary | Native C++ tool build; cross toolchains for target tests |
| Linux host, AArch64/RV64 | Expected | Native host compiler required; release CI is currently cross-host focused |
| Windows host | Supported via WSL2/container | Native Win32 build is not supported |
| macOS host | Supported via container/Linux VM | Native Mach-O host build is not supported |
| AArch64 ELF64 little-endian | Primary target | Executables, PIE and shared objects; broad scalar scope plus the documented fixed 128-bit Advanced SIMD subset |
| RV64 ELF64 little-endian | Primary target | Executables, PIE and shared objects; scalar/RVC workflow covered |
| ARM ELF32, A32 state | Supported scalar target | Executables, dynamic codecs and sibling multi-VM; no Thumb/VFP/NEON |
| x86-64 target | Not implemented | Parser recognizes the machine but protector rejects it |
| PE/Mach-O targets | Not supported | The protected output format remains Linux ELF |

Exact instruction breadth is compiler- and fixture-dependent. Unsupported operations are
rejected; the matrix is not a promise that arbitrary binaries can be protected.

## Minimal host environment

- GNU Make
- C++20 compiler (`g++` by default)
- Python 3
- standard ELF development headers (`elf.h`, normally supplied by libc development files)
- `binutils` for release stripping and inspection

Ubuntu/Debian example:

```sh
sudo apt-get update
sudo apt-get install build-essential clang python3 binutils
```

Build and test:

```sh
make -j$(nproc)
make test
```

Check the embedded release version:

```sh
./build/myvmp --version
```

Override tools conventionally:

```sh
make CXX=clang++ STRIP=llvm-strip -j$(nproc)
make BUILD_DIR=build-debug CXXFLAGS='-std=c++20 -O0 -g -Wall -Wextra -Wpedantic -Werror'
```

## Cross toolchains and QEMU

For the complete target test suite on Ubuntu/Debian:

```sh
sudo apt-get install \
  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu \
  gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf clang \
  qemu-user
```

Expected commands include `aarch64-linux-gnu-gcc`, `riscv64-linux-gnu-gcc`,
`arm-linux-gnueabihf-gcc`, Clang (used to assemble the ARM32 runtime), their
`objdump`/`as` companions, and `qemu-aarch64`,
`qemu-riscv64`, `qemu-arm`.

Clang may be used for corpus generation when a target sysroot is configured, but runtime
blob generation scripts currently expect the GNU cross tools named in the Makefile.

## Windows with WSL2

Install Ubuntu under WSL2, clone the repository inside the WSL filesystem (for example
`~/src/myVMP`, not `/mnt/c/...` for best I/O performance), then use the Ubuntu commands
above. WSL2 is the preferred Windows workflow because the tool, generated ELF files,
cross compilers and QEMU all run in their intended Linux environment.

```powershell
wsl --install -d Ubuntu-24.04
```

After Ubuntu starts:

```sh
sudo apt-get update
sudo apt-get install build-essential clang python3 binutils \
  gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu gcc-arm-linux-gnueabihf \
  binutils-aarch64-linux-gnu binutils-riscv64-linux-gnu binutils-arm-linux-gnueabihf qemu-user
git clone YOUR_REPOSITORY_URL myVMP
cd myVMP
make -j$(nproc) test
```

Native Visual Studio/MinGW compilation is not a supported release path: `<elf.h>`, shell
automation, GNU target tools and QEMU-user behavior would otherwise need separate ports.

## macOS and cross-platform container

Docker Desktop, Podman or another Linux-container runtime provides the same Ubuntu 24.04
environment on macOS and Windows:

```sh
docker build -t myvmp:0.1.0 .
docker run --rm myvmp:0.1.0 build/myvmp --version
docker run --rm myvmp:0.1.0 make release-check
```

To protect files from the current host directory, mount it separately:

```sh
docker run --rm -v "$PWD/samples:/work" myvmp:0.1.0 \
  /workspace/build/myvmp protect /work/app.arm64 target /work/app.protected.arm64 1001
```

On PowerShell use `${PWD}/samples:/work` in the volume argument. Container outputs may
be owned by the container user on Linux; use a matching `--user` when needed. Apple Silicon
runs an arm64 Ubuntu container by default; the checked-in target runtime blobs keep the
host tool build independent of target architecture.

A native macOS port is intentionally deferred. It would require a maintained ELF type
compatibility layer and replacements for Linux/GNU test tooling while still producing
Linux ELF output; the container gives a smaller, reproducible support surface today.

## Runtime blobs

Checked-in native runtime blobs let a normal host build succeed without cross tools.
After editing runtime assembly:

```sh
make generate-aarch64-runtime
make check-aarch64-runtime
make generate-riscv64-runtime
make check-riscv64-runtime
make generate-arm32-runtime
make check-arm32-runtime
```

Generated includes must be reviewed together with their assembly source. Do not hand-edit
the `.inc` byte arrays.

## Sanitizer and static audit build

```sh
make BUILD_DIR=build-sanitize \
  CXXFLAGS='-std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined' -j$(nproc)
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  build-sanitize/myvmp_tests
```

Leak detection may be unavailable under ptrace/managed runners; disabling it does not
disable address or undefined-behavior checks.

## Release and package

```sh
make release
make package
```

`make release` builds with `-O3 -DNDEBUG`, strips the host tool and assembles English and
Chinese documentation. Release objects use an isolated `build/release-build` tree, so a
prior debug/development build cannot be reused accidentally. `make package` creates
`build/dist/myvmp-<version>-linux-<host-arch>-tool.tar.gz` with a
`myvmp-<version>/` root directory.

If the host environment supports static C++ linking:

```sh
make release STATIC=1
make package STATIC=1
```

Version comes from `VERSION`; `myvmp --version`, archive name and archive root must agree.
Release publication and signing are maintained through the repository hosting platform.

Before publishing, run `make reproducible-check package-smoke-test`. This compares two
isolated release builds with a fixed `SOURCE_DATE_EPOCH`, normalizes tar ownership/order/
timestamps, and then extracts the package to verify the binary and required documentation.
