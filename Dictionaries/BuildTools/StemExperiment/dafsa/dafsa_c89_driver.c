/*
 * OpenSpellGEOS: DAFSA evaluation fixture.
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Scratch fixture for the OpenSpellGEOS DAFSA evaluation.
 * Same algorithm as the proposed C implementation, but C89/Watcom style:
 * declarations at the top of each function, file-static build context,
 * and the three fixes observed against the proposed code:
 *   1. a state's transitions_start is assigned when its first transition
 *      is added, and the global transition array end is tracked;
 *   2. a state's transition block is relocated to the array end before
 *      another transition is appended to that state;
 *   3. the register is rehashed when it grows.
 * Host fixture only: stdio, no GEOS headers. SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long usize;
typedef int (*qcmp)(const void *, const void *);

typedef struct Transition {
    byte label;
    byte is_last;
    u16 target;
} Transition;

typedef struct State {
    u32 transitions_start;
    byte transition_count;
    byte is_final;
} State;

typedef struct Dafsa {
    State *states;
    u32 state_count;
    u32 state_capacity;
    Transition *transitions;
    u32 transition_count;
    u32 transition_capacity;
    u32 *register_slots;
    u32 register_capacity;
    u32 register_count;
} Dafsa;

static Dafsa fixture_dafsa;

typedef struct BuildContext {
    Dafsa *d;
    u32 path[256];
    byte previous_word[256];
    int previous_len;
    int first_free_state;
} BuildContext;

static BuildContext fixture_context;
static byte *fixture_words;
static usize *fixture_off;
static usize fixture_count;
static usize *fixture_idx;
static byte *fixture_probe;
static usize *fixture_probe_off;
static usize fixture_probe_count;

static void *xmalloc(size_t n) {
    void *p = malloc(n);

    if (!p) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n);

    if (!p) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return p;
}

static void dafsa_init(Dafsa *d) {
    d->state_capacity = 1024;
    d->states = xmalloc(d->state_capacity * sizeof(State));
    d->state_count = 1;
    memset(&d->states[0], 0, sizeof(State));
    d->transition_capacity = 4096;
    d->transitions = xmalloc(d->transition_capacity * sizeof(Transition));
    d->transition_count = 0;
    d->register_capacity = 4096;
    d->register_slots = xmalloc(d->register_capacity * sizeof(u32));
    memset(d->register_slots, 0, d->register_capacity * sizeof(u32));
    d->register_count = 0;
}

static int states_equal(const Dafsa *d, u32 a, u32 b) {
    const State *sa = &d->states[a];
    const State *sb = &d->states[b];
    u16 i;

    if (sa->is_final != sb->is_final)
        return 0;

    if (sa->transition_count != sb->transition_count)
        return 0;

    for (i = 0; i < sa->transition_count; i++) {
        const Transition *ta = &d->transitions[sa->transitions_start + i];
        const Transition *tb = &d->transitions[sb->transitions_start + i];

        if (ta->label != tb->label)
            return 0;

        if (ta->target != tb->target)
            return 0;
    }

    return 1;
}

static u32 state_hash(const Dafsa *d, u32 id) {
    const State *s = &d->states[id];
    u32 h = 2166136261u;
    u16 i;

    h ^= s->is_final;
    h *= 16777619u;

    for (i = 0; i < s->transition_count; i++) {
        const Transition *t = &d->transitions[s->transitions_start + i];

        h ^= t->label;
        h *= 16777619u;

        h ^= t->target;
        h *= 16777619u;
    }

    return h;
}

static void register_grow_rehash(Dafsa *d) {
    u32 old_capacity = d->register_capacity;
    u32 *old_slots = d->register_slots;
    u32 i;

    d->register_capacity *= 2;
    d->register_slots = xmalloc(d->register_capacity * sizeof(u32));
    memset(d->register_slots, 0, d->register_capacity * sizeof(u32));

    for (i = 0; i < old_capacity; i++) {
        if (old_slots[i] != 0) {
            u32 id = old_slots[i] - 1;
            u32 mask = d->register_capacity - 1;
            u32 h = state_hash(d, id) & mask;

            while (d->register_slots[h] != 0)
                h = (h + 1) & mask;

            d->register_slots[h] = id + 1;
        }
    }

    free(old_slots);
}

static u32 register_find_or_add(Dafsa *d, u32 state_id) {
    u32 mask = d->register_capacity - 1;
    u32 h = state_hash(d, state_id) & mask;

    while (d->register_slots[h] != 0) {
        u32 candidate = d->register_slots[h] - 1;

        if (states_equal(d, candidate, state_id))
            return candidate;

        h = (h + 1) & mask;
    }

    if ((d->register_count + 1) * 4 >= d->register_capacity) {
        register_grow_rehash(d);
        mask = d->register_capacity - 1;
        h = state_hash(d, state_id) & mask;

        while (d->register_slots[h] != 0) {
            u32 candidate = d->register_slots[h] - 1;

            if (states_equal(d, candidate, state_id))
                return candidate;

            h = (h + 1) & mask;
        }
    }

    d->register_slots[h] = state_id + 1;
    d->register_count++;

    return state_id;
}

static void ensure_states(Dafsa *d, u32 needed) {
    if (needed <= d->state_capacity)
        return;

    while (d->state_capacity < needed)
        d->state_capacity *= 2;

    d->states = xrealloc(d->states, d->state_capacity * sizeof(State));
}

static void ensure_transitions(Dafsa *d, u32 needed) {
    if (needed <= d->transition_capacity)
        return;

    while (d->transition_capacity < needed)
        d->transition_capacity *= 2;

    d->transitions = xrealloc(d->transitions,
                              d->transition_capacity * sizeof(Transition));
}

static u32 new_state(Dafsa *d) {
    u32 id;

    ensure_states(d, d->state_count + 1);
    id = d->state_count;
    memset(&d->states[id], 0, sizeof(State));
    d->state_count++;

    return id;
}

/*
 * Fix 1 and 2: the first transition of a state starts at the global array
 * end; a state whose block is no longer at the array end is relocated
 * there before another transition is appended.
 */
