#pragma once

// Bridge between Sigma and the vendored Fathom prober. Fathom's headers can't be included in
// Sigma's sources (Sigma defines white, black, square, file and rank as macros), so this header
// uses only stdint types. SyzygyGlue.c is compiled with Fathom, outside the unity build.

#include <stdint.h>

typedef struct
{
    uint64_t whiteBB, blackBB, kingsBB, queensBB, rooksBB, bishopsBB, knightsBB, pawnsBB; // Square 0 = a1, 63 = h8.
} SZ_POS;

typedef struct
{
    unsigned from, to, promo; // Squares 0..63; promo 0 none, 1 Q, 2 R, 3 B, 4 N.
    int      rank;            // Fathom tbRank: higher is better, equal ranks are equally good.
    int      wdl;             // -2 loss .. 2 win for the side to move at the root.
} SZ_ROOTMOVE;

#define SZ_FAIL      (-100)
#define SZ_MAX_MOVES 193

int  SZ_Init(const char *path); // (Re)initialise; returns the largest piece count loaded, 0 if none.
void SZ_Free(void);
void SZ_FileCounts(int *wdl, int *dtz); // WDL and DTZ files found by the last SZ_Init (0 if none).

// WDL (-2..2) for the side to move, or SZ_FAIL. Needs rule50 == 0 and no castling rights.
int SZ_ProbeWDL(const SZ_POS *p, unsigned ep, int whiteToMove);

// Rank every legal root move (DTZ, falling back to WDL). Returns the move count, or -1.
int SZ_ProbeRoot(const SZ_POS *p, unsigned rule50, unsigned ep, int whiteToMove, int hasRepeated, int useRule50,
                 SZ_ROOTMOVE *out, int maxOut);
