#define _POSIX_C_SOURCE 200809L
#include "mchess8.h"
#include "bitboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
static int64_t uci_now_ms(void) {
    return (int64_t)GetTickCount64();
}
#else
#include <strings.h>
#include <time.h>
static int64_t uci_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
}
#endif

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

void settings_init_defaults(EngineSettings *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->hash_mb = 512;
    cfg->book_mode = 1; /* Tournament */
    cfg->use_tb = 1;
#ifdef _WIN32
    snprintf(cfg->book_path, sizeof(cfg->book_path), "PRBOOK");
    snprintf(cfg->tb_path, sizeof(cfg->tb_path), "TB");
#else
    if (file_exists("PRBOOK/PRBOOK.M80"))
        snprintf(cfg->book_path, sizeof(cfg->book_path), "PRBOOK");
    else if (file_exists("/home/idiot/MChess8/MChess8_UCI_Package/PRBOOK/PRBOOK.M80"))
        snprintf(cfg->book_path, sizeof(cfg->book_path), "/home/idiot/MChess8/MChess8_UCI_Package/PRBOOK");
    else
        snprintf(cfg->book_path, sizeof(cfg->book_path), "/home/idiot/MChess8/MChess8/PRBOOK");
    if (file_exists("TB/KQK.TBW"))
        snprintf(cfg->tb_path, sizeof(cfg->tb_path), "TB");
    else
        snprintf(cfg->tb_path, sizeof(cfg->tb_path), "/home/idiot/MChess8/MChess8/TB");
#endif
    cfg->aggression = 5;
    cfg->mobility = 4;
    cfg->centrality = 1;
    cfg->king_placement = 4;
    cfg->pawn_structure = 5;
    cfg->passed_pawns = 2;
    cfg->random_limit = 0;
    cfg->contempt = 0;
    cfg->ponder = 1;
    cfg->null_move = 1;
    cfg->extensions = 1;
    cfg->deterministic_book = 0;
    cfg->threads = 1;
}

void settings_load_m80(EngineSettings *cfg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    unsigned int v[22];
    int n = 0;
    while (n < 22 && fscanf(f, "%x", &v[n]) == 1) n++;
    fclose(f);
    if (n >= 20) {
        cfg->book_mode = (int)v[1];
        if (cfg->book_mode < 0 || cfg->book_mode > 3) cfg->book_mode = 2;
        cfg->aggression = (int)v[11];
        cfg->mobility = (int)v[12];
        cfg->centrality = (int)v[13];
        cfg->king_placement = (int)v[14];
        cfg->pawn_structure = (int)v[15];
        cfg->passed_pawns = (int)v[16];
        cfg->random_limit = (int)v[19];
    }
}

static uint64_t perft(Board *b, int depth) {
    if (depth == 0) return 1;
    Move moves[MAX_MOVES];
    int n = gen_pseudo_moves(b, moves);
    uint64_t nodes = 0;
    for (int i = 0; i < n; i++) {
        if (!make_move(b, moves[i])) continue;
        nodes += perft(b, depth - 1);
        unmake_move(b, moves[i]);
    }
    return nodes;
}

static void handle_uci(const EngineSettings *cfg) {
    printf("id name MChess Pro 8.0 UCI\n");
    printf("id author Marty Hirsch (UCI port)\n");
    printf("option name Hash type spin default %d min 1 max 4096\n", cfg->hash_mb);
    printf("option name Threads type spin default %d min 1 max 64\n", cfg->threads);
    static const char *book_modes[] = {"Off", "Tournament", "Wide", "Broad"};
    const char *bm = (cfg->book_mode >= 0 && cfg->book_mode <= 3) ? book_modes[cfg->book_mode] : "Tournament";
    printf("option name Book type check default %s\n", cfg->book_mode > 0 ? "true" : "false");
    printf("option name BookMode type combo default %s var Off var Tournament var Wide var Broad\n", bm);
    printf("option name BookPath type string default %s\n", cfg->book_path);
    printf("option name DeterministicBook type check default %s\n", cfg->deterministic_book ? "true" : "false");
    printf("option name Tablebases type check default %s\n", cfg->use_tb ? "true" : "false");
    printf("option name TablebasePath type string default %s\n", cfg->tb_path);
    printf("option name Aggression type spin default %d min 0 max 8\n", cfg->aggression);
    printf("option name Mobility type spin default %d min 0 max 8\n", cfg->mobility);
    printf("option name Centrality type spin default %d min 0 max 8\n", cfg->centrality);
    printf("option name KingPlacement type spin default %d min 0 max 8\n", cfg->king_placement);
    printf("option name PawnStructure type spin default %d min 0 max 8\n", cfg->pawn_structure);
    printf("option name PassedPawns type spin default %d min 0 max 8\n", cfg->passed_pawns);
    printf("option name RandomLimit type spin default %d min 0 max 100\n", cfg->random_limit);
    printf("option name Contempt type spin default %d min -200 max 200\n", cfg->contempt);
    printf("option name NullMove type check default %s\n", cfg->null_move ? "true" : "false");
    printf("option name Extensions type check default %s\n", cfg->extensions ? "true" : "false");
    printf("option name Ponder type check default %s\n", cfg->ponder ? "true" : "false");
    printf("uciok\n");
    fflush(stdout);
}

