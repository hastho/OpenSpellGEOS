/*
 * OpenSpellBench host benchmark driver for OpenSpellGEOS.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Links the portable C89 spell engine (openlex.inc, openmorph.inc) unmodified
 * and measures acceptance decisions and lookup cost on the host.
 *
 * Modes:
 *   decide  Read unique words (one per line, GEOS/mac_roman bytes) and print
 *           "word<TAB>result" where result is 0=miss, 1=exact, 2=folded,
 *           3=morphology.
 *   speed   Read a token stream (one token per line, repetitions kept) and
 *           report elapsed CPU time, reader callback reads and bytes for the
 *           requested number of passes.
 *
 * The check path mirrors the documented adapter order: exact byte lookup,
 * folded-case lookup for non-mixed-case spellings (flag 1 entries accept any
 * case; flag 2 entries require exact case), then bounded morphology over the
 * folded spelling.
 *
 * Usage: bench decide <dct> <words> [--morph 0|1] [--lang N] [--out FILE]
 *        bench speed  <dct> <stream> [--morph 0|1] [--lang N] [--passes N]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "openlex.inc"
#include "openmorph.inc"

#define WORD_MAX (OL_KEY_MAX + 1)

typedef struct {
    FILE *fp;
    unsigned long reads;
    unsigned long bytes;
} FileUser;

static int OL_CALL fileRead(void *userP, unsigned long offset,
                            unsigned short count, unsigned char *bufferP)
{
    FileUser *user = (FileUser *)userP;

    if (fseek(user->fp, (long)offset, SEEK_SET) != 0) {
        return 0;
    }
    user->reads++;
    user->bytes += count;
    if (fread(bufferP, 1, count, user->fp) != count) {
        return 0;
    }
    return 1;
}

/* A spelling is mixed case when an uppercase byte follows a lowercase byte
 * position, e.g. "OpenGEOS". Title case and all caps are not mixed. */
static int wordIsMixed(const unsigned char *wordP, unsigned short length)
{
    unsigned short i;
    unsigned char sawLower = 0;

    for (i = 0; i < length; ++i) {
        if (wordP[i] >= 'a' && wordP[i] <= 'z') {
            sawLower = 1;
        } else if (wordP[i] >= 'A' && wordP[i] <= 'Z') {
            if (sawLower) {
                return 1;
            }
        }
    }
    return 0;
}

static void foldWord(const unsigned char *wordP, unsigned short length,
                     unsigned char *outP)
{
    unsigned short i;

    for (i = 0; i < length; ++i) {
        outP[i] = olLower(wordP[i]);
    }
    outP[length] = 0;
}

/* Returns 0 miss, 1 exact, 2 folded, 3 morphology, -1 reader error. */
static int checkWord(OLContext *lexP, OLMorphWorkspace *morphP,
                     const unsigned char *wordP, unsigned short length,
                     int morphOn, unsigned char language,
                     unsigned char *foldedP)
{
    unsigned char value[8];
    unsigned short valueLength;
    int result;

    foldWord(wordP, length, foldedP);

    result = olFind(lexP, wordP, length, value, (unsigned short)sizeof(value),
                    &valueLength);
    if (result == 1) {
        return 1;
    }
    if (result < 0) {
        return -1;
    }
    if (!wordIsMixed(wordP, length)) {
        result = olFind(lexP, foldedP, length, value,
                        (unsigned short)sizeof(value), &valueLength);
        if (result == 1) {
            if (value[0] == OL_FOLD_CASE) {
                return 2;
            }
            return 0;
        }
        if (result < 0) {
            return -1;
        }
    }
    if (morphOn) {
        result = olMorphCheck(lexP, foldedP, length, morphP, language);
        if (result == 1) {
            return 3;
        }
        if (result < 0) {
            return -1;
        }
    }
    return 0;
}

