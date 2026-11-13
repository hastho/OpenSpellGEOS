# SPDX-License-Identifier: Apache-2.0
"""Prototype of a stem-based German OLX1 dictionary build.

Stems the lowercase surface forms of the pinned DE.words.txt source with the
verified Snowball German stemmer, reports distinct-stem compression, writes
real OLX1 byte sizes with buildlex.build_file, replicates the shipped
priority selection at the released 1000000-byte budget, and measures held-out
recall plus false accepts on generated misspellings at Unicode char level.

Every phase number lands in report.json inside the work directory. The work
directory is scratch; only report.json carries results between runs.
"""

import argparse
import json
import random
import sys
import time
import zipfile
from collections import Counter
from hashlib import sha256
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import buildlex
import compactlex
import snowball_de

LETTERS = "abcdefghijklmnopqrstuvwxyzäöüß"
KINDS = ("deletion", "substitution", "transposition", "doubled")


def log(text):
    print(text, flush=True)


def phase_load(voc_path):
    """Return the raw line count and one lowercase surface form counter."""
    forms = Counter()
    raw = 0
    begin = time.perf_counter()
    with voc_path.open(encoding="utf-8") as stream:
        for line in stream:
            word = line.strip()
            if not word or word.startswith("#"):
                continue
            raw += 1
            forms[word.lower()] += 1
    log("load: %d raw lines, %d lowercase forms, %.1fs" %
        (raw, len(forms), time.perf_counter() - begin))
    return raw, forms


def phase_stems(forms):
    """Stem every lowercase form once and hold every 10th form for recall."""
    counts_all = Counter()
    held = []
    begin = time.perf_counter()
    stem = snowball_de.stem_de
    for index, form in enumerate(forms):
        stem_all = stem(form)
        counts_all[stem_all] += 1
        if index % 10 == 0:
            held.append(form)
        if index % 200000 == 0:
            log("stems: %d forms, %.1fs" % (index, time.perf_counter() - begin))
    log("stems: %d distinct from %d forms, %.1fs" %
        (len(counts_all), len(forms), time.perf_counter() - begin))
    return counts_all, held


def phase_sizes(work, report, voc_path, priority_path, zip_path, forms,
                counts_all, max_bytes):
    """Write real OLX1 builds: surface replication, all forms, all stems."""
    report["surface"] = {}
    report["builds"] = {}
    entries = buildlex.read_words(voc_path)
    report["builds"]["all_forms_entries"] = len(entries)

    compactlex.MAX_FILE_BYTES = max_bytes
    priority = compactlex.Priority(priority_path, {
        "file": priority_path.name, "kind": "source-guided-heuristic"})
    selected, selection = compactlex.select_entries(entries, priority, max_bytes)
    metadata = buildlex.build_file(1, "DE_DE", selected,
                                   work / "surface.DCT")
    if metadata["bytes"] > max_bytes:
        raise ValueError("surface build needs %d bytes, above the %d limit" %
                         (metadata["bytes"], max_bytes))
    report["surface"]["selection"] = selection
    report["surface"]["shipped_bytes"] = metadata["bytes"]
    report["surface"]["shipped_sha256"] = metadata["sha256"]
    log("surface replication: %d records, %d bytes" %
        (selection["selected_records"], metadata["bytes"]))

    shipped = work / "shipped.DCT"
    if not shipped.exists():
        with zipfile.ZipFile(zip_path) as archive:
            shipped.write_bytes(archive.read("DE_DE.DCT"))
    report["surface"]["replication_exact"] = (
        sha256(shipped.read_bytes()).digest() ==
        sha256((work / "surface.DCT").read_bytes()).digest())

    selected_texts = [buildlex.decode_geos(key) for key in selected]
    report["stemset"] = {}
    report["stemset"]["selected_texts"] = len(selected_texts)
    report["stemset"]["lower_forms"] = sum(
        1 for text in selected_texts if text == text.lower())

    stems_all = {snowball_de.stem_de(text.lower()) for text in selected_texts}
    report["stemset"]["stems_all"] = len(stems_all)
    entries_stems = {}
    skipped = 0
    for stem_all in sorted(stems_all):
        try:
            entries_stems[buildlex.encode_key(stem_all)] = b"\x01"
        except ValueError:
            skipped += 1
    report["stemset"]["unencodable_stems"] = skipped
    metadata = buildlex.build_file(1, "DE_DE", entries_stems,
                                   work / "stems-selected.DCT")
    report["builds"]["stems_selected_bytes"] = metadata["bytes"]
    report["builds"]["stems_selected_sha256"] = metadata["sha256"]
    log("stems of selection: %d records, %d bytes" %
        (metadata["entries"], metadata["bytes"]))

    metadata = buildlex.build_file(1, "DE_DE", entries, work / "all-forms.DCT")
    report["builds"]["all_forms_bytes"] = metadata["bytes"]
    report["builds"]["all_forms_entries_written"] = metadata["entries"]
    log("all-forms build: %d records, %d bytes" %
        (metadata["entries"], metadata["bytes"]))

    stems_source = {snowball_de.stem_de(buildlex.decode_geos(key))
                    for key in entries}
    entries_stems = {}
    skipped = 0
    for stem_all in sorted(stems_source):
        try:
            entries_stems[buildlex.encode_key(stem_all)] = b"\x01"
        except ValueError:
            skipped += 1
    report["builds"]["all_stems_distinct"] = len(stems_source)
    report["builds"]["all_stems_unencodable"] = skipped
    metadata = buildlex.build_file(1, "DE_DE", entries_stems,
                                   work / "all-stems.DCT")
    report["builds"]["all_stems_bytes"] = metadata["bytes"]
    report["builds"]["all_stems_entries_written"] = metadata["entries"]
    log("all-stems build: %d records, %d bytes" %
        (metadata["entries"], metadata["bytes"]))
    return selected_texts


