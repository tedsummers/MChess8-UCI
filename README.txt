MChess Pro 8.0 UCI (Standalone Package)
=======================================

Contents:
  mchess8_uci           - 32-bit Linux ELF UCI engine binary
  mchess8_uci_x64       - 64-bit Linux ELF UCI engine binary
  mchess8_uci.exe       - 32-bit Windows PE UCI engine binary
  mchess8_uci_x64.exe   - 64-bit Windows PE UCI engine binary
  mchess8_uci_mac_arm64 - 64-bit macOS Apple Silicon (arm64) binary
  mchess8_uci_mac_intel - 64-bit macOS Intel (x86_64) binary
  SETTINGS.M80          - Default MChess Pro 8.0 engine settings
  PRBOOK/               - Opening Book (PRBOOK.M80 + M80KEY.000..00F)
  TB/                   - Complete 3- and 4-piece Endgame Tablebases (.TBW/.TBB)
  MANUAL.md             - Full user manual for features, modes, and options
  WHATS_NEW.md          - Summary of changes vs. original M-Chess Pro 8.0 (1998)
  source/               - Complete C source code and Makefile

Usage:
  Run the appropriate binary for your platform from this directory,
  or point BookPath / TablebasePath UCI options to PRBOOK/ and TB/.

UCI Options:
  Hash (1..4096 MB, default 512), Threads (1..64, default 1),
  Book (true/false), BookMode (Off/Tournament/Wide/Broad, default Tournament),
  BookPath, DeterministicBook, Tablebases (true/false), TablebasePath,
  Aggression (0..8), Mobility (0..8), Centrality (0..8), KingPlacement (0..8),
  PawnStructure (0..8), PassedPawns (0..8), RandomLimit (0..100, default 0),
  Contempt (-200..200), NullMove (true/false), Extensions (true/false),
  Ponder (true/false, default true)
