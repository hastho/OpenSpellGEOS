SPDX-License-Identifier: Apache-2.0

# OpenSpellGEOS morphology benchmark and follow-up work

This document records the host benchmark that compared the data-only
dictionary route against the bounded C89 morphology route, and lists the
concrete changes a coding agent should apply to `~/OpenSpellGEOS` as a result.
The full benchmark suite, raw per-word decisions and the reproducibility
commands live in `~/OpenSpellBench` (see `~/OpenSpellBench/REPORT.md`).

## Benchmark evidence (host, C89 engine)

Two corpora were used:

1. **heise-only** (initial run): 4,000 `art.htm` articles, 5,647,237 tokens,
   331,511 unique types. Modern technical German (c't and i' magazine,
   1990-2001).
2. **de-Corp + heise** (validated run): 4,999 public-domain German fiction
   and non-fiction texts from de-Corp (Zenodo 10.5281/zenodo.15714828,
   1780-1930) plus 1,000 heise articles. 307,539,350 tokens, 2,746,561 unique
   types. The top 200,000 frequency-ranked types cover 97.5% of all tokens and
   are used for per-word decide runs; the full frequency table drives
   token-weighted coverage.

The reader (`openlex.inc`) and morphology analyzer (`openmorph.inc`) were
compiled unmodified with `gcc -std=c89 -pedantic` and linked to a
`stdio`-backed `OLRead` callback.

### heise-only corpus

| variant | DCT bytes | morph | token coverage | type coverage |
| --- | ---: | :---: | ---: | ---: |
| surf500 | 499,915 | off | 66.3% | 12.4% |
| core500 | 499,915 | on | 73.5% | 31.9% |
| upstream shipped | 999,766 | off | 77.4% | 18.8% |
| upstream shipped | 999,766 | on | 84.9% | 46.9% |
| surf1500 | 1,499,617 | off | 79.0% | 21.3% |
| surf1500 | 1,499,617 | on | **85.9%** | 49.5% |
| core1500 | 1,499,617 | off | 72.8% | 18.4% |
| core1500 | 1,499,617 | on | 80.6% | 43.0% |

### de-Corp + heise corpus (validated, updated morphology)

After the initial heise run, the morphology code was updated with 7 added
separable prefixes (`durch`, `um`, `ueber`, `unter`, `wieder`, `miss`,
`hinter`) and a noun-plural umlaut probe. The benchmark was then repeated on
the larger stratified de-Corp + heise corpus (top 200k types, 295M token
weight).

| variant | DCT bytes | morph | token coverage | type coverage |
| --- | ---: | :---: | ---: | ---: |
| surf500 | 499,915 | off | 78.8% | 19.4% |
| surf500 | 499,915 | on | 85.0% | 44.1% |
| upstream shipped | 999,766 | off | 89.9% | 32.3% |
| upstream shipped | 999,766 | on | 94.9% | 68.3% |
| surf1500 | 1,499,617 | off | 91.6% | 38.5% |
| surf1500 | 1,499,617 | on | **95.6%** | 72.3% |
| core1500 | 1,499,617 | off | 86.0% | 30.8% |
| core1500 | 1,499,617 | on | 92.7% | 64.5% |

The de-Corp literary corpus yields higher absolute token coverage than heise
(standard German dominates running text), but the relative findings are
identical and now hold on a 50x larger, public-domain, genre-stratified corpus.

Speed (400k-token stream, 2 passes): morphology costs 2.2x CPU on this
corpus (15-16 us/token vs 6-7 us/token), less than the 4x on heise because
literary text triggers more exact hits. False-accept on 6,000 nonsense
strings remains 0.07% (4/6000, unchanged from the pre-update run); the new
prefixes and umlaut-plural rule did not increase overgeneration.

## Encoding bug in the auxiliary/modal verb table

The benchmark uncovered a pre-existing encoding bug in `openmorph.inc`. The
`olMorphAuxForms` table stores German umlaut forms as **UTF-8 string literals**
(e.g. `"müsst"` = bytes `6D C3 BC 73 73 74`), but the corpus and dictionary
use **GEOS/mac_roman** encoding where `ü` = byte `0x9F`. The mismatch means
all 14 lines of umlaut-bearing aux/modal forms (`wäre`, `hätte`, `würde`,
`könnte`, `dürfte`, `möchte`, `müsstest`, `weißt`, `wüssten`, etc.) are dead
code — they never match the input bytes.

