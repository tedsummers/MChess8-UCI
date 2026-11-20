# MChess Pro 8.0 UCI — User Manual

**MChess Pro 8.0 UCI** is a native C port of Marty Hirsch's 1998 **M-Chess Professional 8.0** chess engine to the **Universal Chess Interface (UCI)** protocol. Built on a fast 64-bit Magic Bitboard core with multi-threaded Lazy SMP support, it runs natively on Linux, Windows, and macOS (no DOSBox or emulation required) and uses MChess Pro 8.0's original opening book (`PRBOOK/`), settings (`SETTINGS.M80`), Zobrist/material/PSQT evaluation character, and full 3- and 4-piece DTM endgame tablebases (`TB/`).

---

## 1. Package Contents

| File / Folder | Platform / Purpose |
| :--- | :--- |
| `mchess8_uci` | Linux 32-bit x86 (`ELF 32-bit`) |
| `mchess8_uci_x64` | Linux 64-bit x86_64 (`ELF 64-bit`) |
| `mchess8_uci.exe` | Windows 32-bit x86 (`PE32` console) |
| `mchess8_uci_x64.exe` | Windows 64-bit x86_64 (`PE32+` console) |
| `mchess8_uci_mac_arm64` | macOS Apple Silicon (`arm64` Mach-O) |
| `mchess8_uci_mac_intel` | macOS Intel (`x86_64` Mach-O) |
| `SETTINGS.M80` | Default engine configuration loaded at startup |
| `PRBOOK/` | Original MChess Pro 8.0 opening book (`PRBOOK.M80` + `M80KEY.000`–`00F`) |
| `TB/` | Complete 3- and 4-piece DTM endgame tablebases (35 sets / 70 files) |
| `MANUAL.md` | User manual |
| `WHATS_NEW.md` | Summary of changes vs. original M-Chess Pro 8.0 (1998) |
| `source/` | Complete C source code and `Makefile` |

---

## 2. Installation & Quick Start

1. **Keep the directory layout intact:** Place your chosen binary in the same folder as `PRBOOK/`, `TB/`, and `SETTINGS.M80` (the engine automatically looks for them in the current working directory).
2. **Linux / macOS permissions:**
   ```bash
   chmod +x mchess8_uci_x64        # or mchess8_uci / mchess8_uci_mac_arm64 / mchess8_uci_mac_intel
   ```
   *(On macOS, if Gatekeeper blocks an unsigned downloaded binary, run `xattr -d com.apple.quarantine mchess8_uci_mac_*`.)*
3. **Add to any UCI GUI** (Arena, CuteChess, BanksiaGUI, Scid, Lucas Chess, ChessBase/Fritz):
   - Register the binary for your OS as a **UCI engine**.
   - Set the working directory to the package folder (or configure `BookPath` and `TablebasePath` in the engine options).

---

## 3. Opening Book (`PRBOOK/`) & Book Modes

MChess Pro 8.0 UCI reads the original `PRBOOK.M80` and `M80KEY.000`–`M80KEY.00F` opening book files. Each book move carries an internal priority weight (`0`–`7`) and is selected via weighted random choice (seeded per session).

### Book Options
- **`Book`** (`check`, default: `true`)
  Master switch to enable or disable opening book probing.
- **`BookMode`** (`combo`, default: `Tournament`)
  Controls which book lines are eligible:
  - **`Off`**: Disables book probing; the engine searches from move 1.
  - **`Tournament`** *(default)*: Plays only top-weighted mainlines (`wt >= 4`, e.g., `1. e4` and `1. d4` from the starting position). Best for serious/competitive play.
  - **`Wide`**: Plays all standard book lines (`wt >= 1`, e.g., `1. e4`, `1. d4`, `1. c4`, `1. Nf3`, `1. f4`, `1. b4`, `1. b3`, `1. g3`). Good balance of strength and variety.
  - **`Broad`**: Includes all moves present in the book (`wt >= 0`), including rare sidelines and offbeat openings (`1. c3`, `1. a3`, `1. g4`, `1. Nc3`, etc.).
- **`BookPath`** (`string`, default: `PRBOOK`)
  Directory containing `PRBOOK.M80` and `M80KEY.000`–`M80KEY.00F`.
- **`DeterministicBook`** (`check`, default: `false`)
  When `true`, always selects the highest-weighted book move deterministically (useful for reproducible testing).

---

## 4. Endgame Tablebases (`TB/`)

The engine probes MChess Pro 8.0's proprietary **Distance-to-Mate (DTM)** tablebases (`.TBW` when the stronger side is to move, `.TBB` when the weaker side is to move) during both main search and quiescence search whenever `total_pieces <= 4` (with no en passant or castling rights active).

