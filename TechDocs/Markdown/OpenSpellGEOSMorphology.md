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

The separable-prefix participle rule tries 22 prefix entries (`ab`, `an`,
`auf`, `aus`, `bei`, `da`, `ein`, `her`, `hin`, `los`, `mit`, `nach`,
`vor`, `weg`, `zu`, `durch`, `um`, `über`, `unter`, `wieder`, `miss`,
`hinter`). It reconstructs the infinitive when the word is
`prefix + ge + rest` and `rest` ends in `t`. For `-eln`/`-ern` weak verbs
whose infinitive ends in `-n` rather than `-en` (e.g. `handeln` →
`abgehandelt`), the rule also probes `prefix + rest + n` after
`prefix + rest + en` fails.

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

## Static tables and the code-segment budget

`openmorph.inc` carries five small static tables. Only one of them
(`olMorphAuxForms`) is lexical data that could in principle live in the
dictionary; the other four are byte-pattern matchers that must stay in the
code segment.

| Table | Entries | Bytes | Role | Movable to dict? |
| --- | ---: | ---: | --- | :---: |
| `olMorphAuxForms` | 117 | ~791 | aux/modal verb forms (`bin`, `ist`, `war`, `hat`, `wird`, `kann`...) | yes, but kept in code (see below) |
| `olMorphPrefixes` | 22 | 161 | separable-prefix byte patterns (`ab`, `an`, `auf`...`hinter`) | no — matched against input bytes, not looked up in dict |
| `olMorphEndings` | 23 | 184 | inflectional ending byte patterns (`en`, `er`, `est`...) | no — stripped from input, not looked up |
| `olMorphPluralEndings` | 4 | 12 | noun-plural ending byte patterns (`e`, `er`, `n`, `en`) | no — same |
| `olMorphLinkers` | 7 | 35 | compound linking-element byte patterns (`s`, `es`, `n`...) | no — matched at the split boundary |

### Why `olMorphAuxForms` stays in the code segment

The auxiliary/modal verb table is the one candidate for "move lexical
knowledge into the dictionary". 112 of its 117 forms are already present as
exact-match records in the shipped `DE_DE.DCT` (surf1500); only five archaic
subjunctive-II forms (`könnet`, `dürfet`, `müsset`, `sollet`, `wollet`) are
absent and would need to be added. Deleting the table and `olMorphScanAux`
would save ~891 bytes of code segment and add ~50 bytes to the dictionary.

The table is kept in code because it is a **performance cache**, not pure
lexical data. `olMorphScanAux` runs first in `olMorphScanInner` and accepts
these forms with one bounded linear scan over short strings and zero I/O.
Without it, every aux/modal form would flow through the ending-strip,
participle, umlaut-plural and compound paths before being accepted, adding
~20 `olFind` probes per word.

On the 200,000-token heise stream, aux/modal forms account for 3.03% of all
tokens (6,067 occurrences). These are the most common German verbs
(`ist`, `hat`, `war`, `wird`, `kann`, `soll`, `will`, `wurde`, ...). On a
286 with a floppy-drive-backed dictionary, the extra probes per aux word
are a measurable lookup-time regression on exactly the most frequent
tokens. The 891 bytes saved is ~1.8% of the geode's fixed code; the trade
is **fast path for the most common words vs. code size**, and the fast
path wins on a 286.

A compile-time guard (`OlMorphAuxSizeCheck`) rejects any build that lets the
table grow past 2,048 bytes, so the table cannot silently bloat the code
segment. If a future maintainer needs the ~891 bytes for another rule, the
move-to-dict path is the documented fallback: delete the table and
`olMorphScanAux`, add the five missing forms to `DE_DE.priority.txt`, and
accept the ~4× probe cost on the 3% of tokens that are aux/modal forms.

The umlaut-bearing forms in the table (e.g. `wäre`, `hätte`, `würde`,
`könnte`, `weiß`) are stored with explicit mac_roman hex escapes
(`\x8a` for ä, `\x9a` for ö, `\x9f` for ü, `\xa7` for ß), not UTF-8. The
spell checker processes input in GEOS SBCS (mac_roman); a C string literal
containing a raw UTF-8 umlaut (two bytes, e.g. `0xC3 0xA4`) would never
match a one-byte mac_roman input (e.g. `0x8A`). Forms like `m\x9a" "chte"`
use C string concatenation to prevent the hex escape from consuming the
following hex digit (`c`), which would produce an out-of-range character
constant.

