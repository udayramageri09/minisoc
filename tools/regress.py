#!/usr/bin/env python3
"""Run the MiniSoC regression in parallel and triage the results.

  regress.py [--bin build/minisoc] [--manifest tests/regression.yaml]
             [--tags smoke,irq] [--jobs N] [--out out/] [--trace]

Writes per-test logs, junit.xml (for CI) and summary.md (failures grouped by
signature, so 20 failing tests with one root cause show up as ONE bucket).
Exit code: 0 if everything passed, 1 otherwise.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from xml.sax.saxutils import escape

import yaml

# Simulator exit codes (see src/main.cpp)
EXIT_STATUS = {0: "PASS", 1: "FAIL", 2: "ERROR", 3: "SIM_TIMEOUT", 4: "USAGE"}


@dataclass
class Result:
    name: str
    status: str
    seconds: float
    signature: str = ""
    log: str = ""
    tags: list = field(default_factory=list)


def classify(returncode: int | None, output: str, expect_uart: str | None = None) -> tuple[str, str]:
    """Map (exit code, log text) to (status, failure signature)."""
    if returncode is None:
        return "HANG", "wall-clock timeout (simulator did not exit)"
    if returncode < 0:
        return "CRASH", f"killed by signal {-returncode}"
    status = EXIT_STATUS.get(returncode, "CRASH")
    if status == "PASS":
        if "TEST PASSED" not in output:
            return "FAIL", "exit 0 but no TEST PASSED marker"
        if expect_uart and expect_uart not in output:
            return "FAIL", f"golden UART text missing: {expect_uart!r}"
        return "PASS", ""
    for pattern in (r"^FAIL: (.*?)(?: @[\d.]+ \w+)?$", r"^TEST ERROR: (.*)$", r"^(TEST TIMEOUT.*)$", r"^Error: (.*)$"):
        m = re.search(pattern, output, re.MULTILINE)
        if m:
            return status, m.group(1).strip()
    return status, f"exit code {returncode}"


def normalise(sig: str) -> str:
    """Strip volatile details so equal root causes bucket together."""
    sig = re.sub(r"0x[0-9a-fA-F]+", "<hex>", sig)
    sig = re.sub(r"\b\d+(\.\d+)?\b", "<n>", sig)
    return sig


def run_one(t: dict, binary: Path, out: Path, wall_timeout: float, trace: bool) -> Result:
    cmd = [str(binary), "--script" if t["kind"] == "tcl" else "--fw", t["target"]]
    if "timeout_us" in t:
        cmd += ["--timeout-us", str(t["timeout_us"])]
    if trace:
        cmd += ["--trace", str(out / f"{t['name']}.trace.csv")]
    start = time.monotonic()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=wall_timeout)
        rc, text = p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired as e:
        rc, text = None, (e.stdout or b"").decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
    elapsed = time.monotonic() - start
    log = out / f"{t['name']}.log"
    log.write_text(" ".join(cmd) + "\n\n" + text)
    status, sig = classify(rc, text, t.get("expect_uart"))
    return Result(t["name"], status, elapsed, sig, str(log), t.get("tags", []))


def write_junit(results: list[Result], path: Path) -> None:
    fails = sum(r.status != "PASS" for r in results)
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             f'<testsuite name="minisoc" tests="{len(results)}" failures="{fails}">']
    for r in results:
        lines.append(f'  <testcase classname="minisoc" name="{escape(r.name)}" time="{r.seconds:.3f}">')
        if r.status != "PASS":
            lines.append(f'    <failure type="{r.status}" message="{escape(r.signature, {chr(34): "&quot;"})}"/>')
        lines.append("  </testcase>")
    lines.append("</testsuite>")
    path.write_text("\n".join(lines) + "\n")


def write_summary(results: list[Result], path: Path) -> str:
    passed = sum(r.status == "PASS" for r in results)
    L = [f"# MiniSoC regression: {passed}/{len(results)} passed", "",
         "| Test | Status | Time (s) | Signature |", "|---|---|---|---|"]
    for r in sorted(results, key=lambda r: (r.status == "PASS", r.name)):
        L.append(f"| {r.name} | {r.status} | {r.seconds:.2f} | {r.signature} |")
    buckets = defaultdict(list)
    for r in results:
        if r.status != "PASS":
            buckets[(r.status, normalise(r.signature))].append(r.name)
    if buckets:
        L += ["", f"## Failure buckets ({len(buckets)} unique)", ""]
        for (status, sig), names in sorted(buckets.items(), key=lambda kv: -len(kv[1])):
            L.append(f"- **{status}** `{sig}` - {len(names)} test(s): {', '.join(sorted(names))}")
    text = "\n".join(L) + "\n"
    path.write_text(text)
    return text


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default="build/minisoc")
    ap.add_argument("--manifest", default="tests/regression.yaml")
    ap.add_argument("--tags", default="", help="comma-separated; run tests having any of these tags")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--out", default="out")
    ap.add_argument("--wall-timeout", type=float, default=60.0)
    ap.add_argument("--trace", action="store_true", help="write a CSV bus trace per test")
    a = ap.parse_args()

    binary = Path(a.bin)
    if not binary.exists():
        print(f"regress: simulator not found at {binary} (build first)", file=sys.stderr)
        return 1
    tests = yaml.safe_load(Path(a.manifest).read_text())["tests"]
    if a.tags:
        wanted = set(a.tags.split(","))
        tests = [t for t in tests if wanted & set(t.get("tags", []))]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    with ThreadPoolExecutor(max_workers=a.jobs) as pool:  # each test is its own process
        results = list(pool.map(lambda t: run_one(t, binary, out, a.wall_timeout, a.trace), tests))

    for r in results:
        print(f"{r.status:12} {r.name:16} {r.seconds:6.2f}s  {r.signature}")
    write_junit(results, out / "junit.xml")
    write_summary(results, out / "summary.md")
    passed = sum(r.status == "PASS" for r in results)
    print(f"\n{passed}/{len(results)} passed - logs, junit.xml and summary.md in {out}/")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
