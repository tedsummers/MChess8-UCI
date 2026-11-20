# MChess Pro 8.0 UCI

Native cross-platform C11 UCI port of Marty Hirsch's 1998 **M-Chess Professional 8.0** (`MCP8.EXE`), powered by 64-bit Magic Bitboards, Lazy SMP (`Threads` 1–64), a 4-bucket lockless XOR transposition table (up to 4096 MB), original `PRBOOK/` + modern `PRBOOK_MODERN/` opening books (`128,495` positions), and full 35-set 3/4-piece DTM `TB/` endgame tablebases.

MChess Pro 8.0 UCI plays roughly **150–250+ Elo stronger** than the original 1998 DOS program while preserving its opening book, 3/4-piece tablebases, and evaluation character.

- **Download Package:** [Latest Release (`MChess8_UCI_Package.zip`)](https://github.com/tedsummers/MChess8-UCI/releases/latest)
- **Documentation:** [MANUAL.md](MANUAL.md) | [WHATS_NEW.md](WHATS_NEW.md) | [README.txt](README.txt)

---

## Compiling from Source

All source files and the `Makefile` are in [`source/`](source/).

### Requirements
- **Linux:** `gcc` and `make` (add `gcc-multilib` if building 32-bit `-m32`)
- **Cross-compiling Windows / macOS from Linux:** `zig` (`zig cc`)
- **Native macOS / Windows:** `clang` or `gcc` (MinGW-w64) with C11 support

### Build Commands

```bash
cd source

# Linux 64-bit (mchess8_uci_x64)
make linux64

# Linux 32-bit (mchess8_uci)
make

# Windows 64-bit (mchess8_uci_x64.exe) & 32-bit (mchess8_uci.exe) via zig cc
make win64
make win32

# macOS Intel (mchess8_uci_mac_intel) & Apple Silicon (mchess8_uci_mac_arm64) via zig cc
make macos
```

### Direct Single-Command Build (Linux / macOS / MinGW)

```bash
# Linux / macOS
cc -O2 -Wall -Wextra -std=c11 -pthread -o mchess8_uci uci.c board.c movegen.c eval.c tt.c book.c tb.c search.c tables.c bitboard.c

# Windows (MinGW-w64)
gcc -O2 -Wall -Wextra -std=c11 -o mchess8_uci.exe uci.c board.c movegen.c eval.c tt.c book.c tb.c search.c tables.c bitboard.c
```

Place the compiled binary in the same directory as `PRBOOK/`, `TB/`, and `SETTINGS.M80` from the release package (or configure `BookPath` and `TablebasePath` in your UCI GUI).
