#include "mchess8.h"
#include "bitboard.h"
#include "tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char g_book_dir[512] = "/home/idiot/MChess8/MChess8/PRBOOK";
static uint32_t g_rng_state = 0;

static uint32_t next_rand(void) {
    if (g_rng_state == 0) {
        g_rng_state = (uint32_t)time(NULL) ^ ((uint32_t)clock() << 16) ^ 0x9e3779b9U;
        if (g_rng_state == 0) g_rng_state = 0x12345678U;
    }
    g_rng_state = g_rng_state * 1664525U + 1013904223U;
    return g_rng_state >> 16;
}

void book_init(const char *dir) {
    if (dir && dir[0]) {
        snprintf(g_book_dir, sizeof(g_book_dir), "%s", dir);
    }
}

static void compute_m80_book_hash(const Board *b, uint8_t h[6]) {
    memset(h, 0, 6);
    int flip = (b->side == BLACK);

    for (int r = 0; r < 8; r++) {
        for (int f = 0; f < 8; f++) {
            int sq = r * 8 + f;
            int p = b->squares[sq];
            if (p == EMPTY) continue;
            int sq_use = sq;
            int p_use = p;
            if (flip) {
                p_use = (p < 8) ? (p + 7) : (p - 7);
                sq_use = (7 - r) * 8 + f;
            }
            uint16_t off = (uint16_t)(sq_to_zob_off[sq64_to_120(sq_use)] + p_use * 6);
            for (int k = 0; k < 6; k++) {
                h[k] ^= zobrist_table[off + k];
            }
        }
    }

    /* Castling rights: XOR when LOST or rook missing */
    int wk_lost = (b->castling[0] != 0) || (b->squares[7] != WR);
    int wq_lost = (b->castling[1] != 0) || (b->squares[0] != WR);
    int bk_lost = (b->castling[2] != 0) || (b->squares[63] != BR);
    int bq_lost = (b->castling[3] != 0) || (b->squares[56] != BR);

    int c_lost[4];
    if (!flip) {
        c_lost[0] = wk_lost; c_lost[1] = wq_lost;
        c_lost[2] = bk_lost; c_lost[3] = bq_lost;
    } else {
        c_lost[0] = bk_lost; c_lost[1] = bq_lost;
        c_lost[2] = wk_lost; c_lost[3] = wq_lost;
    }

    for (int i = 0; i < 4; i++) {
        if (c_lost[i]) {
            const uint8_t *cz = &dgroup_rom[0x2a76 + i * 6];
            for (int k = 0; k < 6; k++) h[k] ^= cz[k];
        }
    }

    if (b->ep_sq >= 0) {
        uint8_t ep_taker = (b->side == WHITE) ? WP : BP;
        if (BB_PAWN_ATK[b->side ^ 1][b->ep_sq] & b->bb[ep_taker]) {
            int ef = (b->ep_sq & 7) + 1; /* 1..8 */
            const uint8_t *ez = &dgroup_rom[0x2a90 + ef * 6];
            for (int k = 0; k < 6; k++) h[k] ^= ez[k];
        }
    }
}

typedef struct {
    Move m;
    int wt;
} BookCand;

