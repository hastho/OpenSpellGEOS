# OpenSpellGEOS stem-based German dictionary evaluation

SPDX-License-Identifier: Apache-2.0

This document evaluates a stem-based German dictionary design against the
shipped surface-form design. It records host measurements only. It does not
change any shipped file, the OLX1 format, the reader ABI or a data builder.
The evaluation used the pinned German source snapshot in
Dictionaries/BuildTools/Data and the distributed German dataset of 2026-10-03.
Paths are relative to the repository root. Percentages are recorded to three
significant digits.

## Summary

A stem-based dictionary stores Snowball German stems instead of surface forms.
A target morphology module applies the suffix rules at query time. The
evaluation stems every lowercase surface form of the German source snapshot,
writes real OLX1 files with Dictionaries/BuildTools/buildlex.py and measures
acceptance behavior on generated misspellings.

The German surface forms of the source snapshot stem to 804103 distinct stems
from 2152632 lowercase forms. The stems of the distributed 130396-record
vocabulary occupy 471601 bytes in the existing OLX1 format, compared with
999766 bytes for the surface forms. This fits the compact 500000-byte ceiling
and keeps the complete German vocabulary. Held-out recall is 90.148 percent.
Generated misspellings are accepted at 0.40 to 12.68 percent by error class,
where the surface-form design rejects unseen forms. Acceptance of these
near-forms is the recorded gain and risk of the stem design.

The supplied DAFSA C implementation does not compile with the host compiler or
with Watcom as delivered. Empirical testing found four functional defects.
A fixed C89 fixture compiles clean with `wcc -0 -ms -wx`. The evaluation does
not adopt DAFSA in the current design.

## The shipped surface-form design

OpenSpellGEOSFormat.md specifies the immutable, sorted key/value container.
DCT, THS and HYP share one format. Dictionary inputs contain surface forms.
The target does not expand affix rules or interpret TeX patterns. A target
query performs a disk lookup and bounded decoding work. There is no malloc,
recursive traversal, Unicode conversion, or dependency on the host byte order.
Target code is C89.

OpenSpellGEOSFormat.md records the compact release ceiling of 500000 decimal
bytes on each complete DCT, THS and HYP file. The distributed German dataset
of 2026-10-03 applies 1000000 bytes per file (DE_DE.md and BUILD.json, with
`max_file_bytes` 1000000). Its German dictionary DE_DE.DCT holds 130396
surface-form records in 999766 bytes. Uncommon inflections and productive
compounds absent from the input list are not accepted. Productive German
inflections are recorded with the surface form of every needed inflection.

## The stem-based design

The stem design keeps the existing OLX1 container and reader. The dictionary
keys become lowercase Snowball German stems. The value of each stem stays a
dictionary flag. A new target morphology module supplies the suffix rules: at
query time it stems the lowercase query, reads the stem from the file and
accepts the query when the stem is present. The module carries the suffix
table and the R1/R2 region logic in target C89, with bounded buffers and no
malloc, as OpenSpellGEOSFormat.md requires.

The design changes three semantics that this evaluation records:

- Exact case. The shipped design stores the flag of every surface form: flag 1
  permits title and allcaps matches for lowercase entries, flag 2 is exact-only.
  Stems are lowercase, so the flag distinction for proper nouns needs the
  morphology module to carry the case rules. This prototype does not resolve
  that part.
- Display forms. The shipped design reads surface forms for suggestions. The
  stem design must reconstruct a surface form from the stem and the suffix
  class for every displayed suggestion. That is additional risk and is not
  evaluated here.
- Preservation. DE_DE.md records 1114 authored everyday spellings (1102
  canonical entry keys) that must survive selection. A stem-based selection
  must re-verify that count against the morphology module.

## Verified stemmer

Dictionaries/BuildTools/StemExperiment/snowball_de.py implements the published
Snowball German stemming algorithm in Python 3, standard library only. It
includes the umlaut-digraph prelude with the U/Y marking, the R1 clamp at
three letters, the niss rule, the -syst, -st and -et guards, the d-suffix
chains, the apostrophe step and the postlude. The stemmer is verified against
the official snowball-data German verification set: 35053 words, 0 mismatches
(`python3 Dictionaries/BuildTools/StemExperiment/snowball_de.py --voc voc.txt
--expected output.txt`). The module records the region-landing semantics that
resolved the last 31 verification words: the region hop tests the
character at the cursor first, the cursor then advances past the matched
character, and the non-vowel chain may land at the terminator position. The
host stemmer is a measurement tool, not a target C module.