static int openDictionary(const char *pathP, OLContext *lexP, FileUser *userP)
{
    unsigned char value[4];
    unsigned short valueLength;

    userP->fp = fopen(pathP, "rb");
    if (userP->fp == NULL) {
        fprintf(stderr, "cannot open %s\n", pathP);
        return 0;
    }
    fseek(userP->fp, 0, SEEK_END);
    userP->bytes = 0;
    userP->reads = 0;
    lexP->fileSize = (unsigned long)ftell(userP->fp);
    fseek(userP->fp, 0, SEEK_SET);
    if (olOpen(lexP, fileRead, userP, OL_DICTIONARY) != 1) {
        fprintf(stderr, "olOpen failed on %s\n", pathP);
        fclose(userP->fp);
        userP->fp = NULL;
        return 0;
    }
    /* Probe one lookup so all lazy caches are populated before timing. */
    value[0] = 'a';
    olFind(lexP, value, 1, value, (unsigned short)sizeof(value), &valueLength);
    userP->reads = 0;
    userP->bytes = 0;
    return 1;
}

static int runDecide(OLContext *lexP, OLMorphWorkspace *morphP,
                     const char *wordsPathP, int morphOn,
                     unsigned char language, const char *outPathP)
{
    FILE *inP;
    FILE *outP;
    unsigned char line[512];
    unsigned char folded[WORD_MAX];
    unsigned long counts[4];

    inP = fopen(wordsPathP, "rb");
    if (inP == NULL) {
        fprintf(stderr, "cannot open %s\n", wordsPathP);
        return 0;
    }
    outP = stdout;
    if (outPathP != NULL) {
        outP = fopen(outPathP, "wb");
        if (outP == NULL) {
            fprintf(stderr, "cannot write %s\n", outPathP);
            fclose(inP);
            return 0;
        }
    }
    counts[0] = counts[1] = counts[2] = counts[3] = 0;
    while (fgets((char *)line, sizeof(line), inP) != NULL) {
        unsigned short length;
        int decision;

        length = (unsigned short)strlen((char *)line);
        while (length > 0 && (line[length - 1] == '\n' ||
                              line[length - 1] == '\r')) {
            length--;
        }
        if (length == 0) {
            continue;
        }
        if (length > OL_KEY_MAX) {
            line[OL_KEY_MAX] = 0;
            length = OL_KEY_MAX;
        }
        decision = checkWord(lexP, morphP, line, length, morphOn, language,
                             folded);
        if (decision < 0) {
            fprintf(stderr, "reader error at word %s\n", (char *)line);
            decision = 0;
        }
        counts[decision >= 0 && decision <= 3 ? decision : 0]++;
        fprintf(outP, "%s\t%d\n", (char *)line, decision);
    }
    fprintf(stderr,
            "unique=%lu exact=%lu folded=%lu morph=%lu miss=%lu\n",
            counts[0] + counts[1] + counts[2] + counts[3],
            counts[1], counts[2], counts[3], counts[0]);
    if (outP != stdout) {
        fclose(outP);
    }
    fclose(inP);
    return 1;
}

