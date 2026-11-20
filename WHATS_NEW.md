# What's New & Changed vs. Original M-Chess Professional 8.0 (1998)

**MChess Pro 8.0 UCI** is a native, modernized UCI port of Marty Hirsch's 1998 DOS engine (**M-Chess Professional 8.0 / `MCP8.EXE`**). It preserves the chess character, opening book, and endgame tablebases of the original while upgrading the architecture, search, and platform support.

---

## Summary of Differences

| Area | Original `MCP8.EXE` (1998 DOS) | MChess Pro 8.0 UCI |
| :--- | :--- | :--- |
| **Platform** | 32-bit DOS (`DOS/4GW`) | Native **Linux** (32/64-bit), **Windows** (32/64-bit), **macOS** (Intel & Apple Silicon) |
| **Interface** | Built-in DOS graphical UI | Standard **UCI** protocol (`Arena`, `CuteChess`, `BanksiaGUI`, `Scid`, `ChessBase`, etc.) |
| **Board Representation** | 10×12 mailbox (`board[120]`) | **64-bit Magic Bitboards** (`O(1)` slider attacks, `popcnt` eval, **1 MB lockless Pawn Hash Table**) |
| **CPU Threads** | Single-threaded (`1` core) | **Multi-threaded Lazy SMP** (`Threads` `1`–`64`) with persistent worker pool |
| **Transposition Table** | DOS memory-limited hash | Up to **4096 MB** (`Hash` `1`–`4096`, default `512` MB), **4-bucket lockless XOR** with generation aging |
| **Search & Pruning** | Classic 1998 alpha-beta + NMP | Added **Staged TT-First Execution**, **Aspiration Windows**, **LMR**, **X-ray Bitboard SEE**, **RFP**, **Futility**, **LMP**, **Razoring**, **Countermoves**, and **`improving`** pruning |
| **Endgame Tablebases** | External 3/4-piece `.TBW`/`.TBB` | Full **35-set (70-file)** 3- and 4-piece DTM `TB/` bundle included, with cached handles & lock-free POSIX `pread()` |
| **Opening Book** | `PRBOOK.M80` (`Off`/`Tournament`/`Wide`/`Broad`) | Same `PRBOOK/` + modes, plus `DeterministicBook` option and **`Tournament` default** |
| **Playing Style** | `SETTINGS.M80` sliders (`5/4/1/4/5/2`) | Same sliders & MChess Pro 8 eval character with **smooth game-phase interpolation** |

---

## Key Improvements in Detail

### 1. Native Cross-Platform UCI Engine
- Runs directly on modern 64-bit and 32-bit operating systems with no DOSBox or emulation overhead.
- Supports standard UCI commands including `go ponder`, `ponderhit`, `stop`, `isready` (even during active search/ponder), `perft`, and `info depth ... currmove ... currmovenumber ...` reporting.

### 2. 64-Bit Magic Bitboards & Pawn Hash Table (Same Eval Character, ~2.3×+ Speedup)
- Replaced the 10×12 mailbox board scanning and ray loops with 64-bit bitboards and Magic Bitboard lookups (`bitboard.c`).
- Added a **1 MB lockless Pawn Hash Table** (`g_pawn_tt`, 65,536 entries indexed by `pawn_zob`) to cache pawn-structure and passed-pawn bitboards across threads.
- Preserves M-Chess Pro 8.0's exact Zobrist keys, material values, PSQT tables, pawn-structure/passed-pawn/mobility formulas, king pawn shield, and drawish endgame rules.
- Replaces the hard `total_non_pawn <= 2600` endgame cutoff with smooth `0..256` game-phase interpolation so king PSQT and pawn-shield terms transition cleanly across trades.
- Measured **+147 Elo** (`+26 =4 -10`, `70.0%` over 40 games at 50 ms/move) against the 10×12 mailbox port.

### 3. Multi-Threaded Lazy SMP (`Threads` Option)
- Added `Threads` (`1`–`64`, default `1`) backed by a persistent, cache-line-aligned (`_Alignas(64)`) worker pool.
- Uses a 16-byte lockless XOR transposition table (`tt.c`) and lock-free `pread()` tablebase reads on Linux/macOS (`tb.c`) so threads scale cleanly (reaching **4.25M nps** at `depth 12` on 8 threads, **3.61M nps** on 4 threads, and **1.05M nps** at `depth 11` on 1 thread on Kiwipete).

### 4. Modern Search & Tournament-Tuned Defaults
- Added **staged TT-first move execution** (`is_pseudo_legal()` tries `tt_move` before generating or scoring moves, skipping `gen_pseudo_moves()`/`score_moves()` on TT beta cutoffs) and skips redundant `evaluate()` calls on PV nodes.
- Added **endgame & time-management upgrades**: connected/blockaded passed-pawn terms, rook-behind-passer, enemy king distance from passed pawns, semi-open isolated-pawn penalty, endgame null-move verification search, calibrated UCI time pacing (`52`–`60` moves-to-go baseline, `50%` soft bound, `125%` hard cap, and `ponderhit` pacing), and sudden-death low-clock scaling (`+18 =6 -6` / `70.0%` in 30 `5+0` games vs. BikJump v3.1, plus `+12 =17 -11` / `51.2%` in post-match endgame A/B testing).
- Combined Aspiration Windows, LMR, full bitboard X-ray SEE, Reverse Futility / Futility / Late Move Pruning, Razoring, killer/countermove/aged-history move ordering, and `improving`-aware pruning—raising average 300 ms/pos search depth from **`6.20`** to **`12.20`** plies on the 5-position benchmark (and reaching **2.00M nps** single-threaded in endgames).
- Startup defaults (`SETTINGS.M80` and built-in defaults) are tuned for tournament play based on self-play testing:
  - `Hash = 512` MB (max `4096` MB)
  - `BookMode = Tournament` (scored `56.2%` vs. `Wide`)
  - `Ponder = true`
  - `NullMove = true` (scored `60.4%` vs. `NullMove = false`)
  - `Extensions = true`
  - `RandomLimit = 0`
  - `Contempt = 0`
  - Original `SETTINGS.M80` style sliders (`Aggression=5`, `Mobility=4`, `Centrality=1`, `KingPlacement=4`, `PawnStructure=5`, `PassedPawns=2`), which outperformed altered profiles in 40-game confirmation matches.

---

## What Remained Faithful to M-Chess Professional 8.0
- **Opening Book (`PRBOOK/`):** Uses the original `PRBOOK.M80` and `M80KEY.000`–`00F` files and move-weighting logic.
- **Endgame Tablebases (`TB/`):** Uses the original 3- and 4-piece `.TBW`/`.TBB` Distance-to-Mate format across all 35 endgame sets.
- **Personality & Evaluation:** All 6 personality sliders (`Aggression`, `Mobility`, `Centrality`, `KingPlacement`, `PawnStructure`, `PassedPawns`) and MChess Pro 8.0's distinctive evaluation weights remain intact.
