#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Extract a German benchmark corpus from heise mirror HTML and/or plain text.

Unlike BuildTools/corpus_coverage.py this tool keeps the original case of
every token (German nouns are capitalized, and the exact-case lookup path
depends on it) and additionally emits a frequency table and a fixed-size,
seed-shuffled token stream for the timing benchmark.

Outputs (GEOS/mac_roman bytes):
  corpus/unique_words.txt  unique tokens, original case, frequency-sorted
  corpus/freq.tsv          word<TAB>count
  corpus/stream.txt        N sampled tokens in random order (timing input)

Usage:
  python3 tools/extract_corpus.py --heise ~/mirror/heise \
      [--gutenberg-dir corpus/raw/gutenberg] [--stream-size 200000]
"""
import argparse
import html
import os
import random
import re
import sys
from pathlib import Path

WORD_RE = re.compile(
    r"[A-Za-z\xC4\xD6\xDC\xE4\xF6\xFC\xDF]"
    r"[A-Za-z\xC4\xD6\xDC\xE4\xF6\xFC\xDF\-]+")
OUT = Path.home() / "OpenSpellBench" / "corpus"


def read_text(path):
    raw = path.read_bytes()
    for encoding in ("utf-8", "iso-8859-1"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            continue
    return raw.decode("iso-8859-1", errors="replace")


def tokenize(path):
    text = read_text(path)
    if path.suffix.lower() in (".htm", ".html"):
        text = re.sub(r"(?is)<(script|style)[^>]*>.*?</\1>", " ", text)
        text = re.sub(r"<[^>]+>", " ", text)
        text = html.unescape(text)
    words = []
    for token in WORD_RE.findall(text):
        token = token.strip("-")
        if len(token) >= 2:
            words.append(token)
    return words


def collect(paths):
    counts = {}
    for path in paths:
        try:
            for word in tokenize(Path(path)):
                counts[word] = counts.get(word, 0) + 1
        except Exception as exc:
            print(f"skip {path}: {exc}", file=sys.stderr)
    return counts


def write_bytes(path, lines):
    with path.open("wb") as fh:
        for line in lines:
            try:
                fh.write(line.encode("mac_roman") + b"\n")
            except (UnicodeEncodeError, UnicodeDecodeError):
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--heise", default=None)
    parser.add_argument("--heise-pattern", default="art.htm")
    parser.add_argument("--heise-sample", type=int, default=4000)
    parser.add_argument("--extra-dir", default=None,
                        help="additional directory of text/html files")
    parser.add_argument("--stream-size", type=int, default=200000)
    parser.add_argument("--seed", type=int, default=20261120)
    args = parser.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    paths = []
    if args.heise:
        heise = []
        for root, _dirs, names in os.walk(args.heise):
            for name in names:
                if Path(name).match(args.heise_pattern):
                    heise.append(os.path.join(root, name))
        print(f"heise files found: {len(heise)}", flush=True)
        if args.heise_sample and len(heise) > args.heise_sample:
            random.seed(args.seed)
            random.shuffle(heise)
            heise = heise[:args.heise_sample]
        print(f"heise files used: {len(heise)}", flush=True)
        paths.extend(heise)
    if args.extra_dir:
        extra = []
        for root, _dirs, names in os.walk(args.extra_dir):
            for name in names:
                if Path(name).suffix.lower() in (".htm", ".html", ".txt"):
                    extra.append(os.path.join(root, name))
        print(f"extra files: {len(extra)}", flush=True)
        paths.extend(extra)

    print(f"total files: {len(paths)}", flush=True)
    counts = collect(paths)
    total_tokens = sum(counts.values())
    print(f"tokens: {total_tokens:,}  unique: {len(counts):,}", flush=True)

    ordered = sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))
    write_bytes(OUT / "unique_words.txt", [w for w, _ in ordered])
    write_bytes(OUT / "freq.tsv", [f"{w}\t{c}" for w, c in ordered])

    # Timing stream: proportional expansion of the most frequent tokens,
    # shuffled with a fixed seed, exactly stream_size tokens.
    rng = random.Random(args.seed)
    stream = []
    names = [w for w, _ in ordered]
    weights = [c for _, c in ordered]
    population = rng.choices(names, weights=weights, k=args.stream_size)
    stream.extend(population)
    rng.shuffle(stream)
    write_bytes(OUT / "stream.txt", stream)
    print(f"stream tokens: {len(stream):,}", flush=True)


if __name__ == "__main__":
    main()
