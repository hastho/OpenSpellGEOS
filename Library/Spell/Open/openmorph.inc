/*
 * OpenSpellGEOS bounded morphology acceptance for PC/GEOS Ensemble.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The dictionary stores base forms. This fragment accepts an unknown folded
 * spelling when a bounded rule sequence reconstructs a stored base: at most
 * one negating prefix (un-), at most one derivational suffix (-bar), and at
 * most one inflectional ending, plus a two-part compound check with per-part
 * case probes. Every candidate must be found in the selected dictionary; the
 * rules never accept a spelling without a stored base. Derivation changes
 * unsigned short class, so -bar requires a reconstructed verb infinitive, not a bare
 * stem hit. Irregular or semantically opaque derivatives stay as explicit
 * dictionary entries.
 *
 * All scratch lives in the caller-provided workspace. No heap, no libc and
 * no machine-dependent instructions are used. The ending scan is bounded by
 * a fixed table; a long unsigned short can exhaust the probe budget before later
 * endings are tried. Compound parts are checked with case probes and one
 * linking-element strip only; deeper part analysis is a documented upgrade.
 * See TechDocs/Markdown/OpenSpellGEOSMorphology.md for the rule evidence and
 * the measured acceptance and false-accept behavior.
 */
#ifndef OPENMORPH_INC
#define OPENMORPH_INC

#define OL_MORPH_MIN_WORD 3
#define OL_MORPH_MIN_STEM 3
#define OL_MORPH_MIN_PART 3
#define OL_MORPH_MAX_ENDING 7
#define OL_MORPH_LANG_GERMAN 6

/* Caller-owned scratch. One context is used serially by one caller. */
typedef struct {
    unsigned char candidate[OL_KEY_MAX + 1];
    unsigned char part[OL_KEY_MAX + 1];
    unsigned char probe[OL_KEY_MAX + 1];
    unsigned char inner[OL_KEY_MAX + 1];
} OLMorphWorkspace;

/* Inflectional, preterite and participle endings, longest first. Plain
 * endings only; productive derivational suffixes other than -bar stay out
 * of the automatic set. Vowel-class endings strip a bare vowel surface;
 * their reconstruction may only add en, because a stem+n analysis would
 * accept invalid vowel-final spellings of weak verbs. */
static const struct {
    char text[OL_MORPH_MAX_ENDING];
    unsigned char vowelClass;
} olMorphEndings[] = {
    { "endest", 0 }, { "enden", 0 }, { "endes", 0 }, { "ender", 0 },
    { "eten", 0 }, { "eren", 0 }, { "sten", 0 }, { "stes", 0 },
    { "stem", 0 }, { "test", 0 }, { "est", 0 }, { "end", 0 },
    { "es", 1 }, { "em", 1 }, { "er", 1 }, { "en", 1 }, { "et", 0 },
    { "te", 0 }, { "st", 0 }, { "e", 1 }, { "n", 0 }, { "s", 0 },
    { "t", 0 }
};
#define OL_MORPH_ENDING_COUNT \
    ((unsigned short)(sizeof(olMorphEndings) / sizeof(olMorphEndings[0])))

/* Separable verb prefixes for the ge-participle reconstruction. */
static const char olMorphPrefixes[][4] = {
    "ab", "an", "auf", "aus", "bei", "da", "ein", "her", "hin", "los",
    "mit", "nach", "vor", "weg", "zu"
};
#define OL_MORPH_PREFIX_COUNT \
    ((unsigned short)(sizeof(olMorphPrefixes) / sizeof(olMorphPrefixes[0])))

static void OL_CALL olMorphCopy(const unsigned char *sourceP,
                                unsigned char *targetP, unsigned short length)
{
    unsigned short index;
    for (index = 0; index < length; ++index) {
        targetP[index] = sourceP[index];
    }
}

static unsigned short OL_CALL olMorphTextLength(const char *textP)
{
    unsigned short length = 0;
    while (textP[length] != 0) {
        ++length;
    }
    return length;
}

