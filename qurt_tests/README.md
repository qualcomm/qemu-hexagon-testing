# QuRT functional tests

User-process programs for the QuRT RTOS from the Hexagon SDK, run on
`hexagon-sim` (authoritative) and `qemu-system-hexagon`.  Headers and archives
come from `$SDK/rtos/qurt/compute<arch>`; the link line is the one in
`$SDK/build/cmake/hexagon_toolchain.cmake`.  Default target is `v73`
(`hexagon-sim -mv73na_1`, qemu machine `V73NA_1024`); `ARCH=v68` also works.

There is a single test, `qurt_functional`.  It runs `2 * hw_threads + 4`
threads and never touches the scheduler, l2vic, timer or TLB locks itself; it
depends on QuRT using them:

* more threads than hw threads at mixed priorities (scheduler)
* mutex / condvar / barrier traffic with an ordering chain (kernel lock)
* timed sleeps from every thread while holding an HVX unit (qtimer -> l2vic -> ISR)
* HVX lock, vector arithmetic with closed-form expected lanes
* HMX lock; VTCM mapped via `qurt_mem_region_create` (TLB); output tiles compared
  with golden checksums recorded from hexagon-sim (v68 and v73 differ for one
  input pattern)

Each thread adds its results to shared totals that are compared with the
expected sums, so a thread that does not finish or computes wrongly fails.

The test first asserts its requirements (at least 4 hw threads, v68+, a 128B
HVX unit, a working timer, a lockable HMX unit, mappable VTCM) and prints
`FAIL config: need <condition>: ...` if any is missing.

## Build and run

```
make                 # build all tests into build/<arch>/
make check           # run on hexagon-sim
make check-qemu      # run on qemu-system-hexagon
make ARCH=v68 check
```

A test passes when it prints `PASS <name>`, prints no `FAIL` line, and the
simulator exits 0.  A test that prints `SKIP <name> <reason>` is reported as
skipped.  `scripts/qemu_xfail.txt` (if present) lists known qemu failures.

## Notes

* QuRT has no yield call; `qt_yield()` re-sets the thread priority.
* `QURT_TIMER_MIN_DURATION` is 100us and timers may fire up to
  `QURT_SYSCLOCK_ERROR_MARGIN` ticks (10us) early.
* HMX tiles must be 2KB aligned; the `Rt` operand of `mxmem` is a length - 1.
  VTCM is at pa 0xd9000000 (v73) / 0xd8400000 (v68) in the SDK simulators.
