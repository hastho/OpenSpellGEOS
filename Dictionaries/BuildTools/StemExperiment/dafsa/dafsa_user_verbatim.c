/*
 * OpenSpellGEOS: DAFSA evaluation fixture.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The delivered C implementation as supplied, byte-for-byte.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRANSITIONS 256

typedef unsigned char byte;
typedef unsigned short u16;
typedef unsigned long u32;

/* One outgoing transition.
   DOS-friendly: keep this structure small. */
typedef struct Transition {
    byte label;              /* character, 0..255 */
    byte is_last;            /* last outgoing transition of source state */
    u16 target;              /* destination state index */
} Transition;

/* A DAFSA state.
   transitions_start is an index into a global transition array. */
typedef struct State {
    u16 transitions_start;   /* first transition index */
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

    /* Register of minimized states */
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

    /* State 0 is the root. */
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

/* Returns the canonical state ID for `state_id`. */
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
}

/* Minimize states from `first_free_state` through the previous word's
   final state. */
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
            /* Replace every reference to `original` with `canonical`. */
            for (u32 s = 0; s < d->state_count; s++) {
                State *st = &d->states[s];

                for (u16 t = 0; t < st->transition_count; t++) {
                    Transition *tr =
                        &d->transitions[st->transitions_start + t];

                    if (tr->target == original)
                        tr->target = (u16)canonical;
                }
            }

            /* Mark original state unused. */
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

    /* Words must be inserted in ascending byte order. */
    if (ctx->previous_len &&
        memcmp(word, ctx->previous_word, len) <= 0) {
        return 0;
    }

    int prefix = common_prefix_length(
        word, len,
        ctx->previous_word, ctx->previous_len
    );

    /* Minimize the completed part of the previous word. */
    minimize_suffix(
        d,
        ctx->path,
        ctx->previous_len + 1,
        prefix + 1
    );

    /* Reuse the common prefix path. */
    for (int i = prefix; i < len; i++) {
        u32 new_state = new_state(d);

        add_transition(
            d,
            ctx->path[i],
            word[i],
            new_state
        );

        ctx->path[i + 1] = new_state;
    }

    d->states[ctx->path[len]].is_final = 1;

    memcpy(ctx->previous_word, word, len);
    ctx->previous_len = len;

    return 1;
}

void dafsa_build_begin(Dafsa *d, BuildContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->d = d;
    ctx->path[0] = 0; /* root */
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

int main(void) {
    Dafsa dafsa;
    BuildContext ctx;

    const byte *words[] = {
        (const byte *)"arbeit",
        (const byte *)"arbeiten",
        (const byte *)"arbeiter",
        (const byte *)"haus",
        (const byte *)"hauser",
        (const byte *)"hausfrau"
    };

    dafsa_init(&dafsa);
    dafsa_build_begin(&dafsa, &ctx);

    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (!dafsa_add_word(
                &ctx,
                words[i],
                (int)strlen((const char *)words[i])
            )) {
            fprintf(stderr, "Words must be sorted\n");
            return 1;
        }
    }

    dafsa_build_end(&ctx);

    printf("haus: %d\n",
        dafsa_contains(&dafsa, (const byte *)"haus", 4));

    printf("hausfrau: %d\n",
        dafsa_contains(&dafsa, (const byte *)"hausfrau", 8));

    printf("hauser: %d\n",
        dafsa_contains(&dafsa, (const byte *)"hauser", 6));

    printf("hund: %d\n",
        dafsa_contains(&dafsa, (const byte *)"hund", 4));

    return 0;
}