/* Probe one candidate in folded form, then in title case for exact-case
 * nouns. wordP and probeP must not alias. Returns one on a dictionary hit,
 * zero on absence, -1 on malformed data or I/O failure. */
static int OL_CALL olMorphProbe(OLContext *lexP, const unsigned char *wordP,
                                unsigned short length, unsigned char *probeP)
{
    unsigned char flag;
    unsigned short valueLength;
    int result;
    unsigned short index;

    result = olFind(lexP, wordP, (unsigned short)length, &flag, 1, &valueLength);
    if (result != 0) {
        if (result == 1 && valueLength == 1 &&
            (flag == OL_FOLD_CASE || flag == OL_EXACT_CASE)) {
            return 1;
        }
        return result == 1 ? 0 : -1;
    }
    probeP[0] = olUpper(wordP[0]);
    for (index = 1; index < length; ++index) {
        probeP[index] = wordP[index];
    }
    result = olFind(lexP, probeP, (unsigned short)length, &flag, 1, &valueLength);
    if (result == 1 && valueLength == 1 &&
        (flag == OL_FOLD_CASE || flag == OL_EXACT_CASE)) {
        return 1;
    }
    return result < 0 ? -1 : 0;
}

/* Build stem+suffix into candidateP and probe it. probeP must differ from
 * candidateP. Returns one on a hit, zero on absence, -1 on error. */
static int OL_CALL olMorphTry(OLContext *lexP, const unsigned char *stemP,
                              unsigned short stemLength, const char *suffixP,
                              unsigned short suffixLength, unsigned char *candidateP,
                              unsigned char *probeP)
{
    unsigned short index;
    unsigned short total;

    total = stemLength + suffixLength;
    if (total > OL_KEY_MAX || total < OL_MORPH_MIN_WORD) {
        return 0;
    }
    for (index = 0; index < stemLength; ++index) {
        candidateP[index] = stemP[index];
    }
    for (index = 0; index < suffixLength; ++index) {
        candidateP[stemLength + index] = (unsigned char)suffixP[index];
    }
    return olMorphProbe(lexP, candidateP, total, probeP);
}

/* Replace the first GEOS umlaut byte with its plain vowel. Returns one when
 * a replacement happened. GEOS SBCS stores ae oe ue as single bytes. */
static int OL_CALL olMorphDeUmlaut(const unsigned char *sourceP,
                                   unsigned char *targetP, unsigned short length)
{
    static const unsigned char umlautBytes[] = { 0x8A, 0x9A, 0x9F };
    static const unsigned char plainBytes[] = { 'a', 'o', 'u' };
    unsigned short index;
    unsigned short map;

    for (index = 0; index < length; ++index) {
        for (map = 0; map < 3; ++map) {
            if (sourceP[index] == umlautBytes[map]) {
                olMorphCopy(sourceP, targetP, length);
                targetP[index] = plainBytes[map];
                return 1;
            }
        }
    }
    return 0;
}

/* Scan one inflectional ending and probe the reconstructed bases:
 * the stripped stem, stem+n and stem+en, the ge-participle stem without
 * the prefix, and one de-umlauted variant. Returns one on a hit, zero on
 * absence, -1 on error. */