static void add_transition(Dafsa *d, u32 from, byte label, u32 to) {
    State *s = &d->states[from];
    Transition *t;

    if (s->transition_count > 0 &&
        s->transitions_start + s->transition_count != d->transition_count) {
        ensure_transitions(d, d->transition_count + s->transition_count + 1);
        memmove(&d->transitions[d->transition_count],
                &d->transitions[s->transitions_start],
                s->transition_count * sizeof(Transition));
        s->transitions_start = d->transition_count;
    }

    if (s->transition_count == 0)
        s->transitions_start = d->transition_count;

    ensure_transitions(d, s->transitions_start + s->transition_count + 1);
    t = &d->transitions[s->transitions_start + s->transition_count];
    t->label = label;
    t->target = (u16)to;
    t->is_last = 1;

    if (s->transition_count > 0) {
        d->transitions[s->transitions_start + s->transition_count - 1]
            .is_last = 0;
    }

    s->transition_count++;
    d->transition_count = s->transitions_start + s->transition_count;
}

/*
 * Minimizes the completed suffix of the previous word. The replacement
 * scan walks the whole transition array once per merged state, so the
 * complete build is quadratic in the worst case. Acceptable offline on
 * the host; not for a target build phase.
 */
static void minimize_suffix(Dafsa *d, u32 *path, int path_len,
                            int first_free_state) {
    int i;

    for (i = path_len - 1; i >= first_free_state; i--) {
        u32 original = path[i];
        u32 canonical = register_find_or_add(d, original);

        if (canonical != original) {
            u32 total = d->transition_count;
            u32 ti;

            for (ti = 0; ti < total; ti++) {
                if (d->transitions[ti].target == original)
                    d->transitions[ti].target = (u16)canonical;
            }

            d->states[original].transition_count = 0;
            d->states[original].is_final = 0;
        }
    }
}

static int common_prefix_length(const byte *a, int alen, const byte *b,
                                int blen) {
    int n = alen < blen ? alen : blen;
    int i = 0;

    while (i < n && a[i] == b[i])
        i++;

    return i;
}

/*
 * Fix 0b: the insert order check compares only the shared prefix length
 * of the new word and the previous word, never stale bytes beyond the
 * previous word's length.
 */
static int dafsa_add_word(BuildContext *ctx, const byte *word, int len) {
    Dafsa *d = ctx->d;
    int prefix;
    int i;
    int order;
    int clamped;

    if (len >= 256)
        return 0;

    if (ctx->previous_len) {
        clamped = len < ctx->previous_len ? len : ctx->previous_len;
        order = memcmp(word, ctx->previous_word, clamped);

        if (order < 0 || (order == 0 && len <= ctx->previous_len))
            return 0;
    }

    prefix = common_prefix_length(word, len, ctx->previous_word,
                                  ctx->previous_len);

    minimize_suffix(d, ctx->path, ctx->previous_len + 1, prefix + 1);

    for (i = prefix; i < len; i++) {
        u32 fresh = new_state(d);

        add_transition(d, ctx->path[i], word[i], fresh);
        ctx->path[i + 1] = fresh;
    }

    d->states[ctx->path[len]].is_final = 1;
    memcpy(ctx->previous_word, word, len);
    ctx->previous_len = len;

    return 1;
}

static void dafsa_build_begin(Dafsa *d, BuildContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->d = d;
    ctx->path[0] = 0;
}

static void dafsa_build_end(BuildContext *ctx) {
    minimize_suffix(ctx->d, ctx->path, ctx->previous_len + 1, 1);
}

static int dafsa_contains(const Dafsa *d, const byte *word, int len) {
    u32 state = 0;
    int i;

    for (i = 0; i < len; i++) {
        const State *s = &d->states[state];
        byte c = word[i];
        u16 t;
        int found = 0;

        for (t = 0; t < s->transition_count; t++) {
            const Transition *tr = &d->transitions[s->transitions_start + t];

            if (tr->label == c) {
                state = tr->target;
                found = 1;
                break;
            }
        }

        if (!found)
            return 0;
    }

    return d->states[state].is_final;
}

