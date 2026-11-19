#ifndef BITBOARD_H
#define BITBOARD_H

#include <stdint.h>

extern uint64_t BB_PAWN_ATK[2][64];
extern uint64_t BB_KNIGHT_ATK[64];
extern uint64_t BB_KING_ATK[64];
extern uint64_t BB_ROOK_MASK[64];
extern uint64_t BB_BISHOP_MASK[64];
extern uint64_t BB_ROOK_ATK[64][4096];
extern uint64_t BB_BISHOP_ATK[64][512];
extern const uint64_t BB_ROOK_MAGIC[64];
extern const uint64_t BB_BISHOP_MAGIC[64];
extern const uint8_t BB_ROOK_SHIFT[64];
extern const uint8_t BB_BISHOP_SHIFT[64];

extern uint64_t BB_FILE_MASK[8];
extern uint64_t BB_RANK_MASK[8];
extern uint64_t BB_ADJ_FILES[8];
extern uint64_t BB_PASSED_MASK[2][64];
extern uint8_t  BB_CHEBY_DIST[64][64];
extern uint8_t  BB_MANHATTAN_DIST[64][64];

#define FILE_A_BB 0x0101010101010101ULL
#define FILE_H_BB 0x8080808080808080ULL
#define RANK_1_BB 0x00000000000000FFULL
#define RANK_2_BB 0x000000000000FF00ULL
#define RANK_3_BB 0x0000000000FF0000ULL
#define RANK_6_BB 0x0000FF0000000000ULL
#define RANK_7_BB 0x00FF000000000000ULL
#define RANK_8_BB 0xFF00000000000000ULL

static inline int ctz64(uint64_t b) {
    return __builtin_ctzll(b);
}

static inline int pop_lsb(uint64_t *b) {
    int sq = __builtin_ctzll(*b);
    *b &= *b - 1;
    return sq;
}

static inline int popcnt64(uint64_t b) {
    return __builtin_popcountll(b);
}

static inline uint64_t bb_bishop_attacks(int sq, uint64_t occ) {
    occ &= BB_BISHOP_MASK[sq];
    occ *= BB_BISHOP_MAGIC[sq];
    occ >>= BB_BISHOP_SHIFT[sq];
    return BB_BISHOP_ATK[sq][occ];
}

static inline uint64_t bb_rook_attacks(int sq, uint64_t occ) {
    occ &= BB_ROOK_MASK[sq];
    occ *= BB_ROOK_MAGIC[sq];
    occ >>= BB_ROOK_SHIFT[sq];
    return BB_ROOK_ATK[sq][occ];
}

static inline uint64_t bb_queen_attacks(int sq, uint64_t occ) {
    return bb_bishop_attacks(sq, occ) | bb_rook_attacks(sq, occ);
}

void bb_init(void);

#endif