static int OL_CALL olMorphScanEndings(OLContext *lexP,
                                      const unsigned char *wordP,
                                      unsigned short length, OLMorphWorkspace *workP)
{
    unsigned short ending;
    unsigned short endingLength;
    unsigned short stemLength;
    unsigned short index;
    unsigned short match;
    int probe;
    int deumlaut;

    for (ending = 0; ending < OL_MORPH_ENDING_COUNT; ++ending) {
        endingLength = olMorphTextLength(olMorphEndings[ending].text);
        if (length <= endingLength ||
            length - endingLength < OL_MORPH_MIN_STEM) {
            continue;
        }
        match = 1;
        for (index = 0; index < endingLength; ++index) {
            if (wordP[length - endingLength + index] !=
                (unsigned char)olMorphEndings[ending].text[index]) {
                match = 0;
                break;
            }
        }
        if (!match) {
            continue;
        }
        stemLength = length - endingLength;
        probe = olMorphTry(lexP, wordP, stemLength, "", 0,
                           workP->candidate, workP->probe);
        if (probe != 0) {
            return probe;
        }
        if (!olMorphEndings[ending].vowelClass) {
            probe = olMorphTry(lexP, wordP, stemLength, "n", 1,
                               workP->candidate, workP->probe);
            if (probe != 0) {
                return probe;
            }
        }
        probe = olMorphTry(lexP, wordP, stemLength, "en", 2,
                           workP->candidate, workP->probe);
        if (probe != 0) {
            return probe;
        }
        if (stemLength >= 5 && wordP[0] == 'g' && wordP[1] == 'e') {
            probe = olMorphTry(lexP, wordP + 2, stemLength - 2, "", 0,
                               workP->candidate, workP->probe);
            if (probe != 0) {
                return probe;
            }
            probe = olMorphTry(lexP, wordP + 2, stemLength - 2, "en", 2,
                               workP->candidate, workP->probe);
            if (probe != 0) {
                return probe;
            }
        }
        deumlaut = olMorphDeUmlaut(wordP, workP->inner, stemLength);
        if (deumlaut) {
            probe = olMorphTry(lexP, workP->inner, stemLength, "", 0,
                               workP->candidate, workP->probe);
            if (probe != 0) {
                return probe;
            }
        }
    }
    return 0;
}

/* Accept V+-bar derivatives only through a reconstructed infinitive:
 * strip -bar, then probe stem+en and stem+n. Returns one on a hit, zero on
 * absence, -1 on error. */
static int OL_CALL olMorphScanBar(OLContext *lexP, const unsigned char *wordP,
                                  unsigned short length, OLMorphWorkspace *workP)
{
    unsigned short stemLength;
    int probe;

    if (length < 6 || wordP[length - 3] != 'b' || wordP[length - 2] != 'a' ||
        wordP[length - 1] != 'r' || length - 3 < OL_MORPH_MIN_STEM) {
        return 0;
    }
    stemLength = length - 3;
    probe = olMorphTry(lexP, wordP, stemLength, "en", 2,
                       workP->candidate, workP->probe);
    if (probe != 0) {
        return probe;
    }
    return olMorphTry(lexP, wordP, stemLength, "n", 1,
                      workP->candidate, workP->probe);
}

/* Accept V+-ung nouns through a reconstructed verb infinitive:
 * strip -ung, then probe stem+en and stem+n. The noun is capitalized
 * in German; the title-case probe handles that. Returns one on a hit,
 * zero on absence, -1 on error. */
static int OL_CALL olMorphScanUng(OLContext *lexP, const unsigned char *wordP,
                                  unsigned short length, OLMorphWorkspace *workP)
{
    unsigned short stemLength;
    int probe;

    if (length < 6 || wordP[length - 3] != 'u' || wordP[length - 2] != 'n' ||
        wordP[length - 1] != 'g' || length - 3 < OL_MORPH_MIN_STEM) {
        return 0;
    }
    stemLength = length - 3;
    probe = olMorphTry(lexP, wordP, stemLength, "en", 2,
                       workP->candidate, workP->probe);
    if (probe != 0) {
        return probe;
    }
    return olMorphTry(lexP, wordP, stemLength, "n", 1,
                      workP->candidate, workP->probe);
}

/* Accept A+-keit nouns through a reconstructed adjective base:
 * strip -keit, then probe the stem directly and stem+ig. Returns one
 * on a hit, zero on absence, -1 on error. */
