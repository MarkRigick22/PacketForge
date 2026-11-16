# Linux performance execution status

**NOT EXECUTED on Linux.** This upgrade was developed on macOS arm64. Linux perf,
GCC/Clang Linux builds, GitHub-hosted CI and privileged TAP integration have not been
run here. macOS observations are in `upgrade/performance-results.md`; existing
`PERFORMANCE.md` measurements remain historical and are not Linux results.

```sh
bash tools/linux_performance.sh --output linux-scaling.csv --sizes 128 --flows 1000 --rings 256 --batches 32 --skews 0 --policies off --prebuilt 0,10000
# Full Cartesian experiment (large; plan runtime and disk usage):
MATRIX=1 PACKETS=3000000 REPEATS=5 bash tools/run_benchmarks.sh build-release linux-full.csv
# CPU events and call stacks; no sudo or system setting changes inside this script:
bash tools/profile_linux.sh build-profile
# Optional scheduler tracepoints, subject to permission:
SCHED=1 bash tools/profile_linux.sh build-profile
```

The driver stores raw CSV plus `.metadata.json`: CPU/core inventory, architecture,
kernel, compiler, CMake cache, Git revision, current diff summary and all factors.
Unavailable metadata is explicitly marked. Build metadata includes actual CMake
compiler paths; the separately captured `c++ --version` is the shell default.

`perf stat` captures cycles, instructions, cache references/misses, context switches,
branches/misses. `perf record` produces frame-pointer CPU stacks and a text report.
Scheduler tracing is an explicit separate opt-in. `perf_event_paranoid`, unavailable
PMU events, container restrictions and tracefs access can prevent collection. These
are failures, not zero counts or hidden successes. Obtain administrator-approved
access or report the unavailable counters; scripts do not relax kernel security.

TSan has a separate manually triggered workflow on Ubuntu 22.04. It is not combined
with ASan/UBSan. Runtime address-space incompatibilities can occur on some kernels;
the workflow fails visibly rather than continuing on errors or suppressing reports.
Successful local macOS TSan tests do not establish Linux TSan success.
