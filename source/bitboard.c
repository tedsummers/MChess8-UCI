#include "bitboard.h"
#include <stdlib.h>

uint64_t BB_PAWN_ATK[2][64];
uint64_t BB_KNIGHT_ATK[64];
uint64_t BB_KING_ATK[64];
uint64_t BB_ROOK_MASK[64];
uint64_t BB_BISHOP_MASK[64];
uint64_t BB_ROOK_ATK[64][4096];
uint64_t BB_BISHOP_ATK[64][512];

uint64_t BB_FILE_MASK[8];
uint64_t BB_RANK_MASK[8];
uint64_t BB_ADJ_FILES[8];
uint64_t BB_PASSED_MASK[2][64];
uint8_t  BB_CHEBY_DIST[64][64];
uint8_t  BB_MANHATTAN_DIST[64][64];

const uint8_t BB_ROOK_SHIFT[64] = {
    52, 53, 53, 53, 53, 53, 53, 52,
    53, 54, 54, 54, 54, 54, 54, 53,
    53, 54, 54, 54, 54, 54, 54, 53,
    53, 54, 54, 54, 54, 54, 54, 53,
    53, 54, 54, 54, 54, 54, 54, 53,
    53, 54, 54, 54, 54, 54, 54, 53,
    53, 54, 54, 54, 54, 54, 54, 53,
    52, 53, 53, 53, 53, 53, 53, 52
};

const uint8_t BB_BISHOP_SHIFT[64] = {
    58, 59, 59, 59, 59, 59, 59, 58,
    59, 59, 59, 59, 59, 59, 59, 59,
    59, 59, 57, 57, 57, 57, 59, 59,
    59, 59, 57, 55, 55, 57, 59, 59,
    59, 59, 57, 55, 55, 57, 59, 59,
    59, 59, 57, 57, 57, 57, 59, 59,
    59, 59, 59, 59, 59, 59, 59, 59,
    58, 59, 59, 59, 59, 59, 59, 58
};

const uint64_t BB_ROOK_MAGIC[64] = {
    0x8a80104000800020ULL, 0x140002000100040ULL, 0x2801880a0017001ULL, 0x100081001000420ULL,
    0x200020010080420ULL,  0x3001c0002010008ULL, 0x8480008002000100ULL, 0x2080088004402900ULL,
    0x800098204000ULL,     0x2024401000200040ULL, 0x100802000801000ULL, 0x120800800801000ULL,
    0x208808088000400ULL,  0x2802200800400ULL,   0x2200800100020080ULL, 0x801000060821100ULL,
    0x80044006422000ULL,   0x100808020004000ULL, 0x12108a0010204200ULL, 0x140848010000802ULL,
    0x481828014002800ULL,  0x8094004002004100ULL, 0x4010040010010802ULL, 0x20008806104ULL,
    0x100400080208000ULL,  0x2040002120081000ULL, 0x21200680100081ULL,  0x20100080080080ULL,
    0x2000a00200410ULL,    0x20080800400ULL,     0x80088400100102ULL,   0x80004600042881ULL,
    0x4040008040800020ULL, 0x440003000200801ULL, 0x4200011004500ULL,    0x188020010100100ULL,
    0x14800401802800ULL,   0x2080040080800200ULL, 0x124080204001001ULL, 0x200046502000484ULL,
    0x480400080088020ULL,  0x1000422010034000ULL, 0x30200100110040ULL,  0x100021010009ULL,
    0x2002080100110004ULL, 0x202008004008002ULL,  0x20020004010100ULL,   0x2048440040820001ULL,
    0x101002200408200ULL,  0x40802000401080ULL,   0x4008142004410100ULL, 0x2060820c0120200ULL,
    0x1001004080100ULL,    0x20c020080040080ULL,  0x2935610830022400ULL, 0x44440041009200ULL,
    0x280001040802101ULL,  0x2100190040002085ULL, 0x80c0084100102001ULL, 0x4024081001000421ULL,
    0x20030a0244872ULL,    0x12001008414402ULL,   0x2006104900a0804ULL,  0x1004081002402ULL
};

