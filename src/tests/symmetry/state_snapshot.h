#pragma once

#include "Engine.h"

typedef struct
{
    PIECE    Board[boardSize];
    INT      HasMovedTo[boardSize];
    INDEX    PLinx[boardSize];
    SQUARE   PieceLoc[32];
    RANKBITS PawnStructW[8];
    RANKBITS PawnStructB[8];
    ULONG    pieceCount;
    INDEX    LastOffi[white_black];
    INDEX    LastPiece[white_black];
    ATTACK   AttackW[boardSize];
    ATTACK   AttackB[boardSize];
    INDEX    capInx;
    INDEX    promInx;
    INT      mobEval;
    INT      next_mobEval;
} EngineSnapshot;

void Snapshot(const ENGINE *E, EngineSnapshot *snap);
int  AssertSnapshotEqual(const EngineSnapshot *a, const EngineSnapshot *b, const char *label);
