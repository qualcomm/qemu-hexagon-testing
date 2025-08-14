
# SDK examples

Here are some [Hexagon SDK](https://softwarecenter.qualcomm.com/catalog)
example programs.  Download the SDK and then build for Standalone OS, or
download the [Open Source Toolchain](https://github.com/quic/toolchain_for_hexagon)
and build for linux.

## Quick Start

Below is how you can build these tests.  Hexagon Standalone OS is a baremetal
"operating system" and can be used to exercise `qemu-system-hexagon`.  Linux
progams built for hexagon can be emulated on any linux host, using
`qemu-hexagon`'s userspace emulation.

```bash
# Put the directory containing hexagon-clang or hexagon-linux-musl-clang
# in your PATH, and then:

# Configure build for Hexagon Standalone OS, or...
cmake -S $PWD -B ./build-standalone -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/hexagon-standalone.cmake

# Configure build for Hexagon Linux
cmake -S $PWD -B ./build-linux -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/hexagon-linux.cmake

# Build
ninja -C <build-dir>

# Install
ninja -C <build-dir> install
```

## Requirements

- Hexagon SDK
- Hexagon Opensource Toolchain
- CMake 3.16+
- Ninja build system

## Usage

After building a program for standalone OS, you can invoke it like so:

    qemu-system-hexagon -kernel ./prog -append 'arg1 arg2 arg3'

And with a program built for linux, you can invoke it like so:

    qemu-hexagon ./prog arg1 arg2 arg3

For programs with library dependencies (including the C library), you can
point qemu-hexagon to a sysroot path with shared object dependencies, for
example the one with the toolchain:

    qemu-hexagon -L /path/to/toolchain/target/usr ./prog arg1 arg2 arg3
