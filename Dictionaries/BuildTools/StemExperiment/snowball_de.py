# SPDX-License-Identifier: Apache-2.0
"""Snowball German stemmer, Python 3 stdlib only.

Implements the published Snowball German stemming algorithm, including the
umlaut-digraph prelude with the U/Y marking, the R1 region clamp at three
letters, the niss rule, the special guards for -syst, -st and -et, the
d-suffix step with its chains, the apostrophe step, and the postlude that
turns U/Y back and removes umlaut accents.

Verification set: snowball-data german voc.txt and output.txt.
"""

import argparse
import unicodedata
from pathlib import Path

VOWELS = set("aeiouyäöü")
S_ENDING = set("bdfghklmnrt")
ST_ENDING = set("bdfghklmnt")
ET_ENDING = set("dfgklmnrstUzä")
STEP1_ET_GUARD = ("geordn", "intern", "plan", "tick", "tr")


def _prelude(text):
    """Mark u/y between vowels, then apply the digraph replacements.

    The U/Y marking also protects -ue- sequences such as in feuer, which
    the reference algorithm skips through the same marked vowel.
    """
    chars = list(text)

    for k in range(1, len(chars) - 1):
        if chars[k] in ("u", "y") and chars[k - 1] in VOWELS and chars[k + 1] in VOWELS:
            chars[k] = "U" if chars[k] == "u" else "Y"

    marked = "".join(chars)
    out = []
    i = 0

    while i < len(marked):
        pair = marked[i:i + 2]

        if marked[i] == "ß":
            out.append("ss")
            i += 1
        elif pair == "ae":
            out.append("ä")
            i += 2
        elif pair == "oe":
            out.append("ö")
            i += 2
        elif pair == "ue":
            out.append("ü")
            i += 2
        elif pair == "qu":
            out.append("qu")
            i += 2
        else:
            out.append(marked[i])
            i += 1

    return "".join(out)


def _gopast_hop(word, start, want_vowel):
    """Hop forward like the reference hop operator.

    The character at the cursor is tested first, then the cursor advances
    one position regardless. On success the cursor rests past the matched
    character, so a landing position is always one letter beyond the char
    that satisfied the test. The terminator position (len(word)) counts as
    a non-vowel. Returns None when no matching character exists.
    """
    limit = len(word)
    cursor = start

    while cursor <= limit:
        char = word[cursor] if cursor < limit else ''

        if (char in VOWELS) == want_vowel:
            return cursor + 1

        cursor += 1

    return None


def _mark_regions(word):
    """Return R1 and R2 start indices exactly like the reference mark_regions.

    Each chain is gopast v gopast non-v; a failed chain restores the cursor,
    so the second chain starts at the first chain's landing position, or at
    0 when the first chain failed. R1 is clamped to at least 3 letters via
    the x probe (hop 3 setmark x); x counts as 0 when the word is shorter
    than 3 letters, in which case no clamp applies.
    """
    limit = len(word)
    x = 3 if limit >= 3 else 0

    p1 = limit
    landing1 = None
    vowel1 = _gopast_hop(word, 0, True)

    if vowel1 is not None:
        landing1 = _gopast_hop(word, vowel1, False)

        if landing1 is not None:
            p1 = landing1

    if p1 < x:
        p1 = x

    start2 = landing1 if landing1 is not None else 0
    p2 = limit
    vowel2 = _gopast_hop(word, start2, True)

    if vowel2 is not None:
        landing2 = _gopast_hop(word, vowel2, False)

        if landing2 is not None:
            p2 = landing2

    return p1, p2


def _longest_suffix(word, suffixes):
    """Return the first matching suffix from a longest-first list."""
    for suffix in suffixes:
        if word.endswith(suffix):
            return suffix

    return None


def _step1(word, r1):
    """Step 1: plural and other inflection endings.

    em (not preceded by syst), ern, er, erin, erinnen, e, en, es are
    deleted in R1; s is deleted in R1 when preceded by a valid s-ending;
    ln and lns are replaced by l. A deleted e/en/es may additionally
    trigger the niss rule.
    """
    suffix = _longest_suffix(word, (
        "erinnen", "erin", "ern", "lns", "em", "er", "es", "en", "ln", "e", "s"))

    if suffix is None:
        return word

    position = len(word) - len(suffix)

    if suffix == "s":
        if position >= r1 and position >= 1 and word[position - 1] in S_ENDING:
            return word[:position]

        return word

    if suffix in ("ln", "lns"):
        if position >= r1:
            return word[:position] + "l"

        return word

    if position < r1:
        return word

    if suffix == "em" and word[:position].endswith("syst"):
        return word

    return _step1_niss(word, suffix, position)


