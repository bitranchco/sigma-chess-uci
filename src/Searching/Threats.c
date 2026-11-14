/**************************************************************************************************/
/*                                                                                                */
/* Module  : THREATS.C                                                                            */
/* Purpose : This module implements the threat handling.                                          */
/*                                                                                                */
/**************************************************************************************************/

#include "Threats.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                       THREAT ANALYSIS                                          */
/*                                                                                                */
/**************************************************************************************************/

static void AnalyzeThreats0(ENGINE *E);
static void AnalyzeThreats1(ENGINE *E);
static void AnalyzeThreats2(ENGINE *E);

void AnalyzeThreats(ENGINE *E)
{
    NODE *N = E->S.currNode;

    if (!N->quies)
    {
        if (!N->check)
            AnalyzeThreats0(E);
        else
        {
            N->threatEval = maxVal;
            N->escapeSq   = nullSq;
        }
    }
    else if (N->maxPly > 0)
        AnalyzeThreats1(E);
    else
        AnalyzeThreats2(E);
} /* AnalyzeThreats */

/*--------------------------------------- Full Width Analysis ------------------------------------*/
// Analyzes threats at a full width node. The main part of the analysis is threats of higher valued
// or undefended pieces. Additionally, opponent has pawns on his 6th or 7th rank are also considered
// as threats. On exit:
// • N->escapeSq   : The location of the highest valued hung piece (if any).
// • N->eply       : Controls whether escape moves should be extended for this piece. = 0 if
//                   there is only one hung piece and it is attacked by a lower valued piece.
// • N->threatEval : Contains a pessimistic threat evaluation.
// • N->Aloc/SLoc  : Contains the locations of the remaining attacked resp. safe pieces (excl.
//                   the king).

// Helper: Get attacker material value from SmattMask attack bits
static INT SmattMtrl(ATTACK a)
{
    if (a & pMask)
        return pawnMtrl;
    if (a & (nMask | bMask))
        return knightMtrl;
    return rookMtrl; // Rook or queen attacker
}

static void AnalyzeThreats0(ENGINE *E)
{
    NODE   *N        = E->S.currNode;
    SQUARE *PieceLoc = (N->player == white ? E->B.PieceLocW : E->B.PieceLocB);

    SQUARE tsq  = nullSq;
    INT    tply = 1;
    PIECE  tp   = empty; // Piece type at escape square

    INT ALoc_i = 0; // Index into ALoc array
    INT SLoc_i = 0; // Index into SLoc array

    // Scan all player pieces (except king at index 0), most valuable first.
    for (INT i = 1; i <= N->lastPiece; i++)
    {
        SQUARE sq = PieceLoc[i];
        if (sq & 0x88)
            continue;

        ATTACK a = N->Attack_[sq];

        if (!a)
        {
            N->SLoc[SLoc_i++] = sq;
        }
        else if (tsq != nullSq)
        {
            N->ALoc[ALoc_i++] = sq;
        }
        else
        {
            PIECE  piece = E->B.Board[sq];
            ATTACK smatt = E->Global->A.SmattMask[piece];

            if (a & smatt)
            {
                tsq  = sq;
                tp   = piece;
                tply = 0;
            }
            else if (!N->Attack[sq])
            {
                tsq = sq;
                tp  = piece;
            }
            else
            {
                N->ALoc[ALoc_i++] = sq;
            }
        }
    }

    N->ALoc[ALoc_i] = nullSq;
    N->SLoc[SLoc_i] = nullSq;
    N->eply         = tply;
    N->escapeSq     = tsq;

    INT threatEval = 0;

    if (tsq != nullSq)
    {
        threatEval = 100 * E->Global->B.Mtrl[pieceType(tp)];
    }

    // Check for far pawn threats: opponent pawns 1-2 ranks before promotion.
    // Mirrors upstream asm which reads a 2-byte halfword at PawnStructW+9 (white player)
    // or PawnStructW+5 (black player) — i.e. PawnStructB[1..2] or PawnStructW[5..6].
    // (Old code did `PawnStructW[9] || PawnStructB[9]` which read past PawnStructB
    // into the next struct field — caused threatEval += 900 to fire spuriously.)
    BOOL oppFarPawn;
    if (N->player == white)
        oppFarPawn = E->B.PawnStructB[1] || E->B.PawnStructB[2]; // black pawns on ranks 2-3 (white's 7-6)
    else
        oppFarPawn = E->B.PawnStructW[5] || E->B.PawnStructW[6]; // white pawns on ranks 6-7
    if (oppFarPawn)
        threatEval += 900;

    // Check for mate threat (king surrounded)
    SQUARE ksq = PieceLoc[0];
    ATTACK ka  = 0;

    // Accumulate all opponent attacks around king
    static const INT KingDir[8] = {-0x11, -0x10, -0x0F, -0x01, 0x01, 0x0F, 0x10, 0x11};
    for (INT i = 0; i < 8; i++)
        ka |= N->Attack_[ksq + KingDir[i]];

    // Count bits in ka (if more than 1 attack, it's a mate threat)
    INT    bitCount = 0;
    ATTACK temp     = ka;
    while (temp)
    {
        bitCount += temp & 1;
        temp >>= 1;
    }

    if (bitCount > 1)
        threatEval = maxVal;

    N->threatEval = threatEval;
} /* AnalyzeThreats0 */