bool book_probe(const Board *b, const EngineSettings *cfg, Move *out_move) {
    if (cfg->book_mode <= 0) return false;

    uint8_t h[6];
    compute_m80_book_hash(b, h);

    int file_idx = (h[5] >> 4) & 0x0f;
    int sub_idx  = h[5] & 0x0f;

    char key_path[600];
    snprintf(key_path, sizeof(key_path), "%s/M80KEY.%03X", g_book_dir, file_idx);
    FILE *fk = fopen(key_path, "rb");
    if (!fk) return false;

    if (fseek(fk, 0x10 + sub_idx * 10, SEEK_SET) != 0) { fclose(fk); return false; }
    uint8_t hdr[10];
    if (fread(hdr, 1, 10, fk) != 10) { fclose(fk); return false; }

    uint32_t prbook_base = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                           ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    uint32_t key_off     = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                           ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    uint16_t cnt         = (uint16_t)hdr[8] | ((uint16_t)hdr[9] << 8);

    if (cnt == 0) { fclose(fk); return false; }

    int lo = 0, hi = (int)cnt - 1;
    uint8_t rec[8];
    bool found = false;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (fseek(fk, (long)(key_off + (uint32_t)mid * 8), SEEK_SET) != 0) break;
        if (fread(rec, 1, 8, fk) != 8) break;
        int cmp = memcmp(rec, h, 5);
        if (cmp == 0) { found = true; break; }
        else if (cmp < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    fclose(fk);
    if (!found) return false;

    uint16_t rel_off = (uint16_t)rec[6] | ((uint16_t)rec[7] << 8);
    uint32_t byte_pos = (prbook_base + rel_off) * 2;

    char pr_path[600];
    snprintf(pr_path, sizeof(pr_path), "%s/PRBOOK.M80", g_book_dir);
    FILE *fp = fopen(pr_path, "rb");
    if (!fp) return false;
    if (fseek(fp, (long)byte_pos, SEEK_SET) != 0) { fclose(fp); return false; }

    uint16_t buf[64];
    size_t nwords = fread(buf, 2, 64, fp);
    fclose(fp);
    if (nwords == 0) return false;

    Move legal[MAX_MOVES];
    int nlegal = gen_legal_moves((Board *)b, legal);

    BookCand cands[64];
    int ncands = 0;
    int max_wt = -1;
    uint16_t promo_prefix = 0;

    for (size_t i = 0; i < nwords; i++) {
        uint16_t w = buf[i];
        uint16_t fr6 = w & 0x3f;
        uint16_t to6 = (w >> 6) & 0x3f;
        if (promo_prefix == 0 && fr6 == to6) {
            if (to6 >= 2 && to6 <= 4) {
                promo_prefix = to6;
                continue;
            } else {
                break;
            }
        }
        int wt = (w >> 12) & 7;
        int last = (w >> 15) & 1;

        int r1 = fr6 >> 3, f1 = fr6 & 7;
        int r2 = to6 >> 3, f2 = to6 & 7;
        if (b->side == BLACK) {
            r1 = 7 - r1;
            r2 = 7 - r2;
        }
        int sq1 = r1 * 8 + f1;
        int sq2 = r2 * 8 + f2;
        int want_promo = (promo_prefix >= 2 && promo_prefix <= 4) ? (int)promo_prefix : 0;
        promo_prefix = 0;

        for (int j = 0; j < nlegal; j++) {
            if (legal[j].from == sq1 && legal[j].to == sq2) {
                int sp = MOVE_SPEC(legal[j]);
                if (sp >= SPEC_PROMO_N && sp <= SPEC_PROMO_Q) {
                    int expected = want_promo ? want_promo : SPEC_PROMO_Q;
                    if (sp != expected) continue;
                }
                if (ncands < 64) {
                    cands[ncands].m = legal[j];
                    cands[ncands].wt = wt;
                    if (wt > max_wt) max_wt = wt;
                    ncands++;
                }
                break;
            }
        }
        if (last) break;
    }

    if (ncands == 0) return false;

    /* Filter by book_mode */
    int min_wt = 0;
    if (cfg->book_mode == 1) min_wt = (max_wt >= 4) ? 4 : max_wt; /* Tournament */
    else if (cfg->book_mode == 2) min_wt = (max_wt >= 1) ? 1 : 0; /* Wide */
    else min_wt = 0;                                               /* Broad */

    if (cfg->deterministic_book) {
        int best_idx = 0;
        for (int i = 1; i < ncands; i++) {
            if (cands[i].wt > cands[best_idx].wt) best_idx = i;
        }
        *out_move = cands[best_idx].m;
        return true;
    }

    int total_w = 0;
    for (int i = 0; i < ncands; i++) {
        if (cands[i].wt >= min_wt) total_w += (cands[i].wt + 1);
    }
    if (total_w == 0) {
        *out_move = cands[0].m;
        return true;
    }
    int r = (int)(next_rand() % (uint32_t)total_w);
    int acc = 0;
    for (int i = 0; i < ncands; i++) {
        if (cands[i].wt >= min_wt) {
            acc += (cands[i].wt + 1);
            if (r < acc) {
                *out_move = cands[i].m;
                return true;
            }
        }
    }
    *out_move = cands[0].m;
    return true;
}
