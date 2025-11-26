
# Simulator/Emulator validation test

This is a quick-n-dirty project to demonstrate a way to compare execution
between QEMU and ISS.

The idea is to synthesize an initial state and a test packet, run each
through the ISS and QEMU, and compare the state after the test packet
executes.

Inspired by research from Hyunsik Jeong at [KAIST](https://en.wikipedia.org/wiki/KAIST)

## Usage

First, you will need an `iset.py` file with the instruction semantics. By
default, verif will use one from the `/prj` NFS path at Qualcomm machines. If
you are not connected to Qualcomm network, though, you can use the synthetic
`iset.py` that is generated as part of the QEMU-hexagon build at
`<BUILDDIR>/target/hexagon/iset.py`.

For an extensive test, with default parameters, run:

    ./packet_verif verif

If you have a custom iset file, use:

    ./packet_verif verif --iset <ISET_PATH>

For more customization options, check:

    ./packet_verif verif --help

## QEMU coverage

To check how much code our tests cover from QEMU:

1. Compile QEMU with `gcov` support. (See the `--enable-gcov` option from
   QEMU's `configure` script.)

2. Run the tests and generate the coverage report:


    ./packet_verif verif -q path/to/qemu/build/qemu-system-hexagon
    ./packet_verif coverage path/to/qemu/build

3. Open `"coverage/cover_db_html/coverage.html"` in your browser to inspect
   the results.

Note that this process may take a while.
