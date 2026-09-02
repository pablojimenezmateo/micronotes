#!/usr/bin/env python3
"""Run the perf harness against the working tree and against a comparison
commit, then print what moved.

    tools/perf-compare.py [COMMIT]          # COMMIT defaults to HEAD
    ITERATIONS=5 tools/perf-compare.py main

Why this exists: a perf change is only real if it is measured against
something, and "I ran it before my change" stops being available the moment
the change is made. This builds the comparison commit in a throwaway git
worktree, so the baseline is reproducible after the fact and includes nothing
from the working tree.

The two halves of the report are read completely differently, and conflating
them is how perf work goes wrong:

  counters  DETERMINISTIC. The same workload produces byte-identical counter
            values on every run and in every build type, because they count
            events rather than cycles. Any change here is real, and a change
            of 1 is as real as a change of a million. These are the rows that
            prove an optimisation did what it claims.

  timings   NOISY. The same scenario has varied by more than 3x between runs
            on a busy machine with identical counters throughout. A timing
            delta is reported only when it exceeds a k-sigma band built from
            the per-iteration spread of both sides, and even then it is
            evidence, not proof. A timing win with no counter movement behind
            it is usually the machine, not the change.

Environment:
  ITERATIONS   harness runs per side (default 5). More runs, tighter band.
  NOISE_SIGMA  k for the k-sigma band (default 2.0).
  BUILD_TYPE   CMake build type for both sides (default Release). Timings from
               a Debug build are several times the real ones; only use Debug
               here if you are reading counters alone.
  KEEP         "1" keeps the temporary worktree and build tree.
  NO_COLOR     disable colour.
"""

from __future__ import annotations

import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ITERATIONS = int(os.environ.get("ITERATIONS", "5"))
NOISE_SIGMA = float(os.environ.get("NOISE_SIGMA", "2.0"))
BUILD_TYPE = os.environ.get("BUILD_TYPE", "Release")
KEEP = os.environ.get("KEEP") == "1"
USE_COLOR = not os.environ.get("NO_COLOR") and sys.stdout.isatty()

APP = "micronotes"


def paint(code: str, text: str) -> str:
    return f"\x1b[{code}m{text}\x1b[0m" if USE_COLOR else text


GOOD, BAD, DIM = "32", "31", "2"


# --- running -----------------------------------------------------------------

def build_and_run(source: Path, build: Path, label: str) -> list[str]:
    """Configure, build the harness, and run it ITERATIONS times."""
    print(f"building {label} ({source})...", file=sys.stderr)
    subprocess.run(
        ["cmake", "-S", str(source), "-B", str(build),
         f"-DCMAKE_BUILD_TYPE={BUILD_TYPE}",
         f"-D{APP.upper()}_PERF_HARNESS_BUILD=ON"],
        check=True, stdout=subprocess.DEVNULL)
    subprocess.run(
        ["cmake", "--build", str(build), "--target", f"{APP}_perf",
         "-j", str(os.cpu_count() or 4)],
        check=True, stdout=subprocess.DEVNULL)

    binary = build / "bin" / f"{APP}_perf"
    runs = []
    for i in range(ITERATIONS):
        print(f"  {label} run {i + 1}/{ITERATIONS}", file=sys.stderr)
        # The harness exits non-zero when a scenario is over budget, which is
        # exactly the situation this tool is used in. Its output is what
        # matters, not its status.
        result = subprocess.run([str(binary)], cwd=str(source),
                                capture_output=True, text=True)
        runs.append(result.stdout + result.stderr)
    return runs


# --- parsing -----------------------------------------------------------------

# "[perf]   342.368   342.368   502.106   160.963   8.3684   60  layout.update"
SCOPE_ROW = re.compile(
    r"^\[perf\]\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)\s+(\S.*)$")
# "layout.blocks_relaid    10725"
COUNTER_ROW = re.compile(r"^(\S+\.\S+)\s+(\d+)\s*$")


def parse(text: str) -> tuple[dict[str, float], dict[str, int]]:
    """Return (self-ms by scope label, value by counter name)."""
    scopes: dict[str, float] = {}
    counters: dict[str, int] = {}
    in_counters = False
    for line in text.splitlines():
        if line.startswith("=== performance counters"):
            in_counters = True
            continue
        if line.startswith("==="):
            in_counters = False
        if in_counters:
            match = COUNTER_ROW.match(line)
            if match:
                counters[match.group(1)] = int(match.group(2))
            continue
        # Only the first (self-ranked) table; the main-thread ranking below it
        # has a different shape and would double-count these labels.
        match = SCOPE_ROW.match(line)
        if match and " calls  " not in line:
            scopes[match.group(7).strip()] = float(match.group(1))
    return scopes, counters


