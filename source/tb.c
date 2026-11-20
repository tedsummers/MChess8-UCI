#define _POSIX_C_SOURCE 200809L
#include "mchess8.h"
#include "tables.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION g_tb_lock;
static LONG g_tb_lock_init = 0;
static void tb_lock(void) {
    if (InterlockedCompareExchange(&g_tb_lock_init, 1, 0) == 0) {
        InitializeCriticalSection(&g_tb_lock);
        InterlockedExchange(&g_tb_lock_init, 2);
    } else {
        while (InterlockedCompareExchange(&g_tb_lock_init, 2, 2) != 2) Sleep(0);
    }
    EnterCriticalSection(&g_tb_lock);
}
static void tb_unlock(void) { LeaveCriticalSection(&g_tb_lock); }
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
static pthread_mutex_t g_tb_lock = PTHREAD_MUTEX_INITIALIZER;
static void tb_lock(void) { pthread_mutex_lock(&g_tb_lock); }
static void tb_unlock(void) { pthread_mutex_unlock(&g_tb_lock); }
#endif

static char g_tb_dir[512] = "TB";

typedef struct {
    char key[24];
    FILE *fp;
#ifndef _WIN32
    const int8_t *mapped;
    size_t map_size;
#endif
    int tried;
} TBHandle;

static TBHandle g_tb_handles[80];
static int g_tb_handle_cnt = 0;

void tb_init(const char *dir) {
    if (dir && dir[0]) {
        tb_lock();
        if (strcmp(g_tb_dir, dir) != 0) {
            for (int i = 0; i < g_tb_handle_cnt; i++) {
#ifndef _WIN32
                if (g_tb_handles[i].mapped) {
                    munmap((void *)g_tb_handles[i].mapped, g_tb_handles[i].map_size);
                    g_tb_handles[i].mapped = NULL;
                    g_tb_handles[i].map_size = 0;
                }
#endif
                if (g_tb_handles[i].fp) fclose(g_tb_handles[i].fp);
            }
            g_tb_handle_cnt = 0;
        }
        snprintf(g_tb_dir, sizeof(g_tb_dir), "%s", dir);
        tb_unlock();
    }
}

static TBHandle *get_tb_handle(const char *name, const char *ext) {
    char key[24];
    snprintf(key, sizeof(key), "%s%s", name, ext);
    for (int i = 0; i < g_tb_handle_cnt; i++) {
        if (strcmp(g_tb_handles[i].key, key) == 0) {
            return &g_tb_handles[i];
        }
    }
    char path[600];
    snprintf(path, sizeof(path), "%s/%s%s", g_tb_dir, name, ext);
    FILE *f = fopen(path, "rb");
    if (g_tb_handle_cnt < 80) {
        TBHandle *h = &g_tb_handles[g_tb_handle_cnt];
        snprintf(h->key, sizeof(h->key), "%s", key);
        h->fp = f;
#ifndef _WIN32
        h->mapped = NULL;
        h->map_size = 0;
        if (f) {
            struct stat st;
            if (fstat(fileno(f), &st) == 0 && st.st_size > 0) {
                void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fileno(f), 0);
                if (p != MAP_FAILED) {
                    h->mapped = (const int8_t *)p;
                    h->map_size = (size_t)st.st_size;
                }
            }
        }
#endif
        h->tried = 1;
        __atomic_store_n(&g_tb_handle_cnt, g_tb_handle_cnt + 1, __ATOMIC_RELEASE);
        return h;
    }
    if (f) fclose(f);
    return NULL;
}

static const char pc_letters[7] = { ' ', 'P', 'N', 'B', 'R', 'Q', 'K' };

