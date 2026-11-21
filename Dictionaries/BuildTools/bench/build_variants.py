#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build the four German DCT benchmark variants.

The benchmark compares two dictionary strategies at two byte budgets:

  surface    Every inflected surface form competes for the byte budget
             directly (the upstream compact-selection route). The runtime
             performs exact/folded lookups only.
  core       Morphology-aware ranking (rebuild_german.py): high-family core
             forms are appended to the priority so base forms survive
             selection; the runtime additionally runs the bounded
             olMorphCheck reconstruction.

Budgets: 500000 bytes (the upstream per-file compact ceiling) and
1500000 bytes (the morphology sweet-spot analysis recommendation).

Prerequisites: extracted Dictionaries/BuildTools/Data snapshot.

Usage: python3 Dictionaries/BuildTools/bench/build_variants.py [--out DIR]
"""
import argparse
import sys
from pathlib import Path

# Resolve the OpenSpellGEOS repository root from this script's location so the
# bench works from any working directory, not just $HOME.
TOOLS = Path(__file__).resolve().parents[1]          # .../Dictionaries/BuildTools
REPO = TOOLS.parents[1]                              # OpenSpellGEOS root
DEFAULT_OUT = Path(__file__).resolve().parent / "dicts"

sys.path.insert(0, str(TOOLS))
import buildlex  # noqa: E402
import compactlex  # noqa: E402
import rebuild_german as rg  # noqa: E402


def select_core(entries, priority_path, budget, out_dir):
    """rebuild_german flow: existing priority + ranked core section."""
    attest_map = {k.decode("mac_roman"): k for k in entries}
    attest = set(attest_map)
    existing = [w.strip() for w in
                priority_path.read_text(encoding="utf-8").splitlines()
                if w.strip()]
    cores = {}
    for word in attest:
        if len(word) < rg.MIN_CORE_LEN:
            continue
        size = len(rg.family(word, attest))
        is_noun = word[:1].isupper()
        threshold = 2 if is_noun else rg.MIN_FAMILY
        if size >= threshold:
            cores[word] = max(size, 4) if is_noun else size
    existing_folded = {w.lower() for w in existing}
    section = set(cores) - existing_folded
    rows = sorted(section, key=lambda w: (-cores[w], len(w), w))[:rg.SECTION_CAP]
    priority_path_out = out_dir / "DE_DE.priority.core.txt"
    with priority_path_out.open("w", encoding="utf-8") as fh:
        for word in existing:
            fh.write(word + "\n")
        for word in rows:
            fh.write(word + "\n")
    print(f"cores ranked: {len(cores):,}; new section rows: {len(rows):,}",
          flush=True)
    return priority_path_out


def build_variant(name, entries, priority_path, budget, out_dir):
    priority = compactlex.Priority(priority_path)
    selected, est = rg.select_bounded(entries, priority, budget)
    variant_dir = out_dir / name
    variant_dir.mkdir(parents=True, exist_ok=True)
    result = buildlex.build_file(1, "DE_DE", selected,
                                 variant_dir / "DE_DE.DCT", max_bytes=None)
    print(f"{name}: {result['entries']:,} records, "
          f"{result['bytes']:,} bytes", flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT,
                        help="Output directory for the variant DCT files")
    parser.add_argument("--variants", nargs="*",
                        default=["surf500", "surf1500", "core500", "core1500"],
                        help="Subset of variants to build")
    args = parser.parse_args()
    out_dir = args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    data_dir = TOOLS / "Data"
    surface_priority = data_dir / "DE_DE.priority.txt"
    entries = buildlex.read_words(str(data_dir / "DE.words.txt"))
    print(f"attested forms: {len(entries):,}", flush=True)

    core_priority = None
    if any(v.startswith("core") for v in args.variants):
        core_priority = select_core(entries, surface_priority, 500_000, out_dir)

    spec = {
        "surf500": (surface_priority, 500_000),
        "surf1500": (surface_priority, 1_500_000),
        "core500": (core_priority, 500_000),
        "core1500": (core_priority, 1_500_000),
    }
    for name in args.variants:
        priority, budget = spec[name]
        build_variant(name, entries, priority, budget, out_dir)


if __name__ == "__main__":
    main()
