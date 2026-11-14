#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Rebuild the German DCT and HYP at a custom byte budget with the
morphology-aware ranking described in TechDocs/Markdown/OpenSpellGEOSMorphology.md.

The ranking identifies cores: attested forms whose morphology family has at
least four attested relatives for lowercase forms or two for capitalized
nouns. The cores replace the length-ordered fallback in the priority file.
The compound tier is removed because the runtime analyzer validates
compounds from stored parts.

Prerequisites:
    - Extract Dictionaries/BuildTools/Data.zip to Dictionaries/BuildTools/Data
    - The Data snapshot must contain DE.words.txt, DE_DE.priority.txt,
      DE.patterns.txt and DE.hyp-exceptions.json

Usage:
    python3 Dictionaries/BuildTools/rebuild_german.py --budget 1500000

Output:
    DE_DE.DCT and DE_DE.HYP in the specified output directory.
"""
import argparse
import heapq
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import buildlex
import compactlex

MIN_CORE_LEN = 5
MIN_FAMILY = 4
SECTION_CAP = 200_000

SEP_PREFIX = (
    "ab", "an", "auf", "aus", "bei", "da", "ein", "her", "hin", "los",
    "mit", "nach", "vor", "weg", "zu",
)
STEM_SUFFIX = (
    "e", "st", "est", "t", "te", "ten", "test", "tet", "end", "ende",
    "endes", "et", "ete", "eten",
)
GE_SUFFIX = ("t", "et", "ete", "eten")
ABL_OPS = (
    ("au", "äu"), ("ei", "ie"), ("ei", "i"), ("ie", "o"), ("ie", "a"),
    ("e", "i"), ("e", "o"), ("i", "u"), ("i", "e"), ("a", "ä"),
    ("o", "ö"), ("u", "ü"), ("ä", "a"), ("ö", "o"), ("ü", "u"),
)
UMLAUT = (("a", "ä"), ("o", "ö"), ("u", "ü"), ("au", "äu"))
GENERIC = ("e", "en", "er", "es", "em", "n", "s")
SUPERL = ("ste", "sten", "stes", "stem")
NOUN_PL = ("s", "n", "e", "en")


def ablaut_variants(stem):
    out = set()
    for src, dst in ABL_OPS:
        pos = stem.find(src)
        if pos >= 0:
            out.add(stem[:pos] + dst + stem[pos + len(src):])
    return out


def umlaut_variants(word):
    out = set()
    for src, dst in UMLAUT:
        pos = word.find(src)
        if pos > 0:
            out.add(word[:pos] + dst + word[pos + len(src):])
    return out


def family(c, attest):
    fam = set()
    for s in GENERIC:
        fam.add(c + s)
    if c.endswith("en") or c.endswith("n"):
        stem = c[:-2] if c.endswith("en") else c[:-1]
        if stem:
            plain = 0
            for s in STEM_SUFFIX:
                if stem + s in attest:
                    plain += 1
                fam.add(stem + s)
            for s in GE_SUFFIX:
                fam.add("ge" + stem + s)
            if plain >= 2:
                for s in ("t", "et", "te", "eten", "st", "ten"):
                    for p in SEP_PREFIX:
                        if c.startswith(p) and len(c) >= len(p) + 4:
                            rest = c[len(p):]
                            rstem = rest[:-2] if rest.endswith("en") else rest
                            fam.add(p + "ge" + rstem + s)
                for v in ablaut_variants(stem):
                    for s in ("en", "t", "et", "te", "ten", "end"):
                        fam.add(v + s)
                        fam.add("ge" + v + s)
    if c.endswith("eln") or c.endswith("ern"):
        base = c[:-1]
        for s in ("t", "te", "ten", "st", "e"):
            fam.add(base + s)
    for u in umlaut_variants(c):
        for s in ("e", "er", "es", "em", "en", "ere", "eren", "eres",
                   "erem", "ste", "sten", "stes", "stem"):
            fam.add(u + s)
    for s in SUPERL:
        fam.add(c + s)
    if c[:1].isupper():
        for s in NOUN_PL:
            fam.add(c + s)
        for u in umlaut_variants(c):
            for s in ("e", "er", "en"):
                fam.add(u + s)
    return {w for w in fam if w in attest}


def select_bounded(entries, priority, budget):
    max_blocks = (budget - 64) // (buildlex.BLOCK_SIZE + buildlex.INDEX_SIZE)
    candidate_limit = min(len(entries), max_blocks * 204)
    ordered = heapq.nsmallest(candidate_limit, entries, key=priority.key)
    lower, upper = 0, len(ordered)
    best_count, best_bytes = 0, 64
    while lower <= upper:
        count = lower + (upper - lower) // 2
        trial = {key: entries[key] for key in ordered[:count]}
        size = compactlex.estimate_size(trial)
        if size <= budget:
            best_count, best_bytes = count, size
            lower = count + 1
        else:
            upper = count - 1
    return {key: entries[key] for key in ordered[:best_count]}, best_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", default=None,
                        help="Data directory (default: beside this script)")
    parser.add_argument("--output", default=".",
                        help="Output directory for DCT and HYP files")
    parser.add_argument("--budget", type=int, default=1_500_000,
                        help="Per-file byte budget (default: 1500000)")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    data_dir = Path(args.data) if args.data else script_dir / "Data"
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)
    budget = args.budget

    print(f"Data: {data_dir}")
    print(f"Output: {out_dir}")
    print(f"Budget: {budget:,} bytes", flush=True)

    entries = buildlex.read_words(str(data_dir / "DE.words.txt"))
    attest_map = {k.decode("mac_roman"): k for k in entries}
    attest = set(attest_map)
    print(f"Attested forms: {len(attest):,}", flush=True)

    existing = [w.strip() for w in
                (data_dir / "DE_DE.priority.txt").read_text(
                    encoding="utf-8").splitlines() if w.strip()]
    print(f"Existing priority entries: {len(existing):,}", flush=True)

    cores = {}
    for word in attest:
        if len(word) < MIN_CORE_LEN:
            continue
        size = len(family(word, attest))
        is_noun = word[:1].isupper()
        threshold = 2 if is_noun else MIN_FAMILY
        if size >= threshold:
            cores[word] = max(size, 4) if is_noun else size
    print(f"Ranked cores: {len(cores):,}", flush=True)

    existing_folded = {w.lower() for w in existing}
    section = set(cores) - existing_folded
    rows = sorted(section, key=lambda w: (-cores[w], len(w), w))[:SECTION_CAP]
    print(f"New ranked section: {len(rows):,}", flush=True)

    priority_path = out_dir / "DE_DE.priority.new.txt"
    with priority_path.open("w", encoding="utf-8") as fh:
        for word in existing:
            fh.write(word + "\n")
        for word in rows:
            fh.write(word + "\n")
    print(f"Priority file: {len(existing) + len(rows):,} lines", flush=True)

    priority = compactlex.Priority(priority_path)
    selected, est_bytes = select_bounded(entries, priority, budget)
    print(f"Selected: {len(selected):,} records, est {est_bytes:,} bytes",
          flush=True)

    dct_result = buildlex.build_file(1, "DE_DE", selected,
                                     out_dir / "DE_DE.DCT", max_bytes=None)
    print(f"DCT: {dct_result['entries']:,} records, "
          f"{dct_result['bytes']:,} bytes", flush=True)

    vocab_path = out_dir / "DE_DE.vocab.txt"
    vocab_path.write_text(
        "\n".join(sorted({k.decode("mac_roman").lower()
                          for k in selected})) + "\n",
        encoding="utf-8")
    hentries = buildlex.build_hyphenation(
        str(vocab_path),
        patterns=str(data_dir / "DE.patterns.txt"),
        exceptions=str(data_dir / "DE.hyp-exceptions.json"),
        left_min=2, right_min=2)
    hpriority = compactlex.Priority(priority_path)
    hsel, hbytes = select_bounded(hentries, hpriority, budget)
    hyp_result = buildlex.build_file(3, "DE_DE", hsel,
                                     out_dir / "DE_DE.HYP", max_bytes=None)
    print(f"HYP: {hyp_result['entries']:,} records, "
          f"{hyp_result['bytes']:,} bytes", flush=True)

    vocab_path.unlink()
    priority_path.unlink()
    print("Done.", flush=True)


if __name__ == "__main__":
    main()
