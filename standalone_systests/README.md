
# SDK examples

Here are some test cases that exercise some DSP features.  They are intended
to build and run with the Hexagon Standalone OS, available with the Hexagon
SDK.

## Quick Start

Below is how you can build these tests.  Hexagon Standalone OS is a baremetal
"operating system" and can be used to exercise `qemu-system-hexagon`.

```bash
# Put the directory containing hexagon-clang in your PATH, and then:

# Configure build for Hexagon Standalone OS:
cmake -S $PWD -B ./build-standalone -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/hexagon-standalone.cmake

# Build
ninja -C <build-dir>

# Install
ninja -C <build-dir> install
```

## Requirements

- Hexagon SDK
- CMake 3.16+
- Ninja build system

## Usage

After building a program for standalone OS, you can invoke it like so:

    qemu-system-hexagon -kernel ./prog -append 'arg1 arg2 arg3'
