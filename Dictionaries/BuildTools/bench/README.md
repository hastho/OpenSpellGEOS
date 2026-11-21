# OpenSpellGEOS German morphology benchmark harness

This directory holds the host-only benchmark that compared the data-only
dictionary route against the bounded C89 morphology route for German, and the
regression guard derived from it. The full methodology, the 4,000-article heise
corpus run, the per-variant coverage table and the false-accept measurements
live in `TechDocs/Markdown/OpenSpellGEOSMorphologyBenchmark.md` (which
supersedes the original `~/OpenSpellBench/REPORT.md`).

## Files

- `bench.c` — host driver that links `Library/Spell/Open/openlex.inc` and
  `openmorph.inc` unmodified and runs the documented check path (exact byte
  lookup, folded-case lookup, bounded morphology) over a word list. Modes:
  `decide` (per-word acceptance) and `speed` (timed token stream).
- `build_variants.py` — builds the German DCT variants (`surf500`, `surf1500`,
  `core500`, `core1500`) from the pinned `Dictionaries/BuildTools/Data`
  snapshot so the same dictionaries can be rebuilt without network access.
- `extract_corpus.py` — extracts a German token corpus from a directory of
  heise `art.htm` files (or plain text), preserving original case and emitting
  GEOS/mac_roman bytes. Used to regenerate the corpus; not required for the
  regression run.
- `analyze.py` — aggregates `decide` output into the coverage table.
- `decide.py` — small helper for inspecting per-word decisions.
- `heise_sample_1000.txt` — a checked-in 1,000-word frequency-sorted sample
  of the heise corpus (GEOS bytes) used by the regression guard so the bench
  runs without the external `~/mirror/heise` archive.
- `run_benchmarks.py` — the regression entry point. Builds the harness and the
  `surf500`/`surf1500` variants, runs `decide` with morphology off and on over
  the checked-in sample, and asserts that morphology raises coverage above the
  data-only baseline by at least the measured margin. This guards
  `Library/Spell/Open/openmorph.inc` against accidental regressions and
  against a future revert to data-only selection.

## Running the regression

From the OpenSpellGEOS repository root, with
`Dictionaries/BuildTools/Data` already extracted beside `Data.zip`:

```
python3 Dictionaries/BuildTools/bench/run_benchmarks.py
# or, equivalently:
make -C Dictionaries/BuildTools/bench bench
```

The script compiles `bench.c` with `cc -std=c89 -pedantic`, builds the two
surface variants, and exits non-zero if morphology does not improve coverage
on the sample. A passing run prints something like:

```
surf500: m0 725/1000 (72.5%) -> m1 781/1000 (78.1%); margin +5.6 pp
surf1500: m0 930/1000 (93.0%) -> m1 948/1000 (94.8%); margin +1.8 pp
bench regression passed: morphology margin holds on heise sample
```

The checked-in sample is a regression guard, not a coverage claim. The full
4,000-article benchmark measured +6.9 pp at the 1.5 MB budget (85.9% with
morphology versus 79.0% data-only); see the benchmark document for the
complete numbers and the reproducibility commands that use the external
heise mirror.

## Reproducing the full benchmark

The commands in `TechDocs/Markdown/OpenSpellGEOSMorphologyBenchmark.md`
rebuild all four variants, extract the full corpus from `~/mirror/heise`, run
`decide` and `speed` over every variant, and feed the results to
`analyze.py`. Those steps need the external heise archive and several minutes
of host time; the in-repo `run_benchmarks.py` is the fast subset that runs in
CI without external data.