### What the other tables are not

The four pattern-matching tables (`olMorphPrefixes`, `olMorphEndings`,
`olMorphPluralEndings`, `olMorphLinkers`) are **not** dictionary entries.
They are byte patterns matched against the input spelling's own bytes —
e.g. `olMorphPrefixes` checks that the input starts with `ab` before
looking for `ab + ge + rest + t`. Moving them to the dictionary would
require a round-trip lookup per pattern, which is slower than the current
direct byte comparison and adds no attestation value (the attestation is
already done by the `olFind` probe on the reconstructed stem). They stay
in the code segment by design.

## Documented gaps

- Strong-verb pure participles such as `geschoben` from `schieben` are not
  accepted unless the participle itself has an adjectival declension family
  and is stored as a base. The ablaut back-mapping from `o` to `ie` is not
  in the rule set because it is ambiguous. The benchmark showed that the
  most frequent strong-verb participles (`gegangen`, `geschrieben`,
  `getrunken`, ...) are already present as exact entries in the surf1500
  dictionary, so no code rule is needed for them. See
  `TechDocs/Markdown/OpenSpellGEOSMorphologyBenchmark.md`.
- Noun plurals that use umlaut only, such as `Wände` from `Wand`, are
  accepted when the singular is in the dictionary. The bounded
  `olMorphScanUmlautPlural` probe reverts the umlaut in the full word and
  strips one of `e`/`er`/`n`/`en`, so `Mütter` → `Mutter` and `Häuser` →
  `Haus` now resolve without a stored plural. The Free German Dictionary
  source does not contain every singular noun (e.g. `Wand`), so `Wände`
  stays unknown. This is a source-data gap, not a rule limitation.
- `-eln`/`-ern` weak-verb participles (`abhandeln` → `abgehandelt`) are
  now accepted via the `stem + n` fallback in the participle rule; the
  original `stem + en` probe produced the wrong infinitive for this verb
  class. See the "Follow-up" section in
  `TechDocs/Markdown/OpenSpellGEOSMorphologyBenchmark.md`.
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

The German dictionary is built with the standard frequency-ranked surface
priority (`Dictionaries/BuildTools/Data/DE_DE.priority.txt`) via
`Dictionaries/BuildTools/build_german_surface.py`, not the morphology-aware
core/base-form rebuild. The benchmark in
`TechDocs/Markdown/OpenSpellGEOSMorphologyBenchmark.md` measured that
surface-ranked selection plus runtime morphology reaches 85.9% token
coverage on a 4,000-article heise corpus, beating the core/base-form
rebuild (80.6%) by 5.3 percentage points. German inflection is handled at
runtime by `openmorph.inc`; the dictionary keeps the most frequent surface
forms so the cheap exact path fires often.

| File | Records | Bytes |
| --- | ---: | ---: |
| DE_DE.DCT | 194,009 | 1,499,617 |
| DE_DE.HYP | 173,841 | 1,499,617 |

The DCT and HYP are at the 1,500,000-byte budget. The THS and GDI files
are unchanged. The ranking uses only the pinned Free German Dictionary
snapshot. No external corpus or copyleft-licensed data is used.

### Rebuild tool

The script `Dictionaries/BuildTools/build_german_surface.py` automates the
surface-ranked rebuild at the German 1,500,000-byte budget. It requires the
extracted Data snapshot (`Dictionaries/BuildTools/Data/`).

```text
python3 Dictionaries/BuildTools/build_german_surface.py --budget 1500000
```

The `--budget` option sets the per-file byte ceiling (default 1,500,000).
The script selects entries with the surface priority, writes the DCT,
regenerates the HYP from the selected vocabulary, and removes its temporary
files. The deprecated `rebuild_german.py` (morphology-aware core ranking)
is retained for reference but is not the default and not recommended; see
`TechDocs/Markdown/OpenSpellGEOSData.md` and
`TechDocs/Markdown/OpenSpellGEOSMorphologyBenchmark.md` for the evidence.

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
