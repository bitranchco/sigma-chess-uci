#pragma once

#include "Move.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                          TYPE DEFINITIONS                                      */
/*                                                                                                */
/**************************************************************************************************/

typedef ULONG HKEY;

typedef struct
{
    ULONG randKey;

    HKEY HashCode[pieces][128]; // Hash codes for each piece on each square. Used
                                // for incremental update of "hashKey".
    HKEY o_oHashCodeB;          // Hash key changes caused by castling moves.
    HKEY o_oHashCodeW;          // NOTE: "UpdateDrawState" depends on word order.
    HKEY o_o_oHashCodeB;
    HKEY o_o_oHashCodeW;
} HASHCODE_COMMON;