def collect(runs: list[str]) -> tuple[dict[str, list[float]], dict[str, int]]:
    scopes: dict[str, list[float]] = {}
    counters: dict[str, int] = {}
    for text in runs:
        run_scopes, run_counters = parse(text)
        for label, value in run_scopes.items():
            scopes.setdefault(label, []).append(value)
        # Counters are deterministic, so the last run's values are every run's.
        counters.update(run_counters)
    return scopes, counters


# --- reporting ---------------------------------------------------------------

def report_counters(current: dict[str, int], target: dict[str, int]) -> int:
    print("\n=== counters (deterministic: every difference is real) ===")
    names = sorted(set(current) | set(target))
    changed = 0
    for name in names:
        now, was = current.get(name, 0), target.get(name, 0)
        if now == was:
            continue
        changed += 1
        delta = now - was
        pct = (delta / was * 100.0) if was else float("inf")
        colour = GOOD if delta < 0 else BAD
        arrow = f"{delta:+d}" if abs(delta) < 1_000_000 else f"{delta / 1e6:+.1f}M"
        pct_text = "  (new)" if not was else f"  ({pct:+.1f}%)"
        print(f"  {name:<46} {was:>14,} -> {now:>14,}  " + paint(colour, arrow + pct_text))
    if not changed:
        print("  no counter moved: the two sides did exactly the same work")
    return changed


def report_timings(current: dict[str, list[float]], target: dict[str, list[float]]) -> None:
    print(f"\n=== scope self-time, ms (noisy: {NOISE_SIGMA}-sigma band over "
          f"{ITERATIONS} runs a side) ===")
    names = sorted(set(current) | set(target),
                   key=lambda n: -max(statistics.median(current.get(n, [0])),
                                      statistics.median(target.get(n, [0]))))
    for name in names:
        now_samples = current.get(name, [0.0])
        was_samples = target.get(name, [0.0])
        now = statistics.median(now_samples)
        was = statistics.median(was_samples)
        delta = now - was
        spread = max(statistics.pstdev(now_samples) if len(now_samples) > 1 else 0.0,
                     statistics.pstdev(was_samples) if len(was_samples) > 1 else 0.0)
        band = NOISE_SIGMA * spread
        change = f"({delta / was * 100.0:+7.1f}%)" if was else "(new label)"
        line = f"  {name:<46} {was:>10.3f} -> {now:>10.3f}  {delta:+9.3f} {change}"
        if abs(delta) <= band:
            print(paint(DIM, line + "  [noise]"))
        else:
            print(paint(GOOD if delta < 0 else BAD, line))
    print("\n  A timing move with no counter behind it is usually the machine.")


def main() -> int:
    commit = sys.argv[1] if len(sys.argv) > 1 else "HEAD"
    sha = subprocess.run(["git", "rev-parse", commit], cwd=REPO,
                         capture_output=True, text=True, check=True).stdout.strip()

    scratch = Path(tempfile.mkdtemp(prefix=f"{APP}-perf-compare-"))
    worktree = scratch / "baseline"
    try:
        subprocess.run(["git", "worktree", "add", "--detach", str(worktree), sha],
                       cwd=REPO, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        # A fresh worktree has empty submodule directories, and md4c is a
        # submodule: without this the baseline fails to configure and the tool
        # reports nothing rather than a comparison.
        subprocess.run(["git", "submodule", "update", "--init", "--recursive"],
                       cwd=worktree, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        current_runs = build_and_run(REPO, REPO / f"build-perf-compare", "working tree")
        target_runs = build_and_run(worktree, scratch / "build-baseline", f"{commit} ({sha[:9]})")

        current_scopes, current_counters = collect(current_runs)
        target_scopes, target_counters = collect(target_runs)

        print(f"\nworking tree vs {commit} ({sha[:9]}), {BUILD_TYPE}, "
              f"{ITERATIONS} runs a side")
        report_counters(current_counters, target_counters)
        report_timings(current_scopes, target_scopes)
        return 0
    finally:
        if KEEP:
            print(f"\nkept: {scratch}", file=sys.stderr)
        else:
            subprocess.run(["git", "worktree", "remove", "--force", str(worktree)],
                           cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
