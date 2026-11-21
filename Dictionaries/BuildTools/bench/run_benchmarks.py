#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host regression benchmark for the OpenSpellGEOS German morphology rules.

Builds the surface-ranked German DCT variants (500 KB and 1.5 MB), links the
portable C89 reader and morphology analyzer (openlex.inc, openmorph.inc) into
the bench harness, runs the decide path over a checked-in heise sample word
list with morphology off and on, and asserts that morphology raises token
coverage above the data-only baseline by at least the measured margin.

This guards the bounded morphology rules in
Library/Spell/Open/openmorph.inc against accidental regressions and against a
future revert to data-only selection. The full benchmark, raw per-word
decisions and the 4,000-article corpus methodology are described in
TechDocs/Markdown/OpenSpellGEOSMorphologyBenchmark.md (and the original
REPORT.md that lived in ~/OpenSpellBench; this script is the in-repo
reproduction path).

Prerequisites:
    - Extract Dictionaries/BuildTools/Data.zip to Dictionaries/BuildTools/Data
    - A C89 compiler (cc/gcc) and Python 3
    - No network access is required

Usage, from the OpenSpellGEOS repository root:

    python3 Dictionaries/BuildTools/bench/run_benchmarks.py

Exit status: 0 on success, non-zero if the morphology margin regresses or any
build/run step fails.
"""
import subprocess
import sys
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
REPO = BENCH_DIR.parents[2]
LIB = REPO / "Library" / "Spell" / "Open"
SAMPLE = BENCH_DIR / "heise_sample_1000.txt"
DICTS_DIR = BENCH_DIR / "dicts"
BENCH_BIN = BENCH_DIR / "bench"

# Minimum coverage gain (percentage points) morphology must add over the
# data-only baseline on the checked-in sample. The full 4,000-article
# benchmark measured +6.9 pp at the 1.5 MB budget; the smaller checked-in
# sample is a regression guard, not a coverage claim, so the threshold is
# conservative.
MIN_MARGIN_PP_1500 = 1.0
MIN_MARGIN_PP_500 = 3.0


def run(cmd, **kw):
    print("+ " + " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run(cmd, **kw)


def build_harness():
    """Compile bench.c against the in-repo openlex.inc and openmorph.inc."""
    cc = ["cc", "-x", "c", "-std=c89", "-pedantic", "-Wall", "-Wextra",
          "-I", str(LIB), str(BENCH_DIR / "bench.c"), "-o", str(BENCH_BIN)]
    result = run(cc)
    if result.returncode != 0:
        raise SystemExit("bench harness build failed")
    if not BENCH_BIN.exists():
        raise SystemExit("bench binary missing after build")


def build_variants():
    """Build the surf500 and surf1500 DCT variants used by the regression."""
    script = BENCH_DIR / "build_variants.py"
    result = run([sys.executable, str(script),
                  "--out", str(DICTS_DIR),
                  "--variants", "surf500", "surf1500"])
    if result.returncode != 0:
        raise SystemExit("variant build failed")
    for name in ("surf500", "surf1500"):
        if not (DICTS_DIR / name / "DE_DE.DCT").exists():
            raise SystemExit("missing variant DCT: " + name)


def decide(variant, morph):
    """Run bench decide and return (unique, exact, folded, morph, miss)."""
    dct = DICTS_DIR / variant / "DE_DE.DCT"
    result = run([str(BENCH_BIN), "decide", str(dct), str(SAMPLE),
                  "--morph", str(morph), "--out", "/dev/null"],
                 capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"bench decide failed for {variant} m{morph}")
    summary = None
    for line in result.stderr.splitlines():
        if line.startswith("unique="):
            summary = line
    if not summary:
        raise SystemExit(f"no summary line for {variant} m{morph}")
    parts = dict(kv.split("=") for kv in summary.split() if "=" in kv)
    return {k: int(v) for k, v in parts.items()}


def coverage(counts):
    accepted = counts["exact"] + counts["folded"] + counts["morph"]
    total = counts["unique"]
    return accepted, total, accepted / total if total else 0.0


def check_margin(variant, min_pp):
    m0 = decide(variant, 0)
    m1 = decide(variant, 1)
    acc0, total, cov0 = coverage(m0)
    acc1, _, cov1 = coverage(m1)
    margin_pp = (cov1 - cov0) * 100.0
    print(f"{variant}: m0 {acc0}/{total} ({cov0*100:.1f}%) "
          f"-> m1 {acc1}/{total} ({cov1*100:.1f}%); margin {margin_pp:+.1f} pp",
          flush=True)
    # Morphology must not reduce coverage.
    if acc1 < acc0:
        raise SystemExit(f"{variant}: morphology REDUCED coverage "
                         f"({acc1} < {acc0})")
    if margin_pp < min_pp:
        raise SystemExit(f"{variant}: morphology margin {margin_pp:.1f} pp "
                         f"below threshold {min_pp} pp")
    if m1["morph"] == 0:
        raise SystemExit(f"{variant}: morphology accepted nothing; "
                         "openmorph.inc is not firing")


def main():
    build_harness()
    build_variants()
    check_margin("surf500", MIN_MARGIN_PP_500)
    check_margin("surf1500", MIN_MARGIN_PP_1500)
    print("bench regression passed: morphology margin holds on heise sample",
          flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
