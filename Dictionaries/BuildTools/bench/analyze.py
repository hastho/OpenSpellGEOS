#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Combine per-word decide results with token frequencies and the oracle.

Primary metric: token-weighted coverage. On real German text the fraction of
tokens a spell checker accepts is the user-visible "false flag" rate. A higher
coverage means fewer correct words are underlined as misspelled.

Secondary metric: oracle precision/recall. The oracle is the igerman98 headword
list (a lemmatised reference). Inflected forms accepted by morphology are not
listed as headwords, so recall is the meaningful half; precision is reported
with an explicit caveat.
"""
from __future__ import unicode_literals
import io
import json
import os
import sys

CORPUS = "corpus/freq.tsv"
RESULTS = "results"
ORACLE = "/tmp/igerman98_words.txt"
BLACKLIST = "/tmp/blacklist.txt"


def read_set(path):
    s = set()
    with io.open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            w = line.strip()
            if w:
                s.add(w)
    return s


def read_freq(path):
    freq = {}
    with io.open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 2:
                try:
                    freq[parts[0]] = int(parts[1])
                except ValueError:
                    pass
    return freq


def read_decisions(path):
    d = {}
    word = None
    with io.open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            w = line.rstrip("\n")
            if word is None:
                word = w
            else:
                d[word] = w.strip()
                word = None
    return d


def main():
    freq = read_freq(CORPUS)
    oracle = read_set(ORACLE)
    oracle -= read_set(BLACKLIST)
    total_tokens = sum(freq.values())

    paths = [os.path.join(RESULTS, n)
             for n in sorted(os.listdir(RESULTS))
             if n.startswith("decide_") and n.endswith(".tsv")]

    rows = []
    for p in paths:
        name = os.path.basename(p)[7:-4]
        dec = read_decisions(p)
        type_acc = type_mis = 0
        tok_acc = tok_mis = 0
        tok_exact = tok_fold = tok_morph = 0
        for w, s in dec.items():
            f = freq.get(w, 1)
            if s in ("1", "2", "3"):
                type_acc += 1
                tok_acc += f
                if s == "1":
                    tok_exact += f
                elif s == "2":
                    tok_fold += f
                else:
                    tok_morph += f
            else:
                type_mis += 1
                tok_mis += f
        # oracle halves (vs lemmatised headwords; inflected forms caveat)
        accepted = [w for w, s in dec.items() if s in ("1", "2", "3")]
        tp = sum(1 for w in accepted if w in oracle)
        rows.append({
            "config": name,
            "dct_bytes": 0,
            "type_coverage": type_acc / (type_acc + type_mis),
            "token_coverage": tok_acc / total_tokens,
            "token_exact": tok_exact,
            "token_folded": tok_fold,
            "token_morph": tok_morph,
            "types_accepted": type_acc,
            "tokens_total": total_tokens,
            "oracle_tp": tp,
            "oracle_fp": len(accepted) - tp,
        })

    # attach dct sizes
    sizes = {
        "upstream500": 999766, "surf500": 499915, "surf1500": 1499617,
        "core500": 499915, "core1500": 1499617,
    }
    for r in rows:
        base = r["config"].rsplit("_m", 1)[0]
        r["dct_bytes"] = sizes.get(base, 0)

    rows.sort(key=lambda r: r["config"])
    out = {
        "total_tokens": total_tokens,
        "total_types": sum(1 for _ in read_freq(CORPUS)),
        "oracle_headwords": len(oracle),
        "rows": rows,
    }
    with io.open(os.path.join(RESULTS, "analysis.json"), "w",
                 encoding="utf-8") as fh:
        json.dump(out, fh, indent=2, ensure_ascii=False)

    sys.stdout.write(
        "total tokens: {:,}  types: {:,}  oracle headwords: {:,}\n\n".format(
            out["total_tokens"], out["total_types"], out["oracle_headwords"]))
    fmt = "{:22s} {:>10s} {:>9s} {:>9s} {:>11s} {:>9s} {:>9s}\n"
    sys.stdout.write(fmt.format(
        "config", "dct bytes", "type cov", "tok cov",
        "tok morph", "orcl TP", "orcl FP"))
    for r in rows:
        sys.stdout.write(fmt.format(
            r["config"], "{:,}".format(r["dct_bytes"]),
            "{:.1%}".format(r["type_coverage"]),
            "{:.1%}".format(r["token_coverage"]),
            "{:,}".format(r["token_morph"]),
            "{:,}".format(r["oracle_tp"]),
            "{:,}".format(r["oracle_fp"])))


if __name__ == "__main__":
    main()
