#pragma once

#include "General.h"
#include "Board.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                       CONSTANTS & MACROS                                       */
/*                                                                                                */
/**************************************************************************************************/

#define pvSize 128

#define mateWhite 10
#define mateBlack 11

#define closeness(sq1, sq2) (G->V.Closeness[(sq1) - (sq2)])
#define dist(sq1, sq2)      (9 - closeness(sq1, sq2))

#define cOccupySqPV(c, sq, P, N, B, R, Q, K, P_, N_, B_, R_, Q_, K_)                                                   \
    {                                                                                                                  \
        if (c == white)                                                                                                \
            OccupySqPV(sq, P, N, B, R, Q, K, P_, N_, B_, R_, Q_, K_);                                                  \
        else                                                                                                           \
            OccupySqPV(sq, P_, N_, B_, R_, Q_, K_, P, N, B, R, Q, K);                                                  \
    }
#define cAttackSqPV(c, sq, P, N, B, R, Q, K, P_, N_, B_, R_, Q_, K_)                                                   \
    {                                                                                                                  \
        if (c == white)                                                                                                \
            AttackSqPV(sq, P, N, B, R, Q, K, P_, N_, B_, R_, Q_, K_);                                                  \
        else                                                                                                           \
            AttackSqPV(sq, P_, N_, B_, R_, Q_, K_, P, N, B, R, Q, K);                                                  \
    }

/**************************************************************************************************/
/*                                                                                                */
/*                                          TYPE DEFINITIONS                                      */
/*                                                                                                */
/**************************************************************************************************/

typedef struct                         // Pawn structure information for a single file
{                                      // and colour.
    BOOL   occupied;                   // Is file occupied by pawn(s) of given colour.
    SQUARE sq;                         // Location of front (most advanced) pawn on file.
    INT    rFront, rRear;              // Rank of front/rear pawn(s) on file.
    BOOL   passed, backward, isolated; // Is this pawn passed/backward/isolated?
} PAWNDATA;

/*------------------------------------- Engine Specific State ------------------------------------*/

typedef struct
{

    INT BaseVal[pieces]; // "BaseVal[wPawn.. bKing]". The basis value for each piece
                         // type ≈ ±100*Mtrl[p].   Bishops are usually weighted slightly
                         // more than knights. If one player is materially ahead his
                         // pieces are valued   lower than the opponents pieces thereby
                         // encouraging   exchanges.

    INT PieceVal[pieces][pvSize]; // The piece value table "PieceVal[wKing..bKing][a1..h8]".
                                  // Indicates for each piece on each square the value of the
                                  // piece on the square, reflecting positional aspects like
                                  // centralization, king attack, 7th rank occupation, e.t.c.
                                  // (Note that black pieces have negative piece values). The
                                  // table serves as a basis for the evaluation function (in
                                  // terms of the integer "sumPV" described below) and can be
                                  // defined as follows:
                                  //
                                  //     PieceVal[p][sq] = BaseVal[p] + PosVal(p, sq, Board),
                                  //
                                  // where "PosVal" is a (pseudo) function returning a positio-
                                  // nal value as mentioned above.
                                  // The "PieceVal" table is calculated only at the root of the
                                  // search tree.

    INT sumPV; // The sum of piece values over all pieces on the board.
               // "sumPV" constitutes the central part of the evaluation
               // function. It is calculated at the root of the search tree
               // and is then updated incrementally as moves are being
               // performed and retracted.

    INT styleNormPV; // Playing style normalization value. This value is used to
                     // correct the sumPV base value, so the score won't look
                     // overly optimistic (resp. pessimistic) if the style is
                     // aggressive (resp. defensive).

    COLOUR winColour;  // Colour of side that is materially ahead. = -1 if mtrl is
    COLOUR loseColour; // equal. Losing side.

    //--- Mobility Evaluation ---

    INT MobVal[boardSize]; // Piece mobility evaluation for each occupied square
    INT sumMob;            // Total mobility evaluation sum for all pieces (like sumPV)

    //--- Utility Structures ---

    INT phase;                  // 0..9 (opening ... middle game ... end game). Is
                                // inversely proportional to the officer material on
                                // the board.
    INT TotalMtrl[white_black]; // Total material for each side (in pawn units).
    INT OffiMtrl[white_black];  // Officer material for each side (in pawn units).
    INT PawnMtrl[white_black];  // Pawn material for each side (in pawn units).

    PAWNDATA PawnDataW[8], // Pawn structure information for each file and
        PawnDataB[8];      // colour indicating if it's occupied and if so if
                           // the front most pawn is passed/backward/isolated.

    INT PawnCentrePV[64]; // Centrality bonus based on the geometric centre of all pawns.
                          // Is used in the end game by kings, knights and bishops.

    INT o_oPV[white_black],      // Incremental piece value bonuses for castling. Is
        o_o_oPV[white_black];    // used during search by ComputePVchange().
    BOOL KingRight[white_black]; // Should king be standing to the right (g1/g8)?

    BOOL kbnk; // KBNK ending?

} PIECEVAL_STATE;

/*-------------------------------- Global Read-only Data Structures ------------------------------*/

typedef struct
{
    // Generic base tables:
    INT PawnPV[64];
    INT KnightPV[64];
    INT BishopPV[64];
    INT RookPV[64];
    INT QueenPV[64];
    INT KingPV[64];

    // Special purpose tables:
    INT LoneKingPV[64];
    INT MKingPV[64];
    INT KBNK[64];
    INT Kcf[10];
    INT Closeness_[120], Closeness[136];
} PIECEVAL_COMMON;
