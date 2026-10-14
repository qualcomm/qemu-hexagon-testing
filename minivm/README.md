# MiniVM Tests

This directory builds the Hexagon MiniVM hypervisor and tests.

The default tool names target the bare-metal Hexagon cross toolchain. The SDK
toolchain and simulator can be supplied explicitly:

```bash
make -C minivm archive \
  MINIVM_CC=/path/to/hexagon-clang \
  MINIVM_LD=/path/to/hexagon-clang \
  MINIVM_OBJCOPY=/path/to/hexagon-llvm-objcopy \
  MINIVM_SIM=/path/to/hexagon-sim
```

Useful targets are `build`, `archive`, `test`, and `clean`. The MiniVM source
is fetched from the `v$(MINIVM_VERSION)` tag on first use. The `test` target is
available for manual simulator experiments, but is not run in CI because the
current `hexagon-sim` does not implement the MiniVM `vm*` virtualization path.