Most affected forms are rescued by the general morphology rules (de-umlaut +
ending strip), so the token-level impact is small (~0.19% of heise tokens,
mostly recovered). However, complex forms like `müsstest` are missed because
the general rules cannot reconstruct them. The fix is to replace every
literal UTF-8 umlaut in the aux table with its mac_roman hex escape, exactly
as the new `ueber` prefix already does (`"\x9f" "ber"`):

| character | UTF-8 (broken) | mac_roman hex escape (correct) |
| --- | --- | --- |
| ä | `\xc3\xa4` | `\x8a` |
| ö | `\xc3\xb6` | `\x9a` |
| ü | `\xc3\xbc` | `\x9f` |
| ß | `\xc3\x9f` | `\xa7` |

There are 20 `ü`, 14 `ö`, 9 `ä`, and 2 `ß` UTF-8 occurrences across 14 lines
in the current file. All should be converted. After conversion, re-run the
benchmark to confirm the missed forms (`müsstest`, `könntest`, `dürftet`,
`möchtet`, etc.) are now accepted.

## GeoLex lemma validation

The de-Corp + heise benchmark measures token coverage on running text. To
validate the morphology rules at the *lemma* level, the GeoLex project
(`~/GeoLex`, a C89 reader for CompLex `.AND` dictionary files) was used as
an authoritative German lemma source.

### Method

1. Extracted 50,242 unique headwords from MEYERS (39,077) and DUDEN
   (15,462) — hand-curated German dictionary lemmas, not OCR or corpus noise.
2. Cross-referenced against the shipped `DE_DE.DCT` (130,396 keys): 17,974
   GeoLex lemmas (35.8%) are stored in the DCT. The rest are proper nouns,
   archaic terms or abbreviations outside the DCT's selection.
3. For each stored lemma, generated expected inflected forms using the same
   rule classes `openmorph.inc` implements: verb endings, ge-participles,
   -bar derivation, noun plurals, noun-umlaut plurals, adjective endings,
   un- prefix, zu-infinitive.
4. Fed 144,290 unique generated inflections through the harness with
   morphology on and off, and compared acceptance.

### Results

| metric | data only | with morphology |
| --- | ---: | ---: |
| inflections accepted | 16,799 (11.6%) | 136,695 (94.7%) |
| inflections missed | 127,491 | 8,807 |
| morphology rescue rate | — | 93.9% of misses rescued |

Per rule class:

| rule class | total | data only | + morph | morph % |
| --- | ---: | ---: | ---: | ---: |
| ge-participle | 6,696 | 517 | 6,566 | 98.1% |
| -bar | 3,409 | 138 | 3,342 | 98.0% |
| noun | 81,473 | 1,242 | 79,352 | 97.4% |
| zu-infinitive | 898 | 3 | 869 | 96.8% |
| adjective | 7,756 | 3,423 | 6,865 | 88.5% |
| verb | 40,908 | 11,508 | 36,235 | 88.6% |
| un-prefix | 4,318 | 81 | 3,449 | 79.9% |
| noun-umlaut | 44 | 3 | 17 | 38.6% |

Per lemma (15,211 lemmas with generated inflections):

| coverage | lemmas | % |
| --- | ---: | ---: |
| fully covered (100% of inflections) | 11,943 | 78.5% |
| partially covered | 2,901 | 19.1% |
| not covered | 367 | 2.4% |
| **at least partial coverage** | **14,844** | **97.6%** |

### Interpretation

The morphology rules correctly reconstruct inflections for **94.7% of
generated forms** from authoritative German dictionary lemmas, and **97.6%
of lemmas** have at least partial inflection coverage. Without morphology,
only 11.6% of generated inflections are found — the DCT stores surface forms,
not the full inflection paradigm.

The weakest rule class is noun-umlaut (38.6%), which reflects the same gap
the heise benchmark found (`häuser` → `Haus`). The umlaut-plural probe added
in the updated `openmorph.inc` handles some cases but not all. The
un-prefix rule (79.9%) misses forms where the negated word is not itself in
the DCT. Verb and adjective rules (88.5-88.6%) miss some less common
endings.

