#include "mchess8.h"
#include "bitboard.h"
#include "tables.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static int piece_from_char(char c) {
    switch (c) {
        case 'P': return WP; case 'N': return WN; case 'B': return WB;
        case 'R': return WR; case 'Q': return WQ; case 'K': return WK;
        case 'p': return BP; case 'n': return BN; case 'b': return BB;
        case 'r': return BR; case 'q': return BQ; case 'k': return BK;
        default: return EMPTY;
    }
}

static inline int piece_side(int p) {
    return (p >= BP) ? BLACK : WHITE;
}

static inline void bb_put(Board *b, int sq, uint8_t p) {
    uint64_t bit = 1ULL << sq;
    b->squares[sq] = p;
    b->bb[p] |= bit;
    b->occ[piece_side(p)] |= bit;
    b->occ[2] |= bit;
}

static inline void bb_del(Board *b, int sq, uint8_t p) {
    uint64_t bit = ~(1ULL << sq);
    b->squares[sq] = EMPTY;
    b->bb[p] &= bit;
    b->occ[piece_side(p)] &= bit;
    b->occ[2] &= bit;
}

static void zob_xor_piece(Board *b, int sq, int p) {
    if (p == EMPTY) return;
    uint16_t off = (uint16_t)(sq_to_zob_off[sq64_to_120(sq)] + p * 6);
    uint16_t k0 = (uint16_t)zobrist_table[off] | ((uint16_t)zobrist_table[off + 1] << 8);
    uint16_t k1 = (uint16_t)zobrist_table[off + 2] | ((uint16_t)zobrist_table[off + 3] << 8);
    uint16_t k2 = (uint16_t)zobrist_table[off + 4] | ((uint16_t)zobrist_table[off + 5] << 8);
    b->zob[0] ^= k0;
    b->zob[1] ^= k1;
    b->zob[2] ^= k2;
    if (p == WP || p == BP) {
        b->pawn_zob[0] ^= k0;
        b->pawn_zob[1] ^= k1;
        b->pawn_zob[2] ^= k2;
    }
}

static bool ep_hash_active(const Board *b) {
    if (b->ep_sq < 0) return false;
    uint8_t ep_taker = (b->side == WHITE) ? WP : BP;
    return (BB_PAWN_ATK[b->side ^ 1][b->ep_sq] & b->bb[ep_taker]) != 0;
}

static void zob_xor_ep(Board *b) {
    if (ep_hash_active(b)) {
        int f = b->ep_sq & 7;
        b->zob[0] ^= ep_zob[f][0];
        b->zob[1] ^= ep_zob[f][1];
        b->zob[2] ^= ep_zob[f][2];
    }
}

void zobrist_compute_full(Board *b) {
    b->zob[0] = b->zob[1] = b->zob[2] = 0;
    b->pawn_zob[0] = b->pawn_zob[1] = b->pawn_zob[2] = 0;
    uint64_t occ = b->occ[2];
    while (occ) {
        int sq = pop_lsb(&occ);
        zob_xor_piece(b, sq, b->squares[sq]);
    }
    for (int i = 0; i < 4; i++) {
        if (b->castling[i] == 0) {
            b->zob[0] ^= castling_zob[i][0];
            b->zob[1] ^= castling_zob[i][1];
            b->zob[2] ^= castling_zob[i][2];
        }
    }
    zob_xor_ep(b);
}

uint64_t board_tt_key(const Board *b) {
    uint64_t k = (uint64_t)b->zob[0] | ((uint64_t)b->zob[1] << 16) | ((uint64_t)b->zob[2] << 32);
    if (b->side == BLACK) k ^= 0x9e3779b97f4aULL;
    return k;
}

uint64_t board_book_hash(const Board *b) {
    return (uint64_t)b->zob[0] | ((uint64_t)b->zob[1] << 16) | ((uint64_t)b->zob[2] << 32);
}

static void recompute_material_and_counts(Board *b) {
    memset(b->piece_counts, 0, sizeof(b->piece_counts));
    memset(b->bb, 0, sizeof(b->bb));
    b->occ[0] = b->occ[1] = b->occ[2] = 0;
    b->total_pieces = 0;
    b->total_pawns = 0;
    b->w_mat = 0;
    b->b_mat = 0;
    b->mat_balance = 0;
    b->psqt_score = 0;
    b->wk_sq = 0;
    b->bk_sq = 0;

    for (int sq = 0; sq < 64; sq++) {
        uint8_t p = b->squares[sq];
        if (p == EMPTY) continue;
        uint64_t bit = 1ULL << sq;
        b->bb[p] |= bit;
        b->occ[piece_side(p)] |= bit;
        b->occ[2] |= bit;
        b->piece_counts[p]++;
        b->total_pieces++;
        if (p == WP || p == BP) b->total_pawns++;
        if (p == WK) b->wk_sq = (uint8_t)sq;
        if (p == BK) b->bk_sq = (uint8_t)sq;
        int sq120 = sq64_to_120(sq);
        if (p >= WP && p <= WK) {
            b->mat_balance += piece_val[p];
            if (p != WP && p != WK) b->w_mat += piece_val[p];
            if (p == WP) b->psqt_score += wp_psqt[sq120];
        } else if (p >= BP && p <= BK) {
            b->mat_balance -= piece_val[p];
            if (p != BP && p != BK) b->b_mat += piece_val[p];
            if (p == BP) b->psqt_score -= bp_psqt[sq120];
        }
    }
}

