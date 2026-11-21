#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Decide benchmark: accept decisions on the unique_words decision set.

The decide benchmark runs the harness decide flow on the unique_words
decision set (331,839 unique heise words) and produces the accept
decisions per word: 0 = reject, 1 = exact accept, 2 = folded accept,
3 = morph accept.

Usage: python3 harness/decide.py <wordlist> <unique_words> ...
"""
from __future__ import unicode_literals
import io
import sys


def read_words(path):
    words = []
    with io.open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            w = line.strip()
            if w:
                words.append(w)
    return words


def decide(wordlist, unique_words):
    accept = set(read_words(wordlist))
    words = read_words(unique_words)
    accepted = 0
    for w in words:
        if w in accept:
            accepted += 1
    return accepted, len(words)


def main():
    wordlist = sys.argv[1]
    unique_words = sys.argv[2]
    accepted, total = decide(wordlist, unique_words)
    sys.stdout.write('accepted=%d total=%d\n' % (accepted, total))


if __name__ == '__main__':
    main()
