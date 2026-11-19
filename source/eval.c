#include "mchess8.h"
#include "bitboard.h"
#include "tables.h"
#include <stdlib.h>

#define PAWN_TT_SIZE 65536
#define PAWN_TT_MASK (PAWN_TT_SIZE - 1)
#define DARK_SQ_BB 0xAA55AA55AA55AA55ULL
#define CRAMP_W_BB 0x00003C0000000000ULL
#define CRAMP_B_BB 0x00000000003C0000ULL

typedef struct {
    uint64_t key_xor;
    uint64_t data; /* bits 0..15: int16_t pstr_score; bits 16..63: (passed_w | passed_b) >> 8 */
} PawnTTEntry;

static PawnTTEntry g_pawn_tt[PAWN_TT_SIZE];

static inline int abs_i(int x) { return x < 0 ? -x : x; }

static const int knight_psqt[8][8] = {
    {-50,-40,-30,-30,-30,-30,-40,-50},
    {-40,-20,  0,  5,  5,  0,-20,-40},
    {-30,  5, 10, 15, 15, 10,  5,-30},
    {-30,  0, 15, 20, 20, 15,  0,-30},
    {-30,  5, 15, 20, 20, 15,  5,-30},
    {-30,  0, 10, 15, 15, 10,  0,-30},
    {-40,-20,  0,  0,  0,  0,-20,-40},
    {-50,-40,-30,-30,-30,-30,-40,-50}
};

static const int bishop_psqt[8][8] = {
    {-20,-10,-10,-10,-10,-10,-10,-20},
    {-10,  5,  0,  0,  0,  0,  5,-10},
    {-10, 10, 10, 10, 10, 10, 10,-10},
    {-10,  0, 10, 10, 10, 10,  0,-10},
    {-10,  5,  5, 10, 10,  5,  5,-10},
    {-10,  0,  5, 10, 10,  5,  0,-10},
    {-10,  0,  0,  0,  0,  0,  0,-10},
    {-20,-10,-10,-10,-10,-10,-10,-20}
};

static const int king_mg_psqt[8][8] = {
    { 20, 30, 10,  0,  0, 10, 30, 20},
    { 20, 20,  0,  0,  0,  0, 20, 20},
    {-10,-20,-20,-20,-20,-20,-20,-10},
    {-20,-30,-30,-40,-40,-30,-30,-20},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30},
    {-30,-40,-40,-50,-50,-40,-40,-30}
};

static const int king_eg_psqt[8][8] = {
    {-50,-30,-30,-30,-30,-30,-30,-50},
    {-30,-30,  0,  0,  0,  0,-30,-30},
    {-30,-10, 20, 30, 30, 20,-10,-30},
    {-30,-10, 30, 40, 40, 30,-10,-30},
    {-30,-10, 30, 40, 40, 30,-10,-30},
    {-30,-10, 20, 30, 30, 20,-10,-30},
    {-30,-20,-10,  0,  0,-10,-20,-30},
    {-50,-40,-30,-20,-20,-30,-40,-50}
};

static const int passed_bonus[8] = { 0, 15, 25, 45, 75, 120, 180, 0 };

