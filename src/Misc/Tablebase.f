#pragma once

#include "Engine.h"
#include "SyzygyGlue.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                          FUNCTION PROTOTYPES                                   */
/*                                                                                                */
/**************************************************************************************************/

void   TB_SetPath(const char *path);
INT    TB_Cardinality(ENGINE *E);
INT    TB_PieceCount(ULONG pieceCount);
BOOL   TB_BuildPos(const PIECE Board[], SZ_POS *p);
SQUARE TB_EpSquare(const MOVE *lastMove);
INT    TB_ScoreFromWDL(ENGINE *E, INT wdl, INT depth);
void   TB_FilterRootMoves(ENGINE *E);
BOOL   TB_ProbeNode(ENGINE *E, NODE *N, INT *score);