def phase_recall(forms, held):
    """Measure held-out recall: stems of held forms against the 90% stems."""
    begin = time.perf_counter()
    stems90 = set()
    stem = snowball_de.stem_de
    for index, form in enumerate(forms):
        if index % 10 != 0:
            stems90.add(stem(form))
    covered = sum(1 for form in held if stem(form) in stems90)
    log("held-out recall: %d/%d = %.3f%%, %.1fs" %
        (covered, len(held), covered * 100.0 / len(held),
         time.perf_counter() - begin))
    return {"held_forms": len(held), "covered": covered,
            "recall_percent": covered * 100.0 / len(held)}


def mutate(word, kind, rng):
    """Return one generated misspelling at Unicode char level, or None."""
    if kind == "deletion":
        position = rng.randrange(len(word))
        return word[:position] + word[position + 1:]
    if kind == "substitution":
        for _ in range(4):
            position = rng.randrange(len(word))
            result = word[:position] + rng.choice(LETTERS) + word[position + 1:]
            if result != word:
                return result
        return None
    if kind == "transposition":
        for _ in range(4):
            position = rng.randrange(len(word) - 1)
            result = word[:position] + word[position + 1] + word[position] + \
                word[position + 2:]
            if result != word:
                return result
        return None
    position = rng.randrange(len(word))
    return word[:position + 1] + word[position] + word[position + 1:]


def phase_false_accepts(selected_texts, total):
    """Count accepted misspellings under stem and exact-form designs."""
    begin = time.perf_counter()
    rng = random.Random(20261113)
    lower_forms = {text for text in selected_texts if text == text.lower()}
    candidates = sorted({text.lower() for text in selected_texts
                         if len(text) >= 2})
    stems_lower = {snowball_de.stem_de(text) for text in lower_forms}
    stems_all = {snowball_de.stem_de(text.lower()) for text in selected_texts}
    per_class = total // len(KINDS)
    result = {}
    for kind in KINDS:
        counters = {"generated": 0, "true_accept": 0, "false_accept": 0,
                    "false_accept_lower_only": 0, "skipped": 0}
        for _ in range(per_class):
            word = candidates[rng.randrange(len(candidates))]
            query = mutate(word, kind, rng)
            if query is None:
                counters["skipped"] += 1
                continue
            counters["generated"] += 1
            if query in lower_forms:
                counters["true_accept"] += 1
                continue
            if snowball_de.stem_de(query) in stems_all:
                counters["false_accept"] += 1
            if snowball_de.stem_de(query) in stems_lower:
                counters["false_accept_lower_only"] += 1
        result[kind] = counters
        log("false accepts %s: %d generated, %d true, %d false (%.2f%%)" %
            (kind, counters["generated"], counters["true_accept"],
             counters["false_accept"],
             counters["false_accept"] * 100.0 /
             max(counters["generated"], 1)))
    result["lower_forms"] = len(lower_forms)
    result["candidates"] = len(candidates)
    log("false accepts done in %.1fs" % (time.perf_counter() - begin))
    return result


def phase_coverage(forms, report, selected_texts):
    """Compare shipped and stem design coverage of the full source corpus."""
    begin = time.perf_counter()
    lower_forms = {text for text in selected_texts if text == text.lower()}
    stems_all = {snowball_de.stem_de(text.lower()) for text in selected_texts}
    overlap = sum(1 for form in forms if form in lower_forms)
    covered = sum(1 for form in forms
                  if snowball_de.stem_de(form) in stems_all)
    log("coverage: exact %d/%d, stems %d/%d, %.1fs" %
        (overlap, len(forms), covered, len(forms),
         time.perf_counter() - begin))
    return {"forms": len(forms),
            "exact_covered": overlap,
            "exact_percent": overlap * 100.0 / len(forms),
            "stem_covered": covered,
            "stem_percent": covered * 100.0 / len(forms)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parent.parent.parent.parent
    parser.add_argument("--voc", type=Path,
                        default=root / "Dictionaries/BuildTools/Data/DE.words.txt")
    parser.add_argument("--priority", type=Path,
                        default=root / "Dictionaries/BuildTools/Data/DE_DE.priority.txt")
    parser.add_argument("--zip", type=Path,
                        default=root / "Dictionaries/DE_DE.zip")
    parser.add_argument("--work", type=Path,
                        default=Path("/home/thomas/pcgeos-tools/temp/stemexp"))
    parser.add_argument("--max-bytes", type=int, default=1000000)
    parser.add_argument("--misspellings", type=int, default=100000)
    args = parser.parse_args(argv)
    work = args.work
    work.mkdir(parents=True, exist_ok=True)
    report = {"voc": str(args.voc), "max_bytes": args.max_bytes}

    raw, forms = phase_load(args.voc)
    report["raw_lines"] = raw
    report["lowercase_forms"] = len(forms)

    counts_all, held = phase_stems(forms)
    report["stems"] = {"distinct": len(counts_all),
                       "forms": len(forms),
                       "held_forms": len(held)}

    selected_texts = phase_sizes(work, report, args.voc, args.priority,
                                 args.zip, forms, counts_all, args.max_bytes)

    report["recall"] = phase_recall(forms, held)
    report["false_accepts"] = phase_false_accepts(selected_texts,
                                                   args.misspellings)
    report["coverage"] = phase_coverage(forms, report, selected_texts)

    with (work / "report.json").open("w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=1, ensure_ascii=False)
    log("report written: %s" % (work / "report.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
