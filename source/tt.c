#include "mchess8.h"
#include <stdlib.h>
#include <string.h>

#define TT_CLUSTER_SIZE 4

typedef struct {
    uint64_t key_xor;
    uint64_t data;
} TTEntry;

static inline uint64_t tt_pack(int16_t score, uint8_t from, uint8_t to, uint8_t promo_spec, int8_t depth, uint8_t bound, uint8_t gen) {
    return ((uint64_t)(uint16_t)score)
         | ((uint64_t)from << 16)
         | ((uint64_t)to << 24)
         | ((uint64_t)promo_spec << 32)
         | ((uint64_t)(uint8_t)depth << 40)
         | ((uint64_t)bound << 48)
         | ((uint64_t)gen << 56);
}

static inline void tt_unpack(uint64_t d, int16_t *score, uint8_t *from, uint8_t *to, uint8_t *promo_spec, int8_t *depth, uint8_t *bound, uint8_t *gen) {
    *score      = (int16_t)(uint16_t)(d & 0xffffU);
    *from       = (uint8_t)((d >> 16) & 0xffU);
    *to         = (uint8_t)((d >> 24) & 0xffU);
    *promo_spec = (uint8_t)((d >> 32) & 0xffU);
    *depth      = (int8_t)(uint8_t)((d >> 40) & 0xffU);
    *bound      = (uint8_t)((d >> 48) & 0xffU);
    *gen        = (uint8_t)((d >> 56) & 0xffU);
}

typedef struct {
    TTEntry entries[TT_CLUSTER_SIZE];
} TTBucket;

static TTBucket *g_tt = NULL;
static size_t g_tt_mask = 0;
static size_t g_tt_buckets = 0;
static uint8_t g_tt_gen = 0;

void tt_new_search(void) {
    g_tt_gen++;
}

void tt_init(int mb) {
    if (mb < 1) mb = 1;
    if (mb > 4096) mb = 4096;
    uint64_t bytes = (uint64_t)mb * 1024ULL * 1024ULL;
    uint64_t count = 1;
    while (count * 2ULL * sizeof(TTBucket) <= bytes) {
        count *= 2ULL;
    }
    if ((size_t)count == g_tt_buckets && g_tt != NULL) return;
    free(g_tt);
    g_tt = NULL;
    while (count >= 256) {
        g_tt = (TTBucket *)calloc((size_t)count, sizeof(TTBucket));
        if (g_tt) break;
        count /= 2ULL;
    }
    g_tt_buckets = g_tt ? (size_t)count : 0;
    g_tt_mask = g_tt ? ((size_t)count - 1) : 0;
}

void tt_clear(void) {
    if (g_tt && g_tt_buckets > 0) {
        memset(g_tt, 0, g_tt_buckets * sizeof(TTBucket));
    }
}

static int score_to_tt(int score, int ply) {
    if (score > MATE_SCORE - 500) return score + ply;
    if (score < -MATE_SCORE + 500) return score - ply;
    return score;
}

static int score_from_tt(int score, int ply) {
    if (score > MATE_SCORE - 500) return score - ply;
    if (score < -MATE_SCORE + 500) return score + ply;
    return score;
}

bool tt_probe(const Board *b, int depth, int alpha, int beta, int ply, int *out_score, Move *out_move) {
    if (!g_tt) return false;
    uint64_t key = board_tt_key(b);
    TTBucket *bucket = &g_tt[key & g_tt_mask];
    for (int i = 0; i < TT_CLUSTER_SIZE; i++) {
        TTEntry *e = &bucket->entries[i];
        uint64_t d = e->data;
        uint64_t k = e->key_xor ^ d;
        if (k != key || d == 0) continue;

        int16_t e_score;
        uint8_t e_from, e_to, e_spec, e_bound, e_gen;
        int8_t e_depth;
        tt_unpack(d, &e_score, &e_from, &e_to, &e_spec, &e_depth, &e_bound, &e_gen);
        if (e_bound == TT_NONE) continue;

        out_move->from = e_from;
        out_move->to = e_to;
        out_move->piece_spec = e_spec;
        out_move->flags = 0;
        out_move->sort_score = 0;

        if (e_depth >= depth) {
            int s = score_from_tt(e_score, ply);
            if (e_bound == TT_EXACT) {
                *out_score = s;
                return true;
            }
            if (e_bound == TT_LOWER && s >= beta) {
                *out_score = s;
                return true;
            }
            if (e_bound == TT_UPPER && s <= alpha) {
                *out_score = s;
                return true;
            }
        }
        return false;
    }
    return false;
}

void tt_store(const Board *b, int depth, int bound, int score, int ply, Move best_move) {
    if (!g_tt) return;
    uint64_t key = board_tt_key(b);
    TTBucket *bucket = &g_tt[key & g_tt_mask];
    TTEntry *replace = &bucket->entries[0];
    int worst_prio = 1000000;
    for (int i = 0; i < TT_CLUSTER_SIZE; i++) {
        TTEntry *e = &bucket->entries[i];
        uint64_t d = e->data;
        uint64_t k = e->key_xor ^ d;
        if (d == 0) {
            replace = e;
            break;
        }
        int16_t e_score;
        uint8_t e_from, e_to, e_spec, e_bound, e_gen;
        int8_t e_depth;
        tt_unpack(d, &e_score, &e_from, &e_to, &e_spec, &e_depth, &e_bound, &e_gen);
        if (e_bound == TT_NONE) {
            replace = e;
            break;
        }
        if (k == key) {
            if (depth < e_depth && bound != TT_EXACT) return;
            replace = e;
            break;
        }
        int prio = (int)e_depth + (e_bound == TT_EXACT ? 4 : 0) + (e_gen == g_tt_gen ? 8 : 0);
        if (prio < worst_prio) {
            worst_prio = prio;
            replace = e;
        }
    }
    uint64_t new_data = tt_pack((int16_t)score_to_tt(score, ply), best_move.from, best_move.to,
                                best_move.piece_spec, (int8_t)depth, (uint8_t)bound, g_tt_gen);
    if (replace->data == new_data && (replace->key_xor ^ replace->data) == key) return;
    replace->key_xor = key ^ new_data;
    replace->data = new_data;
}
