/**************************************************************************************************/
/*                                                                                                */
/* Module  : TABLEBASE.C                                                                          */
/* Purpose : Syzygy endgame tablebase probing (through the Fathom bridge in SyzygyGlue.c).        */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"

#include "Tablebase.f"

static INT tbLargest = 0; // Largest piece count loaded (0 = no tables). Fathom state is global.

/**************************************************************************************************/
/*                                                                                                */
/*                                         SETUP & GATING                                         */
/*                                                                                                */
/**************************************************************************************************/

void TB_SetPath(const char *path) // "" or "<empty>" frees the tables.
{
    if (!path || !*path || strcmp(path, "<empty>") == 0)
    {
        SZ_Free();
        tbLargest = 0;
        return;
    }
    tbLargest = (INT)SZ_Init(path);

    // Report what loaded (the usual "info string Found ..." form), so a mistyped path is visible.
    int wdl, dtz;
    SZ_FileCounts(&wdl, &dtz);
    if (tbLargest > 0)
        printf("info string Found %d WDL and %d DTZ tablebase files (up to %d-man).\n", wdl, dtz, tbLargest);
    else
        printf("info string Found 0 WDL and 0 DTZ tablebase files.\n");
    fflush(stdout);
} /* TB_SetPath */

INT TB_Cardinality(ENGINE *E) // Largest piece count we may probe, 0 if disabled.
{
    return Min(tbLargest, E->P.tbProbeLimit);
} /* TB_Cardinality */

INT TB_PieceCount(ULONG pc) // Pawns, minors and majors per side (see PieceCountBit), plus the kings.
{
    return (INT)((pc & 0xF) + ((pc >> 8) & 0xF) + ((pc >> 12) & 0xF) + ((pc >> 16) & 0xF) + ((pc >> 24) & 0xF) +
                 ((pc >> 28) & 0xF) + 2);
} /* TB_PieceCount */

/**************************************************************************************************/
/*                                                                                                */
/*                                         BOARD CONVERSION                                       */
/*                                                                                                */
/**************************************************************************************************/

BOOL TB_BuildPos(const PIECE Board[], SZ_POS *p) // 0x88 board -> bitboards (bit rank*8 + file).
{
    memset(p, 0, sizeof(*p));
    for (INT r = 0; r < 8; r++)
        for (INT f = 0; f < 8; f++)
        {
            PIECE pc = Board[square(f, r)];
            if (pc == empty)
                continue;
            uint64_t bit = (uint64_t)1 << (r * 8 + f);
            if (pieceColour(pc) == white)
                p->whiteBB |= bit;
            else
                p->blackBB |= bit;
            switch (pieceType(pc))
            {
            case pawn:
                p->pawnsBB |= bit;
                break;
            case knight:
                p->knightsBB |= bit;
                break;
            case bishop:
                p->bishopsBB |= bit;
                break;
            case rook:
                p->rooksBB |= bit;
                break;
            case queen:
                p->queensBB |= bit;
                break;
            case king:
                p->kingsBB |= bit;
                break;
            default:
                return false;
            }
        }
    return true;
} /* TB_BuildPos */

SQUARE TB_EpSquare(const MOVE *m) // Square passed over by a double pawn push, else nullSq.
{
    if (isNull(*m) || pieceType(m->piece) != pawn || (m->to - m->from != 0x20 && m->from - m->to != 0x20))
        return nullSq;
    return (m->from + m->to) / 2;
} /* TB_EpSquare */

INT TB_ScoreFromWDL(ENGINE *E, INT wdl, INT depth) // Score for the side to move.
{
    switch (wdl)
    {
    case 2:
        return tbWinVal - depth;
    case 1:
        return E->P.tbRule50 ? drawVal + 1 : tbWinVal - depth;
    case -1:
        return E->P.tbRule50 ? drawVal - 1 : depth - tbWinVal;
    case -2:
        return depth - tbWinVal;
    default:
        return drawVal;
    }
} /* TB_ScoreFromWDL */

/**************************************************************************************************/
/*                                                                                                */
/*                                           ROOT PROBING                                         */
/*                                                                                                */
/**************************************************************************************************/

// If the root is in the tables, keep only the root moves with the best tablebase rank (Fathom's
// scheme: every win that converts within the 50-move rule ranks equal, so the search still picks
// among them). With MultiPV, keep every move with the best result instead, so analysis shows
// several lines. Runs after "go searchmoves" has restricted the root moves. Sets tbRootScore.