### Included 3- and 4-Piece Sets (35 Sets / 70 Files)
- **3-Piece (5 sets):** `KQK`, `KRK`, `KBK`, `KNK`, `KPK`
- **4-Piece, 3 vs 1 Pawnless (10 sets):** `KQQK`, `KQRK`, `KQBK`, `KQNK`, `KRRK`, `KRBK`, `KRNK`, `KBBK`, `KBNK`, `KNNK`
- **4-Piece, 2 vs 2 Pawnless (10 sets):** `KQKQ`, `KQKR`, `KQKB`, `KQKN`, `KRKR`, `KRKB`, `KRKN`, `KBKB`, `KBKN`, `KNKN`
- **4-Piece, Pawned (10 sets):** `KQPK`, `KRPK`, `KBPK`, `KNPK`, `KPPK`, `KQKP`, `KRKP`, `KBKP`, `KNKP`, `KPKP`

### Tablebase Options
- **`Tablebases`** (`check`, default: `true`)
  Enables or disables endgame tablebase probing.
- **`TablebasePath`** (`string`, default: `TB`)
  Directory containing the `.TBW` and `.TBB` files.

---

## 5. Playing Style & Evaluation Settings

At startup, the engine loads defaults from `SETTINGS.M80` if present. All MChess Pro 8.0 personality sliders are exposed as UCI options:

| UCI Option | Type | Default | Range | Effect |
| :--- | :--- | :---: | :---: | :--- |
| **`Aggression`** | `spin` | `5` | `0`–`8` | Scales king-zone attack bonuses and king-safety pressure. Higher values favor attacking play. |
| **`Mobility`** | `spin` | `4` | `0`–`8` | Scales piece mobility bonuses for knights, bishops, rooks, and queens. |
| **`Centrality`** | `spin` | `1` | `0`–`8` | Scales piece-square centralization and advancement terms. |
| **`KingPlacement`** | `spin` | `4` | `0`–`8` | Scales middlegame king shelter/castling safety and endgame king centralization. |
| **`PawnStructure`** | `spin` | `5` | `0`–`8` | Scales penalties for doubled and isolated pawns. |
| **`PassedPawns`** | `spin` | `2` | `0`–`8` | Scales passed-pawn advancement and endgame king-support bonuses. |
| **`RandomLimit`** | `spin` | `0` | `0`–`100` | MChess Pro 8.0 random-limit setting (default `0` for tournament play). |
| **`Contempt`** | `spin` | `0` | `-200`–`200` | Draw-avoidance/draw-seeking score in centipawns (applied on 50-move/repetition draws). Positive values avoid draws; negative values seek draws. |

---

## 6. Search, Memory & Thinking Options

| UCI Option | Type | Default | Range | Description |
| :--- | :--- | :---: | :---: | :--- |
| **`Hash`** | `spin` | `512` | `1`–`4096` | Lockless 4-bucket transposition table size in MB with generation aging. *(64-bit builds support up to 4096 MB; 32-bit builds automatically cap at the OS per-process address limit if needed.)* |
| **`Threads`** | `spin` | `1` | `1`–`64` | Number of search threads (Lazy SMP with a persistent worker pool). Set to your physical core count (or logical threads) for multi-core speedup. |
| **`Ponder`** | `check` | `true` | `true`/`false` | Enables thinking on the opponent's time. Outputs `bestmove <move> ponder <reply>` and supports `go ponder`, `ponderhit`, and `stop`. |
| **`NullMove`** | `check` | `true` | `true`/`false` | Enables null-move pruning (disabled automatically in check, PV nodes, and low-material endgames to avoid zugzwang errors). |
| **`Extensions`** | `check` | `true` | `true`/`false` | Enables check extensions (+1 ply when in check, capped at `ply < 24`). |

The search also uses staged TT-first move execution, aspiration windows, Late Move Reductions (LMR), full bitboard X-ray SEE, Reverse Futility / Futility / Late Move Pruning, Razoring, killer/countermove/history ordering, `improving`-aware pruning, endgame null-move verification search, calibrated UCI time pacing (`50%` soft bound / `125%` hard cap with sudden-death low-clock protection), and emits `info depth ... currmove <move> currmovenumber <n>` during root searches (`depth >= 6` or `elapsed >= 200 ms`).

---

## 7. Supported UCI Commands

- **`uci`** — Prints engine ID (`MChess Pro 8.0 UCI`), all options, and `uciok`.
- **`isready`** — Responds `readyok` once initialization is complete (also handled during active search/ponder).
- **`setoption name <Name> value <Value>`** — Configures any option listed above.
- **`ucinewgame`** — Clears the transposition table and move history, and resets to the starting position.
- **`position [startpos | fen <fenstring>] [moves <m1> <m2> ...]`** — Sets up a board position and replays moves.
- **`go ...`** — Starts searching with any combination of:
  - `depth <N>` (up to `63` plies; default cap `32`)
  - `movetime <ms>`
  - `wtime <ms> btime <ms> [winc <ms>] [binc <ms>] [movestogo <N>]`
  - `nodes <N>`
  - `infinite`
  - `ponder` (ponder until `ponderhit` or `stop`)
- **`ponderhit`** — Switches an active `go ponder` search to normal timed search.
- **`stop`** — Interrupts the active search and immediately outputs `bestmove`.
- **`perft <depth>`** — Counts leaf nodes to `<depth>` from the current position (diagnostic).
- **`quit`** — Exits the engine immediately.
