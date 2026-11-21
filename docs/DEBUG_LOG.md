# Debug log: real issues hit while building MiniSoC

## 1. A passing test that couldn't catch a real bug (mutation testing)
**Symptom.** I deliberately broke the timer (expiry after `LOAD + 1` ticks instead of
`LOAD`) and the whole regression stayed green.
**Cause.** The one-shot test polled `STATUS` every 100 ns and accepted 5000-5200 ns.
A 10 ns error is invisible inside that window.
**Fix.** Added an interrupt-timed check: 300 ticks must raise the IRQ exactly 3000 ns
after the `CTRL` write. With the bug planted the regression goes red with one bucket:
`FAIL IRQ latency from CTRL write (ns): expected <hex>, got <hex>`.
**Lesson.** A test is only useful if it fails when the behaviour is wrong. Break the
model on purpose to prove it.

## 2. SystemC thread stack overflow
**Symptom.** Sanitizer build: `SEGV ... in Cpu::run()` before the first transaction.
**Cause.** `SC_THREAD`s are coroutines with a 64 KiB stack by default. The embedded Tcl
interpreter, plus ASan's larger stack frames, overflowed it. A normal build might
silently corrupt memory instead of crashing.
**Fix.** `set_stack_size(1 << 20)` for the CPU thread.

## 3. ASan false positives with coroutines
**Symptom.** Still `SEGV` reading a member right at thread start, with a 1 MiB stack.
**Cause.** ASan's fake-stack mode (`detect_stack_use_after_return`) can't follow the
hand-written stack switches that SystemC's QuickThreads coroutines do.
**How I confirmed it.** The same binary passes with
`ASAN_OPTIONS=detect_stack_use_after_return=0`. The heap, leak and UB checks all stay on.
**Fix.** `__asan_default_options()` in `main.cpp`, compiled only in sanitizer builds.

## 4. Test helper type confusion
**Symptom.** The UART test aborted with a Tcl error inside `hex`.
**Cause.** `expect_eq` hex-formatted every value, including the UART output string.
**Fix.** Format only integers; compare strings with `ne`.
