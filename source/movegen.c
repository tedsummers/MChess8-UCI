#include "mchess8.h"
#include "bitboard.h"
#include <string.h>

static inline void add_move(Move *moves, int *cnt, int from, int to, int pc, int spec, int is_cap) {
    Move *m = &moves[*cnt];
    m->from = (uint8_t)from;
    m->to = (uint8_t)to;
    m->piece_spec = (uint8_t)((pc << 4) | (spec & 0x0f));
    m->flags = is_cap ? 0x80 : 0x00;
    m->sort_score = 0;
    (*cnt)++;
}

static inline void add_promos(Move *moves, int *cnt, int from, int to, int pc, int is_cap) {
    add_move(moves, cnt, from, to, pc, SPEC_PROMO_Q, is_cap);
    add_move(moves, cnt, from, to, pc, SPEC_PROMO_R, is_cap);
    add_move(moves, cnt, from, to, pc, SPEC_PROMO_B, is_cap);
    add_move(moves, cnt, from, to, pc, SPEC_PROMO_N, is_cap);
}

int gen_pseudo_moves(const Board *b, Move *moves) {
    int cnt = 0;
    int side = b->side;
    uint64_t own = b->occ[side];
    uint64_t enemy = b->occ[side ^ 1];
    uint64_t all = b->occ[2];
    uint64_t empty = ~all;

    if (side == WHITE) {
        uint64_t pawns = b->bb[WP];
        uint64_t push1 = (pawns << 8) & empty;
        uint64_t push_promo = push1 & RANK_8_BB;
        uint64_t push_norm = push1 & ~RANK_8_BB;
        uint64_t push2 = ((push_norm & RANK_3_BB) << 8) & empty;

        while (push_norm) {
            int to = pop_lsb(&push_norm);
            add_move(moves, &cnt, to - 8, to, WP, SPEC_NONE, 0);
        }
        while (push2) {
            int to = pop_lsb(&push2);
            add_move(moves, &cnt, to - 16, to, WP, SPEC_NONE, 0);
        }
        while (push_promo) {
            int to = pop_lsb(&push_promo);
            add_promos(moves, &cnt, to - 8, to, WP, 0);
        }

        uint64_t cap_l = ((pawns & ~FILE_A_BB) << 7) & enemy;
        uint64_t cap_r = ((pawns & ~FILE_H_BB) << 9) & enemy;
        uint64_t cl_promo = cap_l & RANK_8_BB, cl_norm = cap_l & ~RANK_8_BB;
        uint64_t cr_promo = cap_r & RANK_8_BB, cr_norm = cap_r & ~RANK_8_BB;

        while (cl_norm) { int to = pop_lsb(&cl_norm); add_move(moves, &cnt, to - 7, to, WP, SPEC_NONE, 1); }
        while (cr_norm) { int to = pop_lsb(&cr_norm); add_move(moves, &cnt, to - 9, to, WP, SPEC_NONE, 1); }
        while (cl_promo) { int to = pop_lsb(&cl_promo); add_promos(moves, &cnt, to - 7, to, WP, 1); }
        while (cr_promo) { int to = pop_lsb(&cr_promo); add_promos(moves, &cnt, to - 9, to, WP, 1); }

        if (b->ep_sq >= 0) {
            uint64_t ep_takers = BB_PAWN_ATK[BLACK][b->ep_sq] & pawns;
            while (ep_takers) {
                int from = pop_lsb(&ep_takers);
                add_move(moves, &cnt, from, b->ep_sq, WP, SPEC_EP, 1);
            }
        }
    } else {
        uint64_t pawns = b->bb[BP];
        uint64_t push1 = (pawns >> 8) & empty;
        uint64_t push_promo = push1 & RANK_1_BB;
        uint64_t push_norm = push1 & ~RANK_1_BB;
        uint64_t push2 = ((push_norm & RANK_6_BB) >> 8) & empty;

        while (push_norm) {
            int to = pop_lsb(&push_norm);
            add_move(moves, &cnt, to + 8, to, BP, SPEC_NONE, 0);
        }
        while (push2) {
            int to = pop_lsb(&push2);
            add_move(moves, &cnt, to + 16, to, BP, SPEC_NONE, 0);
        }
        while (push_promo) {
            int to = pop_lsb(&push_promo);
            add_promos(moves, &cnt, to + 8, to, BP, 0);
        }

        uint64_t cap_l = ((pawns & ~FILE_H_BB) >> 7) & enemy;
        uint64_t cap_r = ((pawns & ~FILE_A_BB) >> 9) & enemy;
        uint64_t cl_promo = cap_l & RANK_1_BB, cl_norm = cap_l & ~RANK_1_BB;
        uint64_t cr_promo = cap_r & RANK_1_BB, cr_norm = cap_r & ~RANK_1_BB;

        while (cl_norm) { int to = pop_lsb(&cl_norm); add_move(moves, &cnt, to + 7, to, BP, SPEC_NONE, 1); }
        while (cr_norm) { int to = pop_lsb(&cr_norm); add_move(moves, &cnt, to + 9, to, BP, SPEC_NONE, 1); }
        while (cl_promo) { int to = pop_lsb(&cl_promo); add_promos(moves, &cnt, to + 7, to, BP, 1); }
        while (cr_promo) { int to = pop_lsb(&cr_promo); add_promos(moves, &cnt, to + 9, to, BP, 1); }

        if (b->ep_sq >= 0) {
            uint64_t ep_takers = BB_PAWN_ATK[WHITE][b->ep_sq] & pawns;
            while (ep_takers) {
                int from = pop_lsb(&ep_takers);
                add_move(moves, &cnt, from, b->ep_sq, BP, SPEC_EP, 1);
            }
        }
    }

    int base = (side == WHITE) ? WP : BP;
    uint64_t knights = b->bb[base + 1];
    while (knights) {
        int from = pop_lsb(&knights);
        uint64_t atk = BB_KNIGHT_ATK[from] & ~own;
        uint64_t caps = atk & enemy;
        uint64_t quiets = atk & empty;
        while (caps)   { int to = pop_lsb(&caps);   add_move(moves, &cnt, from, to, base + 1, SPEC_NONE, 1); }
        while (quiets) { int to = pop_lsb(&quiets); add_move(moves, &cnt, from, to, base + 1, SPEC_NONE, 0); }
    }

    uint64_t bishops = b->bb[base + 2];
    while (bishops) {
        int from = pop_lsb(&bishops);
        uint64_t atk = bb_bishop_attacks(from, all) & ~own;
        uint64_t caps = atk & enemy;
        uint64_t quiets = atk & empty;
        while (caps)   { int to = pop_lsb(&caps);   add_move(moves, &cnt, from, to, base + 2, SPEC_NONE, 1); }
        while (quiets) { int to = pop_lsb(&quiets); add_move(moves, &cnt, from, to, base + 2, SPEC_NONE, 0); }
    }

    uint64_t rooks = b->bb[base + 3];
    while (rooks) {
        int from = pop_lsb(&rooks);
        uint64_t atk = bb_rook_attacks(from, all) & ~own;
        uint64_t caps = atk & enemy;
        uint64_t quiets = atk & empty;
        while (caps)   { int to = pop_lsb(&caps);   add_move(moves, &cnt, from, to, base + 3, SPEC_NONE, 1); }
        while (quiets) { int to = pop_lsb(&quiets); add_move(moves, &cnt, from, to, base + 3, SPEC_NONE, 0); }
    }

    uint64_t queens = b->bb[base + 4];
    while (queens) {
        int from = pop_lsb(&queens);
        uint64_t atk = bb_queen_attacks(from, all) & ~own;
        uint64_t caps = atk & enemy;
        uint64_t quiets = atk & empty;
        while (caps)   { int to = pop_lsb(&caps);   add_move(moves, &cnt, from, to, base + 4, SPEC_NONE, 1); }
        while (quiets) { int to = pop_lsb(&quiets); add_move(moves, &cnt, from, to, base + 4, SPEC_NONE, 0); }
    }

    int ksq = (side == WHITE) ? b->wk_sq : b->bk_sq;
    uint64_t katk = BB_KING_ATK[ksq] & ~own;
    uint64_t kcaps = katk & enemy;
    uint64_t kquiets = katk & empty;
    while (kcaps)   { int to = pop_lsb(&kcaps);   add_move(moves, &cnt, ksq, to, base + 5, SPEC_NONE, 1); }
    while (kquiets) { int to = pop_lsb(&kquiets); add_move(moves, &cnt, ksq, to, base + 5, SPEC_NONE, 0); }

    if (side == WHITE && ksq == 4) {
        if (b->castling[0] == 0 && b->squares[5] == EMPTY && b->squares[6] == EMPTY && b->squares[7] == WR) {
            if (!is_sq_attacked(b, 4, BLACK) && !is_sq_attacked(b, 5, BLACK) && !is_sq_attacked(b, 6, BLACK)) {
                add_move(moves, &cnt, 4, 6, WK, SPEC_CASTLE, 0);
            }
        }
        if (b->castling[1] == 0 && b->squares[3] == EMPTY && b->squares[2] == EMPTY && b->squares[1] == EMPTY && b->squares[0] == WR) {
            if (!is_sq_attacked(b, 4, BLACK) && !is_sq_attacked(b, 3, BLACK) && !is_sq_attacked(b, 2, BLACK)) {
                add_move(moves, &cnt, 4, 2, WK, SPEC_CASTLE, 0);
            }
        }
    } else if (side == BLACK && ksq == 60) {
        if (b->castling[2] == 0 && b->squares[61] == EMPTY && b->squares[62] == EMPTY && b->squares[63] == BR) {
            if (!is_sq_attacked(b, 60, WHITE) && !is_sq_attacked(b, 61, WHITE) && !is_sq_attacked(b, 62, WHITE)) {
                add_move(moves, &cnt, 60, 62, BK, SPEC_CASTLE, 0);
            }
        }
        if (b->castling[3] == 0 && b->squares[59] == EMPTY && b->squares[58] == EMPTY && b->squares[57] == EMPTY && b->squares[56] == BR) {
            if (!is_sq_attacked(b, 60, WHITE) && !is_sq_attacked(b, 59, WHITE) && !is_sq_attacked(b, 58, WHITE)) {
                add_move(moves, &cnt, 60, 58, BK, SPEC_CASTLE, 0);
            }
        }
    }

    return cnt;
}

