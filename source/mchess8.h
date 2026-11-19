#ifndef MCHESS8_H
#define MCHESS8_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PLY 64
#define MAX_GAME_PLY 1024
#define MAX_MOVES 256
#define MATE_SCORE 20000
#define INF_SCORE 30000

enum {
    EMPTY = 0,
    WP = 1, WN = 2, WB = 3, WR = 4, WQ = 5, WK = 6,
    BORDER = 7,
    BP = 8, BN = 9, BB = 10, BR = 11, BQ = 12, BK = 13
};

enum {
    WHITE = 0,
    BLACK = 1
};

enum {
    SPEC_NONE = 0,
    SPEC_EP = 1,
    SPEC_PROMO_N = 2,
    SPEC_PROMO_B = 3,
    SPEC_PROMO_R = 4,
    SPEC_PROMO_Q = 5,
    SPEC_CASTLE = 6
};

typedef struct {
    uint8_t from;
    uint8_t to;
    uint8_t piece_spec; /* (piece << 4) | special */
    uint8_t flags;      /* bit 7 = capture/ep */
    int32_t sort_score;
} Move;

#define MOVE_PIECE(m) ((m).piece_spec >> 4)
#define MOVE_SPEC(m)  ((m).piece_spec & 0x0f)
#define MOVE_IS_CAP(m) (((m).flags & 0x80) != 0)

static inline int sq64_to_120(int sq64) {
    return 0x15 + (sq64 >> 3) * 10 + (sq64 & 7);
}

typedef struct {
    uint8_t captured;
    int8_t ep_sq;
    int halfmove;
    int castling[4];
    uint16_t zob[3];
    uint16_t pawn_zob[3];
    int16_t mat_balance;
    int16_t w_mat;
    int16_t b_mat;
    int16_t psqt_score;
} Undo;

typedef struct {
    uint8_t squares[64];
    uint64_t bb[14];     /* piece bitboards indexed by WP..WK, BP..BK */
    uint64_t occ[3];     /* 0=WHITE, 1=BLACK, 2=BOTH */
    int side;
    int ep_sq;           /* 0..63 or -1 if none */
    int castling[4];     /* 0 = available, >0 = ply lost */
    int halfmove;
    int game_ply;
    uint8_t wk_sq;       /* 0..63 */
    uint8_t bk_sq;       /* 0..63 */
    uint16_t zob[3];
    uint16_t pawn_zob[3];
    int16_t mat_balance; /* White - Black in half-cp */
    int16_t w_mat;       /* White non-king non-pawn material */
    int16_t b_mat;       /* Black non-king non-pawn material */
    int16_t psqt_score;  /* Incremental positional score */
    int total_pieces;
    int total_pawns;
    int piece_counts[14];
    uint64_t hash_history[MAX_GAME_PLY];
    Undo undo_stack[MAX_GAME_PLY];
} Board;

typedef struct {
    int hash_mb;
    int book_mode;       /* 0=Off, 1=Tournament, 2=Wide, 3=Broad */
    char book_path[512];
    int use_tb;
    char tb_path[512];
    int aggression;      /* 0..8, default 5 */
    int mobility;        /* 0..8, default 4 */
    int centrality;      /* 0..8, default 1 */
    int king_placement;  /* 0..8, default 4 */
    int pawn_structure;  /* 0..8, default 5 */
    int passed_pawns;    /* 0..8, default 2 */
    int random_limit;    /* 0..100, default 10 */
    int contempt;        /* half-cp, default 0 */
    int ponder;          /* 0/1 */
    int null_move;       /* 0/1 */
    int extensions;      /* 0/1 */
    int deterministic_book; /* 0/1 */
    int threads;         /* 1..64, default 1 */
} EngineSettings;

typedef struct {
    uint64_t nodes;
    uint64_t tb_hits;
    int max_depth;
    int stop;
    int64_t start_time_ms;
    int64_t stop_time_ms;
    int64_t soft_time_ms;
    int64_t alloc_ms;
    int infinite;
    int ponder;
    uint64_t max_nodes;
    Move best_move;
    Move ponder_move;
    int best_score;
    Move pv[MAX_PLY][MAX_PLY];
    int pv_len[MAX_PLY];
    int16_t history[14][64];
    Move killers[MAX_PLY][2];
    Move countermoves[14][64];
    Move move_stack[MAX_PLY];
    int eval_stack[MAX_PLY];
    int thread_id;
} SearchState;

/* board.c */
void board_init_startpos(Board *b);
bool board_set_fen(Board *b, const char *fen);
void zobrist_compute_full(Board *b);
uint64_t board_book_hash(const Board *b);
uint64_t board_tt_key(const Board *b);
bool is_sq_attacked(const Board *b, int sq, int by_side);
bool in_check(const Board *b, int side);
bool make_move(Board *b, Move m);
void unmake_move(Board *b, Move m);
void make_null_move(Board *b);
void unmake_null_move(Board *b);
int count_repetitions(const Board *b);

/* movegen.c */
int gen_pseudo_moves(const Board *b, Move *moves);
int gen_legal_moves(Board *b, Move *moves);
int gen_pseudo_captures(const Board *b, Move *moves);
void move_to_uci(Move m, char *buf);
bool uci_to_move(Board *b, const char *str, Move *out);

/* book.c */
void book_init(const char *dir);
bool book_probe(const Board *b, const EngineSettings *cfg, Move *out_move);

/* tb.c */
void tb_init(const char *dir);
bool tb_probe(const Board *b, int ply, int *out_score);

/* eval.c */
int evaluate(const Board *b, const EngineSettings *cfg);

/* tt.c */
enum { TT_NONE = 0, TT_UPPER = 1, TT_LOWER = 2, TT_EXACT = 3 };
void tt_init(int mb);
void tt_clear(void);
void tt_new_search(void);
bool tt_probe(const Board *b, int depth, int alpha, int beta, int ply, int *out_score, Move *out_move);
void tt_store(const Board *b, int depth, int bound, int score, int ply, Move best_move);

/* search.c */
void search_go(Board *b, const EngineSettings *cfg, SearchState *st);
void search_clear_history(void);
void search_shutdown(void);

/* uci.c */
void settings_init_defaults(EngineSettings *cfg);
void settings_load_m80(EngineSettings *cfg, const char *path);

#endif
