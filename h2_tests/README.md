# h2 (hexagon-hypervisor) functional tests

Guest programs that run under the [h2 hypervisor](https://github.com/qualcomm/hexagon-hypervisor)
booter on `hexagon-sim` (authoritative) and `qemu-system-hexagon`.

There is a single test, `h2_functional`.  It runs `2 * hw_threads + 4` tasks and
never touches the scheduler, l2vic, timer or TLB locks itself; it depends on h2
using them:

* more tasks than hw threads at mixed priorities (scheduler)
* mutex / condvar / barrier traffic with an ordering chain (kernel lock)
* `h2_nanosleep` from every task while holding an HVX context (qtimer -> l2vic -> ISR)
* HVX acquire, vector arithmetic with closed-form expected lanes
* HMX acquire; VTCM mapped with `h2_tlb_alloc` (tlblock); output tiles compared
  with golden checksums recorded from hexagon-sim (v68 and v73 differ for one
  input pattern)

Each task adds its results to shared totals that are compared with the
expected sums, so a task that does not finish or computes wrongly fails.

The test first asserts its requirements (at least 4 hw threads, 128-byte HVX
with a context, an HMX unit, enough VTCM, timer and l2vic present) and prints
`FAIL config: need <condition>: ...` if any is missing.

## Build and run

```
make                 # clones h2 into build/h2-src (or use H2_SRC=<checkout>), builds it and the tests
make check           # run on hexagon-sim
make check-qemu      # run on qemu-system-hexagon
make ARCHV=68        # v68 instead of the default v73
```

The h2 build uses `USE_PKW=0` (Qualcomm-internal tool selector) and
`NULL_ANGEL_TRAP=1` (hexagon-sim hangs in the booter without it).

A test passes when it prints `PASS <name>`, prints no `FAIL` line, and the
simulator exits 0.  `scripts/qemu_xfail.txt` lists tests that currently fail on
qemu; they are reported as `XFAIL` and do not fail `make check-qemu`.

## Notes for writing tests

* `h2_sem_init()` initializes the count to **1**; use `h2_sem_init_val(&s, 0)`.
* A guest gets no VTCM mapping; `h2_hmx` pins one with `h2_tlb_alloc()`
  (pages larger than 256KB were rejected by the kernel here).
* HMX tiles must be 2KB aligned; the `Rt` operand of `mxmem` is a length - 1.
  HMX is programmed with the `Q6_*` intrinsics from `hmx_hexagon_protos.h`.
* The kernel unit tests under h2's own `kernel/` are privileged; guests cannot
  execute `k0lock`/`tlblock` themselves, so those locks are exercised through
  the traps that take them (blocking synchronization, TLB alloc/free).
