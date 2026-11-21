---
name: add-peripheral
description: Add a new memory-mapped peripheral model to MiniSoC from a register description, with spec entry, SystemC model, wiring, tests and regression entry.
---

# Add a peripheral to MiniSoC

Input: a register description (names, offsets, access types, fields, behaviour)
from a datasheet or a markdown table. If any behaviour is ambiguous (reset values,
what a write to a busy register does), list the assumptions in the PR description
instead of guessing silently.

## Steps
1. **Spec**: add a `peripheral_types.<type>` entry and an `instances` entry in
   `spec/minisoc.yaml`. Pick a free 4 KiB window and, if it interrupts, an unused
   IRQ number. Run `python3 tools/regmap_gen.py spec/minisoc.yaml /tmp/gen` and fix any
   `spec error` it reports (overlaps, alignment, field clashes).
2. **Model**: in `src/peripherals.h`, derive from `RegPeripheral` and implement
   `reg_read` / `reg_write` with a `switch` over the generated `<TYPE>_<REG>_OFF`
   macros. Return `false` for unknown offsets. Follow the single-writer rule for
   any `sc_out` (notify an event, drive it from one SC_METHOD).
3. **Wire**: in `src/main.cpp`, instantiate the model, add `bus.map(...)` and
   `bus.isock.bind(...)` *in the same order*, connect its IRQ to
   `intc.irq_in[<NAME>_IRQ]`.
4. **Test**: write `tests/test_<type>.tcl` using `tests/lib.tcl` helpers. Cover
   reset values, each access type (RO ignored, W1C clears), the main behaviour
   with exact timing, and the IRQ path through the INTC.
5. **Register**: add the test to `tests/regression.yaml` with tags.
6. **Prove the test bites**: break one behaviour in the model, confirm
   `tools/regress.py` goes red with a clear signature, then revert.
7. Run `scripts/check.sh` and `scripts/check.sh --asan`; both must pass.
