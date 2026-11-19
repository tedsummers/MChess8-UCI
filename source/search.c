#define _POSIX_C_SOURCE 200809L
#include "mchess8.h"
#include "bitboard.h"
#include "tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
static int64_t now_ms(void) {
    return (int64_t)GetTickCount64();
}
static int input_available(void) {
    static int init = 0, is_pipe = 0;
    static HANDLE inh;
    if (!init) {
        init = 1;
        inh = GetStdHandle(STD_INPUT_HANDLE);
        DWORD dw;
        is_pipe = !GetConsoleMode(inh, &dw);
    }
    if (is_pipe) {
        DWORD avail = 0;
        if (!PeekNamedPipe(inh, NULL, 0, NULL, &avail, NULL)) return 1;
        return avail > 0;
    }
    return _kbhit();
}
#else
#include <unistd.h>
#include <sys/select.h>
#include <pthread.h>
static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
}
static int input_available(void) {
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0;
}
#endif

static int16_t g_root_history[14][64];
_Alignas(64) static volatile int g_smp_stop = 0;

void search_clear_history(void) {
    memset(g_root_history, 0, sizeof(g_root_history));
}

static void check_stop(SearchState *st) {
    if ((st->nodes & 1023ULL) == 0) {
        if (__atomic_load_n(&g_smp_stop, __ATOMIC_RELAXED)) {
            st->stop = 1;
            return;
        }
        if (st->thread_id != 0) return;
        if (st->max_nodes > 0 && st->nodes >= st->max_nodes) {
            st->stop = 1;
            __atomic_store_n(&g_smp_stop, 1, __ATOMIC_RELAXED);
            return;
        }
        if (!st->infinite && st->stop_time_ms > 0 && now_ms() >= st->stop_time_ms) {
            st->stop = 1;
            __atomic_store_n(&g_smp_stop, 1, __ATOMIC_RELAXED);
            return;
        }
        if (input_available()) {
            char buf[256];
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                st->stop = 1;
                __atomic_store_n(&g_smp_stop, 1, __ATOMIC_RELAXED);
                return;
            }
            if (strncmp(buf, "stop", 4) == 0) {
                st->stop = 1;
                st->ponder = 0;
                __atomic_store_n(&g_smp_stop, 1, __ATOMIC_RELAXED);
            } else if (strncmp(buf, "ponderhit", 9) == 0) {
                st->ponder = 0;
                st->infinite = 0;
                int64_t now = now_ms();
                int64_t alloc = st->alloc_ms > 0 ? st->alloc_ms : 500;
                st->soft_time_ms = now + alloc;
                st->stop_time_ms = now + alloc * 2;
            } else if (strncmp(buf, "isready", 7) == 0) {
                printf("readyok\n");
                fflush(stdout);
            } else if (strncmp(buf, "quit", 4) == 0) {
                st->stop = 1;
                __atomic_store_n(&g_smp_stop, 1, __ATOMIC_RELAXED);
                exit(0);
            }
        }
    }
}

static bool same_move(Move a, Move b) {
    return a.from == b.from && a.to == b.to && MOVE_SPEC(a) == MOVE_SPEC(b);
}

/* Bitboard swap-list Static Exchange Evaluation (SEE) with X-ray sliders */
static uint8_t find_least_attacker(const Board *b, uint64_t occ, int sq, int side, int *out_sq) {
    uint8_t base = (side == WHITE) ? WP : BP;
    uint64_t atk;

    atk = BB_PAWN_ATK[side ^ 1][sq] & b->bb[base] & occ;
    if (atk) { *out_sq = ctz64(atk); return base; }

    atk = BB_KNIGHT_ATK[sq] & b->bb[base + 1] & occ;
    if (atk) { *out_sq = ctz64(atk); return (uint8_t)(base + 1); }

    uint64_t diag = bb_bishop_attacks(sq, occ) & occ;
    atk = diag & b->bb[base + 2];
    if (atk) { *out_sq = ctz64(atk); return (uint8_t)(base + 2); }

    uint64_t ortho = bb_rook_attacks(sq, occ) & occ;
    atk = ortho & b->bb[base + 3];
    if (atk) { *out_sq = ctz64(atk); return (uint8_t)(base + 3); }

    atk = (diag | ortho) & b->bb[base + 4];
    if (atk) { *out_sq = ctz64(atk); return (uint8_t)(base + 4); }

    atk = BB_KING_ATK[sq] & b->bb[base + 5] & occ;
    if (atk) { *out_sq = ctz64(atk); return (uint8_t)(base + 5); }

    return EMPTY;
}