bool tb_probe(const Board *b, int ply, int *out_score) {
    if (b->total_pieces > 4 || b->total_pieces < 2) return false;
    if (b->ep_sq >= 0) return false;
    if (b->castling[0] == 0 || b->castling[1] == 0 || b->castling[2] == 0 || b->castling[3] == 0) {
        return false;
    }

    uint8_t w_sq[4], b_sq[4];
    int w_pt[4], b_pt[4];
    int nw = 0, nb = 0;

    /* Collect in order K(6), Q(5), R(4), B(3), N(2), P(1) */
    for (int pt = 6; pt >= 1; pt--) {
        for (int sq = 0; sq < 64; sq++) {
            uint8_t p = b->squares[sq];
            if (p == pt && nw < 4) {
                w_sq[nw] = (uint8_t)sq;
                w_pt[nw] = pt;
                nw++;
            }
            if (p == pt + 7 && nb < 4) {
                b_sq[nb] = (uint8_t)sq;
                b_pt[nb] = pt;
                nb++;
            }
        }
    }

    if (nw == 0 || nb == 0 || nw + nb > 4) return false;

    /* Determine stronger side */
    int flipped = 0;
    if (nb > nw) {
        flipped = 1;
    } else if (nb == nw) {
        for (int i = 1; i < nw; i++) {
            if (b_pt[i] > w_pt[i]) { flipped = 1; break; }
            if (b_pt[i] < w_pt[i]) { flipped = 0; break; }
        }
    }

    char name[16];
    int sq64[4];
    int ptype[4];
    int n = 0;

    if (!flipped) {
        for (int i = 0; i < nw; i++) { name[n] = pc_letters[w_pt[i]]; sq64[n] = w_sq[i]; ptype[n] = w_pt[i]; n++; }
        for (int i = 0; i < nb; i++) { name[n] = pc_letters[b_pt[i]]; sq64[n] = b_sq[i]; ptype[n] = b_pt[i]; n++; }
    } else {
        for (int i = 0; i < nb; i++) { name[n] = pc_letters[b_pt[i]]; sq64[n] = b_sq[i] ^ 0x38; ptype[n] = b_pt[i]; n++; }
        for (int i = 0; i < nw; i++) { name[n] = pc_letters[w_pt[i]]; sq64[n] = w_sq[i] ^ 0x38; ptype[n] = w_pt[i]; n++; }
    }
    name[n] = '\0';

    const char *ext = (b->side == flipped) ? ".TBW" : ".TBB";

    int base[4] = { 64, 64, 64, 64 };
    if (b->total_pawns != 0) {
        int pidx = n - 1;
        while (pidx >= 1 && ptype[pidx] != 1) pidx--;
        if ((sq64[pidx] & 7) >= 4) {
            for (int i = 0; i < n; i++) sq64[i] ^= 7;
        }
        base[pidx] = 32;
        sq64[pidx] = ((sq64[pidx] >> 3) << 2) | (sq64[pidx] & 3);
    } else {
        int kidx = n - 1;
        if (sq64[kidx] >= 32) {
            for (int i = 0; i < n; i++) sq64[i] ^= 0x38;
        }
        if ((sq64[kidx] & 7) >= 4) {
            for (int i = 0; i < n; i++) sq64[i] ^= 7;
        }
        if ((sq64[kidx] >> 3) > (sq64[kidx] & 7)) {
            for (int i = 0; i < n; i++) {
                sq64[i] = ((sq64[i] & 7) << 3) | (sq64[i] >> 3);
            }
        }
        base[kidx] = 10;
        sq64[kidx] = tb_tri[sq64[kidx]];
    }

    uint32_t idx = 0;
    for (int i = n - 1; i >= 1; i--) {
        idx = (idx + (uint32_t)sq64[i]) * (uint32_t)base[i - 1];
    }
    idx += (uint32_t)sq64[0];

    int8_t v = 127;
#ifdef _WIN32
    tb_lock();
    TBHandle *h = get_tb_handle(name, ext);
    FILE *f = h ? h->fp : NULL;
    if (f && fseek(f, (long)idx, SEEK_SET) == 0) {
        if (fread(&v, 1, 1, f) != 1) v = 127;
    }
    tb_unlock();
    if (!f) return false;
#else
    /* Fast lock-free lookup into mmap'd TB memory; only lock on first open */
    TBHandle *h = NULL;
    char key[24];
    snprintf(key, sizeof(key), "%s%s", name, ext);
    int cnt = __atomic_load_n(&g_tb_handle_cnt, __ATOMIC_ACQUIRE);
    for (int i = 0; i < cnt; i++) {
        if (strcmp(g_tb_handles[i].key, key) == 0) {
            h = &g_tb_handles[i];
            break;
        }
    }
    if (!h && cnt < 80) {
        tb_lock();
        h = get_tb_handle(name, ext);
        tb_unlock();
    }
    if (!h) return false;
    if (h->mapped) {
        if (idx < h->map_size) v = h->mapped[idx];
    } else if (h->fp) {
        if (pread(fileno(h->fp), &v, 1, (off_t)idx) != 1) v = 127;
    } else {
        return false;
    }
#endif

    if (v < -126 || v > 126) return false;

    if (v == 0) {
        *out_score = 0;
    } else if (v > 0) {
        *out_score = (18000 + (int)v * 2) - ply;
    } else {
        *out_score = (-18000 + (int)v * 2) + ply;
    }
    return true;
}
