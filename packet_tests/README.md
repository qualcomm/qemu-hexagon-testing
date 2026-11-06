# Packet regression tests

These bare-metal V85 tests need a Hexagon clang with V85 support. They do
not need the SDK libraries or Rust. Each `pbNN` binary tests one packet
bug and prints a named PASS or a value mismatch. Exceptions are bounded;
an unexpected fault fails instead of waiting without an end.

Build from the repository root:

```sh
cmake -S packet_tests -B build-packets -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${PWD}/cmake/hexagon-standalone.cmake" \
  -DHEXAGON_ARCH=v85 \
  -DQEMU_HEXAGON=/path/to/qemu-system-hexagon
cmake --build build-packets
ctest --test-dir build-packets --output-on-failure
DESTDIR=build-packets/install cmake --install build-packets
cmake --build build-packets --target package_packet_tests
```

The shared toolchain uses `HEXAGON_SDK_ROOT` or `HEXAGON_SDK_TOOLCHAIN`,
as in the other standalone suites. To use a compiler outside the SDK,
set `HEXAGON_SDK_TOOLCHAIN=/absolute/path/to/compiler/bin` before configuring.
Configuration checks for V85, HMX and HLX compiler support.

Installation uses `PacketTests_6.7.0.0_v85/bin`. The functional archive
`build-packets/packet_tests.tar.gz` contains only `bin/pb*` executables,
with fixed archive timestamps and no gzip timestamp. CI uploads the archive
and `packet_tests.tar.gz.sha256` through the existing release workflow.
The checksum applies to that build; another compiler can produce a different
checksum.

For QEMU's functional suite, set `QEMU_TEST_HEXAGON_PACKET_URL` to the
versioned release archive URL and `QEMU_TEST_HEXAGON_PACKET_SHA256` to its
SHA-256 checksum. QEMU uses its standard asset cache and archive extraction.
No packet archive has been published yet; a fixed Asset URL and checksum
must be added to `test_packets.py` after publication. Do not use a moving
`latest` URL.

For local development, `QEMU_TEST_HEXAGON_PACKET_DIR` overrides the archive
and selects the absolute `build-packets/bin` path.
