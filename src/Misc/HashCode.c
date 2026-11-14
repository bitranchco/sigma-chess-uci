/**************************************************************************************************/
/*                                                                                                */
/* Module  : HASHCODE.C                                                                           */
/* Purpose : This module implements the hash code routines.                                       */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"

#include "HashCode.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                   COMPUTING/UPDATING HASH KEY                                  */
/*                                                                                                */
/**************************************************************************************************/

HKEY CalcHashKey(GLOBAL *Global, PIECE Board[]) // Computes "hashKey" from scratch for the
{                                               // specified board position.
    register HKEY hashKey = 0;

    for (SQUARE sq = a1; sq <= h8; sq++)
        if (onBoard(sq) && Board[sq])
            hashKey ^= Global->H.HashCode[Board[sq]][sq];

    return hashKey;
} /* CalcHashKey */

HKEY HashKeyChange(GLOBAL *Global, MOVE *m) // Computes and returns hash key change caused by the move m.
{
    HASHCODE_COMMON *H = &(Global->H);
    HKEY             dkey;
    COLOUR           player = pieceColour(m->piece);

    dkey = H->HashCode[m->piece][m->from] ^ H->HashCode[m->piece][m->to];

    if (m->cap)
        dkey ^= H->HashCode[m->cap][m->to];

    switch (m->type)
    {
    case mtype_Normal:
        break;
    case mtype_O_O:
        dkey ^= H->HashCode[rook + player][m->from + 1] ^ H->HashCode[rook + player][m->from + 3];
        break;
    case mtype_O_O_O:
        dkey ^= H->HashCode[rook + player][m->from - 1] ^ H->HashCode[rook + player][m->from - 4];
        break;
    case mtype_EP:
        dkey ^= H->HashCode[pawn + (black - player)][m->to - (player == white ? 0x10 : -0x10)];
        break;
    default:
        dkey ^= H->HashCode[m->type][m->to];
    }

    return dkey;
} /* HashKeyChange */

/**************************************************************************************************/
/*                                                                                                */
/*                                      START UP INITIALIZATION                                   */
/*                                                                                                */
/**************************************************************************************************/

static HKEY RandKey(GLOBAL *Global);

void InitHashCodeModule(GLOBAL *Global) // Initializes transposition hash codes.
{
    HASHCODE_COMMON *H = &(Global->H);

    H->randKey = 310660507;

    for (PIECE p = pawn; p <= king; p++)
        for (SQUARE sq = a1; sq <= h8; sq++)
            if (onBoard(sq))
            {
                H->HashCode[white + p][sq] = RandKey(Global);
                H->HashCode[black + p][sq] = RandKey(Global);
            }

    // Clear hashcodes for pawns on first an eighth rank:
    for (INT f = 0; f <= 7; f++)
    {
        H->HashCode[wPawn][square(f, 0)] = 0;
        H->HashCode[wPawn][square(f, 7)] = 0;
        H->HashCode[bPawn][square(f, 0)] = 0;
        H->HashCode[bPawn][square(f, 7)] = 0;
    }

    H->o_oHashCodeW   = H->HashCode[wRook][h1] ^ H->HashCode[wRook][f1];
    H->o_oHashCodeB   = H->HashCode[bRook][h8] ^ H->HashCode[bRook][f8];
    H->o_o_oHashCodeW = H->HashCode[wRook][a1] ^ H->HashCode[wRook][d1];
    H->o_o_oHashCodeB = H->HashCode[bRook][a8] ^ H->HashCode[bRook][d8];
} /* InitHashCodeModule */

static HKEY RandKey(GLOBAL *Global)
{
    Global->H.randKey *= 1103515245;
    Global->H.randKey += 12345;
    return Global->H.randKey;
} /* RandKey */
