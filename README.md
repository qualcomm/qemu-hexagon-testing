# qemu hexagon testing

This project contains programs to exercise QEMU hexagon.

## Branches

**main**: Primary development branch. Contributors should develop submissions based on this branch, and submit pull requests to this branch.

## Quick Start

### SDK Download and Build

```bash
cd /tmp/hexagon-build
wget https://softwarecenter.qualcomm.com/api/download/software/sdks/Hexagon_SDK/Linux/Debian/6.4.0.2/Hexagon_SDK_lnx.zip
unzip hexagon_sdk.zip
cd qemu-hexagon-testing

cmake -S standalone_systests -B build-systests \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=${PWD}/cmake/hexagon-standalone.cmake \
  -DHEXAGON_SDK_ROOT=/tmp/hexagon-build/Hexagon_SDK/6.4.0.2 \
  -DHEXAGON_ARCH=v68

cmake --build build-systests
DESTDIR=build-systests/install cmake --install build-systests
ls -la build-systests/bin/
```

## Requirements

- **Hexagon SDK 6.4.0.2**: Download from Qualcomm Software Center
- **Hexagon Linux Toolchain** (for Linux builds): [Codelinaro Hexagon Toolchain](https://artifacts.codelinaro.org/artifactory/codelinaro-toolchain-for-hexagon/20.1.4/clang+llvm-20.1.4-cross-hexagon-unknown-linux-musl.tar.zst)
- **CMake 3.16+**
- **Ninja build system** (recommended) or Make
- **Dependencies**: `build-essential`, `wget`, `unzip`, `zstd`

### Installing Dependencies (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y cmake build-essential wget unzip ninja-build qemu-user zstd libncurses5
```

## Test Suites

This project includes multiple test suites for exercising QEMU Hexagon emulation:

### Standalone System Tests (`standalone_systests/`)

A comprehensive collection of 60+ system-level tests for the Hexagon Standalone OS environment:

- **MMU Tests**: `mmu_asids`, `mmu_cacheops`, `mmu_multi_tlb`, `mmu_overlap`, `mmu_page_size`, `mmu_permissions`, `hsv39_tlb`
- **HVX Tests**: `hvx_64b`, `hvx_ext`, `hvx-multi`, `hvx_nocoproc`
- **Interrupt Tests**: `fastint`, `fastl2vic`, `levelint`, `pendalot`, `pend_wake_wait`
- **System Tests**: `access`, `badva`, `checkforpriv`, `float_excp`, `invalid_opcode`, `tlblock`, `timer_reg`
- **Thread Tests**: `test-thread`, `thread_scheduling`
- **And many more...** (see `standalone_systests/CMakeLists.txt` for full list)

### HVX Examples (`sdk_examples/`)

HVX (Hexagon Vector eXtensions) example programs demonstrating vector processing capabilities.

## Usage


## Development

How to develop new features/fixes for the software. Maybe different than "usage". Also provide details on how to contribute via a [CONTRIBUTING.md file](CONTRIBUTING.md).

## Getting in Contact

How to contact maintainers. E.g. GitHub Issues, GitHub Discussions could be indicated for many cases. However a mail list or list of Maintainer e-mails could be shared for other types of discussions. E.g.

* [Report an Issue on GitHub](../../issues)
* [Open a Discussion on GitHub](../../discussions)
* [E-mail us](mailto:bcain@qti.qualcomm.com) for general questions

## License

*qemu-hexagon-testing*

*qemu-hexagon-testing* is licensed under the [BSD-3-clause License](https://spdx.org/licenses/BSD-3-Clause.html). See [LICENSE.txt](LICENSE.txt) for the full license text.
