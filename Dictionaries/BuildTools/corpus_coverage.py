#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Measure dictionary coverage against a text corpus.

Extracts words from a directory of text or HTML files, converts them to
GEOS mac_roman encoding, and writes one unique word per line to an output
file. The output can be used with the host morphology test driver to
measure acceptance rate, false-accept rate and the sweet-spot curve for
DCT budget sizing.

Usage:
    python3 Dictionaries/BuildTools/corpus_coverage.py \\
        --input ~/mirror/heise \\
        --pattern art.htm \\
        --encoding iso-8859-1 \\
        --sample 500 \\
        --output corpus_words.txt

The output file is sorted by descending frequency and uses mac_roman
byte encoding so it can be read directly by C test programs that use
the OLX1 reader.
"""
import argparse
import html
import os
import random
import re
from pathlib import Path


def extract_words(file_path, encoding):
    with open(file_path, "r", encoding=encoding, errors="replace") as f:
        raw = f.read()
    text = re.sub(r"<[^>]+>", " ", raw)
    text = html.unescape(text)
    pattern = r"[A-Za-z\xC4\xD6\xDC\xE4\xF6\xFC\xDF][A-Za-z\xC4\xD6\xDC\xE4\xF6\xFC\xDF\-]+"
    tokens = re.findall(pattern, text)
    return [t.strip("-") for t in tokens if len(t.strip("-")) >= 2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True,
                        help="Input directory to scan recursively")
    parser.add_argument("--pattern", default="*.txt",
                        help="File name pattern to match (default: *.txt)")
    parser.add_argument("--encoding", default="utf-8",
                        help="File encoding (default: utf-8)")
    parser.add_argument("--sample", type=int, default=0,
                        help="Random sample size (0 = all files)")
    parser.add_argument("--output", default="corpus_words.txt",
                        help="Output file (mac_roman, frequency-sorted)")
    parser.add_argument("--seed", type=int, default=20261113,
                        help="Random seed for sampling")
    args = parser.parse_args()

    files = []
    for root, dirs, names in os.walk(args.input):
        for name in names:
            if name == args.pattern or Path(name).match(args.pattern):
                files.append(os.path.join(root, name))

    if args.sample and len(files) > args.sample:
        random.seed(args.seed)
        random.shuffle(files)
        files = files[:args.sample]

    print(f"Files: {len(files)}", flush=True)

    word_counts = {}
    for path in files:
        try:
            words = extract_words(path, args.encoding)
        except Exception:
            continue
        for w in words:
            wl = w.lower()
            word_counts[wl] = word_counts.get(wl, 0) + 1

    sorted_words = sorted(word_counts.items(), key=lambda x: (-x[1], x[0]))
    print(f"Unique words: {len(sorted_words):,}", flush=True)

    with open(args.output, "wb") as f:
        for word, count in sorted_words:
            try:
                f.write(word.encode("mac_roman") + b"\n")
            except (UnicodeEncodeError, UnicodeDecodeError):
                pass

    print(f"Output: {args.output} ({len(sorted_words):,} words)", flush=True)


if __name__ == "__main__":
    main()