static int runSpeed(OLContext *lexP, OLMorphWorkspace *morphP,
                    const char *streamPathP, int morphOn,
                    unsigned char language, long passes, FileUser *fileUserP)
{
    FILE *inP;
    unsigned char line[512];
    unsigned char folded[WORD_MAX];
    unsigned long tokens;
    unsigned long accepted;
    unsigned long pathCounts[4];
    unsigned long startReads;
    unsigned long startBytes;
    unsigned long totalReads;
    unsigned long totalBytes;
    clock_t startClock;
    clock_t endClock;
    double seconds;
    long pass;

    inP = fopen(streamPathP, "rb");
    if (inP == NULL) {
        fprintf(stderr, "cannot open %s\n", streamPathP);
        return 0;
    }
    totalReads = 0;
    totalBytes = 0;
    accepted = 0;
    pathCounts[0] = pathCounts[1] = pathCounts[2] = pathCounts[3] = 0;
    tokens = 0;
    startClock = clock();
    for (pass = 0; pass < passes; ++pass) {
        rewind(inP);
        while (fgets((char *)line, sizeof(line), inP) != NULL) {
            unsigned short length;
            int decision;

            length = (unsigned short)strlen((char *)line);
            while (length > 0 && (line[length - 1] == '\n' ||
                                  line[length - 1] == '\r')) {
                length--;
            }
            if (length == 0) {
                continue;
            }
            if (length > OL_KEY_MAX) {
                length = OL_KEY_MAX;
            }
            startReads = fileUserP->reads;
            startBytes = fileUserP->bytes;
            decision = checkWord(lexP, morphP, line, length, morphOn,
                                 language, folded);
            totalReads += fileUserP->reads - startReads;
            totalBytes += fileUserP->bytes - startBytes;
            if (decision < 0) {
                decision = 0;
            }
            pathCounts[decision]++;
            if (decision != 0) {
                accepted++;
            }
            tokens++;
        }
    }
    endClock = clock();
    seconds = (double)(endClock - startClock) / (double)CLOCKS_PER_SEC;
    fprintf(stderr,
            "tokens=%lu passes=%ld accepted=%lu exact=%lu folded=%lu "
            "morph=%lu miss=%lu seconds=%.3f us_per_token=%.3f "
            "reads=%lu bytes=%lu\n",
            tokens, passes, accepted, pathCounts[1], pathCounts[2],
            pathCounts[3], pathCounts[0], seconds,
            tokens > 0 ? seconds * 1e6 / ((double)tokens * (double)passes)
                       : 0.0,
            totalReads, totalBytes);
    fclose(inP);
    return 1;
}

int main(int argc, char **argv)
{
    OLContext *lexP;
    OLMorphWorkspace *morphP;
    FileUser fileUser;
    int morphOn;
    unsigned char language;
    long passes;
    const char *outPathP;

    if (argc < 4) {
        fprintf(stderr,
                "usage: bench decide|speed <dct> <words> [--morph 0|1] "
                "[--lang N] [--passes N] [--out FILE]\n");
        return 1;
    }
    lexP = (OLContext *)malloc(sizeof(OLContext));
    morphP = (OLMorphWorkspace *)malloc(sizeof(OLMorphWorkspace));
    if (lexP == NULL || morphP == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    memset(lexP, 0, sizeof(OLContext));
    memset(morphP, 0, sizeof(OLMorphWorkspace));
    morphOn = 1;
    language = 6;
    passes = 1;
    outPathP = NULL;
    if (!openDictionary(argv[2], lexP, &fileUser)) {
        return 1;
    }
    if (strcmp(argv[1], "decide") == 0) {
        int i;

        for (i = 4; i < argc; ++i) {
            if (strcmp(argv[i], "--morph") == 0 && i + 1 < argc) {
                morphOn = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
                language = (unsigned char)atoi(argv[++i]);
            } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
                outPathP = argv[++i];
            }
        }
        if (!runDecide(lexP, morphP, argv[3], morphOn, language, outPathP)) {
            return 1;
        }
    } else if (strcmp(argv[1], "speed") == 0) {
        int i;

        for (i = 4; i < argc; ++i) {
            if (strcmp(argv[i], "--morph") == 0 && i + 1 < argc) {
                morphOn = atoi(argv[++i]);
            } else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
                language = (unsigned char)atoi(argv[++i]);
            } else if (strcmp(argv[i], "--passes") == 0 && i + 1 < argc) {
                passes = atol(argv[++i]);
            }
        }
        if (!runSpeed(lexP, morphP, argv[3], morphOn, language, passes,
                      &fileUser)) {
            return 1;
        }
    } else {
        fprintf(stderr, "unknown mode %s\n", argv[1]);
        return 1;
    }
    fprintf(stderr, "context_bytes=%lu morph_bytes=%lu\n",
            (unsigned long)sizeof(OLContext),
            (unsigned long)sizeof(OLMorphWorkspace));
    if (lexP != NULL && fileUser.fp != NULL) {
        fclose(fileUser.fp);
    }
    free(lexP);
    free(morphP);
    return 0;
}