void TB_FilterRootMoves(ENGINE *E)
{
    SEARCH_STATE      *S = &E->S;
    static SZ_ROOTMOVE TM[SZ_MAX_MOVES];
    static const PIECE PromoType[5] = {empty, queen, rook, bishop, knight};
    INT                Key[maxLegalMoves]; // Rank (or result) of each root move.
    SZ_POS             pos;

    S->tbRootActive = false;
    if (TB_Cardinality(E) == 0 || E->P.reduceStrength || E->P.playingMode == mode_Mate ||
        TB_PieceCount(E->B.pieceCount) > TB_Cardinality(E) || castleRights(E->B.Board, E->B.HasMovedTo) != 0 ||
        !TB_BuildPos(E->B.Board, &pos))
        return;

    DRAWDATA *dt = S->rootNode->drawData;
    SQUARE    ep = TB_EpSquare(&E->P.lastMove);

    // Fathom keeps the clock in 8 bits; past 100 the result no longer changes, so cap it there.
    // "Repeated" means any position since the last capture or pawn move occurred before, not
    // only the root: Fathom then ranks wins by DTZ, so the engine makes progress instead of
    // shuffling into a repetition draw.
    LONG rule50      = MinL(100, Rule50Count(E, dt, E->P.lastMoveNo));
    BOOL hasRepeated = false;
    for (INT k = 0; k <= E->P.lastMoveNo - dt->irr && !hasRepeated; k++)
        hasRepeated = dt[-k].repCount > 0;

    INT n = SZ_ProbeRoot(&pos, (unsigned)rule50, ep == nullSq ? 0 : rank(ep) * 8 + file(ep), E->P.player == white,
                         hasRepeated, E->P.tbRule50, TM, SZ_MAX_MOVES);
    if (n <= 0)
        return;

    BOOL byResult = (S->multiPVCount > 1);
    INT  best = -maxint, bestWdl = -2, k = 0;

    for (INT i = 0; i < S->numRootMoves; i++) // Find each root move in Fathom's list.
    {
        MOVE *m = &S->RootMoves[i];
        INT   j;
        for (j = 0; j < n; j++)
        {
            INT from = (INT)TM[j].from, to = (INT)TM[j].to; // Fathom squares: rank * 8 + file.
            if (square(from & 7, from >> 3) == m->from && square(to & 7, to >> 3) == m->to &&
                PromoType[TM[j].promo] == (isPromotion(*m) ? pieceType(m->type) : empty))
                break;
        }
        if (j == n)
            return; // Move lists disagree: leave the root alone.
        Key[i] = (byResult ? TM[j].wdl : TM[j].rank);
        if (Key[i] > best)
            best = Key[i], bestWdl = TM[j].wdl;
    }

    for (INT i = 0; i < S->numRootMoves; i++)
        if (Key[i] == best)
            S->RootMoves[k++] = S->RootMoves[i];
    S->numRootMoves = k;

    S->tbRootActive = true;
    S->tbRootScore  = TB_ScoreFromWDL(E, bestWdl, 0);
    S->tbHits += n; // One probe per root move.
} /* TB_FilterRootMoves */

/**************************************************************************************************/
/*                                                                                                */
/*                                          SEARCH PROBING                                        */
/*                                                                                                */
/**************************************************************************************************/

// Probe the WDL tables at a search node reached by a capture or pawn move (halfmove clock 0, as
// the WDL tables require). Cheapest tests first: most nodes fail the piece count. Results are not
// stored in the transposition table (the caller exits the node without storing). Like a
// transposition table cutoff, never at a PV node: the deeper PV nodes would keep their pvNode
// flags and replay the old line's moves unchecked in the next root move's subtree.

BOOL TB_ProbeNode(ENGINE *E, NODE *N, INT *score)
{
    INT card = TB_Cardinality(E);
    if (card == 0 || TB_PieceCount(E->B.pieceCount) > card || N->drawData->irr != N->gameDepth ||
        N->drawData->castle != 0 || N->ply < E->P.tbProbeDepth || N->pvNode || E->P.reduceStrength ||
        E->P.playingMode == mode_Mate)
        return false;

    SZ_POS pos;
    if (!TB_BuildPos(E->B.Board, &pos))
        return false;

    SQUARE ep  = TB_EpSquare(&(N - 1)->m);
    INT    wdl = SZ_ProbeWDL(&pos, ep == nullSq ? 0 : rank(ep) * 8 + file(ep), N->player == white);
    if (wdl == SZ_FAIL)
        return false;

    E->S.tbHits++;
    *score = TB_ScoreFromWDL(E, wdl, N->depth);
    return true;
} /* TB_ProbeNode */
