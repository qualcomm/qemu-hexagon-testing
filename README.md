# qemu hexagon testing

This project contains programs to exercise QEMU hexagon.

## Branches

**main**: Primary development branch. Contributors should develop submissions based on this branch, and submit pull requests to this branch.

## Quick Start

```bash
# Configure build (Standalone OS)
cmake -S $PWD -B ./build-standalone -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/hexagon-standalone.cmake

# Configure build (Linux)
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

Describe how to use the project.

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
