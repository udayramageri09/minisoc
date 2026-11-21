"""Unit tests for regression result classification and failure bucketing."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import regress  # noqa: E402


def test_classify_pass():
    assert regress.classify(0, "TEST PASSED\n") == ("PASS", "")


def test_classify_fail_extracts_first_check():
    log = "LOG: ok\nFAIL: timer LOAD: expected 0x1, got 0x2 @120 ns\nTEST FAILED (1 checks)\n"
    assert regress.classify(1, log) == ("FAIL", "timer LOAD: expected 0x1, got 0x2")


def test_classify_golden_uart_mismatch():
    status, sig = regress.classify(0, "TEST PASSED\n", expect_uart="hello")
    assert status == "FAIL" and "golden" in sig


def test_classify_hang_and_crash():
    assert regress.classify(None, "")[0] == "HANG"
    assert regress.classify(-11, "")[0] == "CRASH"


def test_normalise_buckets_same_root_cause():
    a = regress.normalise("bus error at 0x10001040 after 12 ns")
    b = regress.normalise("bus error at 0x20000000 after 7 ns")
    assert a == b
