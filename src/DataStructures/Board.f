#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                        FUNCTION PROTOTYPES                                     */
/*                                                                                                */
/**************************************************************************************************/

void CalcBoardState(ENGINE *E);

void NewBoard(PIECE Board[]);
void ClearTable(INT T[]);
void CopyTable(INT Source[], INT Dest[]);
BOOL EqualTable(INT T1[], INT T2[]);

ULONG CalcPieceCount(GLOBAL *Global, PIECE Board[]);

void InitBoardState(BOARD_STATE *B);
void InitBoardModule(GLOBAL *Global); // Startup initialization routine.