const uint64_t BB_BISHOP_MAGIC[64] = {
    0x40040844404084ULL,   0x2004208a004208ULL,   0x10190041080202ULL,   0x108060845042010ULL,
    0x581104180800210ULL,  0x2112080446200010ULL, 0x1080820820060210ULL, 0x3c0808410220200ULL,
    0x4050404440404ULL,    0x21001420088ULL,      0x24d0080801082102ULL, 0x1020a0a020400ULL,
    0x40308200402ULL,      0x4011002100800ULL,    0x401484104104005ULL,  0x801010402020200ULL,
    0x400210c3880100ULL,   0x404022024108200ULL,  0x810018200204102ULL,  0x4002801a02003ULL,
    0x85040820080400ULL,   0x810102c808880400ULL, 0xe900410884800ULL,    0x8002020480840102ULL,
    0x220200865090201ULL,  0x2010100a02021202ULL, 0x152048408022401ULL,  0x20080002081110ULL,
    0x4001001021004000ULL, 0x800040400a011002ULL, 0xe4004081011002ULL,   0x1c004001012080ULL,
    0x8004200962a00220ULL, 0x8422100208500202ULL, 0x2000402200300c08ULL, 0x8646020080080080ULL,
    0x80020a0200100808ULL, 0x2010004880111000ULL, 0x623000a080011400ULL, 0x42008c0340209202ULL,
    0x209188240001000ULL,  0x400408a884001800ULL, 0x110400a6080400ULL,   0x1840060a44020800ULL,
    0x90080104000041ULL,   0x201011000808101ULL,  0x1a2208080504f080ULL, 0x8012020600211212ULL,
    0x500861011240000ULL,  0x180806108200800ULL,  0x4000020e01040044ULL, 0x300000261044000aULL,
    0x802241102020002ULL,  0x20906061210001ULL,   0x5a84841004010310ULL, 0x4010801011c04ULL,
    0xa010109502200ULL,    0x4a02012000ULL,       0x500201010098b028ULL, 0x8040002811040900ULL,
    0x28000010020204ULL,   0x6000020202d0240ULL,  0x8918844842082200ULL, 0x4010011029020020ULL
};

static uint64_t calc_rook_mask(int sq) {
    uint64_t mask = 0;
    int r = sq >> 3, f = sq & 7;
    for (int rr = r + 1; rr <= 6; rr++) mask |= 1ULL << (rr * 8 + f);
    for (int rr = r - 1; rr >= 1; rr--) mask |= 1ULL << (rr * 8 + f);
    for (int ff = f + 1; ff <= 6; ff++) mask |= 1ULL << (r * 8 + ff);
    for (int ff = f - 1; ff >= 1; ff--) mask |= 1ULL << (r * 8 + ff);
    return mask;
}

static uint64_t calc_bishop_mask(int sq) {
    uint64_t mask = 0;
    int r = sq >> 3, f = sq & 7;
    for (int rr = r + 1, ff = f + 1; rr <= 6 && ff <= 6; rr++, ff++) mask |= 1ULL << (rr * 8 + ff);
    for (int rr = r + 1, ff = f - 1; rr <= 6 && ff >= 1; rr++, ff--) mask |= 1ULL << (rr * 8 + ff);
    for (int rr = r - 1, ff = f + 1; rr >= 1 && ff <= 6; rr--, ff++) mask |= 1ULL << (rr * 8 + ff);
    for (int rr = r - 1, ff = f - 1; rr >= 1 && ff >= 1; rr--, ff--) mask |= 1ULL << (rr * 8 + ff);
    return mask;
}

static uint64_t calc_rook_atk_slow(int sq, uint64_t occ) {
    uint64_t atk = 0;
    int r = sq >> 3, f = sq & 7;
    for (int rr = r + 1; rr <= 7; rr++) { uint64_t b = 1ULL << (rr * 8 + f); atk |= b; if (occ & b) break; }
    for (int rr = r - 1; rr >= 0; rr--) { uint64_t b = 1ULL << (rr * 8 + f); atk |= b; if (occ & b) break; }
    for (int ff = f + 1; ff <= 7; ff++) { uint64_t b = 1ULL << (r * 8 + ff); atk |= b; if (occ & b) break; }
    for (int ff = f - 1; ff >= 0; ff--) { uint64_t b = 1ULL << (r * 8 + ff); atk |= b; if (occ & b) break; }
    return atk;
}