## Corpus measurements

The pinned source snapshot is Dictionaries/BuildTools/Data/DE.words.txt,
with 2152637 lines, byte-sorted. After lowercasing and removal of five case
collisions it holds 2152632 lowercase forms. Stemming yields 804103 distinct
stems, approximately 2.68 forms per stem. No stem failed the GEOS encoding or
the 64-byte key limit; the unencodable counts are 0.

Held-out recall: every tenth lowercase form is held out in the fixed file
order. The stems of the other 90 percent form a set. A held-out form is
accepted when its stem is in that set: 194057 of 215264 held-out forms are
covered, 90.148 percent. The surface-form design has no unseen-form recall by
construction; it accepts only forms stored in the file.

Coverage of the complete source snapshot by the distributed German
vocabulary: 69703 lowercase forms coincide with vocabulary entries, 3.238
percent. 247768 forms have a stem in the stems of the distributed vocabulary,
11.510 percent.

## Real OLX1 builds

All files are written by buildlex.build_file in the existing OLX1 format.
The sparse index stays on disk and the reader bounds stay unchanged.

| Build | Records | Bytes |
| --- | ---: | ---: |
| All source forms, surface keys | 2152637 | 18287641 |
| All source forms, stem keys | 2152637 | 8091334 |
| Distributed vocabulary, surface keys | 130397 | 999766 |
| Distributed vocabulary, stem keys | 58073 | 471601 |

The surface replication selects the fitting priority prefix at the
1000000-byte budget with Dictionaries/BuildTools/compactlex.py. It gives
130397 records in 999766 bytes, against 130396 records and 999766 bytes
recorded in BUILD.json. The byte total is identical; the record count differs
by one because the pinned email-form supplement of the distributed build is
absent from the Data snapshot. The SHA-256 differs from the distributed file.
The distributed-vocabulary stems fit the compact 500000-byte ceiling with
28399 bytes margin.

## Acceptance behavior

Generated misspellings probe acceptance at Unicode char level. The probe
selects 25000 misspellings per error class from the vocabulary, with the
fixed seed 20261113: deletion removes one char, substitution replaces one
char, transposition swaps two adjacent chars, doubling duplicates one char.
The probe counts a misspelling as a true accept when it coincides with a
lowercase vocabulary entry. The probe counts it as a false accept when the
stem design accepts it and the entry is absent. The probe records the false
accepts for both stem sets: the stems of every vocabulary entry (all case
forms lowercased) and the stems of the lowercase entries only.

| Error class | Generated | True accepts | False accepts (all) | False accepts (lower only) |
| --- | ---: | ---: | ---: | ---: |
| Deletion | 25000 | 1575 | 3171 (12.684%) | 1508 (6.032%) |
| Substitution | 25000 | 141 | 704 (2.816%) | 352 (1.408%) |
| Transposition | 24990 | 62 | 751 (3.005%) | 443 (1.773%) |
| Doubling | 25000 | 17 | 100 (0.400%) | 35 (0.140%) |

The surface-form design rejects every probed misspelling that is absent from
the vocabulary. The stem design accepts a near-form when the morphology module
maps it onto a stored stem. Most deletion probes apply the same suffix rules
as a valid inflection of the same stem, for example the removal of a trailing
suffix char. The probe counts such a form as a false accept, although a
speaker may read it as a valid inflection. The rates above record that
behavior per error class; they are not a complete linguistic review.

## Supplied DAFSA implementation findings

The evaluation also tested the supplied DAFSA C implementation as delivered,
with the host compiler and with Watcom. Empirical findings, with exact
numbers:

- The delivered code does not compile with the host compiler: a local
  `u32 new_state` shadows the `new_state` function. Its example word list is
  unsorted (`arbeiter` precedes `arbeiten`), so the builder stops with
  "Words must be sorted" and exit status 1.