static int see_capture(const Board *b, Move m) {
    int gain[32];
    int d = 0;

    uint8_t vic = b->squares[m.to];
    if (MOVE_SPEC(m) == SPEC_EP) vic = WP;
    uint8_t att = MOVE_PIECE(m);
    int spec = MOVE_SPEC(m);

    gain[0] = piece_val[vic];
    if (spec >= SPEC_PROMO_N && spec <= SPEC_PROMO_Q) {
        gain[0] += promo_delta[spec];
        att = (b->side == WHITE) ? (uint8_t)spec : (uint8_t)(spec + 7);
    }

    uint64_t occ = b->occ[2] ^ (1ULL << m.from);
    if (spec == SPEC_EP) {
        int ep_cap_sq = (b->side == WHITE) ? (m.to - 8) : (m.to + 8);
        occ ^= (1ULL << ep_cap_sq);
    }

    int side = b->side ^ 1;
    int cur_val = piece_val[att];
    while (d < 31) {
        int att_sq = 0;
        uint8_t next_att = find_least_attacker(b, occ, m.to, side, &att_sq);
        if (next_att == EMPTY) break;
        d++;
        gain[d] = cur_val - gain[d - 1];
        occ ^= (1ULL << att_sq);
        cur_val = (next_att == WK || next_att == BK) ? 20000 : piece_val[next_att];
        side ^= 1;
    }
    while (d > 0) {
        int prev = gain[d - 1];
        int cur = -gain[d];
        gain[d - 1] = (cur < prev) ? cur : prev;
        d--;
    }
    return gain[0];
}

static void score_moves(const Board *b, const SearchState *st, Move *moves, int n, Move tt_move, int ply) {
    Move cm = {0};
    if (ply > 0 && ply < MAX_PLY) {
        Move prev = st->move_stack[ply - 1];
        if (prev.piece_spec != 0) {
            cm = st->countermoves[MOVE_PIECE(prev)][prev.to];
        }
    }
    for (int i = 0; i < n; i++) {
        Move *m = &moves[i];
        if (tt_move.piece_spec != 0 && same_move(*m, tt_move)) {
            m->sort_score = 1000000;
        } else if (MOVE_IS_CAP(*m)) {
            uint8_t vic = b->squares[m->to];
            if (MOVE_SPEC(*m) == SPEC_EP) vic = WP;
            uint8_t att = MOVE_PIECE(*m);
            int s = see_capture(b, *m);
            m->sort_score = (s >= 0 ? 200000 : 50000) + piece_val[vic] * 10 - piece_val[att];
        } else if (MOVE_SPEC(*m) >= SPEC_PROMO_N && MOVE_SPEC(*m) <= SPEC_PROMO_Q) {
            m->sort_score = 190000 + MOVE_SPEC(*m);
        } else if (ply < MAX_PLY && same_move(*m, st->killers[ply][0])) {
            m->sort_score = 80000;
        } else if (ply < MAX_PLY && same_move(*m, st->killers[ply][1])) {
            m->sort_score = 70000;
        } else if (cm.piece_spec != 0 && same_move(*m, cm)) {
            m->sort_score = 65000;
        } else {
            m->sort_score = st->history[MOVE_PIECE(*m)][m->to];
        }
    }
}

static void pick_move(Move *moves, int n, int idx) {
    int best = idx;
    for (int i = idx + 1; i < n; i++) {
        if (moves[i].sort_score > moves[best].sort_score) best = i;
    }
    if (best != idx) {
        Move tmp = moves[idx];
        moves[idx] = moves[best];
        moves[best] = tmp;
    }
}

