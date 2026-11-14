SPDX-License-Identifier: Apache-2.0

# OpenSpellGEOS bounded morphology acceptance

The dictionary stores base forms. The library accepts an unknown spelling
when a bounded rule sequence reconstructs a stored base. The rules never
accept a spelling without a stored base. No heap, no libc and no
machine-dependent instructions are used. The analyzer runs on the 286 target.

## Architecture

The check path has three stages:

1. Exact byte lookup of the typed spelling.
2. Folded-case lookup for non-mixed-case spellings.
3. Bounded morphology reconstruction over the folded spelling.

Stage three uses the `olMorphCheck` function in `Library/Spell/Open/openmorph.inc`.
It tries at most one negating prefix, one derivational suffix and one
inflectional ending, then a two-part compound check. The caller provides
the `OLMorphWorkspace` scratch structure; it is part of the
`OpenSpellContext` and adds 260 bytes to the 8192-byte context budget.

## Rule classes

### Tier 1: automatic inflection and derivation

| Rule | Input | Reconstruction | Example |
| --- | --- | --- | --- |
| Ending strip (consonant) | behandelt | stem + n, stem + en | behandel + n = behandeln |
| Ending strip (vowel) | umfließe | stem + en | umfließ + en = umfließen |
| ge-participle strip | gezeichnet | stem + 2, stem + 2 + en | zeichn + en = zeichnen |
| Separable-prefix participle | angezeigt | prefix + rest + en | an + zeig + en = anzeigen |
| De-umlaut | Wände | stem with ae/oe/ue to a/o/u | wand |
| Derivational -bar | einstellbar | stem + en, stem + n | einstell + en = einstellen |
| Negating un- | unlesbar | inner scan of word + 2 | lesbar then -bar then lesen |

Vowel-class endings (`e`, `es`, `em`, `er`, `en`) reconstruct with `stem + en`
only. Consonant-class endings also add `stem + n`. This split prevents the
acceptance of invalid vowel-final spellings of weak verbs such as
`behandele`, which would otherwise strip to `behandel` and reconstruct
`behandeln`.

The ending table has 23 entries, ordered longest first. The ending must
match the word tail before the stem is used. The ge-participle strip runs
only when the stem starts with `ge` and has at least five characters.

The separable-prefix participle rule tries 15 prefix entries (`ab`, `an`,
`auf`, `aus`, `bei`, `da`, `ein`, `her`, `hin`, `los`, `mit`, `nach`,
`vor`, `weg`, `zu`). It reconstructs the infinitive when the word is
`prefix + ge + rest` and `rest` ends in `t`.

### Compound words

The compound check splits the folded spelling at each position where both
parts have at least three characters. Each part is probed in folded form
and in title case. The left part also tries one linking-element strip
(`s`, `es`, `n`, `en`). The whole word itself is not required to be stored.

Example: `Schriftgröße` splits into `Schrift` and `größe`. The right part
is probed in title case as `Größe`, which is a stored exact-case noun.

### Tier 2: restricted suffixes

The suffixes `-isch`, `-ig`, `-los`, `-lich`, `-schaft`, `-ei` and `-tum`
are not accepted automatically. Their derivatives are stored as dictionary
bases when the source attests them with sufficient family evidence. A
curated allow-list or frequency filter is the documented upgrade path.

### Tier 3: irregular forms

Irregular comparatives (`gut` to `besser`), suppletive plurals
(`Mann` to `Maenner`) and strong-verb ablaut participles without adjectival
use stay as explicit dictionary entries. The analyzer does not generate
them.

## Documented gaps

- Strong-verb pure participles such as `geschoben` from `schieben` are not
  accepted unless the participle itself has an adjectival declension family
  and is stored as a base. The ablaut back-mapping from `o` to `ie` is not
  in the rule set because it is ambiguous.
- Noun plurals that use umlaut only, such as `Wände` from `Wand`, are
  accepted only when the singular is in the dictionary. The Free German
  Dictionary source does not contain `Wand` (the singular noun), so
  `Wände` stays unknown. This is a source-data gap, not a rule limitation.
- Compound parts are checked with case probes and one linking-element strip
  only. Deeper part analysis, such as inflection stripping on a compound
  part, is a documented upgrade.