static byte *read_file(const char *path, usize *out_len) {
    FILE *f = fopen(path, "rb");
    byte *buf;
    usize cap = 1ul << 22;
    usize len = 0;
    usize n;

    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }

    buf = xmalloc(cap);

    while ((n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;

        if (len == cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }

    fclose(f);
    *out_len = len;

    return buf;
}

static void split_lines(byte *buf, usize len, byte **out_buf,
                        usize **out_off, usize *out_count) {
    usize cap_w = 1ul << 16;
    usize *off = xmalloc((cap_w + 1) * sizeof(usize));
    usize count = 0;
    usize i;

    off[0] = 0;

    for (i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            usize start = off[count];
            usize end = i;
            usize wlen;

            while (end > start && (buf[end - 1] == '\r' ||
                                   buf[end - 1] == ' '))
                end--;

            wlen = end - start;

            if (wlen > 0) {
                if (count + 1 >= cap_w) {
                    cap_w *= 2;
                    off = xrealloc(off, (cap_w + 1) * sizeof(usize));
                }

                off[count + 1] = off[count] + wlen + 1;
                count++;
                buf[start + wlen] = 0;
            }
        }
    }

    *out_buf = buf;
    *out_off = off;
    *out_count = count;
}

static usize word_cmp(usize a, usize b) {
    usize alen = fixture_off[a + 1] - fixture_off[a] - 1;
    usize blen = fixture_off[b + 1] - fixture_off[b] - 1;
    usize n = alen < blen ? alen : blen;
    int c = memcmp(fixture_words + fixture_off[a],
                   fixture_words + fixture_off[b], n);

    if (c != 0)
        return c < 0 ? -1 : 1;

    if (alen < blen)
        return -1;

    if (alen > blen)
        return 1;

    return 0;
}

static int cmp_idx(const void *pa, const void *pb) {
    usize a = *(const usize *)pa;
    usize b = *(const usize *)pb;

    return word_cmp(a, b);
}

static void build(void) {
    Dafsa *d = &fixture_dafsa;
    BuildContext *ctx = &fixture_context;
    u32 added = 0;
    u32 live = 0;
    u32 i;

    dafsa_init(d);
    dafsa_build_begin(d, ctx);

    for (i = 0; i < fixture_count; i++) {
        u32 w = fixture_idx[i];
        u32 len = fixture_off[w + 1] - fixture_off[w] - 1;

        if (added > 0) {
            u32 pw = fixture_idx[added - 1];
            u32 plen = fixture_off[pw + 1] - fixture_off[pw] - 1;

            if (len == plen && !memcmp(fixture_words + fixture_off[w],
                                       fixture_words + fixture_off[pw], len))
                continue;
        }

        if (!dafsa_add_word(ctx, fixture_words + fixture_off[w], (int)len)) {
            fprintf(stderr, "add_word rejected at %lu\n",
                    (unsigned long)i);
            exit(1);
        }

        added++;
    }

    dafsa_build_end(ctx);

    for (i = 0; i < d->state_count; i++) {
        if (d->states[i].is_final || d->states[i].transition_count > 0)
            live++;
    }

    printf("lines=%lu unique=%lu states_raw=%lu states_live=%lu "
           "transitions=%lu\n",
           (unsigned long)fixture_count, (unsigned long)added,
           (unsigned long)d->state_count, (unsigned long)live,
           (unsigned long)d->transition_count);

    {
        u32 yes = 0;
        u32 no = 0;

        for (i = 0; i < fixture_probe_count; i++) {
            u32 len = fixture_probe_off[i + 1] - fixture_probe_off[i] - 1;
            int r = dafsa_contains(d, fixture_probe + fixture_probe_off[i],
                                   (int)len);

            if (r)
                yes++;
            else
                no++;

            printf("%s=%d\n", fixture_probe + fixture_probe_off[i], r);
        }

        printf("probes=%lu yes=%lu no=%lu\n",
               (unsigned long)fixture_probe_count,
               (unsigned long)yes, (unsigned long)no);
    }
}

int main(int argc, char **argv) {
    usize total_len;
    byte *buf;

    if (argc != 3) {
        fprintf(stderr, "usage: %s sorted-words probes\n", argv[0]);
        return 2;
    }

    buf = read_file(argv[1], &total_len);
    split_lines(buf, total_len, &fixture_words, &fixture_off,
                &fixture_count);

    fixture_idx = xmalloc(fixture_count * sizeof(usize));

    for (total_len = 0; total_len < fixture_count; total_len++)
        fixture_idx[total_len] = total_len;

    qsort(fixture_idx, fixture_count, sizeof(usize), (qcmp)cmp_idx);

    buf = read_file(argv[2], &total_len);
    split_lines(buf, total_len, &fixture_probe, &fixture_probe_off,
                &fixture_probe_count);

    build();

    return 0;
}
