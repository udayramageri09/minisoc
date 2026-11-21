---
name: triage-regression
description: Take a failing MiniSoC regression run to a root cause and a minimal fix, using the failure buckets, logs, bus traces and gdb.
---

# Triage a MiniSoC regression

1. Read `out/summary.md`. Work on **buckets**, not individual tests: the largest
   bucket is usually one root cause. Note the status: FAIL (a check failed),
   ERROR (Tcl or firmware exception, e.g. bus error), SIM_TIMEOUT (waited for
   an event that never came), HANG (the process did not exit), CRASH (signal).
2. Open one representative `out/<test>.log`. The first `FAIL:` line has the
   simulated time of the failure.
3. Reproduce just that test: `build/minisoc --script <file> --trace /tmp/t.csv`.
   In the trace, find the transactions just before the failure time. Check
   address, data and response status against `build/generated/regmap.md`.
4. Decide which side is wrong: the **model** (behaviour differs from the spec
   `desc`), the **test** (expectation wrong or too loose), or the **spec**.
5. For a CRASH or memory problem, rebuild with `scripts/build.sh Debug --asan`
   and rerun, or use `scripts/debug.sh`.
6. Fix the smallest thing. Rerun the bucket (`--tags`), then `scripts/check.sh`.
7. Report: the bucket, the root cause in one sentence, the fix, and the evidence
   (the before/after test result).
