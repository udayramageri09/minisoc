"""Unit tests for spec validation and register-map generation."""
import copy
import sys
from pathlib import Path

import pytest
import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import regmap_gen as rg  # noqa: E402

SPEC = yaml.safe_load((Path(__file__).resolve().parents[2] / "spec" / "minisoc.yaml").read_text())


def spec():
    return copy.deepcopy(SPEC)

def test_real_spec_is_valid():
    out = rg.validate(spec())
    assert out["num_irqs"] == 2


def test_generated_header_has_absolute_addresses_and_masks():
    h = rg.gen_header(rg.validate(spec()))
    assert "#define TIMER0_LOAD (TIMER0_BASE + 0x04u)" in h
    assert "#define UART_STATUS_OVERRUN_MSK 0x00000004u" in h
    assert "#define TIMER0_IRQ 0u" in h


def test_overlapping_regions_rejected():
    s = spec()
    s["instances"][1]["base"] = s["instances"][0]["base"] + 0x800  # TIMER0 inside UART0 window
    with pytest.raises(rg.SpecError, match="overlap"):
        rg.validate(s)


def test_misaligned_offset_rejected():
    s = spec()
    s["peripheral_types"]["timer"]["registers"][1]["offset"] = 0x06
    with pytest.raises(rg.SpecError, match="aligned"):
        rg.validate(s)


def test_overlapping_fields_rejected():
    s = spec()
    s["peripheral_types"]["uart"]["registers"][1]["fields"].append({"name": "BAD", "lsb": 1, "width": 2})
    with pytest.raises(rg.SpecError, match="overlaps another field"):
        rg.validate(s)


def test_field_outside_register_rejected():
    s = spec()
    s["peripheral_types"]["uart"]["registers"][0]["fields"][0] = {"name": "DATA", "lsb": 30, "width": 4}
    with pytest.raises(rg.SpecError, match="outside"):
        rg.validate(s)


def test_duplicate_irq_rejected():
    s = spec()
    s["instances"][0]["irq"] = s["instances"][1]["irq"]
    with pytest.raises(rg.SpecError, match="irq"):
        rg.validate(s)


def test_bad_access_type_rejected():
    s = spec()
    s["peripheral_types"]["intc"]["registers"][0]["access"] = "RX"
    with pytest.raises(rg.SpecError, match="access"):
        rg.validate(s)


def test_register_must_fit_in_window():
    s = spec()
    s["instances"][1]["size"] = 0x8  # TIMER0 has a register at 0x0C
    with pytest.raises(rg.SpecError, match="does not fit"):
        rg.validate(s)