static int OL_CALL olMorphScanKeit(OLContext *lexP, const unsigned char *wordP,
                                   unsigned short length, OLMorphWorkspace *workP)
{
    unsigned short stemLength;
    int probe;

    if (length < 7 || wordP[length - 4] != 'k' || wordP[length - 3] != 'e' ||
        wordP[length - 2] != 'i' || wordP[length - 1] != 't' ||
        length - 4 < OL_MORPH_MIN_STEM) {
        return 0;
    }
    stemLength = length - 4;
    probe = olMorphTry(lexP, wordP, stemLength, "", 0,
                       workP->candidate, workP->probe);
    if (probe != 0) {
        return probe;
    }
    return olMorphTry(lexP, wordP, stemLength, "ig", 2,
                      workP->candidate, workP->probe);
}

/* Accept A+-heit nouns through a reconstructed adjective base:
 * strip -heit, then probe the stem directly. Returns one on a hit,
 * zero on absence, -1 on error. */
static int OL_CALL olMorphScanHeit(OLContext *lexP, const unsigned char *wordP,
                                   unsigned short length, OLMorphWorkspace *workP)
{
    unsigned short stemLength;

    if (length < 7 || wordP[length - 4] != 'h' || wordP[length - 3] != 'e' ||
        wordP[length - 2] != 'i' || wordP[length - 1] != 't' ||
        length - 4 < OL_MORPH_MIN_STEM) {
        return 0;
    }
    stemLength = length - 4;
    return olMorphTry(lexP, wordP, stemLength, "", 0,
                      workP->candidate, workP->probe);
}

/* Reconstruct separable-prefix participles: prefix+ge+rest probes the
 * infinitive prefix+rest when the participle marker is removable. Returns
 * one on a hit, zero on absence, -1 on error. */
static int OL_CALL olMorphScanParticiple(OLContext *lexP,
                                         const unsigned char *wordP,
                                         unsigned short length, OLMorphWorkspace *workP)
{
    unsigned short prefix;
    unsigned short prefixLength;
    unsigned short restOffset;
    unsigned short restLength;
    int probe;

    if (length < 7) {
        return 0;
    }
    for (prefix = 0; prefix < OL_MORPH_PREFIX_COUNT; ++prefix) {
        prefixLength = olMorphTextLength(olMorphPrefixes[prefix]);
        restOffset = prefixLength + 2;
        if (length <= restOffset + OL_MORPH_MIN_STEM - 1 ||
            wordP[prefixLength] != 'g' || wordP[prefixLength + 1] != 'e') {
            continue;
        }
        olMorphCopy((const unsigned char *)olMorphPrefixes[prefix],
                    workP->inner, prefixLength);
        restLength = length - restOffset;
        if (wordP[length - 1] == 't') {
            olMorphCopy(wordP + restOffset, workP->inner + prefixLength,
                        restLength - 1);
            probe = olMorphTry(lexP, workP->inner, prefixLength + restLength - 1,
                               "en", 2, workP->candidate, workP->probe);
            if (probe != 0) {
                return probe;
            }
        }
    }
    return 0;
}

/* Run the suffix and ending scans once. depth guards the un- recursion. */
static int OL_CALL olMorphScanInner(OLContext *lexP, const unsigned char *wordP,
                                    unsigned short length, OLMorphWorkspace *workP,
                                    unsigned short depth, unsigned char language)
{
    int probe;

    if (language == OL_MORPH_LANG_GERMAN) {
        probe = olMorphScanBar(lexP, wordP, length, workP);
        if (probe != 0) {
            return probe;
        }
        probe = olMorphScanUng(lexP, wordP, length, workP);
        if (probe != 0) {
            return probe;
        }
        probe = olMorphScanKeit(lexP, wordP, length, workP);
        if (probe != 0) {
            return probe;
        }
        probe = olMorphScanHeit(lexP, wordP, length, workP);
        if (probe != 0) {
            return probe;
        }
        probe = olMorphScanParticiple(lexP, wordP, length, workP);
        if (probe != 0) {
            return probe;
        }
        probe = olMorphScanEndings(lexP, wordP, length, workP);
        if (probe != 0) {
            return probe;
        }
    }
    if (depth == 0 && length >= 5 && wordP[0] == 'u' && wordP[1] == 'n') {
        probe = olMorphScanInner(lexP, wordP + 2, length - 2, workP, 1,
                                 language);
        if (probe != 0) {
            return probe;
        }
    }
    return 0;
}