def _step1_niss(word, suffix, position):
    """Apply the niss rule after a deleted e/en/es ending."""
    if suffix not in ("e", "en", "es"):
        return word[:position]

    stem = word[:position]

    if stem.endswith("niss"):
        stem = stem[:-1]

    return stem


def _step2(word, r1):
    """Step 2: en, er, est deleted in R1; st with st-ending plus 3 letters;
    et with a valid et-ending and none of the guarded prefixes."""
    suffix = _longest_suffix(word, ("est", "en", "er", "st", "et"))

    if suffix is None:
        return word

    position = len(word) - len(suffix)

    if position < r1:
        return word

    if suffix == "st":
        if position >= 4 and word[position - 1] in ST_ENDING:
            return word[:position]

        return word

    if suffix == "et":
        if position >= 1 and word[position - 1] in ET_ENDING:
            stem = word[:position]

            if not any(stem.endswith(guard) for guard in STEP1_ET_GUARD):
                return stem

        return word

    return word[:position]


def _step3(word, r2, r1):
    """Step 3: d-suffixes with their chains.

    end/ung deleted in R2 with the ig chain; ig/ik/isch deleted in R2 when
    not preceded by e; lich/heit deleted in R2 with the er/en chain in R1;
    keit deleted in R2 with the lich/ig chain in R2.
    """
    suffix = _longest_suffix(word, (
        "isch", "lich", "keit", "heit", "end", "ung", "ig", "ik"))

    if suffix is None:
        return word

    position = len(word) - len(suffix)

    if position < r2:
        return word

    if suffix in ("ig", "ik", "isch"):
        if position >= 1 and word[position - 1] != "e":
            return word[:position]

        return word

    stem = word[:position]

    if suffix in ("end", "ung"):
        if stem.endswith("ig"):
            chain = len(stem) - 2

            if chain >= r2 and chain >= 1 and stem[chain - 1] != "e":
                stem = stem[:chain]

        return stem

    if suffix in ("lich", "heit"):
        if stem.endswith("er") or stem.endswith("en"):
            chain = len(stem) - 2

            if chain >= r1:
                stem = stem[:chain]

        return stem

    if stem.endswith("lich") or stem.endswith("ig"):
        chain = len(stem) - (4 if stem.endswith("lich") else 2)

        if chain >= r2:
            stem = stem[:chain]

    return stem


def _step4(word):
    """Step 4: possessive apostrophe suffixes, leaving at least 2 letters."""
    suffix = _longest_suffix(word, ("'sch", "'s", "'"))

    if suffix is None:
        return word

    if len(word) - len(suffix) >= 2:
        return word[:len(word) - len(suffix)]

    return word


def _postlude(stem):
    """Turn U/Y back into u/y and remove umlaut accents."""
    stem = stem.replace("U", "u").replace("Y", "y")
    return stem.translate(str.maketrans({"ä": "a", "ö": "o", "ü": "u"}))


def stem_de(word):
    """Return the Snowball German stem of one word.

    Input is lowercased and NFC-normalized first; the published algorithm
    assumes already lowercased words.
    """
    word = unicodedata.normalize("NFC", word.lower())
    word = _prelude(word)
    r1, r2 = _mark_regions(word)
    word = _step1(word, r1)
    word = _step2(word, r1)
    word = _step3(word, r2, r1)
    word = _step4(word)
    return _postlude(word)


def main(argv=None):
    """Compare one verification word list against its expected stems."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--voc", type=Path, required=True)
    parser.add_argument("--expected", type=Path, required=True)
    args = parser.parse_args(argv)
    with args.voc.open(encoding="utf-8") as stream:
        words = [line.rstrip("\n") for line in stream]
    with args.expected.open(encoding="utf-8") as stream:
        expected = [line.rstrip("\n") for line in stream]
    if len(words) != len(expected):
        raise ValueError("word count %d does not match %d" %
                         (len(words), len(expected)))
    mismatches = [(word, want, stem_de(word))
                  for word, want in zip(words, expected)
                  if stem_de(word) != want]
    print("words %d, mismatches %d" % (len(words), len(mismatches)))
    for word, want, got in mismatches[:20]:
        print("%r expected %r got %r" % (word, want, got))
    return 1 if mismatches else 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