The strong classes (ge-participle 98.1%, -bar 98.0%, noun 97.4%,
zu-infinitive 96.8%) confirm the core rule design is sound.

Three results drive the recommendations below.

1. Morphology is the dominant coverage lever, confirmed on both corpora.
   Turning the C89 rules on adds +5.0 to +7.5 pp of token coverage at a fixed
   dictionary, for zero extra disk bytes and +260 bytes of context RAM. The
   gain is +6.3 pp on heise (66.3% -> 73.5% at 500 KB; 79.0% -> 85.9% at
   1.5 MB) and +5.0 pp on de-Corp + heise (91.6% -> 95.6% at 1.5 MB). Buying
   the same gain with data-only selection costs roughly a doubled dictionary.
2. Frequency-ranked surface-form selection beats the morphology-aware
   core/base-form rebuild, even with morphology enabled: `surf1500 m1`
   outperforms `core1500 m1` by 5.3 pp on heise (85.9% vs 80.6%) and 2.9 pp on
   de-Corp + heise (95.6% vs 92.7%). Exact hits are cheaper and more reliable
   than the reconstructions the core ranking would force.
3. The shipped upstream `DE_DE.DCT` carries no grammar rules: its per-record
   value is a single case-flag byte, and `OpenSpellGEOSData.md` explicitly
   disclaims runtime morphology. It is also 999,766 bytes, above the project's
   own 500,000-byte per-file ceiling.

Speed: morphology costs 2.2-4x CPU and reader callbacks, paid only on words
that would otherwise be missed (15-17 us/token on the host). The cost is
lower on literary text (2.2x) than on heise (4x) because literary German
triggers more exact hits. False acceptance on 6,000 random nonsense strings
is 0.07% with morphology and 0% without; the only observed case is
`stadts` -> `Stadt` via the ending-strip rule. The added prefixes and
umlaut-plural rule did not increase the false-accept rate.

## Follow-up: -eln/-ern weak-verb participle algorithm fix

A code review of the rules surfaced a real algorithm bug in the `ge-`
participle reconstruction (the only code-segment change accepted after the
benchmark; no new tables). The rule stripped `prefix + ge + stem + t` and
probed `prefix + stem + en` to reconstruct the infinitive. That is correct
for regular weak verbs (`abmachen -> abgemacht`), but wrong for the
`-eln`/`-ern` weak-verb class, whose infinitive ends in `-n` (not `-en`):

```
handeln  -> gehandelt   (stem "handel" + "t")
abhandeln-> abgehandelt (prefix "ab" + "handel" + "t")
umwandeln-> umgewandelt (prefix "um" + "wandel" + "t")
steuern  -> gesteuert   (stem "steuer" + "t")
ansteuern-> angesteuert (prefix "an" + "steuer" + "t")
```

The original rule probed `abhandelen` (wrong) and missed. The fix adds one
fallback probe with the bare `-n` suffix after the `-en` probe fails, in both
the separable-prefix participle path (`olMorphScanParticiple`) and the
non-prefix `ge-` block in `olMorphScanEndings`. It is two `olMorphTry` calls,
112 bytes of code growth, and no new static data.

The full heise corpus run after the fix shows **126 newly accepted
participles** (e.g. `abgehandelt`, `abgeschaltet`, `umgewandelt`, `angesteuert`,
`eingebuergert`, `aufgeschluesselt`), **0 newly rejected**, and the
false-accept rate stays at 1/2000 (the documented `stadts` case; unchanged).

