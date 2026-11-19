#ifndef TABLES_H
#define TABLES_H
#include <stdint.h>

extern const uint8_t zobrist_table[0x1500];
extern const uint16_t sq_to_zob_off[120];
extern const uint16_t castling_zob[4][3];
extern const uint16_t ep_zob[8][3];
extern const uint8_t board_file[120];
extern const uint8_t board_rank[120];
extern const int16_t piece_val[16];
extern const int16_t promo_delta[16];
extern const uint8_t tb_tri[64];
extern const int8_t wp_psqt[120];
extern const int8_t bp_psqt[120];
extern const uint8_t dgroup_rom[0x3600];
#endif
