#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <omp.h>
#include "tables.h"

enum { EMPTY=0, WP=1, WN=2, WB=3, WR=4, WQ=5, WK=6, BORDER=7, BP=8, BN=9, BB=10, BR=11, BQ=12, BK=13 };

#define VAL_ILLEGAL 32767
#define VAL_DRAW    32766

static const int knight_off[8] = { -21, -19, -12, -8, 8, 12, 19, 21 };
static const int king_off[8]   = { -11, -10, -9, -1, 1, 9, 10, 11 };
static const int diag_off[4]   = { -11, -9, 9, 11 };
static const int ortho_off[4]  = { -10, -1, 1, 10 };

typedef struct {
    int n_w, n_b;
    int pt_w[4], pt_b[4];
    int n_total;
    int has_pawns;
    char name[8];
} TBSpec;

/* In-memory sub-TB cache */
typedef struct {
    char key[32];
    int8_t *data;
    uint32_t size;
} CachedTB;

static CachedTB g_cache[128];
static int g_cache_cnt = 0;

static const int8_t *get_cached_tb(const char *tb_dir, const char *name, const char *ext, uint32_t *out_sz) {
    char key[32];
    snprintf(key, sizeof(key), "%s.%s", name, ext);
    for (int i = 0; i < g_cache_cnt; i++) {
        if (strcmp(g_cache[i].key, key) == 0) {
            *out_sz = g_cache[i].size;
            return g_cache[i].data;
        }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.%s", tb_dir, name, ext);
    FILE *f = fopen(path, "rb");
    int8_t *buf = NULL;
    uint32_t sz = 0;
    if (f) {
        fseek(f, 0, SEEK_END);
        sz = (uint32_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = (int8_t *)malloc(sz);
        if (buf && fread(buf, 1, sz, f) != sz) { free(buf); buf = NULL; sz = 0; }
        fclose(f);
    }
    if (buf && g_cache_cnt < 128) {
        snprintf(g_cache[g_cache_cnt].key, 32, "%s", key);
        g_cache[g_cache_cnt].data = buf;
        g_cache[g_cache_cnt].size = sz;
        g_cache_cnt++;
    }
    *out_sz = sz;
    return buf;
}

static void preload_sub_tbs(const char *tb_dir) {
    const char *names[] = {
        "KQK","KRK","KBK","KNK","KPK",
        "KQQK","KQRK","KQBK","KQNK","KRRK","KRBK","KRNK","KBBK","KBNK","KNNK",
        "KQKQ","KQKR","KQKB","KQKN","KRKR","KRKB","KRKN","KBKB","KBKN","KNKN",
        NULL
    };
    for (int i = 0; names[i]; i++) {
        uint32_t sz;
        get_cached_tb(tb_dir, names[i], "TBW", &sz);
        get_cached_tb(tb_dir, names[i], "TBB", &sz);
    }
}

static int parse_pt(char c) {
    switch (c) {
        case 'P': return 1; case 'N': return 2; case 'B': return 3;
        case 'R': return 4; case 'Q': return 5; case 'K': return 6;
        default: return 0;
    }
}

static TBSpec parse_spec(const char *name) {
    TBSpec s;
    memset(&s, 0, sizeof(s));
    snprintf(s.name, sizeof(s.name), "%s", name);
    int k_seen = 0;
    for (int i = 0; name[i]; i++) {
        int pt = parse_pt(name[i]);
        if (pt == 1) s.has_pawns = 1;
        if (pt == 6) {
            k_seen++;
            if (k_seen == 1) s.pt_w[s.n_w++] = pt;
            else s.pt_b[s.n_b++] = pt;
        } else {
            if (k_seen == 1) s.pt_w[s.n_w++] = pt;
            else s.pt_b[s.n_b++] = pt;
        }
    }
    s.n_total = s.n_w + s.n_b;
    return s;
}

static uint32_t tb_size(const TBSpec *s) {
    uint32_t sz = s->has_pawns ? 32 : 10;
    for (int i = 0; i < s->n_total - 1; i++) sz *= 64;
    return sz;
}

static inline int sq64_to_120(int sq64) {
    return 0x15 + (sq64 >> 3) * 10 + (sq64 & 7);
}

static bool is_attacked(const uint8_t *brd, int sq, int by_black) {
    if (!by_black) {
        if (brd[sq - 9] == WP || brd[sq - 11] == WP) return true;
        for (int i = 0; i < 8; i++) {
            if (brd[sq + knight_off[i]] == WN) return true;
            if (brd[sq + king_off[i]] == WK) return true;
        }
        for (int i = 0; i < 4; i++) {
            int d = diag_off[i], s = sq + d;
            while (brd[s] == EMPTY) s += d;
            if (brd[s] == WB || brd[s] == WQ) return true;
            d = ortho_off[i]; s = sq + d;
            while (brd[s] == EMPTY) s += d;
            if (brd[s] == WR || brd[s] == WQ) return true;
        }
    } else {
        if (brd[sq + 9] == BP || brd[sq + 11] == BP) return true;
        for (int i = 0; i < 8; i++) {
            if (brd[sq + knight_off[i]] == BN) return true;
            if (brd[sq + king_off[i]] == BK) return true;
        }
        for (int i = 0; i < 4; i++) {
            int d = diag_off[i], s = sq + d;
            while (brd[s] == EMPTY) s += d;
            if (brd[s] == BB || brd[s] == BQ) return true;
            d = ortho_off[i]; s = sq + d;
            while (brd[s] == EMPTY) s += d;
            if (brd[s] == BR || brd[s] == BQ) return true;
        }
    }
    return false;
}

static const char pc_letters[7] = { ' ', 'P', 'N', 'B', 'R', 'Q', 'K' };

static int probe_tb_cached(const char *tb_dir, const uint8_t *brd, int side, int8_t *out_v) {
    (void)tb_dir;
    uint8_t w_sq[4], b_sq[4];
    int w_pt[4], b_pt[4], nw = 0, nb = 0, total_pawns = 0;
    for (int pt = 6; pt >= 1; pt--) {
        for (int sq = 0x15; sq <= 0x62; sq++) {
            uint8_t p = brd[sq];
            if (p == pt && nw < 4) {
                w_sq[nw] = (uint8_t)(((board_rank[sq] - 2) << 3) | (board_file[sq] - 1));
                w_pt[nw++] = pt;
                if (pt == 1) total_pawns++;
            } else if (p == pt + 7 && nb < 4) {
                b_sq[nb] = (uint8_t)(((board_rank[sq] - 2) << 3) | (board_file[sq] - 1));
                b_pt[nb++] = pt;
                if (pt == 1) total_pawns++;
            }
        }
    }
    if (nw + nb <= 2) { *out_v = 0; return 1; }
    if (nw + nb == 3 && total_pawns == 0 && w_pt[1] <= 3 && nw == 2 && nb == 1) { *out_v = 0; return 1; }
    if (nw + nb == 3 && total_pawns == 0 && b_pt[1] <= 3 && nb == 2 && nw == 1) { *out_v = 0; return 1; }

    int flipped = 0;
    if (nb > nw) flipped = 1;
    else if (nb == nw) {
        for (int i = 1; i < nw; i++) {
            if (b_pt[i] > w_pt[i]) { flipped = 1; break; }
            if (b_pt[i] < w_pt[i]) { flipped = 0; break; }
        }
    }
    char name[16];
    int sq64[4], ptype[4], n = 0;
    if (!flipped) {
        for (int i = 0; i < nw; i++) { name[n] = pc_letters[w_pt[i]]; sq64[n] = w_sq[i]; ptype[n] = w_pt[i]; n++; }
        for (int i = 0; i < nb; i++) { name[n] = pc_letters[b_pt[i]]; sq64[n] = b_sq[i]; ptype[n] = b_pt[i]; n++; }
    } else {
        for (int i = 0; i < nb; i++) { name[n] = pc_letters[b_pt[i]]; sq64[n] = b_sq[i] ^ 0x38; ptype[n] = b_pt[i]; n++; }
        for (int i = 0; i < nw; i++) { name[n] = pc_letters[w_pt[i]]; sq64[n] = w_sq[i] ^ 0x38; ptype[n] = w_pt[i]; n++; }
    }
    name[n] = '\0';
    int base[4] = {64,64,64,64};
    if (total_pawns) {
        int pidx = n - 1;
        while (pidx >= 1 && ptype[pidx] != 1) pidx--;
        if ((sq64[pidx] & 7) >= 4) for (int i = 0; i < n; i++) sq64[i] ^= 7;
        base[pidx] = 32;
        sq64[pidx] = ((sq64[pidx] >> 3) << 2) | (sq64[pidx] & 3);
    } else {
        int kidx = n - 1;
        if (sq64[kidx] >= 32) for (int i = 0; i < n; i++) sq64[i] ^= 0x38;
        if ((sq64[kidx] & 7) >= 4) for (int i = 0; i < n; i++) sq64[i] ^= 7;
        if ((sq64[kidx] >> 3) > (sq64[kidx] & 7)) {
            for (int i = 0; i < n; i++) sq64[i] = ((sq64[i] & 7) << 3) | (sq64[i] >> 3);
        }
        base[kidx] = 10;
        sq64[kidx] = tb_tri[sq64[kidx]];
    }
    uint32_t idx = 0;
    for (int i = n - 1; i >= 1; i--) idx = (idx + (uint32_t)sq64[i]) * (uint32_t)base[i - 1];
    idx += (uint32_t)sq64[0];

    const char *ext = (side == flipped) ? "TBW" : "TBB";
    char key[32];
    snprintf(key, sizeof(key), "%s.%s", name, ext);
    for (int i = 0; i < g_cache_cnt; i++) {
        if (strcmp(g_cache[i].key, key) == 0 && g_cache[i].data && idx < g_cache[i].size) {
            int8_t v = g_cache[i].data[idx];
            *out_v = v;
            return (v >= -126 && v <= 126);
        }
    }
    return 0;
}

static uint32_t same_tb_index(const TBSpec *s, const int *w_sq64, const int *b_sq64) {
    int sq64[4], ptype[4], n = 0;
    for (int i = 0; i < s->n_w; i++) { sq64[n] = w_sq64[i]; ptype[n] = s->pt_w[i]; n++; }
    for (int i = 0; i < s->n_b; i++) { sq64[n] = b_sq64[i]; ptype[n] = s->pt_b[i]; n++; }

    int base[4] = {64,64,64,64};
    if (s->has_pawns) {
        int pidx = n - 1;
        while (pidx >= 1 && ptype[pidx] != 1) pidx--;
        if ((sq64[pidx] & 7) >= 4) for (int i = 0; i < n; i++) sq64[i] ^= 7;
        base[pidx] = 32;
        sq64[pidx] = ((sq64[pidx] >> 3) << 2) | (sq64[pidx] & 3);
    } else {
        int kidx = n - 1;
        if (sq64[kidx] >= 32) for (int i = 0; i < n; i++) sq64[i] ^= 0x38;
        if ((sq64[kidx] & 7) >= 4) for (int i = 0; i < n; i++) sq64[i] ^= 7;
        if ((sq64[kidx] >> 3) > (sq64[kidx] & 7)) {
            for (int i = 0; i < n; i++) sq64[i] = ((sq64[i] & 7) << 3) | (sq64[i] >> 3);
        }
        base[kidx] = 10;
        sq64[kidx] = tb_tri[sq64[kidx]];
    }
    uint32_t idx = 0;
    for (int i = n - 1; i >= 1; i--) idx = (idx + (uint32_t)sq64[i]) * (uint32_t)base[i - 1];
    idx += (uint32_t)sq64[0];
    return idx;
}

static const int tri_to_sq64[10] = { 0, 1, 2, 3, 9, 10, 11, 18, 19, 27 };

static void decode_index(const TBSpec *s, uint32_t idx, int *sq64_out) {
    int n = s->n_total;
    int pidx = -1;
    if (s->has_pawns) {
        pidx = n - 1;
        while (pidx >= 1) {
            int pt = (pidx < s->n_w) ? s->pt_w[pidx] : s->pt_b[pidx - s->n_w];
            if (pt == 1) break;
            pidx--;
        }
    }
    for (int i = 0; i < n; i++) {
        int b = (s->has_pawns && i == pidx) ? 32 : ((!s->has_pawns && i == n - 1) ? 10 : 64);
        int val = (int)(idx % (uint32_t)b);
        idx /= (uint32_t)b;
        if (s->has_pawns && i == pidx) {
            sq64_out[i] = ((val >> 2) << 3) | (val & 3);
        } else if (!s->has_pawns && i == n - 1) {
            sq64_out[i] = tri_to_sq64[val];
        } else {
            sq64_out[i] = val;
        }
    }
}

static inline int8_t encode_v(int16_t pv) {
    if (pv == VAL_ILLEGAL) return 127;
    if (pv == 0 || pv == VAL_DRAW) return 0;
    if (pv > 0) {
        int moves = (pv + 1) / 2;
        int v = 127 - moves;
        return (int8_t)(v < 1 ? 1 : v);
    } else {
        int plies = -pv - 1;
        int moves = plies / 2;
        int v = -(126 - moves);
        return (int8_t)(v > -1 ? -1 : v);
    }
}

typedef struct { int from, to, promo; } GenMove;

static int gen_moves_120(const uint8_t *brd, int side, GenMove *out) {
    int cnt = 0;
    for (int sq = 0x15; sq <= 0x62; sq++) {
        uint8_t p = brd[sq];
        if (p == EMPTY || p == BORDER) continue;
        if (side == 0 && p > WK) continue;
        if (side == 1 && p < BP) continue;
        int pt = (side == 0) ? p : (p - 7);
        if (pt == 1) {
            int dir = (side == 0) ? 10 : -10;
            int rank = board_rank[sq] - 2;
            int pr = (side == 0) ? 6 : 1;
            int fwd = sq + dir;
            if (brd[fwd] == EMPTY) {
                if (rank == pr) {
                    for (int k = 5; k >= 2; k--) out[cnt++] = (GenMove){sq, fwd, k};
                } else {
                    out[cnt++] = (GenMove){sq, fwd, 0};
                    if (rank == (side == 0 ? 1 : 6) && brd[fwd + dir] == EMPTY)
                        out[cnt++] = (GenMove){sq, fwd + dir, 0};
                }
            }
            int caps[2] = { sq + dir - 1, sq + dir + 1 };
            for (int c = 0; c < 2; c++) {
                uint8_t t = brd[caps[c]];
                if (t != BORDER && t != EMPTY && ((side == 0 && t >= BP) || (side == 1 && t <= WK))) {
                    if (rank == pr) {
                        for (int k = 5; k >= 2; k--) out[cnt++] = (GenMove){sq, caps[c], k};
                    } else {
                        out[cnt++] = (GenMove){sq, caps[c], 0};
                    }
                }
            }
        } else if (pt == 2) {
            for (int i = 0; i < 8; i++) {
                int to = sq + knight_off[i]; uint8_t t = brd[to];
                if (t != BORDER && (t == EMPTY || (side == 0 ? t >= BP : t <= WK)))
                    out[cnt++] = (GenMove){sq, to, 0};
            }
        } else if (pt == 3 || pt == 4 || pt == 5) {
            const int *dirs = (pt == 3) ? diag_off : (pt == 4 ? ortho_off : king_off);
            int nd = (pt == 5) ? 8 : 4;
            for (int i = 0; i < nd; i++) {
                int d = dirs[i];
                for (int to = sq + d; brd[to] != BORDER; to += d) {
                    uint8_t t = brd[to];
                    if (t == EMPTY) out[cnt++] = (GenMove){sq, to, 0};
                    else {
                        if (side == 0 ? t >= BP : t <= WK) out[cnt++] = (GenMove){sq, to, 0};
                        break;
                    }
                }
            }
        } else if (pt == 6) {
            for (int i = 0; i < 8; i++) {
                int to = sq + king_off[i]; uint8_t t = brd[to];
                if (t != BORDER && (t == EMPTY || (side == 0 ? t >= BP : t <= WK)))
                    out[cnt++] = (GenMove){sq, to, 0};
            }
        }
    }
    return cnt;
}

static void init_brd(uint8_t *brd) {
    for (int i = 0; i < 120; i++) {
        int r = (i - 0x15) / 10, f = (i - 0x15) % 10;
        brd[i] = (i >= 0x15 && i <= 0x62 && r >= 0 && r < 8 && f >= 0 && f < 8) ? EMPTY : BORDER;
    }
}

static void generate_tb(const char *tb_dir, const char *name) {
    TBSpec s = parse_spec(name);
    uint32_t N = tb_size(&s);
    printf("Generating %s (N=%u)...\n", name, N);
    fflush(stdout);

    int16_t *val[2];
    val[0] = (int16_t *)malloc(N * sizeof(int16_t));
    val[1] = (int16_t *)malloc(N * sizeof(int16_t));
    if (!val[0] || !val[1]) { fprintf(stderr, "OOM\n"); exit(1); }

    #pragma omp parallel
    {
        uint8_t brd[120];
        init_brd(brd);

        #pragma omp for schedule(dynamic, 4096)
        for (uint32_t idx = 0; idx < N; idx++) {
            int sq64[4];
            decode_index(&s, idx, sq64);
            bool bad = false;
            for (int i = 0; i < s.n_total; i++) {
                int pt = (i < s.n_w) ? s.pt_w[i] : s.pt_b[i - s.n_w];
                if (pt == 1 && ((sq64[i] >> 3) == 0 || (sq64[i] >> 3) == 7)) { bad = true; break; }
                for (int j = i + 1; j < s.n_total; j++) {
                    if (sq64[i] == sq64[j]) { bad = true; break; }
                }
                if (bad) break;
            }
            if (bad) { val[0][idx] = VAL_ILLEGAL; val[1][idx] = VAL_ILLEGAL; continue; }

            int sq120[4];
            for (int i = 0; i < s.n_total; i++) {
                sq120[i] = sq64_to_120(sq64[i]);
                int pt = (i < s.n_w) ? s.pt_w[i] : s.pt_b[i - s.n_w];
                brd[sq120[i]] = (uint8_t)((i < s.n_w) ? pt : (pt + 7));
            }
            int wk_sq = sq120[0], bk_sq = sq120[s.n_w];

            for (int side = 0; side < 2; side++) {
                if (is_attacked(brd, (side == 0) ? bk_sq : wk_sq, side)) {
                    val[side][idx] = VAL_ILLEGAL;
                    continue;
                }
                GenMove mvs[128];
                int nm = gen_moves_120(brd, side, mvs);
                int legal = 0;
                for (int i = 0; i < nm; i++) {
                    uint8_t pc = brd[mvs[i].from], cap = brd[mvs[i].to];
                    brd[mvs[i].from] = EMPTY;
                    brd[mvs[i].to] = mvs[i].promo ? (uint8_t)(side == 0 ? mvs[i].promo : mvs[i].promo + 7) : pc;
                    int ksq = (pc == WK) ? mvs[i].to : (pc == BK ? mvs[i].to : (side == 0 ? wk_sq : bk_sq));
                    bool chk = is_attacked(brd, ksq, side ^ 1);
                    brd[mvs[i].from] = pc; brd[mvs[i].to] = cap;
                    if (!chk) { legal++; break; }
                }
                if (legal == 0) {
                    bool in_chk = is_attacked(brd, (side == 0) ? wk_sq : bk_sq, side ^ 1);
                    val[side][idx] = in_chk ? -1 : VAL_DRAW;
                } else {
                    val[side][idx] = 0;
                }
            }
            for (int i = 0; i < s.n_total; i++) brd[sq120[i]] = EMPTY;
        }
    }

    /* Compact active list of unsolved (idx << 1) | side */
    uint32_t *active = (uint32_t *)malloc(2 * (size_t)N * sizeof(uint32_t));
    uint32_t n_active = 0;
    for (uint32_t idx = 0; idx < N; idx++) {
        if (val[0][idx] == 0) active[n_active++] = (idx << 1);
        if (val[1][idx] == 0) active[n_active++] = (idx << 1) | 1;
    }

    int zero_streak = 0;
    for (int d = 1; d <= 250; d++) {
        int changed = 0;
        for (int pass = 0; pass < 2; pass++) {
            int want_win = (pass == 0);
            #pragma omp parallel reduction(+:changed)
            {
                uint8_t brd[120];
                init_brd(brd);

                #pragma omp for schedule(dynamic, 4096)
                for (uint32_t a = 0; a < n_active; a++) {
                    uint32_t item = active[a];
                    uint32_t idx = item >> 1;
                    int side = (int)(item & 1);
                    if (val[side][idx] != 0) continue;

                    int sq64[4], sq120[4];
                    decode_index(&s, idx, sq64);
                    for (int i = 0; i < s.n_total; i++) {
                        sq120[i] = sq64_to_120(sq64[i]);
                        int pt = (i < s.n_w) ? s.pt_w[i] : s.pt_b[i - s.n_w];
                        brd[sq120[i]] = (uint8_t)((i < s.n_w) ? pt : (pt + 7));
                    }
                    int wk_sq = sq120[0], bk_sq = sq120[s.n_w];

                    GenMove mvs[128];
                    int nm = gen_moves_120(brd, side, mvs);
                    bool found_win = false;
                    bool all_losing = true;

                    for (int i = 0; i < nm; i++) {
                        uint8_t pc = brd[mvs[i].from], cap = brd[mvs[i].to];
                        brd[mvs[i].from] = EMPTY;
                        brd[mvs[i].to] = mvs[i].promo ? (uint8_t)(side == 0 ? mvs[i].promo : mvs[i].promo + 7) : pc;
                        int ksq = (pc == WK) ? mvs[i].to : (pc == BK ? mvs[i].to : (side == 0 ? wk_sq : bk_sq));
                        bool chk = is_attacked(brd, ksq, side ^ 1);
                        if (!chk) {
                            int16_t child_pv = 0;
                            if (cap != EMPTY || mvs[i].promo != 0) {
                                int8_t sv = 0;
                                if (probe_tb_cached(tb_dir, brd, side ^ 1, &sv)) {
                                    if (sv < 0) child_pv = (int16_t)(-1 - (126 + sv) * 2);
                                    else if (sv > 0) child_pv = (int16_t)((127 - sv) * 2 - 1);
                                    else child_pv = VAL_DRAW;
                                }
                            } else {
                                int nw2 = 0, nb2 = 0, w2[4], b2[4];
                                for (int pt = 6; pt >= 1; pt--) {
                                    for (int q = 0x15; q <= 0x62; q++) {
                                        if (brd[q] == pt) w2[nw2++] = ((board_rank[q]-2)<<3)|(board_file[q]-1);
                                        else if (brd[q] == pt+7) b2[nb2++] = ((board_rank[q]-2)<<3)|(board_file[q]-1);
                                    }
                                }
                                uint32_t cidx = same_tb_index(&s, w2, b2);
                                child_pv = val[side ^ 1][cidx];
                            }

                            if (want_win) {
                                if (child_pv < 0 && ((-child_pv - 1) <= d - 1)) {
                                    found_win = true;
                                    brd[mvs[i].from] = pc; brd[mvs[i].to] = cap;
                                    break;
                                }
                            } else {
                                if (!(child_pv > 0 && child_pv < VAL_DRAW && child_pv <= d - 1)) {
                                    all_losing = false;
                                    brd[mvs[i].from] = pc; brd[mvs[i].to] = cap;
                                    break;
                                }
                            }
                        }
                        brd[mvs[i].from] = pc;
                        brd[mvs[i].to] = cap;
                    }

                    for (int i = 0; i < s.n_total; i++) brd[sq120[i]] = EMPTY;

                    if (want_win && found_win) {
                        val[side][idx] = (int16_t)d;
                        changed++;
                    } else if (!want_win && all_losing) {
                        val[side][idx] = (int16_t)(-1 - d);
                        changed++;
                    }
                }
            }
        }
        if (changed == 0) {
            zero_streak++;
            if (zero_streak >= 2) break;
        } else {
            zero_streak = 0;
            uint32_t w = 0;
            for (uint32_t a = 0; a < n_active; a++) {
                uint32_t item = active[a];
                if (val[item & 1][item >> 1] == 0) active[w++] = item;
            }
            n_active = w;
        }
    }
    free(active);

    int8_t *out = (int8_t *)malloc(N);
    for (int side = 0; side < 2; side++) {
        for (uint32_t idx = 0; idx < N; idx++) out[idx] = encode_v(val[side][idx]);
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.%s", tb_dir, name, side == 0 ? "TBW" : "TBB");
        FILE *f = fopen(path, "wb");
        fwrite(out, 1, N, f);
        fclose(f);
        /* Refresh cache for newly generated TB */
        preload_sub_tbs(tb_dir);
    }
    printf("  Done %s\n", name);
    fflush(stdout);
    free(out);
    free(val[0]);
    free(val[1]);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <tb_dir> <TB_NAME> [...]\n", argv[0]);
        return 1;
    }
    preload_sub_tbs(argv[1]);
    for (int i = 2; i < argc; i++) {
        generate_tb(argv[1], argv[i]);
    }
    return 0;
}