static void handle_setoption(EngineSettings *cfg, char *line) {
    char *name_p = strstr(line, "name ");
    if (!name_p) return;
    name_p += 5;
    char *val_p = strstr(name_p, " value ");
    char name[128] = {0};
    char val[512] = {0};
    if (val_p) {
        size_t nlen = (size_t)(val_p - name_p);
        if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
        memcpy(name, name_p, nlen);
        name[nlen] = '\0';
        snprintf(val, sizeof(val), "%s", val_p + 7);
        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen - 1] == '\n' || val[vlen - 1] == '\r' || val[vlen - 1] == ' ')) {
            val[--vlen] = '\0';
        }
    } else {
        snprintf(name, sizeof(name), "%s", name_p);
    }

    if (strcasecmp(name, "Hash") == 0) {
        cfg->hash_mb = atoi(val);
        tt_init(cfg->hash_mb);
    } else if (strcasecmp(name, "Threads") == 0) {
        int v = atoi(val);
        cfg->threads = v < 1 ? 1 : (v > 64 ? 64 : v);
    } else if (strcasecmp(name, "Book") == 0) {
        if (strcasecmp(val, "false") == 0 || strcmp(val, "0") == 0) cfg->book_mode = 0;
        else if (cfg->book_mode == 0) cfg->book_mode = 1;
    } else if (strcasecmp(name, "BookMode") == 0) {
        if (strcasecmp(val, "Off") == 0) cfg->book_mode = 0;
        else if (strcasecmp(val, "Tournament") == 0) cfg->book_mode = 1;
        else if (strcasecmp(val, "Wide") == 0) cfg->book_mode = 2;
        else if (strcasecmp(val, "Broad") == 0) cfg->book_mode = 3;
    } else if (strcasecmp(name, "BookPath") == 0) {
        snprintf(cfg->book_path, sizeof(cfg->book_path), "%s", val);
        book_init(cfg->book_path);
    } else if (strcasecmp(name, "DeterministicBook") == 0) {
        cfg->deterministic_book = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0);
    } else if (strcasecmp(name, "Tablebases") == 0) {
        cfg->use_tb = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0);
    } else if (strcasecmp(name, "TablebasePath") == 0) {
        snprintf(cfg->tb_path, sizeof(cfg->tb_path), "%s", val);
        tb_init(cfg->tb_path);
    } else if (strcasecmp(name, "Aggression") == 0) { int v = atoi(val); cfg->aggression = v < 0 ? 0 : (v > 8 ? 8 : v); }
    else if (strcasecmp(name, "Mobility") == 0) { int v = atoi(val); cfg->mobility = v < 0 ? 0 : (v > 8 ? 8 : v); }
    else if (strcasecmp(name, "Centrality") == 0) { int v = atoi(val); cfg->centrality = v < 0 ? 0 : (v > 8 ? 8 : v); }
    else if (strcasecmp(name, "KingPlacement") == 0) { int v = atoi(val); cfg->king_placement = v < 0 ? 0 : (v > 8 ? 8 : v); }
    else if (strcasecmp(name, "PawnStructure") == 0) { int v = atoi(val); cfg->pawn_structure = v < 0 ? 0 : (v > 8 ? 8 : v); }
    else if (strcasecmp(name, "PassedPawns") == 0) { int v = atoi(val); cfg->passed_pawns = v < 0 ? 0 : (v > 8 ? 8 : v); }
    else if (strcasecmp(name, "RandomLimit") == 0) { int v = atoi(val); cfg->random_limit = v < 0 ? 0 : (v > 100 ? 100 : v); }
    else if (strcasecmp(name, "Contempt") == 0) { int v = atoi(val); if (v < -200) v = -200; if (v > 200) v = 200; cfg->contempt = v * 2; }
    else if (strcasecmp(name, "NullMove") == 0) cfg->null_move = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0);
    else if (strcasecmp(name, "Extensions") == 0) cfg->extensions = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0);
    else if (strcasecmp(name, "Ponder") == 0) cfg->ponder = (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0);
}

static void handle_position(Board *b, char *line) {
    char *p = line + 9;
    while (*p == ' ') p++;
    if (strncmp(p, "startpos", 8) == 0) {
        board_init_startpos(b);
        p += 8;
    } else if (strncmp(p, "fen ", 4) == 0) {
        p += 4;
        char *moves_p = strstr(p, " moves ");
        if (moves_p) *moves_p = '\0';
        Board backup = *b;
        if (!board_set_fen(b, p)) {
            *b = backup;
            return;
        }
        if (moves_p) {
            *moves_p = ' ';
            p = moves_p;
        } else {
            return;
        }
    }
    char *moves_p = strstr(p, "moves");
    if (moves_p) {
        moves_p += 5;
        char *tok = strtok(moves_p, " \t\r\n");
        while (tok) {
            Move m;
            if (uci_to_move(b, tok, &m)) {
                make_move(b, m);
            }
            tok = strtok(NULL, " \t\r\n");
        }
    }
}

