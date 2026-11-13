/*
 * OpenSpellGEOS: DAFSA evaluation fixture.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRANSITIONS 256

typedef unsigned char byte;
typedef unsigned short u16;
typedef unsigned long u32;

typedef struct Transition {
    byte label;
    byte is_last;
    u16 target;
} Transition;

typedef struct State {
    u16 transitions_start;
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

void dafsa_init(Dafsa *d) {
    memset(d, 0, sizeof(*d));

    d->state_capacity = 1024;
    d->states = xmalloc(d->state_capacity * sizeof(State));

    d->transition_capacity = 4096;
    d->transitions = xmalloc(d->transition_capacity * sizeof(Transition));

    d->register_capacity = 4096;
    d->register_slots = xmalloc(d->register_capacity * sizeof(u32));

    d->state_count = 1;
    memset(&d->states[0], 0, sizeof(State));
}

static int states_equal(const Dafsa *d, u32 a, u32 b) {
    const State *sa = &d->states[a];
    const State *sb = &d->states[b];

    if (sa->is_final != sb->is_final)
        return 0;

    if (sa->transition_count != sb->transition_count)
        return 0;

    for (u16 i = 0; i < sa->transition_count; i++) {
        const Transition *ta =
            &d->transitions[sa->transitions_start + i];
        const Transition *tb =
            &d->transitions[sb->transitions_start + i];

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

    h ^= s->is_final;
    h *= 16777619u;

    for (u16 i = 0; i < s->transition_count; i++) {
        const Transition *t =
            &d->transitions[s->transitions_start + i];

        h ^= t->label;
        h *= 16777619u;

        h ^= t->target;
        h *= 16777619u;
    }

    return h;
}

static void register_grow(Dafsa *d) {
    d->register_capacity *= 2;
    d->register_slots = xrealloc(
        d->register_slots,
        d->register_capacity * sizeof(u32)
    );
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

    if ((d->register_count + 1) * 4 >= d->register_capacity)
        register_grow(d);

    d->register_slots[h] = state_id + 1;
    d->register_count++;

    return state_id;
}

static void ensure_states(Dafsa *d, u32 needed) {
    if (needed <= d->state_capacity)
        return;

    while (d->state_capacity < needed)
        d->state_capacity *= 2;

    d->states = xrealloc(
        d->states,
        d->state_capacity * sizeof(State)
    );
}

static void ensure_transitions(Dafsa *d, u32 needed) {
    if (needed <= d->transition_capacity)
        return;

    while (d->transition_capacity < needed)
        d->transition_capacity *= 2;

    d->transitions = xrealloc(
        d->transitions,
        d->transition_capacity * sizeof(Transition)
    );
}

static u32 new_state(Dafsa *d) {
    ensure_states(d, d->state_count + 1);

    State *s = &d->states[d->state_count];
    memset(s, 0, sizeof(*s));

    return d->state_count++;
}

static void add_transition(Dafsa *d, u32 from, byte label, u32 to) {
    State *s = &d->states[from];

    if (s->transition_count > 0 &&
        (u32)(s->transitions_start + s->transition_count) != d->transition_count) {
        ensure_transitions(
            d,
            d->transition_count + s->transition_count + 1
        );
        memmove(&d->transitions[d->transition_count],
                &d->transitions[s->transitions_start],
                s->transition_count * sizeof(Transition));
        s->transitions_start = (u32)d->transition_count;
    }

    if (s->transition_count == 0)
        s->transitions_start = (u32)d->transition_count;

    ensure_transitions(
        d,
        s->transitions_start + s->transition_count + 1
    );

    Transition *t =
        &d->transitions[s->transitions_start + s->transition_count];

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

static void minimize_suffix(
    Dafsa *d,
    u32 *path,
    int path_len,
    int first_free_state
) {
    for (int i = path_len - 1; i >= first_free_state; i--) {
        u32 original = path[i];
        u32 canonical = register_find_or_add(d, original);

        if (canonical != original) {
            for (u32 s = 0; s < d->state_count; s++) {
                State *st = &d->states[s];

                for (u16 t = 0; t < st->transition_count; t++) {
                    Transition *tr =
                        &d->transitions[st->transitions_start + t];

                    if (tr->target == original)
                        tr->target = (u16)canonical;
                }
            }

            d->states[original].transition_count = 0;
            d->states[original].is_final = 0;
        }
    }
}

typedef struct BuildContext {
    Dafsa *d;
    u32 path[256];
    byte previous_word[256];
    int previous_len;
    int first_free_state;
} BuildContext;

static int common_prefix_length(
    const byte *a,
    int alen,
    const byte *b,
    int blen
) {
    int n = alen < blen ? alen : blen;
    int i = 0;

    while (i < n && a[i] == b[i])
        i++;

    return i;
}

int dafsa_add_word(BuildContext *ctx, const byte *word, int len) {
    Dafsa *d = ctx->d;

    if (len >= 256)
        return 0;

    if (ctx->previous_len) {
        int clamped = len < ctx->previous_len ? len : ctx->previous_len;
        int order = memcmp(word, ctx->previous_word, clamped);

        if (order < 0 || (order == 0 && len <= ctx->previous_len))
            return 0;
    }

    int prefix = common_prefix_length(
        word, len,
        ctx->previous_word, ctx->previous_len
    );

    minimize_suffix(
        d,
        ctx->path,
        ctx->previous_len + 1,
        prefix + 1
    );

    for (int i = prefix; i < len; i++) {
        u32 fresh = new_state(d);

        add_transition(
            d,
            ctx->path[i],
            word[i],
            fresh
        );

        ctx->path[i + 1] = fresh;
    }

    d->states[ctx->path[len]].is_final = 1;

    memcpy(ctx->previous_word, word, len);
    ctx->previous_len = len;

    return 1;
}

void dafsa_build_begin(Dafsa *d, BuildContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->d = d;
    ctx->path[0] = 0;
}

void dafsa_build_end(BuildContext *ctx) {
    minimize_suffix(
        ctx->d,
        ctx->path,
        ctx->previous_len + 1,
        1
    );
}

int dafsa_contains(const Dafsa *d, const byte *word, int len) {
    u32 state = 0;

    for (int i = 0; i < len; i++) {
        const State *s = &d->states[state];
        byte c = word[i];
        int found = 0;

        for (u16 t = 0; t < s->transition_count; t++) {
            const Transition *tr =
                &d->transitions[s->transitions_start + t];

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

typedef unsigned int usize;
typedef int (*qcmp)(const void *, const void *);

static byte *read_file(const char *path, usize *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    usize cap = 1 << 22, len = 0;
    byte *buf = xmalloc(cap);
    usize n;
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

static byte *g_words;
static usize *g_off;
static usize g_count;

static int word_cmp(usize a, usize b) {
    usize alen = g_off[a + 1] - g_off[a];
    usize blen = g_off[b + 1] - g_off[b];
    usize n = alen < blen ? alen : blen;
    int c = memcmp(g_words + g_off[a], g_words + g_off[b], n);
    if (c != 0)
        return c;
    if (alen < blen)
        return -1;
    if (alen > blen)
        return 1;
    return 0;
}

static usize *g_idx;

static int cmp_idx(const void *pa, const void *pb) {
    usize a = *(const usize *)pa;
    usize b = *(const usize *)pb;
    return word_cmp(a, b);
}

static void split_lines(byte *buf, usize len, byte **out_buf, usize **out_off, usize *out_count) {
    usize cap_w = 1 << 16;
    usize *off = xmalloc((cap_w + 1) * sizeof(usize));
    usize count = 0;
    off[0] = 0;
    for (usize i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            usize start = off[count];
            usize end = i;
            while (end > start && (buf[end - 1] == '\r' || buf[end - 1] == ' '))
                end--;
            usize wlen = end - start;
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

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s sorted-words probes\n", argv[0]);
        return 2;
    }

    usize wlen_total;
    byte *wbuf = read_file(argv[1], &wlen_total);
    split_lines(wbuf, wlen_total, &g_words, &g_off, &g_count);

    g_idx = xmalloc(g_count * sizeof(usize));
    for (usize i = 0; i < g_count; i++)
        g_idx[i] = i;
    qsort(g_idx, g_count, sizeof(usize), (qcmp)cmp_idx);

    Dafsa dafsa;
    BuildContext ctx;
    dafsa_init(&dafsa);
    dafsa_build_begin(&dafsa, &ctx);

    usize added = 0;
    for (usize i = 0; i < g_count; i++) {
        usize w = g_idx[i];
        usize len = g_off[w + 1] - g_off[w] - 1;
        if (added > 0) {
            usize pw = g_idx[added - 1];
            usize plen = g_off[pw + 1] - g_off[pw] - 1;
            if (len == plen && !memcmp(g_words + g_off[w], g_words + g_off[pw], len))
                continue;
        }
        if (!dafsa_add_word(&ctx, g_words + g_off[w], (int)len)) {
            fprintf(stderr, "add_word rejected at %lu\n", (unsigned long)i);
            return 1;
        }
        added++;
    }
    dafsa_build_end(&ctx);

    {
        u32 live = 0;
        u32 si;

        for (si = 0; si < dafsa.state_count; si++) {
            if (dafsa.states[si].is_final || dafsa.states[si].transition_count > 0)
                live++;
        }

        printf("lines=%lu unique=%lu states_raw=%lu states_live=%lu transitions=%lu\n",
        (unsigned long)g_count, (unsigned long)added,
        (unsigned long)dafsa.state_count,
        (unsigned long)live,
        (unsigned long)dafsa.transition_count);
    }

    usize plen_total;
    byte *pbuf = read_file(argv[2], &plen_total);
    byte *pprobe;
    usize *poff;
    usize pcount;
    split_lines(pbuf, plen_total, &pprobe, &poff, &pcount);

    usize yes = 0, no = 0;
    for (usize i = 0; i < pcount; i++) {
        usize len = poff[i + 1] - poff[i] - 1;
        int r = dafsa_contains(&dafsa, pprobe + poff[i], (int)len);
        if (r)
            yes++;
        else
            no++;
        printf("%s=%d\n", pprobe + poff[i], r);
    }
    printf("probes=%lu yes=%lu no=%lu\n",
        (unsigned long)pcount, (unsigned long)yes, (unsigned long)no);

    return 0;
}
