#include "mchess8.h"
#include "bitboard.h"
#include "tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Exact M80 book hash from book.c */
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
            int ef = (b->ep_sq & 7) + 1;
            const uint8_t *ez = &dgroup_rom[0x2a90 + ef * 6];
            for (int k = 0; k < 6; k++) h[k] ^= ez[k];
        }
    }
}

typedef struct {
    uint16_t fr6;
    uint16_t to6;
    uint8_t wt;
    uint8_t promo;
} MoveEntry;

typedef struct {
    uint8_t h[6];
    MoveEntry *moves;
    int nmoves;
    int cap;
    uint32_t word_off;
} PosEntry;

#define HT_SIZE (1U << 20)
#define HT_MASK (HT_SIZE - 1U)
static int *g_ht = NULL;
static PosEntry *g_pos = NULL;
static int g_npos = 0;
static int g_cap = 0;

static uint32_t hash6(const uint8_t h[6]) {
    uint64_t v = 0;
    memcpy(&v, h, 6);
    v ^= v >> 30;
    v *= 0xbf58476d1ce4e5b9ULL;
    v ^= v >> 27;
    return (uint32_t)(v & HT_MASK);
}

static int find_or_add_pos(const uint8_t h[6]) {
    uint32_t idx = hash6(h);
    while (g_ht[idx] >= 0) {
        int pi = g_ht[idx];
        if (memcmp(g_pos[pi].h, h, 6) == 0) return pi;
        idx = (idx + 1) & HT_MASK;
    }
    if (g_npos == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 16384;
        g_pos = (PosEntry *)realloc(g_pos, (size_t)g_cap * sizeof(PosEntry));
    }
    int pi = g_npos++;
    PosEntry *pe = &g_pos[pi];
    memcpy(pe->h, h, 6);
    pe->moves = NULL;
    pe->nmoves = 0;
    pe->cap = 0;
    pe->word_off = 0;
    g_ht[idx] = pi;
    return pi;
}

static void add_move(const Board *b, Move m, int wt) {
    uint8_t h[6];
    compute_m80_book_hash(b, h);
    int r1 = m.from >> 3, f1 = m.from & 7;
    int r2 = m.to >> 3, f2 = m.to & 7;
    if (b->side == BLACK) {
        r1 = 7 - r1;
        r2 = 7 - r2;
    }
    uint16_t fr6 = (uint16_t)(r1 * 8 + f1);
    uint16_t to6 = (uint16_t)(r2 * 8 + f2);
    int sp = MOVE_SPEC(m);
    uint8_t promo = (sp >= SPEC_PROMO_N && sp <= SPEC_PROMO_R) ? (uint8_t)sp : 0;

    int pidx = find_or_add_pos(h);
    PosEntry *pe = &g_pos[pidx];
    for (int i = 0; i < pe->nmoves; i++) {
        if (pe->moves[i].fr6 == fr6 && pe->moves[i].to6 == to6 && pe->moves[i].promo == promo) {
            if (wt > pe->moves[i].wt) pe->moves[i].wt = (uint8_t)wt;
            return;
        }
    }
    if (pe->nmoves == pe->cap) {
        pe->cap = pe->cap ? pe->cap * 2 : 4;
        pe->moves = (MoveEntry *)realloc(pe->moves, (size_t)pe->cap * sizeof(MoveEntry));
    }
    pe->moves[pe->nmoves++] = (MoveEntry){fr6, to6, (uint8_t)wt, promo};
}

static int cmp_pos(const void *a, const void *b) {
    const PosEntry *pa = (const PosEntry *)a;
    const PosEntry *pb = (const PosEntry *)b;
    if (pa->h[5] != pb->h[5]) return (int)pa->h[5] - (int)pb->h[5];
    return memcmp(pa->h, pb->h, 5);
}