void board_init_startpos(Board *b) {
    board_set_fen(b, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

bool board_set_fen(Board *b, const char *fen) {
    memset(b, 0, sizeof(*b));
    b->ep_sq = -1;
    const char *p = fen;
    int rank = 7, file = 0;
    while (*p && *p != ' ') {
        if (*p == '/') {
            rank--;
            file = 0;
        } else if (*p >= '1' && *p <= '8') {
            file += (*p - '0');
        } else {
            int pc = piece_from_char(*p);
            if (pc == EMPTY || rank < 0 || file >= 8) return false;
            b->squares[rank * 8 + file] = (uint8_t)pc;
            file++;
        }
        p++;
    }
    while (*p == ' ') p++;
    b->side = (*p == 'b') ? BLACK : WHITE;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    b->castling[0] = b->castling[1] = b->castling[2] = b->castling[3] = 1;
    if (*p == '-') {
        p++;
    } else {
        while (*p && *p != ' ') {
            if (*p == 'K') b->castling[0] = 0;
            else if (*p == 'Q') b->castling[1] = 0;
            else if (*p == 'k') b->castling[2] = 0;
            else if (*p == 'q') b->castling[3] = 0;
            p++;
        }
    }
    while (*p == ' ') p++;
    if (*p && *p != '-') {
        int ef = p[0] - 'a';
        int er = p[1] - '1';
        if (ef >= 0 && ef < 8 && er >= 0 && er < 8) {
            b->ep_sq = er * 8 + ef;
        }
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    b->halfmove = 0;
    if (*p && isdigit((unsigned char)*p)) {
        sscanf(p, "%d", &b->halfmove);
    }
    b->game_ply = 1;
    recompute_material_and_counts(b);
    zobrist_compute_full(b);
    b->hash_history[0] = board_tt_key(b);
    return true;
}

bool is_sq_attacked(const Board *b, int sq, int by_side) {
    if (by_side == WHITE) {
        if (BB_PAWN_ATK[BLACK][sq] & b->bb[WP]) return true;
        if (BB_KNIGHT_ATK[sq] & b->bb[WN]) return true;
        if (BB_KING_ATK[sq] & b->bb[WK]) return true;
        if (bb_bishop_attacks(sq, b->occ[2]) & (b->bb[WB] | b->bb[WQ])) return true;
        if (bb_rook_attacks(sq, b->occ[2]) & (b->bb[WR] | b->bb[WQ])) return true;
    } else {
        if (BB_PAWN_ATK[WHITE][sq] & b->bb[BP]) return true;
        if (BB_KNIGHT_ATK[sq] & b->bb[BN]) return true;
        if (BB_KING_ATK[sq] & b->bb[BK]) return true;
        if (bb_bishop_attacks(sq, b->occ[2]) & (b->bb[BB] | b->bb[BQ])) return true;
        if (bb_rook_attacks(sq, b->occ[2]) & (b->bb[BR] | b->bb[BQ])) return true;
    }
    return false;
}

bool in_check(const Board *b, int side) {
    int ksq = (side == WHITE) ? b->wk_sq : b->bk_sq;
    return is_sq_attacked(b, ksq, side ^ 1);
}

bool make_move(Board *b, Move m) {
    if (b->game_ply >= MAX_GAME_PLY - 1) return false;
    Undo *u = &b->undo_stack[b->game_ply];
    u->ep_sq = (int8_t)b->ep_sq;
    u->halfmove = b->halfmove;
    memcpy(u->castling, b->castling, sizeof(u->castling));
    memcpy(u->zob, b->zob, sizeof(u->zob));
    memcpy(u->pawn_zob, b->pawn_zob, sizeof(u->pawn_zob));
    u->mat_balance = b->mat_balance;
    u->w_mat = b->w_mat;
    u->b_mat = b->b_mat;
    u->psqt_score = b->psqt_score;

    int from = m.from;
    int to = m.to;
    int spec = MOVE_SPEC(m);
    uint8_t pc = MOVE_PIECE(m);
    uint8_t cap = b->squares[to];
    u->captured = cap;

    if (b->ep_sq >= 0) {
        zob_xor_ep(b);
        b->ep_sq = -1;
    }

    zob_xor_piece(b, from, pc);
    bb_del(b, from, pc);

    if (cap != EMPTY) {
        zob_xor_piece(b, to, cap);
        bb_del(b, to, cap);
        b->piece_counts[cap]--;
        b->total_pieces--;
        if (cap == WP || cap == BP) b->total_pawns--;
        if (cap >= WP && cap <= WK) {
            b->mat_balance -= piece_val[cap];
            if (cap != WP && cap != WK) b->w_mat -= piece_val[cap];
            if (cap == WP) b->psqt_score -= wp_psqt[sq64_to_120(to)];
        } else {
            b->mat_balance += piece_val[cap];
            if (cap != BP && cap != BK) b->b_mat -= piece_val[cap];
            if (cap == BP) b->psqt_score += bp_psqt[sq64_to_120(to)];
        }
    }

    if (spec == SPEC_EP) {
        int cap_sq = (b->side == WHITE) ? (to - 8) : (to + 8);
        uint8_t ep_pc = b->squares[cap_sq];
        u->captured = ep_pc;
        zob_xor_piece(b, cap_sq, ep_pc);
        bb_del(b, cap_sq, ep_pc);
        b->piece_counts[ep_pc]--;
        b->total_pieces--;
        b->total_pawns--;
        if (ep_pc == WP) {
            b->mat_balance -= piece_val[WP];
            b->psqt_score -= wp_psqt[sq64_to_120(cap_sq)];
        } else {
            b->mat_balance += piece_val[BP];
            b->psqt_score += bp_psqt[sq64_to_120(cap_sq)];
        }
    }

    uint8_t placed = pc;
    if (spec >= SPEC_PROMO_N && spec <= SPEC_PROMO_Q) {
        int promo_offset = spec;
        placed = (b->side == WHITE) ? (uint8_t)promo_offset : (uint8_t)(promo_offset + 7);
        b->piece_counts[pc]--;
        b->piece_counts[placed]++;
        b->total_pawns--;
        if (b->side == WHITE) {
            b->mat_balance += promo_delta[promo_offset];
            b->w_mat += piece_val[placed];
            b->psqt_score -= wp_psqt[sq64_to_120(from)];
        } else {
            b->mat_balance -= promo_delta[promo_offset];
            b->b_mat += piece_val[placed];
            b->psqt_score += bp_psqt[sq64_to_120(from)];
        }
    } else {
        if (pc == WP) b->psqt_score += wp_psqt[sq64_to_120(to)] - wp_psqt[sq64_to_120(from)];
        else if (pc == BP) b->psqt_score -= bp_psqt[sq64_to_120(to)] - bp_psqt[sq64_to_120(from)];
    }

    bb_put(b, to, placed);
    zob_xor_piece(b, to, placed);

    if (pc == WK) b->wk_sq = (uint8_t)to;
    else if (pc == BK) b->bk_sq = (uint8_t)to;

    if (spec == SPEC_CASTLE) {
        int r_from, r_to;
        uint8_t rook = (b->side == WHITE) ? WR : BR;
        if (to > from) { r_from = to + 1; r_to = to - 1; }
        else { r_from = to - 2; r_to = to + 1; }
        zob_xor_piece(b, r_from, rook);
        bb_del(b, r_from, rook);
        bb_put(b, r_to, rook);
        zob_xor_piece(b, r_to, rook);
    }

    if (pc == WP && to - from == 16) {
        b->ep_sq = from + 8;
    } else if (pc == BP && from - to == 16) {
        b->ep_sq = from - 8;
    }

    if (pc == WK) {
        if (b->castling[0] == 0) { b->castling[0] = b->game_ply; b->zob[0] ^= castling_zob[0][0]; b->zob[1] ^= castling_zob[0][1]; b->zob[2] ^= castling_zob[0][2]; }
        if (b->castling[1] == 0) { b->castling[1] = b->game_ply; b->zob[0] ^= castling_zob[1][0]; b->zob[1] ^= castling_zob[1][1]; b->zob[2] ^= castling_zob[1][2]; }
    } else if (pc == BK) {
        if (b->castling[2] == 0) { b->castling[2] = b->game_ply; b->zob[0] ^= castling_zob[2][0]; b->zob[1] ^= castling_zob[2][1]; b->zob[2] ^= castling_zob[2][2]; }
        if (b->castling[3] == 0) { b->castling[3] = b->game_ply; b->zob[0] ^= castling_zob[3][0]; b->zob[1] ^= castling_zob[3][1]; b->zob[2] ^= castling_zob[3][2]; }
    }
    if (from == 7  || to == 7)  { if (b->castling[0] == 0) { b->castling[0] = b->game_ply; b->zob[0] ^= castling_zob[0][0]; b->zob[1] ^= castling_zob[0][1]; b->zob[2] ^= castling_zob[0][2]; } }
    if (from == 0  || to == 0)  { if (b->castling[1] == 0) { b->castling[1] = b->game_ply; b->zob[0] ^= castling_zob[1][0]; b->zob[1] ^= castling_zob[1][1]; b->zob[2] ^= castling_zob[1][2]; } }
    if (from == 63 || to == 63) { if (b->castling[2] == 0) { b->castling[2] = b->game_ply; b->zob[0] ^= castling_zob[2][0]; b->zob[1] ^= castling_zob[2][1]; b->zob[2] ^= castling_zob[2][2]; } }
    if (from == 56 || to == 56) { if (b->castling[3] == 0) { b->castling[3] = b->game_ply; b->zob[0] ^= castling_zob[3][0]; b->zob[1] ^= castling_zob[3][1]; b->zob[2] ^= castling_zob[3][2]; } }

    if (pc == WP || pc == BP || cap != EMPTY || spec == SPEC_EP) b->halfmove = 0;
    else b->halfmove++;

    int moved_side = b->side;
    b->side ^= 1;
    zob_xor_ep(b);
    b->game_ply++;
    b->hash_history[b->game_ply - 1] = board_tt_key(b);

    if (in_check(b, moved_side)) {
        unmake_move(b, m);
        return false;
    }
    return true;
}

void unmake_move(Board *b, Move m) {
    if (b->game_ply <= 0) return;
    b->game_ply--;
    b->side ^= 1;
    Undo *u = &b->undo_stack[b->game_ply];

    int from = m.from;
    int to = m.to;
    int spec = MOVE_SPEC(m);
    uint8_t pc = MOVE_PIECE(m);

    if (spec == SPEC_CASTLE) {
        int r_from, r_to;
        uint8_t rook = (b->side == WHITE) ? WR : BR;
        if (to > from) { r_from = to + 1; r_to = to - 1; }
        else { r_from = to - 2; r_to = to + 1; }
        bb_del(b, r_to, rook);
        bb_put(b, r_from, rook);
    }

    uint8_t moved_on_to = b->squares[to];
    bb_del(b, to, moved_on_to);

    if (spec >= SPEC_PROMO_N && spec <= SPEC_PROMO_Q) {
        b->piece_counts[moved_on_to]--;
        b->piece_counts[pc]++;
        b->total_pawns++;
    }

    bb_put(b, from, pc);
    if (pc == WK) b->wk_sq = (uint8_t)from;
    else if (pc == BK) b->bk_sq = (uint8_t)from;

    if (spec == SPEC_EP) {
        int cap_sq = (b->side == WHITE) ? (to - 8) : (to + 8);
        bb_put(b, cap_sq, u->captured);
        b->piece_counts[u->captured]++;
        b->total_pieces++;
        b->total_pawns++;
    } else if (u->captured != EMPTY) {
        bb_put(b, to, u->captured);
        b->piece_counts[u->captured]++;
        b->total_pieces++;
        if (u->captured == WP || u->captured == BP) b->total_pawns++;
    }

    b->ep_sq = u->ep_sq;
    b->halfmove = u->halfmove;
    memcpy(b->castling, u->castling, sizeof(u->castling));
    memcpy(b->zob, u->zob, sizeof(u->zob));
    memcpy(b->pawn_zob, u->pawn_zob, sizeof(u->pawn_zob));
    b->mat_balance = u->mat_balance;
    b->w_mat = u->w_mat;
    b->b_mat = u->b_mat;
    b->psqt_score = u->psqt_score;
}

void make_null_move(Board *b) {
    if (b->game_ply >= MAX_GAME_PLY - 1) return;
    Undo *u = &b->undo_stack[b->game_ply];
    u->ep_sq = (int8_t)b->ep_sq;
    u->halfmove = b->halfmove;
    memcpy(u->zob, b->zob, sizeof(u->zob));
    if (b->ep_sq >= 0) {
        zob_xor_ep(b);
        b->ep_sq = -1;
    }
    b->halfmove++;
    b->side ^= 1;
    b->game_ply++;
    b->hash_history[b->game_ply - 1] = board_tt_key(b);
}

void unmake_null_move(Board *b) {
    if (b->game_ply <= 0) return;
    b->game_ply--;
    b->side ^= 1;
    Undo *u = &b->undo_stack[b->game_ply];
    b->ep_sq = u->ep_sq;
    b->halfmove = u->halfmove;
    memcpy(b->zob, u->zob, sizeof(u->zob));
}

int count_repetitions(const Board *b) {
    uint64_t cur = b->hash_history[b->game_ply - 1];
    int count = 0;
    int limit = b->game_ply - 1 - b->halfmove;
    if (limit < 0) limit = 0;
    for (int i = b->game_ply - 3; i >= limit; i -= 2) {
        if (b->hash_history[i] == cur) count++;
    }
    return count;
}
