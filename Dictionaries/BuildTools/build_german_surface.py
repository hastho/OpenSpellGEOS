#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Rebuild the German DCT and HYP at a 1.5 MB byte budget with the standard
frequency-ranked surface priority (DE_DE.priority.txt).

This is the recommended German build path documented in
TechDocs/Markdown/OpenSpellGEOSData.md. The benchmark in
TechDocs/Markdown/OpenSpellGEOSMorphologyBenchmark.md measured that the
surface-ranked dictionary plus runtime morphology (in
Library/Spell/Open/openmorph.inc) reaches 85.9% token coverage on a 4,000-
article heise corpus, beating a morphology-aware core/base-form rebuild
(80.6%) by 5.3 percentage points because exact surface hits are cheaper and
more reliable than rule reconstructions. German inflection is handled at
runtime by openmorph.inc, so the dictionary keeps the most frequent surface
forms and lets the rules rescue the long tail.

The 1,500,000-byte per-file budget exceeds the generic 500,000-byte ceiling
used by the other profiles; selection uses rebuild_german.select_bounded,
which does not call compactlex.check_budget, so the German exception is
explicit and isolated to this script. HYP is rebuilt from the selected DCT
vocabulary so hyphenation stays consistent with the larger spelling
dictionary.

Prerequisites:
    - Extract Dictionaries/BuildTools/Data.zip to Dictionaries/BuildTools/Data
    - The Data snapshot must contain DE.words.txt, DE_DE.priority.txt,
      DE.patterns.txt and DE.hyp-exceptions.json

Usage:
    python3 Dictionaries/BuildTools/build_german_surface.py \\
        --output DICTS-german --budget 1500000

Output:
    DE_DE.DCT and DE_DE.HYP in the specified output directory, plus a JSON
    metadata summary on stdout for updating BUILD.json.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import buildlex  # noqa: E402
import compactlex  # noqa: E402
import rebuild_german as rg  # noqa: E402


def select_surface(entries, priority, budget):
    """Frequency-ranked surface selection at the German 1.5 MB budget.

    Mirrors compactlex.select_entries but uses rebuild_german.select_bounded
    so the budget may exceed compactlex.MAX_FILE_BYTES for the documented
    German exception. Returns (selected, estimated_bytes).
    """
    return rg.select_bounded(entries, priority, budget)


def build_german(data_dir, out_dir, budget):
    data_dir = data_dir.resolve()
    out_dir = out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    surface_priority = data_dir / "DE_DE.priority.txt"
    entries = buildlex.read_words(str(data_dir / "DE.words.txt"))
    print(f"Attested forms: {len(entries):,}", flush=True)

    priority = compactlex.Priority(
        surface_priority,
        metadata={
            "file": "DE_DE.priority.txt",
            "kind": "source-guided-heuristic",
            "source": ("Authored essentials and attested relatives; existing words "
                       "also present as reviewed LOD German translation headwords, "
                       "then attested relatives. Heuristic, not native corpus "
                       "frequency. Surface-ranked per the morphology benchmark."),
        },
    )
    selected, est_bytes = select_surface(entries, priority, budget)
    print(f"Selected: {len(selected):,} records, est {est_bytes:,} bytes",
          flush=True)

    dct_path = out_dir / "DE_DE.DCT"
    dct_result = buildlex.build_file(1, "DE_DE", selected, dct_path,
                                     max_bytes=None)
    print(f"DCT: {dct_result['entries']:,} records, "
          f"{dct_result['bytes']:,} bytes", flush=True)
    if dct_result["bytes"] > budget:
        raise ValueError(
            f"DE_DE.DCT {dct_result['bytes']:,} bytes exceeds budget {budget:,}")

    # Rebuild HYP from the selected DCT vocabulary so hyphenation coverage
    # tracks the larger spelling dictionary.
    vocab_path = out_dir / ".DE_DE.vocab.tmp"
    vocab_path.write_text(
        "\n".join(sorted({k.decode("mac_roman").lower()
                          for k in selected})) + "\n",
        encoding="utf-8")
    hentries = buildlex.build_hyphenation(
        str(vocab_path),
        patterns=str(data_dir / "DE.patterns.txt"),
        exceptions=str(data_dir / "DE.hyp-exceptions.json"),
        left_min=2, right_min=2)
    hsel, hbytes = select_surface(hentries, priority, budget)
    hyp_path = out_dir / "DE_DE.HYP"
    hyp_result = buildlex.build_file(3, "DE_DE", hsel, hyp_path,
                                    max_bytes=None)
    print(f"HYP: {hyp_result['entries']:,} records, "
          f"{hyp_result['bytes']:,} bytes", flush=True)
    if hyp_result["bytes"] > budget:
        raise ValueError(
            f"DE_DE.HYP {hyp_result['bytes']:,} bytes exceeds budget {budget:,}")
    vocab_path.unlink()

    metadata = {
        "budget": budget,
        "priority": {
            "file": "DE_DE.priority.txt",
            "kind": "source-guided-heuristic",
            "sha256": hashlib.sha256(surface_priority.read_bytes()).hexdigest(),
            "ranked_keys": priority.metadata.get("ranked_keys"),
        },
        "dct": {
            "file": str(dct_path),
            "entries": dct_result["entries"],
            "bytes": dct_result["bytes"],
            "sha256": dct_result["sha256"],
            "blocks": dct_result["blocks"],
            "selection": {
                "policy": compactlex.SELECTION_POLICY,
                "candidate_records": len(entries),
                "selected_records": len(selected),
                "omitted_records": len(entries) - len(selected),
                "max_bytes": budget,
                "estimated_bytes": est_bytes,
                "optimality": "largest fitting tested prefix; not a global "
                              "maximum claim",
            },
        },
        "hyp": {
            "file": str(hyp_path),
            "entries": hyp_result["entries"],
            "bytes": hyp_result["bytes"],
            "sha256": hyp_result["sha256"],
            "blocks": hyp_result["blocks"],
            "selection": {
                "policy": compactlex.SELECTION_POLICY,
                "candidate_records": len(hentries),
                "selected_records": len(hsel),
                "omitted_records": len(hentries) - len(hsel),
                "max_bytes": budget,
                "estimated_bytes": hbytes,
                "optimality": "largest fitting tested prefix; not a global "
                              "maximum claim",
            },
        },
    }
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", default=None,
                        help="Data directory (default: beside this script)")
    parser.add_argument("--output", default=".",
                        help="Output directory for DCT and HYP files")
    parser.add_argument("--budget", type=int, default=1_500_000,
                        help="Per-file byte ceiling (default: 1500000)")
    parser.add_argument("--metadata", default=None,
                        help="Optional path to write a JSON metadata summary")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    data_dir = Path(args.data) if args.data else script_dir / "Data"
    out_dir = Path(args.output)
    budget = args.budget

    print(f"Data: {data_dir}")
    print(f"Output: {out_dir}")
    print(f"Budget: {budget:,} bytes", flush=True)

    metadata = build_german(data_dir, out_dir, budget)
    summary = {
        "project": "OpenSpellGEOS",
        "format": "OLX1",
        "profile": "DE_DE",
        "build_path": "surface-frequency-ranked-plus-runtime-morphology",
        "max_bytes": budget,
        **metadata,
    }
    text = json.dumps(summary, indent=2, sort_keys=True) + "\n"
    if args.metadata:
        Path(args.metadata).write_text(text, encoding="utf-8")
        print(f"Metadata written to {args.metadata}", flush=True)
    else:
        sys.stdout.write(text)
    print("Done.", flush=True)


if __name__ == "__main__":
    main()