static uint64_t calc_bishop_atk_slow(int sq, uint64_t occ) {
    uint64_t atk = 0;
    int r = sq >> 3, f = sq & 7;
    for (int rr = r + 1, ff = f + 1; rr <= 7 && ff <= 7; rr++, ff++) { uint64_t b = 1ULL << (rr * 8 + ff); atk |= b; if (occ & b) break; }
    for (int rr = r + 1, ff = f - 1; rr <= 7 && ff >= 0; rr++, ff--) { uint64_t b = 1ULL << (rr * 8 + ff); atk |= b; if (occ & b) break; }
    for (int rr = r - 1, ff = f + 1; rr >= 0 && ff <= 7; rr--, ff++) { uint64_t b = 1ULL << (rr * 8 + ff); atk |= b; if (occ & b) break; }
    for (int rr = r - 1, ff = f - 1; rr >= 0 && ff >= 0; rr--, ff--) { uint64_t b = 1ULL << (rr * 8 + ff); atk |= b; if (occ & b) break; }
    return atk;
}

void bb_init(void) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    for (int i = 0; i < 8; i++) {
        BB_FILE_MASK[i] = FILE_A_BB << i;
        BB_RANK_MASK[i] = 0xFFULL << (i * 8);
    }
    for (int i = 0; i < 8; i++) {
        BB_ADJ_FILES[i] = ((i > 0) ? BB_FILE_MASK[i - 1] : 0) | ((i < 7) ? BB_FILE_MASK[i + 1] : 0);
    }

    for (int s1 = 0; s1 < 64; s1++) {
        int r1 = s1 >> 3, f1 = s1 & 7;
        for (int s2 = 0; s2 < 64; s2++) {
            int r2 = s2 >> 3, f2 = s2 & 7;
            int dr = abs(r1 - r2), df = abs(f1 - f2);
            BB_CHEBY_DIST[s1][s2] = (uint8_t)(dr > df ? dr : df);
            BB_MANHATTAN_DIST[s1][s2] = (uint8_t)(dr + df);
        }

        /* Passed pawn masks */
        uint64_t file_span = BB_FILE_MASK[f1] | BB_ADJ_FILES[f1];
        uint64_t w_ahead = 0, b_ahead = 0;
        for (int rr = r1 + 1; rr < 8; rr++) w_ahead |= BB_RANK_MASK[rr];
        for (int rr = r1 - 1; rr >= 0; rr--) b_ahead |= BB_RANK_MASK[rr];
        BB_PASSED_MASK[0][s1] = file_span & w_ahead;
        BB_PASSED_MASK[1][s1] = file_span & b_ahead;

        /* Pawn attacks */
        uint64_t b = 1ULL << s1;
        BB_PAWN_ATK[0][s1] = ((b & ~FILE_A_BB) << 7) | ((b & ~FILE_H_BB) << 9);
        BB_PAWN_ATK[1][s1] = ((b & ~FILE_H_BB) >> 7) | ((b & ~FILE_A_BB) >> 9);

        /* Knight & King attacks */
        static const int ndr[8] = {-2, -2, -1, -1, 1, 1, 2, 2};
        static const int ndf[8] = {-1, 1, -2, 2, -2, 2, -1, 1};
        uint64_t na = 0, ka = 0;
        for (int k = 0; k < 8; k++) {
            int rr = r1 + ndr[k], ff = f1 + ndf[k];
            if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) na |= 1ULL << (rr * 8 + ff);
        }
        for (int dr = -1; dr <= 1; dr++) {
            for (int df = -1; df <= 1; df++) {
                if (dr == 0 && df == 0) continue;
                int rr = r1 + dr, ff = f1 + df;
                if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) ka |= 1ULL << (rr * 8 + ff);
            }
        }
        BB_KNIGHT_ATK[s1] = na;
        BB_KING_ATK[s1] = ka;

        /* Magic Rook & Bishop attack tables */
        BB_ROOK_MASK[s1] = calc_rook_mask(s1);
        uint64_t rmask = BB_ROOK_MASK[s1];
        uint64_t sub = 0;
        do {
            uint64_t idx = (sub * BB_ROOK_MAGIC[s1]) >> BB_ROOK_SHIFT[s1];
            BB_ROOK_ATK[s1][idx] = calc_rook_atk_slow(s1, sub);
            sub = (sub - rmask) & rmask;
        } while (sub);

        BB_BISHOP_MASK[s1] = calc_bishop_mask(s1);
        uint64_t bmask = BB_BISHOP_MASK[s1];
        sub = 0;
        do {
            uint64_t idx = (sub * BB_BISHOP_MAGIC[s1]) >> BB_BISHOP_SHIFT[s1];
            BB_BISHOP_ATK[s1][idx] = calc_bishop_atk_slow(s1, sub);
            sub = (sub - bmask) & bmask;
        } while (sub);
    }
}