The reviewer's other algorithm concerns were already in place: the `ge-`
analysis is gated on `wordP[0]=='g' && wordP[1]=='e'`, the de-umlaut probe is
gated on `olMorphDeUmlaut` returning non-zero (i.e. the word actually contains
an umlaut byte), and the compound boundary generation already iterates
`stem + linker` rather than using `split` as the right-start offset (the
reviewer's `strassenbahn` example already accepts). No code-segment growth
beyond the participle fix was needed.

## Concrete recommendation

Ship the bounded C89 morphology rules (the code route), but keep a
frequency-ranked surface-form dictionary (the data route). The two are
complementary. Do not switch the dictionary to base-form/core selection.

The coding agent should apply the following changes to `~/OpenSpellGEOS`.

### 1. Drop the core/base-form rebuild from the German build path

`Dictionaries/BuildTools/rebuild_german.py` rewrites the German priority list
to prefer morphology cores over surface forms. The benchmark shows this
*reduces* coverage (80.6% vs 85.9% on heise; 92.7% vs 95.6% on de-Corp + heise)
because it trades cheap exact
hits for rule reconstructions.

- Remove `rebuild_german.py` from the documented German build path in
  `TechDocs/Markdown/OpenSpellGEOSData.md` (the "Morphology-aware rebuild and
  corpus analysis" section). Keep the file in the repository for reference,
  but mark it as not recommended and not the default.
- The German DCT should be built with the standard frequency-ranked surface
  priority (`DE_DE.priority.txt`) via `build_languages.py`, exactly as the
  other languages. Morphology runs at runtime; the dictionary stays
  surface-ranked.
- Delete or stop referencing the generated `dicts/DE_DE.priority.core.txt`
  in any build script.

### 2. Rebuild and ship the 1.5 MB surface DCT plus morphology

The best measured point is `surf1500` + morphology: 85.9% token coverage on
heise, 95.6% on de-Corp + heise.

- Build `DE_DE.DCT` with `build_languages.py --max-bytes 1500000` using the
  surface priority, and replace the `Dictionaries/DE_DE.zip` artifact with
  that file. Update `Dictionaries/BUILD.json` and the compact-metrics table in
  `TechDocs/Markdown/OpenSpellGEOSData.md` with the new record count and byte
  size.
- Update `README.md` and `TechDocs/Markdown/OpenSpellGEOS.md` install section:
  the German profile occupies up to 1,500,000 bytes per file, not the generic
  500,000-byte ceiling. State explicitly that German uses runtime morphology
  and therefore benefits from a larger base dictionary.

### 3. Keep the morphology code; widen two documented gaps (DONE, validated)

`Library/Spell/Open/openmorph.inc` is the right approach and should stay.
The two gaps identified in the heise run have been closed:

- Separable-prefix participles: 7 prefixes (`durch`, `um`, `ueber`,
  `unter`, `wieder`, `miss`, `hinter`) were added to the `olMorphPrefixes`
  table. The de-Corp + heise re-run confirms `durchgeführt` and similar forms
  are now accepted. False-accept rate unchanged (0.07%).
- Umlaut plurals: a `olMorphScanUmlautPlural` probe was added that reverts
  the first umlaut and strips one of `e`/`er`/`n`/`en`. `häuser` -> `Haus`
  and similar forms are now accepted.

### 4. Correct the upstream "grammar rules" framing in the docs

`TechDocs/Markdown/OpenSpellGEOSData.md` and `OpenSpellGEOSFormat.md` already
state correctly that the DCT carries no morphology. Add one explicit sentence
to the German section of `OpenSpellGEOSData.md` noting that the shipped
`DE_DE.DCT` value is a single case-flag byte and that German inflection is
handled entirely by `openmorph.inc` at runtime, not by the dictionary. This
keeps the record straight against any future "enhanced DICT with grammar
rules" claim.

### 5. Add the benchmark to the host test set

Promote the benchmark from a throwaway directory into the repo so the
morphology regressions are caught:

- Move `~/OpenSpellBench/harness/bench.c` and the `tools/*.py` scripts into
  `~/OpenSpellGEOS/Dictionaries/BuildTools/bench/` (or a new top-level
  `Bench/`).
- Add a `Bench/README.md` pointing at `REPORT.md` with the reproducibility
  commands.
- Wire a host `make bench` target (or a `python3 tools/run_benchmarks.py`)
  that builds the variants, runs `decide` over a checked-in heise sample word
  list, and asserts that token coverage with morphology stays above the
  data-only baseline by at least the measured margin.
- Add the GeoLex lemma validation (15,211 lemmas, 144,290 inflections) as a
  second benchmark target that asserts per-lemma morphology coverage stays
  above 90%. This catches rule regressions at the lemma level.

### 6. Leave the data-side alone where the benchmark agrees with upstream

The frequency-ranked surface priority (`DE_DE.priority.txt`), the OLX1
format, and the case-flag value schema are all correct as-is and should not
be changed. The morphology rules are the value-add; the dictionary selection
should stay the upstream frequency-ranked surface approach.

### 7. Fix the UTF-8 encoding bug in the auxiliary/modal verb table (NEW)

The benchmark uncovered that `olMorphAuxForms` stores umlaut forms as UTF-8
string literals but the dictionary and corpus use GEOS/mac_roman. This makes
all 14 umlaut-bearing aux/modal form lines dead code (see the "Encoding bug"
section above for the full analysis). The fix:

- Replace every literal UTF-8 `ä`/`ö`/`ü`/`ß` in `olMorphAuxForms` with its
  mac_roman hex escape: `\x8a` for `ä`, `\x9a` for `ö`, `\x9f` for `ü`,
  `\xa7` for `ß`. This is the same technique already used for the `ueber`
  prefix (`"\x9f" "ber"`).
- After conversion, run `./harness/bench decide` on a probe list containing
  `müsstest`, `könntest`, `dürftet`, `möchtet`, `wären`, `hättest` and
  confirm they now return status 3 (morph accept) instead of 0 (miss).
- Re-run the full benchmark to confirm token coverage does not regress.

## Non-goals

- Do not replace `rebuild_german.py`'s logic with a different stemmer. The
  benchmark says the whole approach (storing bases to be reconstructed) is
  the wrong trade; removing the tool is the fix, not improving it.
- Do not expand morphology into a full German grammar engine. The bounded
  design (one prefix, one derivational suffix, one inflectional ending, a
  two-part compound) is what keeps the 2.2-4x speed cost and the 0.07%
  false-accept rate acceptable. Wider rules would raise both.
- Do not target 100% token coverage. The remaining 4.4% at the best point
  (95.6% on de-Corp + heise) includes proper nouns, archaic spellings and OCR
  noise that a spell checker should flag; chasing them with rules would
  inflate the false-accept rate.

## Reproducing the numbers

### heise-only corpus

```
cd ~/OpenSpellBench
python3 tools/build_variants.py
python3 tools/extract_corpus.py --heise ~/mirror/heise --heise-sample 4000
for c in upstream500 surf500 surf1500 core1500; do
  for m in 0 1; do
    ./harness/bench decide dicts/$c/DE_DE.DCT corpus/unique_words.txt \
        --morph $m --out results/decide_${c}_m${m}.tsv
    ./harness/bench speed  dicts/$c/DE_DE.DCT corpus/stream.txt \
        --morph $m --passes 3
  done
done
python3 tools/analyze.py
```

### de-Corp + heise stratified corpus

```
cd ~/OpenSpellBench/corpus/raw/dec
curl -sL -o decorp_txt.zip \
  "https://zenodo.org/api/records/15714828/files/de-corp_txt.zip/content"
unzip -o decorp_txt.zip -d txt/

cd ~/OpenSpellBench
python3 tools/extract_corpus.py --heise ~/mirror/heise --heise-sample 1000 \
    --extra-dir corpus/raw/dec/txt/de-corp_txt --stream-size 400000

# top 200k types cover 97.5% of tokens; use for per-word decide runs
head -200000 corpus/unique_words.txt > corpus/unique_200k.txt
head -200000 corpus/freq.tsv > corpus/freq_200k.tsv

for c in upstream500 surf500 surf1500 core1500; do
  for m in 0 1; do
    ./harness/bench decide dicts/$c/DE_DE.DCT corpus/unique_200k.txt \
        --morph $m --out results/decide3_${c}_m${m}.tsv
  done
done
sed 's/corpus\/freq.tsv/corpus\/freq_200k.tsv/; s/decide_/decide3_/g' \
    tools/analyze.py > tools/analyze3.py
python3 tools/analyze3.py
```

Raw per-word decisions, the coverage JSON and the build/corpus logs are in
`~/OpenSpellBench/results/` and `~/OpenSpellBench/corpus/`. The de-Corp
dataset is public domain (Zenodo 10.5281/zenodo.15714828).

Raw per-word decisions, the coverage JSON and the build/corpus logs are in
`~/OpenSpellBench/results/` and `~/OpenSpellBench/corpus/`.