- `wcc -0 -ms -wx` stops with error E1058: a C99 for-loop declaration inside
  a block, which the C89 target does not permit.
- `transitions_start` is never assigned. Every state appends its transitions
  at offset 0. On the sorted five-word list {ab, aby, abz, ac, az} the built
  automaton reports 0 transitions and accepts no probe.
- The in-place append corrupts other states' blocks when the block does not
  fit. On the six-word list {arbeit, arbeiter, arbeiten, haus, hauser,
  hausfrau} the build with that repair alone reports arbeit=0, hauser=0,
  hausfrau=1.
- The word-order check compares a fixed prefix of the previous buffer, so it
  reads stale bytes after a replaced last char. On {ADS, ADMV} the check
  falsely rejects ADSL.
- The register holds at most 1023 states without rehash. On the 5000-word
  German list the build with the relocation repair reports 6085 live states,
  and the build with rehash reports 4805 live states and 23182 transitions.
- A C89 fixture with the four repairs compiles clean with `wcc -0 -ms -wx`
  (0 errors, 0 warnings) and with `wcl386 -bcl=linux -wx -q`. The 5000-word
  run accepts 5000 of 5000 probes, with 4805 live states and 23182
  transitions; the six-word run leaves 17 live states. The relocation step
  leaves garbage transitions in the array (21 raw against 18 logical), the
  u16 transition target truncates above 65535 states, the minimize
  replacement scan is quadratic, and the delivered builder needs
  approximately 1.3 KiB of stack for its context.

The evaluation does not adopt DAFSA. The priority-prefix OLX1 selection and
the existing front-coded leaves stay unchanged. The fixtures are recorded for
reproducibility.

## Limits of this evaluation

The measurements are host Python runs. They are not a target C compilation
check, a linked geode test, a GEOS UI test or a runtime I/O benchmark. The
region hop and suffix logic of the target morphology module is designed, not
measured. The stemmer is verified by the official verification set only; that
is not proof of complete linguistic correctness. The misspelling probe
measures generated errors, not typed human errors. The evaluation does not
change the distributed files, the source snapshot or the builders.

## Conclusion

The stem design fits the existing OLX1 format and both release ceilings with
the complete distributed German vocabulary: 471601 bytes, 52.831 percent
smaller than the shipped 999766 bytes. It accepts unseen inflections of
stored stems at a 90.148 percent held-out rate and accepts generated
misspellings at 0.400 to 12.684 percent by error class, where the shipped
design rejects unseen forms. The case semantics for proper nouns and the
display of suggestions need a target morphology module before a ship decision.
The supplied DAFSA implementation is not used.

A next step is a C89 morphology module design with bounded buffers, a case
rule set, the preserved-spelling verification and a suggestion reconstruction
test. The dictionary format, the reader ABI and the data builders stay
unchanged.

## Reproduction

The stemmer verification needs the official snowball-data German lists
voc.txt and output.txt:

    python3 Dictionaries/BuildTools/StemExperiment/snowball_de.py --voc voc.txt --expected output.txt

The corpus evaluation needs the extracted Data snapshot and
Dictionaries/DE_DE.zip:

    python3 Dictionaries/BuildTools/StemExperiment/stem_prototype.py --misspellings 100000

It writes the OLX1 experiments and report.json into the work directory
(default /home/thomas/pcgeos-tools/temp/stemexp). The work directory is
scratch. Dictionaries/BuildTools/StemExperiment/dafsa holds the DAFSA
fixtures: the delivered implementation, the four stepwise repaired drivers,
the C89 fixture and the sorted word lists (w3.txt, w5.txt, w6.txt,
de5k.txt, de5k-probes.txt). The stepwise drivers take one host build command
and one sorted word list with its probe list:

    cc -o dafsa dafsa_fix123_driver.c && ./dafsa de5k.txt de5k-probes.txt
    wcc -0 -ms -wx dafsa_c89_driver.c
    wcl386 -bcl=linux -wx -q -o dafsa_c89 dafsa_c89_driver.c && ./dafsa_c89 de5k.txt de5k-probes.txt