static void handle_go(Board *b, const EngineSettings *cfg, char *line) {
    SearchState st;
    memset(&st, 0, sizeof(st));
    int64_t wtime = -1, btime = -1, winc = 0, binc = 0, movetime = -1;
    int movestogo = 30;
    bool has_movestogo = false;

    char *tok = strtok(line + 2, " \t\r\n");
    while (tok) {
        if (strcmp(tok, "depth") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (tok) st.max_depth = atoi(tok);
        } else if (strcmp(tok, "movetime") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (tok) movetime = atoll(tok);
        } else if (strcmp(tok, "wtime") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (tok) wtime = atoll(tok);
        } else if (strcmp(tok, "btime") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (tok) btime = atoll(tok);
        } else if (strcmp(tok, "winc") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (tok) winc = atoll(tok);
        } else if (strcmp(tok, "binc") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (tok) binc = atoll(tok);
        } else if (strcmp(tok, "movestogo") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (tok) {
                movestogo = atoi(tok);
                if (movestogo > 0) has_movestogo = true;
            }
        } else if (strcmp(tok, "nodes") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (tok) st.max_nodes = (uint64_t)atoll(tok);
        } else if (strcmp(tok, "infinite") == 0) {
            st.infinite = 1;
        } else if (strcmp(tok, "ponder") == 0) {
            st.ponder = 1;
            st.infinite = 1;
        }
        tok = strtok(NULL, " \t\r\n");
    }

    int64_t now = uci_now_ms();
    if (movetime > 0) {
        st.alloc_ms = movetime;
        if (!st.ponder) {
            st.soft_time_ms = now + movetime;
            st.stop_time_ms = now + movetime;
        }
    } else if ((b->side == WHITE && wtime > 0) || (b->side == BLACK && btime > 0)) {
        int64_t t = (b->side == WHITE) ? wtime : btime;
        int64_t inc = (b->side == WHITE) ? winc : binc;
        int mtg = has_movestogo ? (movestogo + 1) : 52;
        if (!has_movestogo && b->game_ply <= 24 && t >= 30000) mtg = 60;
        if (!has_movestogo && inc == 0 && t < 30000) mtg = (t < 10000) ? 60 : 52;
        int64_t alloc = t / mtg + (inc * 7) / 10;
        int64_t max_safe = (t > 60) ? (t - 40) : (t / 3);
        int64_t min_alloc = (t < 2000) ? 5 : 20;
        if (alloc > t / 2) alloc = t / 2;
        if (alloc < min_alloc) alloc = min_alloc;
        if (alloc > max_safe) alloc = max_safe;
        if (alloc < 1) alloc = 1;
        st.alloc_ms = alloc;
        if (!st.ponder) {
            int64_t soft = alloc / 2;
            if (soft < 1) soft = 1;
            st.soft_time_ms = now + soft;
            int64_t hard = (alloc * 5) / 4;
            if (has_movestogo && movestogo >= 2 && hard > t / movestogo) {
                hard = t / movestogo;
            }
            if (hard > max_safe) hard = max_safe;
            if (hard < alloc) hard = alloc;
            st.stop_time_ms = now + hard;
        }
    }

    search_go(b, cfg, &st);
}

int main(void) {
    bb_init();
    EngineSettings cfg;
    settings_init_defaults(&cfg);
    if (file_exists("SETTINGS.M80"))
        settings_load_m80(&cfg, "SETTINGS.M80");
    tt_init(cfg.hash_mb);
    book_init(cfg.book_path);
    tb_init(cfg.tb_path);

    Board b;
    board_init_startpos(&b);

    static char line[32768];
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        if (strcmp(line, "uci") == 0) {
            handle_uci(&cfg);
        } else if (strcmp(line, "isready") == 0) {
            printf("readyok\n");
            fflush(stdout);
        } else if (strcmp(line, "ucinewgame") == 0) {
            tt_clear();
            search_clear_history();
            board_init_startpos(&b);
        } else if (strncmp(line, "setoption ", 10) == 0) {
            handle_setoption(&cfg, line);
        } else if (strncmp(line, "position ", 9) == 0) {
            handle_position(&b, line);
        } else if (strncmp(line, "go", 2) == 0 && (line[2] == '\0' || line[2] == ' ')) {
            handle_go(&b, &cfg, line);
        } else if (strncmp(line, "perft ", 6) == 0) {
            int d = atoi(line + 6);
            printf("perft(%d) = %llu\n", d, (unsigned long long)perft(&b, d));
            fflush(stdout);
        } else if (strcmp(line, "quit") == 0) {
            break;
        }
    }
    search_shutdown();
    return 0;
}