/* Validate one compound part with case probes and one linking-element
 * strip on the left part. Returns one on a hit, zero on absence. */
static int OL_CALL olMorphPart(OLContext *lexP, const unsigned char *partP,
                               unsigned short length, OLMorphWorkspace *workP,
                               unsigned short allowLink)
{
    unsigned short stemLength;
    int probe;

    /* Direct case probe. */
    probe = olMorphProbe(lexP, partP, length, workP->probe);
    if (probe != 0) {
        return probe;
    }
    /* Try one noun ending strip for compound parts: plural -e, dative -n
     * or genitive -s. For example, "attribute" strips to "attribut". */
    if (length > OL_MORPH_MIN_PART + 1 &&
        (partP[length - 1] == 'e' || partP[length - 1] == 'n' ||
         partP[length - 1] == 's')) {
        stemLength = length - 1;
        olMorphCopy(partP, workP->probe, stemLength);
        probe = olMorphProbe(lexP, workP->probe, stemLength,
                             workP->candidate);
        if (probe != 0) {
            return probe;
        }
    }
    /* Try one linking-element strip on the left part. */
    if (!allowLink || length < 5) {
        return 0;
    }
    if (partP[length - 1] == 's' || partP[length - 1] == 'n') {
        stemLength = length - 1;
    } else if (partP[length - 2] == 'e' &&
               (partP[length - 1] == 's' || partP[length - 1] == 'n')) {
        stemLength = length - 2;
    } else {
        return 0;
    }
    if (stemLength < OL_MORPH_MIN_PART) {
        return 0;
    }
    olMorphCopy(partP, workP->candidate, stemLength);
    return olMorphProbe(lexP, workP->candidate, stemLength, workP->probe);
}

/* Accept an attested-compound spelling when both parts are stored bases.
 * The whole unsigned short itself is not required to be stored. Returns one on a
 * hit, zero on absence, -1 on error. */
static int OL_CALL olMorphCompound(OLContext *lexP, const unsigned char *wordP,
                                   unsigned short length, OLMorphWorkspace *workP)
{
    unsigned short split;
    int left;
    int right;

    if (length < 2 * OL_MORPH_MIN_PART) {
        return 0;
    }
    for (split = OL_MORPH_MIN_PART; split <= length - OL_MORPH_MIN_PART;
         ++split) {
        olMorphCopy(wordP, workP->candidate, split);
        left = olMorphPart(lexP, workP->candidate, split, workP, 1);
        if (left != 1) {
            if (left < 0) {
                return left;
            }
            continue;
        }
        olMorphCopy(wordP + split, workP->part, length - split);
        right = olMorphPart(lexP, workP->part, length - split, workP, 0);
        if (right == 1) {
            return 1;
        }
        if (right < 0) {
            return right;
        }
    }
    return 0;
}

/* Public entry. wordP is the folded lowercase spelling. language is the
 * GEOS StandardLanguage code from the active dictionary. German-specific
 * rules (inflectional endings, -bar, -ung, -keit, -heit, separable-prefix
 * participles, de-umlaut) run only for SL_GERMAN (6). The compound splitter
 * and the un- prefix are language-agnostic. Returns one when a bounded
 * reconstruction finds a stored base, zero when the spelling stays unknown,
 * -1 on malformed data or I/O failure. */
int OL_CALL olMorphCheck(OLContext *lexP, const unsigned char *wordP,
                         unsigned short length, OLMorphWorkspace *workP,
                         unsigned char language)
{
    int probe;

    if (length < OL_MORPH_MIN_WORD || length > OL_KEY_MAX) {
        return 0;
    }
    probe = olMorphScanInner(lexP, wordP, length, workP, 0, language);
    if (probe != 0) {
        return probe;
    }
    return olMorphCompound(lexP, wordP, length, workP);
}

#endif
