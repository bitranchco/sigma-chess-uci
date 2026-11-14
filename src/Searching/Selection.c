/**************************************************************************************************/
/*                                                                                                */
/* Module  : SELECTION.C                                                                          */
/* Purpose : This module implements various selection routines used during the search.            */
/*                                                                                                */
/**************************************************************************************************/

#include "Selection.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                 COMPUTING SELECTIVE BASE VALUE                                 */
/*                                                                                                */
/**************************************************************************************************/

void ComputeSelBaseVal(ENGINE *E)
{
    NODE *N = E->S.currNode;

    BYTE pawns7th;
    BOOL opponentHasOfficers, playerHasPawns;

    // Check endgame conditions based on player color
    if (N->player == white)
    {
        opponentHasOfficers = (E->B.pieceCount & 0xFFF00000) != 0;
        playerHasPawns      = (E->B.pieceCount & 0x000F) != 0;
        pawns7th            = E->B.PawnStructW[6];
    }
    else
    {
        opponentHasOfficers = (E->B.pieceCount & 0x0000FFF0) != 0;
        playerHasPawns      = (E->B.pieceCount & 0x000F0000) != 0;
        pawns7th            = E->B.PawnStructB[1];
    }

    // if ("opponent has no officers and player has pawns")
    if (!opponentHasOfficers && playerHasPawns)
    {
        N->selMargin = maxVal;
        return;
    }

    // else if ("player has pawns on 7th")
    if (pawns7th != 0)
    {
        N->selMargin = maxVal;
        return;
    }

    // else
    INT selMargin = 4 * (N->ply - 1);
    //    N->selMargin = 4*(N->ply - 1);

    if (PN->escapeSq != nullSq && PN->m.from != PN->escapeSq && N->Attack[PN->escapeSq] != 0)
    {
        selMargin += PN->threatEval;
    }

    N->selMargin = selMargin;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                        FORWARD PRUNE MOVES                                     */
/*                                                                                                */
/**************************************************************************************************/

// Forward declarations of the threat-evaluation helpers used by SelectMove.
static BOOL Turbulent(ENGINE *E, LONG mval);
static BOOL DirectThreat(ENGINE *E, PIECE tp);
static BOOL IndirectThreat(ENGINE *E, PIECE tp, ATTACK qrbAtt);

BOOL SelectMove(ENGINE *E)
{
    NODE *N = E->S.currNode;

    // Never prune special moves
    if (N->m.type != mtype_Normal || N->gen == gen_J)
        return true;

    // Calc evaluation difference
    // diff = -NN->totalEval + N->selMargin - N->alpha. If this is positive, the move should
    // immediately be searched. If it's negative we need to check if the move is turbulent
    // enough to outweigh the poor positional/material score.

    // 32 bits: a 16-bit diff can wrap.
    LONG diff = (LONG)-NN->totalEval + N->selMargin - N->alpha;

    if (N->m.cap != empty)
        diff += 40 - N->capSelVal;

    if (pieceType(N->m.piece) == pawn)
        diff += 35;

    if (diff > 0)
        return true;

    return Turbulent(E, -diff);
}

/**************************************************************************************************/
/*                                                                                                */
/*                                       TURBULENCE CHECKING                                      */
/*                                                                                                */
/**************************************************************************************************/

// The main turbulence check decides if N->m poses a threat worth at least `mval` points.
// It first picks the minimum target piece type `tp` that satisfies `mval`, then checks
// for direct threats (the moved piece directly attacks an enemy of value >= tp) and
// indirect/discovered threats (moving from m.from unblocks a friendly slider whose ray
// hits an enemy of value >= tp).
//
// `tp` is expressed in the OPPONENT's piece encoding (e.g., for white player, tp = bPawn,
// bKnight, bRook, bQueen, or bKing depending on mval).

static PIECE TpFromMval(COLOUR player, LONG mval)
{
    INT base;
    if (mval <= 350)
        base = (mval < 150) ? pawn : knight;
    else if (mval < 550)
        base = rook;
    else if (mval < 950)
        base = queen;
    else
        base = king;

    // Asm: `li tp, base+black; subf tp, rPlayer, tp` → tp = base + black - player.
    // For white player: tp = base + 0x10 (= b<base>). For black: tp = base (= w<base>).
    return base + black - player;
}

static BOOL Turbulent(ENGINE *E, LONG mval)
{
    NODE *N = E->S.currNode;

    PIECE tp = TpFromMval(N->player, mval);

    if (DirectThreat(E, tp))
        return true;

    // Indirect: any friendly slider attacking m.from?
    ATTACK qrbAtt = N->Attack[N->m.from] & qrbMask;
    if (qrbAtt)
        return IndirectThreat(E, tp, qrbAtt);

    return false;
}

/*--------------------------------------- Direct Threats -----------------------------------------*/
// Helpers for the per-piece direct-threat scans. Each returns true if the moved piece directly
// threatens an enemy worth >= tp.

// Knight-style: probe square `to + dir` for a target piece worth >= tp, value <= killEnemy
// (i.e. a real enemy piece type), AND either worth >= worthEnemy or undefended.
static BOOL dtN(ENGINE *E, SQUARE to, INT dir, PIECE tp, PIECE killEnemy, PIECE worthEnemy)
{
    PIECE p = E->B.Board[to + dir];
    if (p < tp || p > killEnemy)
        return false;
    if (p >= worthEnemy)
        return true;
    return E->S.currNode->Attack_[to + dir] == 0;
}

// King-style: like dtN but with no "worth >= X" shortcut — the target square must be
// undefended (else the king would be lost in retaliation).
static BOOL dtK(ENGINE *E, SQUARE to, INT dir, PIECE tp, PIECE rookEnemy)
{
    PIECE p = E->B.Board[to + dir];
    if (p < tp || p > rookEnemy)
        return false;
    return E->S.currNode->Attack_[to + dir] == 0;
}

// QRB-style: scan from `to` along `tdir` to the first non-empty square, then check it the
// same way as dtN. Returns true if a threat is found.
static BOOL dtQRB(ENGINE *E, SQUARE to, INT tdir, PIECE tp, PIECE killEnemy, PIECE worthEnemy)
{
    SQUARE sq = to;
    PIECE  p;
    do
    {
        sq += tdir;
        p = E->B.Board[sq];
    } while (p == empty);

    if (p < tp || p > killEnemy)
        return false;
    if (p >= worthEnemy)
        return true;
    return E->S.currNode->Attack_[sq] == 0;
}

static BOOL DirectThreat(ENGINE *E, PIECE tp)
{
    NODE   *N      = E->S.currNode;
    GLOBAL *G      = E->Global;
    SQUARE  mto    = N->m.to;
    SQUARE  ksq_   = N->PieceLoc_[0]; // enemy king location
    PIECE   mpiece = pieceType(N->m.piece);
    COLOUR  player = N->player;

    // Closeness shortcut: if the destination is very close to the enemy king, treat as threat.
    if (G->V.Closeness[mto - ksq_] >= 7)
        return true;

    // Enemy piece-type values for this player.
    PIECE eKing   = king + black - player;
    PIECE eQueen  = queen + black - player;
    PIECE eRook   = rook + black - player;
    PIECE eKnight = knight + black - player;
    PIECE ePawn   = pawn + black - player;

    switch (mpiece)
    {
    case pawn: {
        // Asm: `if (rank(m.to) in {6,7}) return true` (i.e. pawn move close to promotion).
        // For white, this means moving to chess rank 7 or 8 (= 0-indexed 6 or 7).
        // For black, moving to chess rank 1 or 2 (= 0-indexed 0 or 1).
        INT toRank = mto >> 4;
        if (player == white)
        {
            if (toRank >= 6)
                return true;
        }
        else
        {
            if (toRank <= 1)
                return true;
        }

        // Check the two diagonal squares ahead of the pawn (m.to + pawnDir +/- 1) for
        // enemies threatened by the pawn. Threats count if worth >= knight, OR if undefended.
        if (dtN(E, mto + N->pawnDir, -1, tp, eKing, eKnight))
            return true;
        if (dtN(E, mto + N->pawnDir, +1, tp, eKing, eKnight))
            return true;
        return false;
    }

    case knight: {
        // 8 knight directions. rTmp2 (worth-shortcut) = enemy(rook).
        static const INT KNDir[8] = {-0x0E, -0x12, -0x1F, -0x21, +0x12, +0x0E, +0x21, +0x1F};
        for (INT i = 0; i < 8; i++)
            if (dtN(E, mto, KNDir[i], tp, eKing, eRook))
                return true;
        return false;
    }

    case bishop: {
        // Bishop direct threat: if tp <= enemy(queen) AND an enemy queen attacks m.to via a
        // DIAGONAL ray, the position is tactically rich enough to count as turbulent.
        // Asm reads low halfword of Attack_[mto] and ANDs with 0x0F = queen diagonal bits 0-3.
        if (tp <= eQueen && (N->Attack_[mto] & 0x0F))
            return true;

        // Then scan diagonals along the move direction (m.dir) and the perpendiculars.
        INT mdir = N->m.dir;
        if (N->m.cap != empty)
        {
            if (dtQRB(E, mto, mdir, tp, eKing, eRook))
                return true;
        }
        INT tdir = G->B.Turn90[mdir];
        if (dtQRB(E, mto, tdir, tp, eKing, eRook))
            return true;
        if (dtQRB(E, mto, -tdir, tp, eKing, eRook))
            return true;
        return false;
    }

    case rook: {
        // Rook direct threat: if tp <= enemy(queen) AND an enemy queen attacks m.to via an
        // ORTHOGONAL ray, the position is turbulent.
        // Asm reads low halfword of Attack_[mto] and ANDs with 0xF0 = queen orthogonal bits 4-7.
        if (tp <= eQueen && (N->Attack_[mto] & 0xF0))
            return true;

        INT mdir = N->m.dir;
        if (N->m.cap != empty)
        {
            if (dtQRB(E, mto, mdir, tp, eKing, eQueen))
                return true;
        }
        INT tdir = G->B.Turn90[mdir];
        if (dtQRB(E, mto, tdir, tp, eKing, eQueen))
            return true;
        if (dtQRB(E, mto, -tdir, tp, eKing, eQueen))
            return true;
        return false;
    }

    case queen: {
        // Scan all 8 queen directions, skipping the move direction itself and its negation.
        INT mdir = N->m.dir;
        for (INT i = 0; i < 8; i++)
        {
            INT tdir = G->B.QueenDir[i];
            if (tdir == 0)
                break;
            // Skip mdir (and skip -mdir unless m.cap, mirroring asm logic).
            if (tdir == mdir)
            {
                if (N->m.cap == empty)
                    continue;
            }
            else if (tdir + mdir == 0)
            {
                continue;
            }
            if (dtQRB(E, mto, tdir, tp, eKing, eKing))
                return true;
        }
        return false;
    }

    case king: {
        // King threats: only if tp <= enemy(rook), and only on undefended squares.
        if (tp > eRook)
            return false;
        static const INT KDir[8] = {-0x0F, -0x11, +0x11, +0x0F, -0x10, +0x10, +0x01, -0x01};
        for (INT i = 0; i < 8; i++)
            if (dtK(E, mto, KDir[i], tp, eRook))
                return true;
        return false;
    }
    }
    return false;
}

/*-------------------------------------- Indirect Threats ----------------------------------------*/
// Checks if moving from N->m.from opens a line for a friendly QRB slider whose ray now hits
// an enemy worth >= tp. qrbAtt = N->Attack[m.from] & qrbMask (precomputed by caller).

static BOOL IndirectThreat(ENGINE *E, PIECE tp, ATTACK qrbAtt)
{
    NODE   *N     = E->S.currNode;
    GLOBAL *G     = E->Global;
    INT     mdir  = G->A.AttackDir[N->m.to - N->m.from] >> 5; // Pawn moves don't set m.dir.
    SQUARE  mfrom = N->m.from;

    // bits = ((qrbAtt | (qrbAtt >> 8)) & 0x00FF) — fold rook nibble + bishop nibble into byte index.
    INT bits = (qrbAtt | (qrbAtt >> 8)) & 0xFF;

    PIECE eKing = king + black - N->player;

    while (bits)
    {
        QRB_DATA *Data = &G->M.QRBdata[bits];
        INT       adir = Data->dir;

        // Skip rays parallel/antiparallel to the move direction (no discovered threat there).
        if (adir == mdir || adir + mdir == 0)
        {
            bits = Data->offset;
            continue;
        }

        // Walk from m.from along adir to the first non-empty square; check what's there.
        SQUARE sq = mfrom;
        PIECE  ip;
        do
        {
            sq += adir;
            ip = E->B.Board[sq];
        } while (ip == empty);

        if (ip < tp || ip > eKing)
        {
            bits = Data->offset;
            continue;
        }
        if (ip == eKing)
            return true;

        // Otherwise: the piece exists and is worth >= tp. It's a threat if undefended,
        // OR if our slider attacks it via the right ray-bit (queen for queen-bit, rook+bishop
        // for rook-bit).
        if (N->Attack_[sq] == 0)
            return true;

        ATTACK rayBit = Data->rayBit;
        if (pieceType(ip) == queen)
        {
            // queen attackers count via rb-bits on the ray
            if (qrbAtt & 0xFF00 & rayBit)
                return true;
        }
        else if (pieceType(ip) == rook)
        {
            // rook attackers count via bishop-bits on the ray
            if (qrbAtt & 0x0F00 & rayBit)
                return true;
        }
        // pieceType < rook: not threatenable by a slider via this scan; fall through.

        bits = Data->offset;
    }
    return false;
}