int gen_pseudo_captures(const Board *b, Move *moves) {
    int cnt = 0;
    int side = b->side;
    uint64_t enemy = b->occ[side ^ 1];
    uint64_t all = b->occ[2];
    uint64_t empty = ~all;

    if (side == WHITE) {
        uint64_t pawns = b->bb[WP];
        uint64_t push_promo = ((pawns << 8) & empty) & RANK_8_BB;
        while (push_promo) {
            int to = pop_lsb(&push_promo);
            add_move(moves, &cnt, to - 8, to, WP, SPEC_PROMO_Q, 0);
        }
        uint64_t cap_l = ((pawns & ~FILE_A_BB) << 7) & enemy;
        uint64_t cap_r = ((pawns & ~FILE_H_BB) << 9) & enemy;
        uint64_t cl_promo = cap_l & RANK_8_BB, cl_norm = cap_l & ~RANK_8_BB;
        uint64_t cr_promo = cap_r & RANK_8_BB, cr_norm = cap_r & ~RANK_8_BB;
        while (cl_norm)  { int to = pop_lsb(&cl_norm);  add_move(moves, &cnt, to - 7, to, WP, SPEC_NONE, 1); }
        while (cr_norm)  { int to = pop_lsb(&cr_norm);  add_move(moves, &cnt, to - 9, to, WP, SPEC_NONE, 1); }
        while (cl_promo) { int to = pop_lsb(&cl_promo); add_promos(moves, &cnt, to - 7, to, WP, 1); }
        while (cr_promo) { int to = pop_lsb(&cr_promo); add_promos(moves, &cnt, to - 9, to, WP, 1); }
        if (b->ep_sq >= 0) {
            uint64_t ep_takers = BB_PAWN_ATK[BLACK][b->ep_sq] & pawns;
            while (ep_takers) {
                int from = pop_lsb(&ep_takers);
                add_move(moves, &cnt, from, b->ep_sq, WP, SPEC_EP, 1);
            }
        }
    } else {
        uint64_t pawns = b->bb[BP];
        uint64_t push_promo = ((pawns >> 8) & empty) & RANK_1_BB;
        while (push_promo) {
            int to = pop_lsb(&push_promo);
            add_move(moves, &cnt, to + 8, to, BP, SPEC_PROMO_Q, 0);
        }
        uint64_t cap_l = ((pawns & ~FILE_H_BB) >> 7) & enemy;
        uint64_t cap_r = ((pawns & ~FILE_A_BB) >> 9) & enemy;
        uint64_t cl_promo = cap_l & RANK_1_BB, cl_norm = cap_l & ~RANK_1_BB;
        uint64_t cr_promo = cap_r & RANK_1_BB, cr_norm = cap_r & ~RANK_1_BB;
        while (cl_norm)  { int to = pop_lsb(&cl_norm);  add_move(moves, &cnt, to + 7, to, BP, SPEC_NONE, 1); }
        while (cr_norm)  { int to = pop_lsb(&cr_norm);  add_move(moves, &cnt, to + 9, to, BP, SPEC_NONE, 1); }
        while (cl_promo) { int to = pop_lsb(&cl_promo); add_promos(moves, &cnt, to + 7, to, BP, 1); }
        while (cr_promo) { int to = pop_lsb(&cr_promo); add_promos(moves, &cnt, to + 9, to, BP, 1); }
        if (b->ep_sq >= 0) {
            uint64_t ep_takers = BB_PAWN_ATK[WHITE][b->ep_sq] & pawns;
            while (ep_takers) {
                int from = pop_lsb(&ep_takers);
                add_move(moves, &cnt, from, b->ep_sq, BP, SPEC_EP, 1);
            }
        }
    }

    int base = (side == WHITE) ? WP : BP;
    uint64_t knights = b->bb[base + 1];
    while (knights) {
        int from = pop_lsb(&knights);
        uint64_t caps = BB_KNIGHT_ATK[from] & enemy;
        while (caps) { int to = pop_lsb(&caps); add_move(moves, &cnt, from, to, base + 1, SPEC_NONE, 1); }
    }
    uint64_t bishops = b->bb[base + 2];
    while (bishops) {
        int from = pop_lsb(&bishops);
        uint64_t caps = bb_bishop_attacks(from, all) & enemy;
        while (caps) { int to = pop_lsb(&caps); add_move(moves, &cnt, from, to, base + 2, SPEC_NONE, 1); }
    }
    uint64_t rooks = b->bb[base + 3];
    while (rooks) {
        int from = pop_lsb(&rooks);
        uint64_t caps = bb_rook_attacks(from, all) & enemy;
        while (caps) { int to = pop_lsb(&caps); add_move(moves, &cnt, from, to, base + 3, SPEC_NONE, 1); }
    }
    uint64_t queens = b->bb[base + 4];
    while (queens) {
        int from = pop_lsb(&queens);
        uint64_t caps = bb_queen_attacks(from, all) & enemy;
        while (caps) { int to = pop_lsb(&caps); add_move(moves, &cnt, from, to, base + 4, SPEC_NONE, 1); }
    }
    int ksq = (side == WHITE) ? b->wk_sq : b->bk_sq;
    uint64_t kcaps = BB_KING_ATK[ksq] & enemy;
    while (kcaps) { int to = pop_lsb(&kcaps); add_move(moves, &cnt, ksq, to, base + 5, SPEC_NONE, 1); }

    return cnt;
}