int evaluate(const Board *b, const EngineSettings *cfg) {
    int score = b->mat_balance;

    if (b->piece_counts[WB] >= 2) score += 80;
    if (b->piece_counts[BB] >= 2) score -= 80;

    int total_non_pawn = b->w_mat + b->b_mat;
    int phase = (total_non_pawn * 256) / 6200;
    if (phase > 256) phase = 256;
    int eg_phase = 256 - phase;

    int cent_w = 8 + cfg->centrality * 2;
    int mob_w  = cfg->mobility;
    int pstr_w = cfg->pawn_structure;
    int pass_w = 6 + cfg->passed_pawns * 2;
    int king_w = 6 + cfg->king_placement;
    int agg_w  = 5 + cfg->aggression;

    int pos_score = b->psqt_score;
    int mob_score = 0;
    int pstr_score = 0;
    int pass_score = 0;
    int ks_score = 0;

    uint64_t wp = b->bb[WP];
    uint64_t bp = b->bb[BP];
    uint64_t w_occ = b->occ[WHITE];
    uint64_t b_occ = b->occ[BLACK];
    uint64_t all_occ = b->occ[2];
    int wk = b->wk_sq;
    int bk = b->bk_sq;
    uint64_t w_ring = BB_KING_ATK[wk] | (1ULL << wk);
    uint64_t b_ring = BB_KING_ATK[bk] | (1ULL << bk);
    int w_atk_cnt = 0, w_atk_wt = 0, w_ring_hits = 0;
    int b_atk_cnt = 0, b_atk_wt = 0, b_ring_hits = 0;

    /* Pawn structure & passed-pawn detection via lockless Pawn Hash Table */
    uint64_t passed_w = 0, passed_b = 0;
    uint64_t bb;
    if (b->total_pawns > 0) {
        uint64_t pkey = (uint64_t)b->pawn_zob[0] | ((uint64_t)b->pawn_zob[1] << 16) | ((uint64_t)b->pawn_zob[2] << 32);
        PawnTTEntry *pe = &g_pawn_tt[pkey & PAWN_TT_MASK];
        uint64_t d = pe->data;
        if ((pe->key_xor ^ d) == pkey && pkey != 0) {
            pstr_score = (int16_t)(uint16_t)(d & 0xffffU);
            uint64_t passed_all = ((d >> 16) << 8);
            passed_w = passed_all & wp;
            passed_b = passed_all & bp;
        } else {
            bb = wp;
            while (bb) {
                int sq = pop_lsb(&bb);
                int f = sq & 7;
                if (popcnt64(wp & BB_FILE_MASK[f]) > 1) pstr_score -= 16;
                if ((wp & BB_ADJ_FILES[f]) == 0) {
                    pstr_score -= 24;
                    if ((bp & BB_FILE_MASK[f]) == 0) pstr_score -= 10;
                }
                if ((bp & BB_PASSED_MASK[0][sq]) == 0) passed_w |= (1ULL << sq);
            }
            bb = bp;
            while (bb) {
                int sq = pop_lsb(&bb);
                int f = sq & 7;
                if (popcnt64(bp & BB_FILE_MASK[f]) > 1) pstr_score += 16;
                if ((bp & BB_ADJ_FILES[f]) == 0) {
                    pstr_score += 24;
                    if ((wp & BB_FILE_MASK[f]) == 0) pstr_score += 10;
                }
                if ((wp & BB_PASSED_MASK[1][sq]) == 0) passed_b |= (1ULL << sq);
            }
            if (pkey != 0) {
                uint64_t new_d = ((uint64_t)(uint16_t)(int16_t)pstr_score) | ((((passed_w | passed_b) >> 8) & 0xFFFFFFFFFFFFULL) << 16);
                pe->key_xor = pkey ^ new_d;
                pe->data = new_d;
            }
        }
        static const int conn_bonus[8] = { 0, 0, 12, 22, 36, 56, 84, 0 };
        bb = passed_w;
        while (bb) {
            int sq = pop_lsb(&bb);
            int r = sq >> 3, f = sq & 7;
            int pb = passed_bonus[r];
            if (b->squares[sq + 8] != EMPTY) pb -= pb / 4;
            else if (eg_phase > 0) pb += (r * 4 * eg_phase) / 256;
            uint64_t supp = BB_ADJ_FILES[f] & (BB_RANK_MASK[r] | (r > 0 ? BB_RANK_MASK[r - 1] : 0));
            if (wp & supp) pb += conn_bonus[r];
            if (eg_phase > 0) {
                int kdiff = (7 - BB_CHEBY_DIST[sq + 8][wk]) * 6 + BB_CHEBY_DIST[sq + 8][bk] * 4;
                pb += (kdiff * eg_phase) / 256;
            }
            uint64_t behind = BB_FILE_MASK[f] & ((1ULL << sq) - 1);
            if (behind & b->bb[WR]) pb += 20;
            if (behind & b->bb[BR]) pb -= 20;
            pass_score += pb;
        }
        bb = passed_b;
        while (bb) {
            int sq = pop_lsb(&bb);
            int r = sq >> 3, f = sq & 7;
            int rr = 7 - r;
            int pb = passed_bonus[rr];
            if (b->squares[sq - 8] != EMPTY) pb -= pb / 4;
            else if (eg_phase > 0) pb += (rr * 4 * eg_phase) / 256;
            uint64_t supp = BB_ADJ_FILES[f] & (BB_RANK_MASK[r] | (r < 7 ? BB_RANK_MASK[r + 1] : 0));
            if (bp & supp) pb += conn_bonus[rr];
            if (eg_phase > 0) {
                int kdiff = (7 - BB_CHEBY_DIST[sq - 8][bk]) * 6 + BB_CHEBY_DIST[sq - 8][wk] * 4;
                pb += (kdiff * eg_phase) / 256;
            }
            uint64_t behind = BB_FILE_MASK[f] & (~((1ULL << (sq + 1)) - 1));
            if (behind & b->bb[BR]) pb += 20;
            if (behind & b->bb[WR]) pb -= 20;
            pass_score -= pb;
        }
    }

    /* White Knights */
    bb = b->bb[WN];
    while (bb) {
        int sq = pop_lsb(&bb);
        int r = sq >> 3, f = sq & 7;
        pos_score += knight_psqt[r][f];
        uint64_t atk = BB_KNIGHT_ATK[sq];
        int m = popcnt64(atk & ~w_occ);
        mob_score += (m - 4) * 5;
        if (BB_CHEBY_DIST[sq][bk] <= 2) ks_score += 12;
        if (atk & b_ring) { w_atk_cnt++; w_atk_wt += 2; w_ring_hits += popcnt64(atk & b_ring & ~b_occ); }
    }

    /* Black Knights */
    bb = b->bb[BN];
    while (bb) {
        int sq = pop_lsb(&bb);
        int r = sq >> 3, f = sq & 7;
        pos_score -= knight_psqt[7 - r][f];
        uint64_t atk = BB_KNIGHT_ATK[sq];
        int m = popcnt64(atk & ~b_occ);
        mob_score -= (m - 4) * 5;
        if (BB_CHEBY_DIST[sq][wk] <= 2) ks_score -= 12;
        if (atk & w_ring) { b_atk_cnt++; b_atk_wt += 2; b_ring_hits += popcnt64(atk & w_ring & ~w_occ); }
    }

    /* White Bishops */
    bb = b->bb[WB];
    while (bb) {
        int sq = pop_lsb(&bb);
        int r = sq >> 3, f = sq & 7;
        pos_score += bishop_psqt[r][f];
        uint64_t atk = bb_bishop_attacks(sq, all_occ);
        int m = popcnt64(atk & ~w_occ);
        mob_score += (m - 6) * 5;
        if (m <= 2) mob_score -= 16;
        uint64_t same_col = ((1ULL << sq) & DARK_SQ_BB) ? DARK_SQ_BB : ~DARK_SQ_BB;
        pstr_score -= popcnt64(wp & same_col) * 4;
        if (BB_CHEBY_DIST[sq][bk] <= 3) ks_score += 10;
        if (atk & b_ring) { w_atk_cnt++; w_atk_wt += 2; w_ring_hits += popcnt64(atk & b_ring & ~b_occ); }
    }

    /* Black Bishops */
    bb = b->bb[BB];
    while (bb) {
        int sq = pop_lsb(&bb);
        int r = sq >> 3, f = sq & 7;
        pos_score -= bishop_psqt[7 - r][f];
        uint64_t atk = bb_bishop_attacks(sq, all_occ);
        int m = popcnt64(atk & ~b_occ);
        mob_score -= (m - 6) * 5;
        if (m <= 2) mob_score += 16;
        uint64_t same_col = ((1ULL << sq) & DARK_SQ_BB) ? DARK_SQ_BB : ~DARK_SQ_BB;
        pstr_score += popcnt64(bp & same_col) * 4;
        if (BB_CHEBY_DIST[sq][wk] <= 3) ks_score -= 10;
        if (atk & w_ring) { b_atk_cnt++; b_atk_wt += 2; b_ring_hits += popcnt64(atk & w_ring & ~w_occ); }
    }

    /* White Rooks */
    bb = b->bb[WR];
    while (bb) {
        int sq = pop_lsb(&bb);
        int r = sq >> 3, f = sq & 7;
        if ((wp & BB_FILE_MASK[f]) == 0) {
            pos_score += ((bp & BB_FILE_MASK[f]) == 0) ? 24 : 12;
        }
        if (r == 6) pos_score += 36;
        uint64_t atk = bb_rook_attacks(sq, all_occ);
        int m = popcnt64(atk & ~w_occ);
        mob_score += (m - 7) * 3;
        if (m <= 2) mob_score -= 20;
        if (atk & b_ring) { w_atk_cnt++; w_atk_wt += 3; w_ring_hits += popcnt64(atk & b_ring & ~b_occ); }
    }

    /* Black Rooks */
    bb = b->bb[BR];
    while (bb) {
        int sq = pop_lsb(&bb);
        int r = sq >> 3, f = sq & 7;
        if ((bp & BB_FILE_MASK[f]) == 0) {
            pos_score -= ((wp & BB_FILE_MASK[f]) == 0) ? 24 : 12;
        }
        if (r == 1) pos_score -= 36;
        uint64_t atk = bb_rook_attacks(sq, all_occ);
        int m = popcnt64(atk & ~b_occ);
        mob_score -= (m - 7) * 3;
        if (m <= 2) mob_score += 20;
        if (atk & w_ring) { b_atk_cnt++; b_atk_wt += 3; b_ring_hits += popcnt64(atk & w_ring & ~w_occ); }
    }

    /* White Queens */
    bb = b->bb[WQ];
    while (bb) {
        int sq = pop_lsb(&bb);
        uint64_t atk = bb_queen_attacks(sq, all_occ);
        int m = popcnt64(atk & ~w_occ);
        mob_score += (m - 12) * 2;
        if (BB_CHEBY_DIST[sq][bk] <= 3) ks_score += 20;
        if (atk & b_ring) { w_atk_cnt++; w_atk_wt += 5; w_ring_hits += popcnt64(atk & b_ring & ~b_occ); }
    }

    /* Black Queens */
    bb = b->bb[BQ];
    while (bb) {
        int sq = pop_lsb(&bb);
        uint64_t atk = bb_queen_attacks(sq, all_occ);
        int m = popcnt64(atk & ~b_occ);
        mob_score -= (m - 12) * 2;
        if (BB_CHEBY_DIST[sq][wk] <= 3) ks_score -= 20;
        if (atk & w_ring) { b_atk_cnt++; b_atk_wt += 5; b_ring_hits += popcnt64(atk & w_ring & ~w_occ); }
    }

    /* White King */
    {
        int r = wk >> 3, f = wk & 7;
        int kpsqt = (king_mg_psqt[r][f] * phase + king_eg_psqt[r][f] * eg_phase) / 256;
        ks_score += (kpsqt * king_w) / 10;
        if (phase > 0 && b->b_mat >= 1100 && r <= 1) {
            int shield = 0;
            for (int df = -1; df <= 1; df++) {
                int ff = f + df;
                if (ff >= 0 && ff < 8) {
                    if (b->squares[wk + 8 + df] == WP) shield += 14;
                    else if (b->squares[wk + 16 + df] == WP) shield += 6;
                    else shield -= 12;
                }
            }
            ks_score += (((shield * phase) / 256) * king_w) / 10;
        }
    }

    /* Black King */
    {
        int r = bk >> 3, f = bk & 7;
        int kpsqt = (king_mg_psqt[7 - r][f] * phase + king_eg_psqt[7 - r][f] * eg_phase) / 256;
        ks_score -= (kpsqt * king_w) / 10;
        if (phase > 0 && b->w_mat >= 1100 && r >= 6) {
            int shield = 0;
            for (int df = -1; df <= 1; df++) {
                int ff = f + df;
                if (ff >= 0 && ff < 8) {
                    if (b->squares[bk - 8 + df] == BP) shield += 14;
                    else if (b->squares[bk - 16 + df] == BP) shield += 6;
                    else shield -= 12;
                }
            }
            ks_score -= (((shield * phase) / 256) * king_w) / 10;
        }
    }

    if (phase > 0) {
        if (b->piece_counts[WQ] > 0 && (w_atk_cnt >= 2 || w_ring_hits >= 3)) {
            int danger = (w_atk_wt * w_atk_wt) + w_ring_hits * 8;
            if (danger > 280) danger = 280;
            ks_score += (danger * phase) / 256;
        }
        if (b->piece_counts[BQ] > 0 && (b_atk_cnt >= 2 || b_ring_hits >= 3)) {
            int danger = (b_atk_wt * b_atk_wt) + b_ring_hits * 8;
            if (danger > 280) danger = 280;
            ks_score -= (danger * phase) / 256;
        }
    }

    pos_score += popcnt64(wp & CRAMP_W_BB) * 20;
    pos_score -= popcnt64(bp & CRAMP_B_BB) * 20;

    score += (pos_score * cent_w) / 10;
    score += (mob_score * mob_w) / 4;
    score += (pstr_score * pstr_w) / 5;
    score += (pass_score * pass_w) / 10;
    score += (ks_score * agg_w) / 10;

    if (b->total_pawns == 0) {
        if (b->w_mat <= 700 && b->b_mat <= 700 && abs_i(score) < 800) {
            if (!(b->piece_counts[WB] == 2 || b->piece_counts[BB] == 2)) {
                return 0;
            }
        }
        if (score > 400 && b->b_mat == 0) {
            int br = bk >> 3, bf = bk & 7;
            int center_dist = abs_i(br * 2 - 7) + abs_i(bf * 2 - 7);
            score += center_dist * 12 + (14 - BB_MANHATTAN_DIST[wk][bk]) * 10;
        } else if (score < -400 && b->w_mat == 0) {
            int wr = wk >> 3, wf = wk & 7;
            int center_dist = abs_i(wr * 2 - 7) + abs_i(wf * 2 - 7);
            score -= center_dist * 12 + (14 - BB_MANHATTAN_DIST[wk][bk]) * 10;
        }
    }

    return (b->side == WHITE) ? score : -score;
}