static int cmp_move_desc(const void *a, const void *b) {
    const MoveEntry *ma = (const MoveEntry *)a;
    const MoveEntry *mb = (const MoveEntry *)b;
    return (int)mb->wt - (int)ma->wt;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <entries.txt> <out_dir>\n", argv[0]);
        return 1;
    }
    bb_init();
    g_ht = (int *)malloc(HT_SIZE * sizeof(int));
    for (uint32_t i = 0; i < HT_SIZE; i++) g_ht[i] = -1;

    FILE *fin = fopen(argv[1], "r");
    if (!fin) { perror("fopen entries"); return 1; }

    char line[4096];
    int added = 0;
    while (fgets(line, sizeof(line), fin)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        if (p[0] == 'F' && p[1] == ' ') {
            /* Format: F <wt> <uci_move> <fen...> */
            char *save = NULL;
            strtok_r(p, " \t\r\n", &save);
            char *wt_s = strtok_r(NULL, " \t\r\n", &save);
            char *mv_s = strtok_r(NULL, " \t\r\n", &save);
            if (!wt_s || !mv_s || !save) continue;
            int wt = atoi(wt_s);
            if (wt < 0) wt = 0;
            if (wt > 7) wt = 7;
            Board b;
            board_set_fen(&b, save);
            Move m;
            if (uci_to_move(&b, mv_s, &m)) {
                add_move(&b, m, wt);
                added++;
            }
        } else {
            /* Format: <wt> <uci_moves...> */
            char *save = NULL;
            char *wt_s = strtok_r(p, " \t\r\n", &save);
            if (!wt_s) continue;
            int wt = atoi(wt_s);
            if (wt < 0) wt = 0;
            if (wt > 7) wt = 7;
            Board b;
            board_init_startpos(&b);
            char *tok;
            while ((tok = strtok_r(NULL, " \t\r\n", &save)) != NULL) {
                Move m;
                if (!uci_to_move(&b, tok, &m)) break;
                add_move(&b, m, wt);
                make_move(&b, m);
                added++;
            }
        }
    }
    fclose(fin);

    qsort(g_pos, (size_t)g_npos, sizeof(PosEntry), cmp_pos);

    char pr_path[600];
    snprintf(pr_path, sizeof(pr_path), "%s/PRBOOK.M80", argv[2]);
    FILE *fp = fopen(pr_path, "wb");
    if (!fp) { perror("fopen PRBOOK.M80"); return 1; }

    uint32_t cur_word = 0;
    for (int i = 0; i < g_npos; i++) {
        PosEntry *pe = &g_pos[i];
        qsort(pe->moves, (size_t)pe->nmoves, sizeof(MoveEntry), cmp_move_desc);
        pe->word_off = cur_word;
        for (int j = 0; j < pe->nmoves; j++) {
            MoveEntry *me = &pe->moves[j];
            if (me->promo >= 2 && me->promo <= 4) {
                uint16_t pw = (uint16_t)(me->promo | (me->promo << 6));
                fwrite(&pw, 2, 1, fp);
                cur_word++;
            }
            uint16_t last = (j == pe->nmoves - 1) ? 1 : 0;
            uint16_t w = (uint16_t)(me->fr6 | (me->to6 << 6) | ((uint16_t)me->wt << 12) | (last << 15));
            fwrite(&w, 2, 1, fp);
            cur_word++;
        }
    }
    fclose(fp);

    for (int fidx = 0; fidx < 16; fidx++) {
        char kpath[600];
        snprintf(kpath, sizeof(kpath), "%s/M80KEY.%03X", argv[2], fidx);
        FILE *fk = fopen(kpath, "wb");
        if (!fk) { perror("fopen M80KEY"); return 1; }

        uint8_t hdr_zero[16] = {0};
        fwrite(hdr_zero, 1, 16, fk);

        uint8_t sub_hdr[16 * 10];
        memset(sub_hdr, 0, sizeof(sub_hdr));
        uint32_t key_off = 16 + 16 * 10;

        int start_i[16], end_i[16];
        for (int s = 0; s < 16; s++) {
            uint8_t target = (uint8_t)((fidx << 4) | s);
            int lo = 0;
            while (lo < g_npos && g_pos[lo].h[5] < target) lo++;
            int hi = lo;
            while (hi < g_npos && g_pos[hi].h[5] == target) hi++;
            start_i[s] = lo;
            end_i[s] = hi;
            int cnt = hi - lo;
            uint32_t base = (cnt > 0) ? g_pos[lo].word_off : 0;
            uint8_t *sh = &sub_hdr[s * 10];
            sh[0] = (uint8_t)(base & 0xff); sh[1] = (uint8_t)((base >> 8) & 0xff);
            sh[2] = (uint8_t)((base >> 16) & 0xff); sh[3] = (uint8_t)((base >> 24) & 0xff);
            sh[4] = (uint8_t)(key_off & 0xff); sh[5] = (uint8_t)((key_off >> 8) & 0xff);
            sh[6] = (uint8_t)((key_off >> 16) & 0xff); sh[7] = (uint8_t)((key_off >> 24) & 0xff);
            sh[8] = (uint8_t)(cnt & 0xff); sh[9] = (uint8_t)((cnt >> 8) & 0xff);
            key_off += (uint32_t)cnt * 8;
        }
        fwrite(sub_hdr, 1, sizeof(sub_hdr), fk);

        for (int s = 0; s < 16; s++) {
            uint32_t base = (end_i[s] > start_i[s]) ? g_pos[start_i[s]].word_off : 0;
            for (int i = start_i[s]; i < end_i[s]; i++) {
                uint8_t rec[8];
                memcpy(rec, g_pos[i].h, 5);
                rec[5] = 0;
                uint16_t rel = (uint16_t)(g_pos[i].word_off - base);
                rec[6] = (uint8_t)(rel & 0xff);
                rec[7] = (uint8_t)((rel >> 8) & 0xff);
                fwrite(rec, 1, 8, fk);
            }
        }
        fclose(fk);
    }

    printf("Built M80 book in %s: %d unique positions, %d entries, %u words\n",
           argv[2], g_npos, added, cur_word);
    return 0;
}