## Measured acceptance and false-accept behavior

The measurement uses the StemExperiment protocol: every tenth form of the
pinned German word list is held out, and the checker accepts a held-out
form when the exact, folded or morphology path finds a stored base.
False-accept classes mutate accepted forms with deletion, substitution,
transposition and letter doubling.

| Metric | Shipped DCT (exact only) | New DCT with morphology |
| --- | ---: | ---: |
| Held-out recall | 6.03 percent | 54.51 percent |
| Deletion false accept | 1.20 percent | 9.54 percent |
| Substitution false accept | 0.20 percent | 4.68 percent |
| Transposition false accept | 0.18 percent | 3.38 percent |
| Doubled-letter false accept | 0.06 percent | 3.56 percent |

The morphology analyzer trades a ninefold recall improvement for a higher
false-accept rate. The false-accept rate is in the same range as the
Snowball stem prototype measured in the stemming evaluation. A separate
compact correction index is the documented upgrade path for broader recall
with lower false acceptance.

## Data rebuild

The German dictionary was rebuilt with a morphology-aware ranking that
selects base forms. The ranking identifies cores: attested forms whose
morphology family has at least four attested relatives for lowercase forms
or two for capitalized nouns. The cores replace the previous length-ordered
fallback in the priority file. The compound tier was removed from the
priority because the analyzer validates compounds at runtime.

| File | Records | Bytes |
| --- | ---: | ---: |
| DE_DE.DCT | 162,329 | 1,499,617 |
| DE_DE.HYP | 139,479 | 1,499,617 |

The DCT and HYP are at the 1,500,000-byte budget. The THS and GDI files
are unchanged. The ranking uses only the pinned Free German Dictionary
snapshot and original morphology rules. No external corpus or
copyleft-licensed data is used.

### Rebuild tool

The script `Dictionaries/BuildTools/rebuild_german.py` automates the
morphology-aware rebuild. It requires the extracted Data snapshot
(`Dictionaries/BuildTools/Data/`) and produces `DE_DE.DCT` and `DE_DE.HYP`.

```text
python3 Dictionaries/BuildTools/rebuild_german.py --budget 1500000
```

The `--budget` option sets the per-file byte ceiling. The sweet-spot
analysis recommends 1,500,000 bytes for German. The script rebuilds the
priority file with the morphology-core ranking, selects the fitting prefix,
writes the DCT, regenerates the HYP from the selected vocabulary, and
removes its temporary files.

### Corpus coverage tool

The script `Dictionaries/BuildTools/corpus_coverage.py` extracts words
from a directory of text or HTML files and writes a frequency-sorted
mac_roman word list. The output is used to measure dictionary coverage
against real-world text.

```text
python3 Dictionaries/BuildTools/corpus_coverage.py \
    --input ~/mirror/heise \
    --pattern art.htm \
    --encoding iso-8859-1 \
    --sample 500 \
    --output corpus_words.txt
```

The output file can be read by a C test program that links the OLX1
reader and the morphology analyzer, measuring the acceptance rate of
the combined exact, folded and morphology paths. The sweet-spot curve
for the German DCT budget is:

| Budget (bytes) | Records | Corpus recall | Notes |
| ---: | ---: | ---: | --- |
| 500,000 | 61,535 | 35.4 percent | Core vocabulary only |
| 750,000 | 90,060 | 42.2 percent | Common words fill in |
| 1,000,000 | 116,878 | 44.8 percent | Shipped size |
| **1,500,000** | **162,329** | **47.2 percent** | **Sweet spot** |
| 2,000,000 | 212,818 | 47.9 percent | Diminishing returns |
| 3,000,000 | 325,540 | 55.7 percent | Mostly English loanwords |

## Runtime cost

The analyzer runs only when the exact and folded lookups fail. It probes
at most 23 endings with up to six candidates each, plus the compound check.
Each candidate requires at most two `olFind` calls (folded and title case).
The worst-case probe count is bounded by the fixed ending table and the
word length. The measured host runtime for the full recall test was 60
seconds for 215,264 held-out forms, including the mutations. The target
runtime per unknown word is a small number of dictionary reads.