static int quiesce(Board *b, const EngineSettings *cfg, SearchState *st, int alpha, int beta, int ply) {
    st->nodes++;
    check_stop(st);
    if (st->stop) return 0;

    if (ply >= MAX_PLY - 1) return evaluate(b, cfg);

    if (cfg->use_tb && b->total_pieces <= 4) {
        int tb_score = 0;
        if (tb_probe(b, ply, &tb_score)) {
            st->tb_hits++;
            return tb_score;
        }
    }

    bool in_chk = in_check(b, b->side);
    int stand_pat = -INF_SCORE;
    if (!in_chk) {
        stand_pat = evaluate(b, cfg);
        if (stand_pat >= beta) return beta;
        if (stand_pat > alpha) alpha = stand_pat;
    }

    Move moves[MAX_MOVES];
    int n = in_chk ? gen_pseudo_moves(b, moves) : gen_pseudo_captures(b, moves);
    Move no_tt = {0};
    score_moves(b, st, moves, n, no_tt, ply);

    int legal_cnt = 0;
    for (int i = 0; i < n; i++) {
        pick_move(moves, n, i);
        Move m = moves[i];

        if (!in_chk && MOVE_IS_CAP(m) && MOVE_SPEC(m) == SPEC_NONE) {
            uint8_t vic = b->squares[m.to];
            if (stand_pat + piece_val[vic] + 400 < alpha) continue;
            if (see_capture(b, m) < -100) continue;
        }

        if (!make_move(b, m)) continue;
        legal_cnt++;
        int score = -quiesce(b, cfg, st, -beta, -alpha, ply + 1);
        unmake_move(b, m);

        if (st->stop) return 0;
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    if (in_chk && legal_cnt == 0) return -MATE_SCORE + ply;
    return alpha;
}

static bool is_pseudo_legal(const Board *b, Move m) {
    if (m.from >= 64 || m.to >= 64 || m.from == m.to) return false;
    uint8_t pc = MOVE_PIECE(m);
    int spec = MOVE_SPEC(m);
    if (pc == EMPTY || b->squares[m.from] != pc) return false;
    int side = b->side;
    if (side == WHITE && pc > WK) return false;
    if (side == BLACK && pc < BP) return false;
    uint64_t to_bit = 1ULL << m.to;
    if (to_bit & b->occ[side]) return false;

    uint8_t cap = b->squares[m.to];
    if (spec == SPEC_EP) {
        if (pc != (side == WHITE ? WP : BP)) return false;
        if (m.to != b->ep_sq || b->ep_sq < 0 || cap != EMPTY) return false;
        return (BB_PAWN_ATK[side][m.from] & to_bit) != 0;
    }
    if (spec == SPEC_CASTLE) {
        if (side == WHITE && pc == WK && m.from == 4) {
            if (m.to == 6) return b->castling[0] == 0 && b->squares[5] == EMPTY && b->squares[6] == EMPTY && b->squares[7] == WR && !is_sq_attacked(b, 4, BLACK) && !is_sq_attacked(b, 5, BLACK) && !is_sq_attacked(b, 6, BLACK);
            if (m.to == 2) return b->castling[1] == 0 && b->squares[3] == EMPTY && b->squares[2] == EMPTY && b->squares[1] == EMPTY && b->squares[0] == WR && !is_sq_attacked(b, 4, BLACK) && !is_sq_attacked(b, 3, BLACK) && !is_sq_attacked(b, 2, BLACK);
        } else if (side == BLACK && pc == BK && m.from == 60) {
            if (m.to == 62) return b->castling[2] == 0 && b->squares[61] == EMPTY && b->squares[62] == EMPTY && b->squares[63] == BR && !is_sq_attacked(b, 60, WHITE) && !is_sq_attacked(b, 61, WHITE) && !is_sq_attacked(b, 62, WHITE);
            if (m.to == 58) return b->castling[3] == 0 && b->squares[59] == EMPTY && b->squares[58] == EMPTY && b->squares[57] == EMPTY && b->squares[56] == BR && !is_sq_attacked(b, 60, WHITE) && !is_sq_attacked(b, 59, WHITE) && !is_sq_attacked(b, 58, WHITE);
        }
        return false;
    }

    int ptype = (side == WHITE) ? pc : (pc - 7);
    if (ptype == 1) {
        int r_to = m.to >> 3;
        bool is_promo_rank = (side == WHITE) ? (r_to == 7) : (r_to == 0);
        if (spec >= SPEC_PROMO_N && spec <= SPEC_PROMO_Q) {
            if (!is_promo_rank) return false;
        } else if (spec == SPEC_NONE) {
            if (is_promo_rank) return false;
        } else {
            return false;
        }
        int diff = (int)m.to - (int)m.from;
        int dir = (side == WHITE) ? 8 : -8;
        if (diff == dir) return cap == EMPTY;
        if (diff == 2 * dir) {
            int r_from = m.from >> 3;
            return r_from == (side == WHITE ? 1 : 6) && cap == EMPTY && b->squares[m.from + dir] == EMPTY;
        }
        return cap != EMPTY && (BB_PAWN_ATK[side][m.from] & to_bit) != 0;
    }
    if (spec != SPEC_NONE) return false;
    if (ptype == 2) return (BB_KNIGHT_ATK[m.from] & to_bit) != 0;
    if (ptype == 3) return (bb_bishop_attacks(m.from, b->occ[2]) & to_bit) != 0;
    if (ptype == 4) return (bb_rook_attacks(m.from, b->occ[2]) & to_bit) != 0;
    if (ptype == 5) return (bb_queen_attacks(m.from, b->occ[2]) & to_bit) != 0;
    if (ptype == 6) return (BB_KING_ATK[m.from] & to_bit) != 0;
    return false;
}

static void record_beta_cutoff(SearchState *st, Move m, int depth, int ply, bool is_quiet) {
    if (!is_quiet) return;
    if (ply < MAX_PLY && !same_move(st->killers[ply][0], m)) {
        st->killers[ply][1] = st->killers[ply][0];
        st->killers[ply][0] = m;
    }
    if (ply > 0 && ply < MAX_PLY && st->move_stack[ply - 1].piece_spec != 0) {
        Move prev = st->move_stack[ply - 1];
        st->countermoves[MOVE_PIECE(prev)][prev.to] = m;
    }
    st->history[MOVE_PIECE(m)][m.to] += depth * depth;
    if (st->history[MOVE_PIECE(m)][m.to] > 16000) {
        for (int p = 0; p < 14; p++)
            for (int s = 0; s < 64; s++)
                st->history[p][s] >>= 1;
    }
}

static int alphabeta(Board *b, const EngineSettings *cfg, SearchState *st, int depth, int alpha, int beta, int ply, bool allow_null) {
    st->pv_len[ply] = 0;
    if (ply > 0) {
        int reps = count_repetitions(b);
        if (b->halfmove >= 100 || reps >= 2 || (ply >= 2 && reps >= 1)) {
            return (ply & 1) ? -cfg->contempt : cfg->contempt;
        }
    }

    bool in_chk = in_check(b, b->side);
    if (in_chk && cfg->extensions && ply < 24 && ply < MAX_PLY - 2) {
        depth++;
    }

    if (depth <= 0) {
        return quiesce(b, cfg, st, alpha, beta, ply);
    }

    st->nodes++;
    check_stop(st);
    if (st->stop) return 0;

    if (ply >= MAX_PLY - 1) return evaluate(b, cfg);

    if (ply > 0 && cfg->use_tb && b->total_pieces <= 4) {
        int tb_score = 0;
        if (tb_probe(b, ply, &tb_score)) {
            st->tb_hits++;
            Move no_move = {0};
            tt_store(b, depth, TT_EXACT, tb_score, ply, no_move);
            return tb_score;
        }
    }

    Move tt_move = {0};
    int tt_score = 0;
    bool is_pv = (beta - alpha > 1);
    if (tt_probe(b, depth, alpha, beta, ply, &tt_score, &tt_move)) {
        if (ply > 0 && !is_pv) {
            return tt_score;
        }
    }

    /* Static eval & improving flag for pruning (skip eval on PV nodes & checks) */
    int own_mat = (b->side == WHITE) ? b->w_mat : b->b_mat;
    int static_eval = (in_chk || is_pv) ? INF_SCORE : evaluate(b, cfg);
    if (ply < MAX_PLY) st->eval_stack[ply] = static_eval;
    bool improving = (!in_chk && !is_pv && ply >= 2 && st->eval_stack[ply - 2] != INF_SCORE && static_eval > st->eval_stack[ply - 2]);
    bool can_prune = (!in_chk && !is_pv && own_mat > 0 && beta < MATE_SCORE - 500 && beta > -MATE_SCORE + 500);
    if (can_prune && depth <= 3) {
        /* Reverse futility pruning (static null move) */
        int rfp_margin = (improving ? 180 : 140) * depth;
        if (depth <= 2 && static_eval - rfp_margin >= beta) {
            return beta;
        }
        /* Razoring at depth 1 */
        if (depth == 1 && static_eval + 300 <= alpha) {
            return quiesce(b, cfg, st, alpha, beta, ply);
        }
    }

    /* Null-move pruning */
    if (cfg->null_move && allow_null && !in_chk && !is_pv && depth >= 3 && own_mat > 700 && beta < MATE_SCORE - 500) {
        int R = (depth >= 6) ? 3 : 2;
        if (ply < MAX_PLY) st->move_stack[ply] = (Move){0};
        make_null_move(b);
        int null_score = -alphabeta(b, cfg, st, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
        unmake_null_move(b);
        if (st->stop) return 0;
        if (null_score >= beta) {
            if (depth >= 6 && (b->w_mat + b->b_mat) <= 3600) {
                int ver = alphabeta(b, cfg, st, depth - 1 - R, beta - 1, beta, ply, false);
                if (st->stop) return 0;
                if (ver >= beta) return beta;
            } else {
                return beta;
            }
        }
    }

    int orig_alpha = alpha;
    int legal_cnt = 0;
    Move best_move = {0};
    int best_score = -INF_SCORE;

    /* Stage 0: Try TT move before generating/scoring all moves */
    bool tt_tried = false;
    if (tt_move.piece_spec != 0 && is_pseudo_legal(b, tt_move)) {
        bool tt_is_quiet = (b->squares[tt_move.to] == EMPTY && MOVE_SPEC(tt_move) != SPEC_EP && MOVE_SPEC(tt_move) < SPEC_PROMO_N);
        if (make_move(b, tt_move)) {
            tt_tried = true;
            legal_cnt++;
            if (ply == 0 && st->thread_id == 0 && (depth >= 6 || now_ms() - st->start_time_ms >= 200)) {
                char mbuf[8];
                move_to_uci(tt_move, mbuf);
                printf("info depth %d currmove %s currmovenumber %d\n", depth, mbuf, legal_cnt);
                fflush(stdout);
            }
            if (ply < MAX_PLY) st->move_stack[ply] = tt_move;
            int score = -alphabeta(b, cfg, st, depth - 1, -beta, -alpha, ply + 1, true);
            unmake_move(b, tt_move);
            if (st->stop) return 0;
            best_score = score;
            best_move = tt_move;
            if (score > alpha) {
                alpha = score;
                st->pv[ply][0] = tt_move;
                int child_len = st->pv_len[ply + 1];
                if (child_len > MAX_PLY - ply - 2) child_len = MAX_PLY - ply - 2;
                for (int k = 0; k < child_len; k++) st->pv[ply][k + 1] = st->pv[ply + 1][k];
                st->pv_len[ply] = child_len + 1;
                if (alpha >= beta) {
                    record_beta_cutoff(st, tt_move, depth, ply, tt_is_quiet);
                    tt_store(b, depth, TT_LOWER, beta, ply, tt_move);
                    return beta;
                }
            }
        }
    }

    Move moves[MAX_MOVES];
    int n = gen_pseudo_moves(b, moves);
    score_moves(b, st, moves, n, tt_move, ply);

    for (int i = 0; i < n; i++) {
        pick_move(moves, n, i);
        Move m = moves[i];
        if (tt_tried && same_move(m, tt_move)) continue;
        bool is_quiet = (!MOVE_IS_CAP(m) && MOVE_SPEC(m) < SPEC_PROMO_N);

        /* Shallow SEE pruning for clearly losing captures */
        if (can_prune && depth <= 2 && legal_cnt > 0 && MOVE_IS_CAP(m) && MOVE_SPEC(m) == SPEC_NONE) {
            if (see_capture(b, m) < -120 * depth) continue;
        }

        if (!make_move(b, m)) continue;
        legal_cnt++;

        if (ply == 0 && st->thread_id == 0 && (depth >= 6 || now_ms() - st->start_time_ms >= 200)) {
            char mbuf[8];
            move_to_uci(m, mbuf);
            printf("info depth %d currmove %s currmovenumber %d\n", depth, mbuf, legal_cnt);
            fflush(stdout);
        }

        bool gives_chk = in_check(b, b->side);

        /* Futility pruning & Late Move Pruning (LMP) at low depths (protect TT & killer moves) */
        bool is_killer = (ply < MAX_PLY && (same_move(m, st->killers[ply][0]) || same_move(m, st->killers[ply][1])));
        if (can_prune && depth <= 3 && legal_cnt > 1 && is_quiet && !gives_chk && !same_move(m, tt_move) && !is_killer) {
            int lmp_limit = improving ? (3 + depth * depth) : (2 + (depth * depth * 3) / 4);
            if (legal_cnt > lmp_limit) {
                unmake_move(b, m);
                continue;
            }
            if (depth <= 2 && static_eval + 180 * depth <= alpha) {
                unmake_move(b, m);
                continue;
            }
        }

        if (ply < MAX_PLY) st->move_stack[ply] = m;

        int score;
        if (legal_cnt == 1) {
            score = -alphabeta(b, cfg, st, depth - 1, -beta, -alpha, ply + 1, true);
        } else {
            int red = 0;
            if (depth >= 3 && legal_cnt >= 3 && !in_chk && is_quiet && !gives_chk) {
                if (!is_pv || legal_cnt >= 6) {
                    red = 1 + (depth >= 5 && legal_cnt >= 6) + (depth >= 8 && legal_cnt >= 12);
                    if (!improving && legal_cnt >= 4) red++;
                    if (st->history[MOVE_PIECE(m)][m.to] > 200 && red > 1) red--;
                    if (is_pv && red > 1) red = 1;
                    if (depth - 1 - red < 1) red = depth - 2;
                    if (red < 0) red = 0;
                }
            }
            score = -alphabeta(b, cfg, st, depth - 1 - red, -alpha - 1, -alpha, ply + 1, true);
            if (!st->stop && red > 0 && score > alpha) {
                score = -alphabeta(b, cfg, st, depth - 1, -alpha - 1, -alpha, ply + 1, true);
            }
            if (!st->stop && score > alpha && score < beta) {
                score = -alphabeta(b, cfg, st, depth - 1, -beta, -alpha, ply + 1, true);
            }
        }
        unmake_move(b, m);

        if (st->stop) return 0;

        if (score > best_score) {
            best_score = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
                st->pv[ply][0] = m;
                int child_len = st->pv_len[ply + 1];
                if (child_len > MAX_PLY - ply - 2) child_len = MAX_PLY - ply - 2;
                for (int k = 0; k < child_len; k++) {
                    st->pv[ply][k + 1] = st->pv[ply + 1][k];
                }
                st->pv_len[ply] = child_len + 1;

                if (alpha >= beta) {
                    record_beta_cutoff(st, m, depth, ply, is_quiet);
                    tt_store(b, depth, TT_LOWER, beta, ply, m);
                    return beta;
                }
            }
        }
    }

    if (legal_cnt == 0) {
        if (in_chk) return -MATE_SCORE + ply;
        return 0;
    }

    int bound = (best_score <= orig_alpha) ? TT_UPPER : TT_EXACT;
    tt_store(b, depth, bound, best_score, ply, best_move);
    return best_score;
}

typedef struct {
    _Alignas(64) SearchState st;
    Board b;
    const EngineSettings *cfg;
    int max_d;
    int id;
} WorkerCtx;

static WorkerCtx *g_pool_workers = NULL;
static int g_pool_count = 0;
static int g_pool_quit = 0;
static int g_pool_job_id = 0;
static int g_pool_active = 0;

#ifdef _WIN32
static HANDLE g_pool_handles[63];
static CRITICAL_SECTION g_pool_mtx;
static CONDITION_VARIABLE g_pool_cv;
static CONDITION_VARIABLE g_pool_done_cv;
static int g_pool_sync_init = 0;
static void pool_sync_ensure(void) {
    if (!g_pool_sync_init) {
        InitializeCriticalSection(&g_pool_mtx);
        InitializeConditionVariable(&g_pool_cv);
        InitializeConditionVariable(&g_pool_done_cv);
        g_pool_sync_init = 1;
    }
}
static void pool_lock(void) { EnterCriticalSection(&g_pool_mtx); }
static void pool_unlock(void) { LeaveCriticalSection(&g_pool_mtx); }
static void pool_wait_job(void) { SleepConditionVariableCS(&g_pool_cv, &g_pool_mtx, INFINITE); }
static void pool_broadcast_job(void) { WakeAllConditionVariable(&g_pool_cv); }
static void pool_wait_done(void) { SleepConditionVariableCS(&g_pool_done_cv, &g_pool_mtx, INFINITE); }
static void pool_signal_done(void) { WakeConditionVariable(&g_pool_done_cv); }
#else
static pthread_t g_pool_handles[63];
static pthread_mutex_t g_pool_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_pool_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_pool_done_cv = PTHREAD_COND_INITIALIZER;
static void pool_sync_ensure(void) {}
static void pool_lock(void) { pthread_mutex_lock(&g_pool_mtx); }
static void pool_unlock(void) { pthread_mutex_unlock(&g_pool_mtx); }
static void pool_wait_job(void) { pthread_cond_wait(&g_pool_cv, &g_pool_mtx); }
static void pool_broadcast_job(void) { pthread_cond_broadcast(&g_pool_cv); }
static void pool_wait_done(void) { pthread_cond_wait(&g_pool_done_cv, &g_pool_mtx); }
static void pool_signal_done(void) { pthread_cond_signal(&g_pool_done_cv); }
#endif

static void run_helper_search(WorkerCtx *w) {
    int id = w->st.thread_id;
    int start_d = 1 + (id % 3);
    if (start_d > w->max_d) start_d = 1;
    int prev_score = 0;
    for (int d = start_d; d <= w->max_d; d++) {
        if (__atomic_load_n(&g_smp_stop, __ATOMIC_RELAXED)) break;
        if (d > start_d && d < w->max_d && ((d + id) & 1) && (id % 3 == 0)) continue;
        int alpha = -INF_SCORE;
        int beta  =  INF_SCORE;
        int delta = 50 + (id & 3) * 15;
        if (d >= 4 && prev_score > -MATE_SCORE + 500 && prev_score < MATE_SCORE - 500) {
            alpha = prev_score - delta;
            beta  = prev_score + delta;
        }
        int score;
        while (1) {
            score = alphabeta(&w->b, w->cfg, &w->st, d, alpha, beta, 0, true);
            if (w->st.stop || __atomic_load_n(&g_smp_stop, __ATOMIC_RELAXED)) break;
            if (score <= alpha) {
                alpha = (delta >= 400) ? -INF_SCORE : (alpha - delta);
                delta *= 3;
            } else if (score >= beta) {
                beta = (delta >= 400) ? INF_SCORE : (beta + delta);
                delta *= 3;
            } else {
                break;
            }
        }
        if (w->st.stop || __atomic_load_n(&g_smp_stop, __ATOMIC_RELAXED)) break;
        prev_score = score;
    }
}

static void worker_loop(WorkerCtx *w) {
    int last_job = 0;
    while (1) {
        pool_lock();
        while (!g_pool_quit && g_pool_job_id == last_job) {
            pool_wait_job();
        }
        if (g_pool_quit) {
            pool_unlock();
            break;
        }
        last_job = g_pool_job_id;
        pool_unlock();

        run_helper_search(w);

        pool_lock();
        g_pool_active--;
        if (g_pool_active == 0) pool_signal_done();
        pool_unlock();
    }
}

#ifdef _WIN32
static DWORD WINAPI smp_thread_proc(LPVOID arg) {
    worker_loop((WorkerCtx *)arg);
    return 0;
}
#else
static void *smp_thread_proc(void *arg) {
    worker_loop((WorkerCtx *)arg);
    return NULL;
}
#endif

static void pool_shutdown(void) {
    if (g_pool_count == 0) return;
    pool_lock();
    g_pool_quit = 1;
    pool_broadcast_job();
    pool_unlock();
    for (int i = 0; i < g_pool_count; i++) {
#ifdef _WIN32
        WaitForSingleObject(g_pool_handles[i], INFINITE);
        CloseHandle(g_pool_handles[i]);
#else
        pthread_join(g_pool_handles[i], NULL);
#endif
    }
    free(g_pool_workers);
    g_pool_workers = NULL;
    g_pool_count = 0;
    g_pool_quit = 0;
}

static void pool_ensure(int n_helpers) {
    pool_sync_ensure();
    if (n_helpers == g_pool_count) return;
    pool_shutdown();
    if (n_helpers <= 0) return;
    g_pool_workers = (WorkerCtx *)calloc((size_t)n_helpers, sizeof(WorkerCtx));
    if (!g_pool_workers) return;
    g_pool_job_id = 0;
    g_pool_quit = 0;
    int started = 0;
    for (int i = 0; i < n_helpers; i++) {
        g_pool_workers[i].id = i + 1;
#ifdef _WIN32
        g_pool_handles[started] = CreateThread(NULL, 0, smp_thread_proc, &g_pool_workers[i], 0, NULL);
        if (g_pool_handles[started]) started++;
#else
        if (pthread_create(&g_pool_handles[started], NULL, smp_thread_proc, &g_pool_workers[i]) == 0) started++;
#endif
    }
    g_pool_count = started;
}

void search_shutdown(void) {
    pool_shutdown();
}

static void print_uci_info(const SearchState *st, const WorkerCtx *workers, int n_helpers, int depth, int score, int64_t elapsed) {
    uint64_t total_nodes = st->nodes;
    uint64_t total_tbhits = st->tb_hits;
    for (int i = 0; i < n_helpers; i++) {
        total_nodes += __atomic_load_n(&workers[i].st.nodes, __ATOMIC_RELAXED);
        total_tbhits += __atomic_load_n(&workers[i].st.tb_hits, __ATOMIC_RELAXED);
    }
    uint64_t nps = elapsed > 0 ? (total_nodes * 1000ULL) / (uint64_t)elapsed : total_nodes * 1000ULL;
    printf("info depth %d ", depth);
    if (score > MATE_SCORE - 500) {
        int mate_in = (MATE_SCORE - score + 1) / 2;
        printf("score mate %d ", mate_in);
    } else if (score < -MATE_SCORE + 500) {
        int mate_in = -((MATE_SCORE + score + 1) / 2);
        printf("score mate %d ", mate_in);
    } else {
        printf("score cp %d ", score / 2);
    }
    printf("nodes %llu nps %llu time %lld ", (unsigned long long)total_nodes,
           (unsigned long long)nps, (long long)elapsed);
    if (total_tbhits > 0) {
        printf("tbhits %llu ", (unsigned long long)total_tbhits);
    }
    printf("pv");
    char buf[8];
    for (int i = 0; i < st->pv_len[0]; i++) {
        move_to_uci(st->pv[0][i], buf);
        printf(" %s", buf);
    }
    printf("\n");
    fflush(stdout);
}

void search_go(Board *b, const EngineSettings *cfg, SearchState *st) {
    tt_new_search();
    __atomic_store_n(&g_smp_stop, 0, __ATOMIC_RELAXED);
    memset(st->pv, 0, sizeof(st->pv));
    memset(st->pv_len, 0, sizeof(st->pv_len));
    memset(st->killers, 0, sizeof(st->killers));
    memset(st->countermoves, 0, sizeof(st->countermoves));
    memset(st->move_stack, 0, sizeof(st->move_stack));
    for (int p = 0; p < 14; p++)
        for (int s = 0; s < 64; s++)
            g_root_history[p][s] >>= 1;
    memcpy(st->history, g_root_history, sizeof(st->history));
    st->nodes = 0;
    st->tb_hits = 0;
    st->stop = 0;
    st->thread_id = 0;
    st->start_time_ms = now_ms();

    /* Opening Book lookup */
    Move book_m = {0};
    if (book_probe(b, cfg, &book_m)) {
        char buf[8];
        move_to_uci(book_m, buf);
        printf("info depth 1 score cp 0 nodes 1 string book move %s\n", buf);
        printf("bestmove %s\n", buf);
        fflush(stdout);
        st->best_move = book_m;
        return;
    }

    Move legal[MAX_MOVES];
    int nlegal = gen_legal_moves(b, legal);
    if (nlegal == 0) {
        printf("bestmove 0000\n");
        fflush(stdout);
        return;
    }
    st->best_move = legal[0];

    int max_d = st->max_depth > 0 ? st->max_depth : 32;
    if (max_d >= MAX_PLY) max_d = MAX_PLY - 1;

    int n_helpers = (cfg->threads > 1) ? (cfg->threads - 1) : 0;
    if (n_helpers > 63) n_helpers = 63;
    pool_ensure(n_helpers);
    int started = g_pool_count;
    if (started > 0 && g_pool_workers) {
        size_t board_prefix = offsetof(Board, hash_history);
        size_t hist_bytes = (size_t)(b->game_ply > 0 ? b->game_ply : 1) * sizeof(uint64_t);
        if (hist_bytes > sizeof(b->hash_history)) hist_bytes = sizeof(b->hash_history);
        for (int i = 0; i < started; i++) {
            WorkerCtx *w = &g_pool_workers[i];
            memcpy(&w->b, b, board_prefix);
            memcpy(w->b.hash_history, b->hash_history, hist_bytes);
            w->cfg = cfg;
            w->st = *st;
            w->st.thread_id = i + 1;
            w->max_d = max_d;
        }
        pool_lock();
        g_pool_active = started;
        g_pool_job_id++;
        pool_broadcast_job();
        pool_unlock();
    }

    int prev_score = 0;
    for (int d = 1; d <= max_d; d++) {
        int alpha = -INF_SCORE;
        int beta  =  INF_SCORE;
        int delta = 50;
        if (d >= 4 && prev_score > -MATE_SCORE + 500 && prev_score < MATE_SCORE - 500) {
            alpha = prev_score - delta;
            beta  = prev_score + delta;
        }

        int score;
        while (1) {
            score = alphabeta(b, cfg, st, d, alpha, beta, 0, true);
            if (st->stop) break;
            if (score > alpha && st->pv_len[0] > 0) {
                st->best_move = st->pv[0][0];
                if (st->pv_len[0] >= 2) st->ponder_move = st->pv[0][1];
            }
            if (score <= alpha) {
                alpha = (delta >= 400) ? -INF_SCORE : (alpha - delta);
                delta *= 3;
            } else if (score >= beta) {
                beta = (delta >= 400) ? INF_SCORE : (beta + delta);
                delta *= 3;
            } else {
                break;
            }
        }

        if (st->stop) break;
        if (st->pv_len[0] > 0) {
            st->best_move = st->pv[0][0];
            st->ponder_move = (st->pv_len[0] >= 2) ? st->pv[0][1] : (Move){0};
        }
        st->best_score = score;
        prev_score = score;
        int64_t elapsed = now_ms() - st->start_time_ms;
        print_uci_info(st, g_pool_workers, started, d, score, elapsed);

        if (!st->infinite && st->soft_time_ms > 0 && now_ms() >= st->soft_time_ms) {
            break;
        }
        if (score > MATE_SCORE - 500 || score < -MATE_SCORE + 500) {
            if (!st->ponder && d >= 4) break;
        }
    }

    __atomic_store_n(&g_smp_stop, 1, __ATOMIC_RELAXED);
    if (started > 0) {
        pool_lock();
        while (g_pool_active > 0) {
            pool_wait_done();
        }
        pool_unlock();
        for (int i = 0; i < started; i++) {
            st->nodes += g_pool_workers[i].st.nodes;
            st->tb_hits += g_pool_workers[i].st.tb_hits;
        }
    }
    memcpy(g_root_history, st->history, sizeof(g_root_history));

    /* UCI spec: during go ponder, wait for stop or ponderhit before outputting bestmove */
    while (st->ponder && !st->stop) {
        char line[256];
        if (fgets(line, sizeof(line), stdin) == NULL) break;
        if (strncmp(line, "stop", 4) == 0 || strncmp(line, "ponderhit", 9) == 0) {
            st->ponder = 0;
            break;
        } else if (strncmp(line, "isready", 7) == 0) {
            printf("readyok\n");
            fflush(stdout);
        } else if (strncmp(line, "quit", 4) == 0) {
            exit(0);
        }
    }

    char buf[8], pbuf[8];
    move_to_uci(st->best_move, buf);
    if (cfg->ponder && st->ponder_move.piece_spec != 0) {
        move_to_uci(st->ponder_move, pbuf);
        printf("bestmove %s ponder %s\n", buf, pbuf);
    } else {
        printf("bestmove %s\n", buf);
    }
    fflush(stdout);
}