/*------------------------------- Quiescence Analysis incl. Escapes ------------------------------*/
// Analyzes threats at a "shallow" quiescence node (where escapes may be searched). On exit:
// • N->escapeSq  : The location of THE piece (if any) that is threatened by a smaller piece.
//                  If there is more than one hung piece, N->escapeSq is set to "nullSq".
// • N->eply       : Controls whether escape moves should be extended for this piece. = 0 if
//                   there is only one hung piece and it is attacked by a lower valued piece.
// • N->threatEval : Contains a pessimistic threat evaluation.

static void AnalyzeThreats1(ENGINE *E)
{
    NODE   *N        = E->S.currNode;
    SQUARE *PieceLoc = (N->player == white ? E->B.PieceLocW : E->B.PieceLocB);

    SQUARE tsq  = nullSq;
    INT    tply = 1;
    INT    tval = 0;

    for (INT i = 1; i <= N->lastPiece; i++)
    {
        SQUARE sq = PieceLoc[i];
        if (sq & 0x88)
            continue;

        ATTACK a = N->Attack_[sq];
        if (!a)
            continue;

        if (tval == 0)
        {
            PIECE  piece = E->B.Board[sq];
            ATTACK smatt = E->Global->A.SmattMask[piece];
            a &= smatt;

            if (a)
            {
                tval = E->Global->B.Mtrl[pieceType(piece)];
                tval -= SmattMtrl(a);
                tval *= 100;
                tply          = 0;
                tsq           = sq;
                N->eply       = tply;
                N->escapeSq   = tsq;
                N->threatEval = tval;
                return;
            }
            else if (!N->Attack[sq])
            {
                tval = N->hungVal1;
            }
        }
        else
        {
            if (!N->Attack[sq] || (a & E->Global->A.SmattMask[E->B.Board[sq]]))
            {
                tval          = N->hungVal2;
                tply          = 1;
                tsq           = nullSq;
                N->eply       = tply;
                N->escapeSq   = tsq;
                N->threatEval = tval;
                return;
            }
        }
    }

    N->escapeSq   = tsq;
    N->eply       = tply;
    N->threatEval = tval;
} /* AnalyzeThreats1 */

/*------------------------------- Quiescence Analysis excl. Escapes ------------------------------*/
// Analyzes threats at a "deep" quiescence node (where no escapes are searched). On exit:
// • N->threatEval : Contains a conservative estimate of any threats.

static void AnalyzeThreats2(ENGINE *E)
{
    NODE   *N        = E->S.currNode;
    SQUARE *PieceLoc = (N->player == white ? E->B.PieceLocW : E->B.PieceLocB);

    SQUARE tsq  = nullSq;
    INT    tply = 1;
    INT    tval = 0;

    for (INT i = 1; i <= N->lastPiece; i++)
    {
        SQUARE sq = PieceLoc[i];
        if (sq & 0x88)
            continue;

        ATTACK a = N->Attack_[sq];
        if (!a)
            continue;

        PIECE piece = E->B.Board[sq];
        if (!(a & E->Global->A.SmattMask[piece]))
            continue;

        if (tval == 0)
            tval = N->hungVal1;
        else
        {
            tval          = N->hungVal2;
            N->escapeSq   = tsq;
            N->eply       = tply;
            N->threatEval = tval;
            return;
        }
    }

    N->escapeSq   = tsq;
    N->eply       = tply;
    N->threatEval = tval;
} /* AnalyzeThreats2 */