int gen_legal_moves(Board *b, Move *moves) {
    Move pseudo[MAX_MOVES];
    int n = gen_pseudo_moves(b, pseudo);
    int legal = 0;
    for (int i = 0; i < n; i++) {
        if (make_move(b, pseudo[i])) {
            unmake_move(b, pseudo[i]);
            moves[legal++] = pseudo[i];
        }
    }
    return legal;
}

void move_to_uci(Move m, char *buf) {
    if (m.from == 0 && m.to == 0) {
        strcpy(buf, "0000");
        return;
    }
    buf[0] = (char)('a' + (m.from & 7));
    buf[1] = (char)('1' + (m.from >> 3));
    buf[2] = (char)('a' + (m.to & 7));
    buf[3] = (char)('1' + (m.to >> 3));
    int spec = MOVE_SPEC(m);
    if (spec >= SPEC_PROMO_N && spec <= SPEC_PROMO_Q) {
        const char promos[] = "  nbrq";
        buf[4] = promos[spec];
        buf[5] = '\0';
    } else {
        buf[4] = '\0';
    }
}

bool uci_to_move(Board *b, const char *str, Move *out) {
    Move legal[MAX_MOVES];
    int n = gen_legal_moves(b, legal);
    char buf[8];
    for (int i = 0; i < n; i++) {
        move_to_uci(legal[i], buf);
        if (strcmp(buf, str) == 0) {
            *out = legal[i];
            return true;
        }
    }
    return false;
}
