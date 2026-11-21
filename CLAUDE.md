# MiniSoC: instructions for AI coding agents

MiniSoC is a SystemC/TLM-2.0 virtual prototype with a Tcl test layer and a Python
regression. Read this file before changing anything.

## Ground rules
- `spec/minisoc.yaml` is the single source of truth for addresses, offsets, IRQs
  and bit fields. Never hard-code an address or mask in C++, firmware or Tcl:
  use the generated `regmap.h` / `regmap.tcl` names.
- C++17 only (it must match how libsystemc was built). No new dependencies.
- A signal has exactly one writer process. Register writes run in the CPU
  thread, so they notify an event; one SC_METHOD per peripheral drives the IRQ.
- Model time with events, not per-tick loops (see `Timer`).
- Wrong-direction accesses (write to RO) are ignored + `note_ignored()`, not bus
  errors. Unmapped offsets return `false` from `reg_read/reg_write`.

## Commands
| Task | Command |
|---|---|
| Build | `scripts/build.sh` |
| Everything CI runs | `scripts/check.sh` (add `--asan` for sanitizers) |
| One test | `build/minisoc --script tests/test_timer.tcl` or `build/minisoc --fw timer_irq` |
| Subset | `python3 tools/regress.py --tags irq` |
| Bus trace | append `--trace out/x.csv` |
| Debugger | `scripts/debug.sh --script tests/test_uart.tcl` |

## Definition of done
1. `scripts/check.sh` and `scripts/check.sh --asan` both report all tests passing.
2. Every behaviour you add has a Tcl test that would FAIL without your change
   (prove it once by breaking the model and re-running).
3. New registers are documented in the YAML `desc` fields (regmap.md is generated).

## Skills
- `.claude/skills/add-peripheral/` adds a new peripheral end to end.
- `.claude/skills/triage-regression/` takes a red regression to a root cause.
