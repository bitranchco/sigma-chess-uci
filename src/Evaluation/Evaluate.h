#pragma once

#include "General.h"
#include "Board.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                       CONSTANTS & MACROS                                       */
/*                                                                                                */
/**************************************************************************************************/

#define kpkDataSize (24 * 1024)

#define queenMob  1
#define rookMob   2
#define bishopMob 3

/**************************************************************************************************/
/*                                                                                                */
/*                                        TYPE DEFINITIONS                                        */
/*                                                                                                */
/**************************************************************************************************/

// Central pawns are penalized more heavily: 7/8 for a, h; 8/8 for b, g; 9/8 for c, d, e and f.

typedef struct
{
    BYTE IsoBackVal[256], // Punishment of isolated/backward pawns on closed files.
        IsoBackVal_[256], // Extra punishment for semi open files.
        IsoVal[256],      // Extra punishment of isolated pawns.
        DobVal[256],      // Punishment of doubled pawns.
        DobIsoVal[256],   // Extra punishment for isolated and doubled pawns.
        PassedVal[256];   // Bonus for passed pawns.
} EVAL_STATE;

typedef struct
{
    RANKBITS BlkPawnsN[256], // Utility bit tables.
        IsoPawns[256];
    BYTE RuleOfSquareTab_[0x77], RuleOfSquareTab[0x78];
    BYTE kpkData[kpkDataSize];
} EVAL_COMMON;
