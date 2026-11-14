/**************************************************************************************************/
/*                                                                                                */
/* Module  : PERFORMMOVE.C                                                                        */
/* Purpose : This module contains Perform/Retract Move routines which are used during the search. */
/*           "PerformMove()" performs the current move (m) at the current node (rNode) on the     */
/*           and updates the various board related data structures incrementally. Similarily, the */
/*           "RetractMove()" routine takes back the current move at the current node.             */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"

#include "PerformMove.f"
#include "Attack.f"

// This module contains the routines for performing and retracting moves during the search.
// Register rNode is expected to contain a pointer to the current node "N". The two high-level
// routines "PerformMove" and "RetractMove" performs/retracts the move "N->m" by dispatching
// on the type of the moving piece (N->m.piece) and then calls the specialized lowel-level
// assembler routines. The following data structures are updated incrementally when a move is
// performed or retracted:
//
//         Board, HasMovedTo,
//         PieceLoc, PLinx, PawnStruct,
//         pieceCount,
//         Attack,
//         N->capInx, N->promInx
//
// The "Perform"-routines are basically designed as follows:
//
//         SubBlockAttack(to); (or SubPieceAttack(to) if it's a capture)
//         SubPieceAttack(from);
//         Move piece on the board
//         AddPieceAttack(to);
//         AddBlockAttack(from);
//
// whereas the "Retract"-routines basically are designed as follows:
//
//         SubBlockAttack(from);
//         SubPieceAttack(to);
//         Move piece back on the board
//         AddPieceAttack(from);
//         AddBlockAttack(to); (or AddPieceAttack(to) if it's a capture)

/**************************************************************************************************/
/*                                                                                                */
/*                                        GENERIC ASM MACROS                                      */
/*                                                                                                */
/**************************************************************************************************/

/*----------------------------------- Generic Perform Move Macros --------------------------------*/
// Generic macro for removing captured pieces in case of captures. Doesn't actually move/remove
// any pieces on the board.
// NOTE: This macro expects a local register called "mobSum" to have been defined.

#define check_remove_cap                                                                                               \
    cmpi cr0, 0, mcap, empty /* if (! m.cap)                             */;                                           \
    mr   rTmp1, dst /*    mobSum -= SubBlockAttack(m.to);       */;                                                    \
    bne - cr0, @C /*                                          */;                                                      \
    bl UpdBlockAttack /* else                                     */;                                                  \
    b @D /* {                                        */;                                                               \
    @C addi rTmp6, rGlobal, GLOBAL.B.PieceCountBit /*    pieceCount -= PieceCountBit[m.cap];   */;                     \
    addi    rTmp2, rEngine, ENGINE.B.PLinx /*    inx = PLinx[m.to];                    */;                             \
    add     rTmp3, dst, dst /*                                          */;                                            \
    rlwinm  rTmp7, mcap, 2, 0, 31 /*                                          */;                                      \
    lhax    rTmp4, rTmp2, rTmp3 /*                                          */;                                        \
    lwzx    rTmp8, rTmp6, rTmp7 /*                                          */;                                        \
    li      rTmp2, nullSq /*                                          */;                                              \
    add     rTmp9, rTmp4, rTmp4 /*                                          */;                                        \
    sth     rTmp4, node(capInx) /*    N->capInx = inx;                      */;                                        \
    subf    rPieceCount, rTmp8, rPieceCount /*                                          */;                            \
    sthx    rTmp2, rPieceLoc_, rTmp9 /*    PieceLoc_[inx] = nullSq;              */;                                   \
    bl      UpdPieceAttack /*    mobSum -= SubPieceAttack(m.to);       */;                                             \
    @D subf mobSum, rTmp1, mobSum /* }                                        */

/*----------------------------------- Generic Retract Move Macros --------------------------------*/
// Generic macro for re-placing captured pieces in case of captures. Restores the contents of
// the destination square.

#define check_replace_cap                                                                                              \
    add  rTmp3, dst, dst /* Board[m.to] = m.cap;                     */;                                               \
    cmpi cr0, 0, mcap, empty /*                                          */;                                           \
    sthx mcap, rBoard, rTmp3 /* if (! m.cap)                             */;                                           \
    bne - cr0, @C /*                                          */;                                                      \
    mr rTmp1, dst /*    AddBlockAttack(m.to);                 */;                                                      \
    bl UpdBlockAttack /*                                          */;                                                  \
    b @D /* else                                     */;                                                               \
    @C lhz rTmp4, node(capInx) /* {  inx = N->capInx;                      */;                                         \
    addi   rTmp6, rGlobal, GLOBAL.B.PieceCountBit /*    pieceCount += PieceCountBit[m.cap];   */;                      \
    rlwinm rTmp7, mcap, 2, 0, 31 /*                                          */;                                       \
    lwzx   rTmp8, rTmp6, rTmp7 /*                                          */;                                         \
    addi   rTmp2, rEngine, ENGINE.B.PLinx /*                                          */;                              \
    add    rTmp9, rTmp4, rTmp4 /*                                          */;                                         \
    sthx   rTmp4, rTmp2, rTmp3 /*    PLinx[m.to] = inx;                    */;                                         \
    mr     rTmp1, dst /*                                          */;                                                  \
    add    rPieceCount, rTmp8, rPieceCount /*                                          */;                             \
    sthx   dst, rPieceLoc_, rTmp9 /*    PieceLoc_[inx] = m.to;                */;                                      \
    bl     UpdPieceAttack /*    AddPieceAttack(m.to);                 */;                                              \
    @D /* }                                        */

/**************************************************************************************************/
/*                                                                                                */
/*                                          PERFORM/RETRACT                                       */
/*                                                                                                */
/**************************************************************************************************/

// The high level perform/retract routines simply dispatch on the piece type of moving piece and
// calls the specialized perform/retract routines. Note that the rooks and bishops share the same
// routines.

// NOTE: When the specialized routines are called, the function return address is stored in r0,
// and NOT in the link register. Therefore a specialized "frame_begin" macro is used.

/*-------------------------------------- Generic Perform Move ------------------------------------*/

static void PerformPawnMove(ENGINE *E);
static void PerformKnightMove(ENGINE *E);
static void PerformRookMove(ENGINE *E);
static void PerformQueenMove(ENGINE *E);
static void PerformKingMove(ENGINE *E);

void PerformMove(ENGINE *E)
{
    NODE *N = E->S.currNode;

    // Dispatch based on piece type
    switch (pieceType(N->m.piece))
    {
    case pawn:
        PerformPawnMove(E);
        break;
    case knight:
        PerformKnightMove(E);
        break;
    case bishop:
        PerformRookMove(E);
        break; // Bishop uses same routine as rook
    case rook:
        PerformRookMove(E);
        break;
    case queen:
        PerformQueenMove(E);
        break;
    case king:
        PerformKingMove(E);
        break;
    }
} /* PerformMove */

/*-------------------------------------- Generic Retract Move ------------------------------------*/

static void RetractPawnMove(ENGINE *E);
static void RetractKnightMove(ENGINE *E);
static void RetractRookMove(ENGINE *E);
static void RetractQueenMove(ENGINE *E);
static void RetractKingMove(ENGINE *E);

void RetractMove(ENGINE *E)
{
    NODE *N = E->S.currNode;

    // Dispatch based on piece type
    switch (pieceType(N->m.piece))
    {
    case pawn:
        RetractPawnMove(E);
        break;
    case knight:
        RetractKnightMove(E);
        break;
    case bishop:
        RetractRookMove(E);
        break; // Bishop uses same routine as rook
    case rook:
        RetractRookMove(E);
        break;
    case queen:
        RetractQueenMove(E);
        break;
    case king:
        RetractKingMove(E);
        break;
    }
} /* RetractMove */

/**************************************************************************************************/
/*                                                                                                */
/*                                            PAWN MOVES                                          */
/*                                                                                                */
/**************************************************************************************************/

#define upd_pawn_attack(sq)                                                                                            \
    add    rTmp7, sq, rPawnDir /* A = (PTR)&AttackP[m.from + pawnDir];  */;                                            \
    rlwinm rTmp7, rTmp7, 2, 0, 31;                                                                                     \
    add    rTmp7, rAttack, rTmp7 /* A[-4] ^= pMaskL;                      */;                                          \
    lhz    rTmp8, -4(rTmp7);                                                                                           \
    lhz    rTmp9, 4(rTmp7) /* A[+4] ^= pMaskR;                      */;                                                \
    xori   rTmp8, rTmp8, 0x0400;                                                                                       \
    xori   rTmp9, rTmp9, 0x0200;                                                                                       \
    sth    rTmp8, -4(rTmp7);                                                                                           \
    sth    rTmp9, 4(rTmp7);

/*-------------------------------------- Perform Pawn Moves --------------------------------------*/

// Helper: Update pawn attacks at a square by toggling bits in diagonal attack squares.
// Convention (matches upstream PPC asm and UpdPieceAttack in Attack.c):
//   pMaskL = pawn's left-forward diagonal target.
//   White pawn at sq: AttackW[sq+0x0F] ^= pMaskL, AttackW[sq+0x11] ^= pMaskR
//   Black pawn at sq: AttackB[sq-0x11] ^= pMaskL, AttackB[sq-0x0F] ^= pMaskR
//
// IMPORTANT: no on-board guard. CalcAttackState's UpdPieceAttack pawn case writes
// these bits unconditionally, so the incremental update must match. A guard here
// previously left stale pMaskL/pMaskR bits on off-board cells (e.g. AttackW[h2+0x11]
// = file-8 padding) when a pawn moved off h-file, which the rebuild would then
// reposition to AttackW[dst+0x11]. The asymmetry let phantom bits accumulate in
// padding cells that downstream code (UpdBlockAttack ray walks past empty squares
// onto sentinel-edge cells) could then read into on-board attack values, yielding
// the bogus "mate 1" PVs we observed. Off-board indices land in AttackW_ padding
// or in-array off-file cells, both of which are safe to write.
static void UpdPawnAttack(ENGINE *E, SQUARE sq, ATTACK *Attack, COLOUR player)
{
    if (player == white)
    {
        Attack[sq + 0x0F] ^= pMaskL;
        Attack[sq + 0x11] ^= pMaskR;
    }
    else
    {
        Attack[sq - 0x11] ^= pMaskL;
        Attack[sq - 0x0F] ^= pMaskR;
    }
}

static void PerformPawnMove(ENGINE *E)
{
    NODE  *N        = E->S.currNode;
    NODE  *nextNode = N + 1;
    SQUARE src      = N->m.from;
    SQUARE dst      = N->m.to;
    PIECE  cap      = N->m.cap;
    INT    mobSum   = N->mobEval;

    // Remove any captured piece and its attack
    if (cap == empty)
    {
        mobSum -= UpdBlockAttack(E, dst);
    }
    else
    {
        INT inx   = E->B.PLinx[dst];
        N->capInx = inx;
        E->B.pieceCount -= E->Global->B.PieceCountBit[cap];
        N->PieceLoc_[inx] = nullSq;
        mobSum -= UpdPieceAttack(E, dst);
    }

    // Move Pawn on the board
    E->B.PLinx[dst]              = E->B.PLinx[src];
    E->B.Board[src]              = empty;
    E->B.Board[dst]              = N->player + pawn;
    N->PieceLoc[E->B.PLinx[dst]] = dst;
    mobSum += UpdBlockAttack(E, src);

    // Update pawn attacks (toggle at source)
    UpdPawnAttack(E, src, N->Attack, N->player);

    // Update pawn structure: remove from source rank
    BYTE *PawnStruct = (N->player == white) ? E->B.PawnStructW : E->B.PawnStructB;
    INT   srcRank    = src >> 4;
    INT   srcFile    = src & 0x07;
    PawnStruct[srcRank] ^= (1 << srcFile); // Toggle bit for this file

    // Handle special move types
    if (isPromotion(N->m))
    {
        // Promotion: change pawn to promoted piece
        // pieceCount -= PieceCountBit[pawn + player];
        // pieceCount += PieceCountBit[m.type];
        E->B.pieceCount -= E->Global->B.PieceCountBit[N->player + pawn];
        E->B.pieceCount += E->Global->B.PieceCountBit[N->m.type];

        // i = ++LastOffi[player];
        INT i = ++E->B.LastOffi[N->player];
        // j = PLinx[m.to];
        INT j = E->B.PLinx[dst];

        // N->promInx = j;
        N->promInx = j;

        // if (i != j) swap pieces in PieceLoc array
        if (i != j)
        {
            // PLinx[to] = i;
            E->B.PLinx[dst] = i;
            // sq = PieceLoc[i];
            SQUARE sq = N->PieceLoc[i];
            // PieceLoc[i] = to;
            N->PieceLoc[i] = dst;
            // PieceLoc[j] = sq;
            N->PieceLoc[j] = sq;
            // if (sq != nullSq) PLinx[sq] = j;
            if (sq != nullSq)
                E->B.PLinx[sq] = j;
        }

        // Board[m.to] = type;
        E->B.Board[dst] = N->m.type;
        // mobSum += AddPieceAttack(m.to);
        mobSum += UpdPieceAttack(E, dst);

        // Note: No pawn structure update for destination (pawn is gone)
    }
    else if (N->m.type == mtype_EP)
    {
        // En passant: update destination pawn structure and attacks normally
        INT dstRank = dst >> 4;
        INT dstFile = dst & 0x07;
        PawnStruct[dstRank] ^= (1 << dstFile);
        UpdPawnAttack(E, dst, N->Attack, N->player);

        // Then handle the captured pawn behind the destination
        INT    pawnDir = (N->player == white) ? 0x10 : -0x10;
        SQUARE epSq    = dst - pawnDir; // Square behind destination

        // mobSum -= SubPieceAttack(sq);
        mobSum -= UpdPieceAttack(E, epSq);
        // mobSum += AddBlockAttack(sq);
        mobSum += UpdBlockAttack(E, epSq);

        // inx = PLinx[sq];
        INT inx = E->B.PLinx[epSq];
        // pieceCount -= PieceCountBit[enemy(pawn)];
        COLOUR enemy = (N->player == white) ? black : white;
        E->B.pieceCount -= E->Global->B.PieceCountBit[enemy + pawn];
        // Board[sq] = empty;
        E->B.Board[epSq] = empty;
        // PieceLoc_[inx] = nullSq;
        N->PieceLoc_[inx] = nullSq;
        // N->capInx = inx
        N->capInx = inx;
    }
    else
    {
        // Normal pawn move: update destination rank and attacks
        INT dstRank = dst >> 4;
        INT dstFile = dst & 0x07;
        PawnStruct[dstRank] ^= (1 << dstFile);
        UpdPawnAttack(E, dst, N->Attack, N->player);
    }

    nextNode->mobEval = mobSum;
} /* PerformPawnMove */

/*-------------------------------------- Retract Pawn Moves --------------------------------------*/

static void RetractPawnMove(ENGINE *E)
{
    NODE  *N   = E->S.currNode;
    SQUARE src = N->m.from;
    SQUARE dst = N->m.to;
    PIECE  cap = N->m.cap;

    // Update pawn structure: restore source rank
    BYTE *PawnStruct = (N->player == white) ? E->B.PawnStructW : E->B.PawnStructB;
    INT   srcRank    = src >> 4;
    INT   srcFile    = src & 0x07;
    PawnStruct[srcRank] ^= (1 << srcFile); // Toggle bit for this file

    // Handle special move types
    if (isPromotion(N->m))
    {
        // Undo promotion: change promoted piece back to pawn
        // pieceCount -= PieceCountBit[pawn + player];
        // pieceCount += PieceCountBit[m.type];
        E->B.pieceCount -= E->Global->B.PieceCountBit[N->m.type];
        E->B.pieceCount += E->Global->B.PieceCountBit[N->player + pawn];

        // i = LastOffi[player]--;
        INT i = E->B.LastOffi[N->player]--;
        // j = N->promInx;
        INT j = N->promInx;

        // if (i != j) swap pieces back in PieceLoc array
        if (i != j)
        {
            // PLinx[to] = j;
            E->B.PLinx[dst] = j;
            // sq = PieceLoc[j];
            SQUARE sq = N->PieceLoc[j];
            // PieceLoc[j] = to;
            N->PieceLoc[j] = dst;
            // PieceLoc[i] = sq;
            N->PieceLoc[i] = sq;
            // if (sq != nullSq) PLinx[sq] = i;
            if (sq != nullSq)
                E->B.PLinx[sq] = i;
        }

        // SubPieceAttack(m.to);
        UpdPieceAttack(E, dst);

        // Note: No pawn structure update for destination (pawn wasn't there)
    }
    else if (N->m.type == mtype_EP)
    {
        // En passant: remove destination pawn structure and attacks
        INT dstRank = dst >> 4;
        INT dstFile = dst & 0x07;
        PawnStruct[dstRank] ^= (1 << dstFile);
        UpdPawnAttack(E, dst, N->Attack, N->player);

        // Then restore the captured pawn behind the destination
        INT    pawnDir = (N->player == white) ? 0x10 : -0x10;
        SQUARE epSq    = dst - pawnDir; // Square behind destination

        // SubBlockAttack(sq);
        UpdBlockAttack(E, epSq);

        // PLinx[sq] = capInx;
        INT inx          = N->capInx;
        E->B.PLinx[epSq] = inx;

        // pieceCount += PieceCountBit[enemy(pawn)];
        COLOUR enemy = (N->player == white) ? black : white;
        E->B.pieceCount += E->Global->B.PieceCountBit[enemy + pawn];

        // Board[sq] = enemy(pawn);
        E->B.Board[epSq] = enemy + pawn;

        // PieceLoc_[capInx] = sq;
        N->PieceLoc_[inx] = epSq;

        // AddPieceAttack(sq);
        UpdPieceAttack(E, epSq);
    }
    else
    {
        // Normal pawn move: remove destination rank and attacks
        INT dstRank = dst >> 4;
        INT dstFile = dst & 0x07;
        PawnStruct[dstRank] ^= (1 << dstFile);
        UpdPawnAttack(E, dst, N->Attack, N->player);
    }

    // Unmove Pawn on the board
    E->B.PLinx[src]              = E->B.PLinx[dst];
    E->B.Board[src]              = N->player + pawn;
    N->PieceLoc[E->B.PLinx[src]] = src;
    UpdBlockAttack(E, src);

    // Update pawn attacks (toggle at source)
    UpdPawnAttack(E, src, N->Attack, N->player);

    // Replace any captured piece and its attack
    E->B.Board[dst] = cap;
    if (cap == empty)
    {
        UpdBlockAttack(E, dst);
    }
    else
    {
        INT inx = N->capInx;
        E->B.pieceCount += E->Global->B.PieceCountBit[cap];
        E->B.PLinx[dst]   = inx;
        N->PieceLoc_[inx] = dst;
        UpdPieceAttack(E, dst);
    }
} /* RetractPawnMove */

/**************************************************************************************************/
/*                                                                                                */
/*                                           KNIGHT MOVES                                         */
/*                                                                                                */
/**************************************************************************************************/

/*-------------------------------------- Perform Knight Moves ------------------------------------*/

// Helper: Update knight attacks by toggling knight attack bits
// NOTE: No onBoard check — must match update_knight_attack() in Attack.c (used by CalcAttackState).
// Off-board entries in the attack array are harmless (never read for game logic) but must be
// consistent between CalcAttackState and incremental updates.
static void UpdKnightAttack(ENGINE *E, SQUARE sq, ATTACK *Attack)
{
    Attack[sq + E->Global->B.KnightDir[0]] ^= (1 << 16);
    Attack[sq + E->Global->B.KnightDir[1]] ^= (1 << 17);
    Attack[sq + E->Global->B.KnightDir[2]] ^= (1 << 18);
    Attack[sq + E->Global->B.KnightDir[3]] ^= (1 << 19);
    Attack[sq + E->Global->B.KnightDir[4]] ^= (1 << 20);
    Attack[sq + E->Global->B.KnightDir[5]] ^= (1 << 21);
    Attack[sq + E->Global->B.KnightDir[6]] ^= (1 << 22);
    Attack[sq + E->Global->B.KnightDir[7]] ^= (1 << 23);
}

static void PerformKnightMove(ENGINE *E)
{
    NODE  *N        = E->S.currNode;
    NODE  *nextNode = N + 1;
    SQUARE src      = N->m.from;
    SQUARE dst      = N->m.to;
    PIECE  cap      = N->m.cap;
    INT    mobSum   = N->mobEval;

    // Remove any captured piece and its attack
    if (cap == empty)
    {
        mobSum -= UpdBlockAttack(E, dst);
    }
    else
    {
        // pieceCount -= PieceCountBit[m.cap];
        INT inx   = E->B.PLinx[dst];
        N->capInx = inx;
        E->B.pieceCount -= E->Global->B.PieceCountBit[cap];
        N->PieceLoc_[inx] = nullSq;
        mobSum -= UpdPieceAttack(E, dst);
    }

    // Move Knight on the board
    E->B.PLinx[dst]              = E->B.PLinx[src];
    E->B.Board[src]              = empty;
    E->B.Board[dst]              = N->player + knight;
    N->PieceLoc[E->B.PLinx[dst]] = dst;
    mobSum += UpdBlockAttack(E, src);

    // "Move" Knight Attack (toggle attack bits at both src and dst)
    UpdKnightAttack(E, src, N->Attack);
    UpdKnightAttack(E, dst, N->Attack);

    nextNode->mobEval = mobSum;
} /* PerformKnightMove */

/*-------------------------------------- Retract Knight Moves ------------------------------------*/

static void RetractKnightMove(ENGINE *E)
{
    NODE  *N   = E->S.currNode;
    SQUARE src = N->m.from;
    SQUARE dst = N->m.to;
    PIECE  cap = N->m.cap;

    // Unmove Knight on the board
    UpdBlockAttack(E, src);
    E->B.PLinx[src]              = E->B.PLinx[dst];
    E->B.Board[src]              = N->player + knight;
    N->PieceLoc[E->B.PLinx[src]] = src;

    // Replace any captured piece and its attack
    E->B.Board[dst] = cap;
    if (cap == empty)
    {
        UpdBlockAttack(E, dst);
    }
    else
    {
        INT inx = N->capInx;
        E->B.pieceCount += E->Global->B.PieceCountBit[cap];
        E->B.PLinx[dst]   = inx;
        N->PieceLoc_[inx] = dst;
        UpdPieceAttack(E, dst);
    }

    // "Unmove" Knight Attack (toggle attack bits at both src and dst)
    UpdKnightAttack(E, src, N->Attack);
    UpdKnightAttack(E, dst, N->Attack);
} /* RetractKnightMove */

/**************************************************************************************************/
/*                                                                                                */
/*                                         ROOK/BISHOP MOVES                                      */
/*                                                                                                */
/**************************************************************************************************/

/*------------------------------------ Perform Rook/Bishop Moves ---------------------------------*/

static void PerformRookMove(ENGINE *E)
{
    NODE  *N        = E->S.currNode;
    NODE  *nextNode = N + 1;
    SQUARE src      = N->m.from;
    SQUARE dst      = N->m.to;
    PIECE  cap      = N->m.cap;
    INT    mobSum   = N->mobEval;

    // Remove source piece attack
    mobSum -= UpdPieceAttack(E, src);

    // Remove any captured piece and its attack
    if (cap == empty)
    {
        mobSum -= UpdBlockAttack(E, dst);
    }
    else
    {
        INT inx   = E->B.PLinx[dst];
        N->capInx = inx;
        E->B.pieceCount -= E->Global->B.PieceCountBit[cap];
        N->PieceLoc_[inx] = nullSq;
        mobSum -= UpdPieceAttack(E, dst);
    }

    // Move Rook/Bishop on the board
    E->B.PLinx[dst]              = E->B.PLinx[src];
    E->B.Board[src]              = empty;
    E->B.Board[dst]              = N->m.piece;
    N->PieceLoc[E->B.PLinx[dst]] = dst;

    // Update attacks and mobility
    mobSum += UpdBlockAttack(E, src);
    mobSum += UpdPieceAttack(E, dst);

    // Update HasMovedTo counter (for castling rights)
    E->B.HasMovedTo[dst]++;

    nextNode->mobEval = mobSum;
} /* PerformRookMove */

static void CastleRook(ENGINE *E)
{
    PerformRookMove(E);
}

/*------------------------------------ Retract Rook/Bishop Moves ---------------------------------*/

static void RetractRookMove(ENGINE *E)
{
    NODE  *N   = E->S.currNode;
    SQUARE src = N->m.from;
    SQUARE dst = N->m.to;
    PIECE  cap = N->m.cap;

    // Update HasMovedTo counter (for castling rights)
    E->B.HasMovedTo[dst]--;

    // Remove destination piece attack
    UpdPieceAttack(E, dst);

    // Unmove Rook/Bishop on the board
    UpdBlockAttack(E, src);
    E->B.PLinx[src]              = E->B.PLinx[dst];
    E->B.Board[src]              = N->m.piece;
    N->PieceLoc[E->B.PLinx[src]] = src;

    // Replace any captured piece and its attack
    E->B.Board[dst] = cap;
    if (cap == empty)
    {
        UpdBlockAttack(E, dst);
    }
    else
    {
        INT inx = N->capInx;
        E->B.pieceCount += E->Global->B.PieceCountBit[cap];
        E->B.PLinx[dst]   = inx;
        N->PieceLoc_[inx] = dst;
        UpdPieceAttack(E, dst);
    }

    // Restore source piece attack
    UpdPieceAttack(E, src);
} /* RetractRookMove */

static void UncastleRook(ENGINE *E)
{
    RetractRookMove(E);
}

/**************************************************************************************************/
/*                                                                                                */
/*                                           QUEEN MOVES                                          */
/*                                                                                                */
/**************************************************************************************************/

static void PerformQueenMove(ENGINE *E)
{
    NODE  *N        = E->S.currNode;
    NODE  *nextNode = N + 1;
    SQUARE src      = N->m.from;
    SQUARE dst      = N->m.to;
    PIECE  cap      = N->m.cap;
    INT    mobSum   = N->mobEval;

    // Remove source piece attack
    mobSum -= UpdPieceAttack(E, src);

    // Remove any captured piece and its attack
    if (cap == empty)
    {
        mobSum -= UpdBlockAttack(E, dst);
    }
    else
    {
        INT inx   = E->B.PLinx[dst];
        N->capInx = inx;
        E->B.pieceCount -= E->Global->B.PieceCountBit[cap];
        N->PieceLoc_[inx] = nullSq;
        mobSum -= UpdPieceAttack(E, dst);
    }

    // Move Queen on the board
    E->B.PLinx[dst]              = E->B.PLinx[src];
    E->B.Board[src]              = empty;
    E->B.Board[dst]              = N->m.piece;
    N->PieceLoc[E->B.PLinx[dst]] = dst;

    // Update attacks and mobility
    mobSum += UpdBlockAttack(E, src);
    mobSum += UpdPieceAttack(E, dst);

    nextNode->mobEval = mobSum;
} /* PerformQueenMove */

static void RetractQueenMove(ENGINE *E)
{
    NODE  *N   = E->S.currNode;
    SQUARE src = N->m.from;
    SQUARE dst = N->m.to;
    PIECE  cap = N->m.cap;

    // Remove destination piece attack
    UpdPieceAttack(E, dst);

    // Unmove Queen on the board
    UpdBlockAttack(E, src);
    E->B.PLinx[src]              = E->B.PLinx[dst];
    E->B.Board[src]              = N->m.piece;
    N->PieceLoc[E->B.PLinx[src]] = src;

    // Replace any captured piece and its attack
    E->B.Board[dst] = cap;
    if (cap == empty)
    {
        UpdBlockAttack(E, dst);
    }
    else
    {
        INT inx = N->capInx;
        E->B.pieceCount += E->Global->B.PieceCountBit[cap];
        E->B.PLinx[dst]   = inx;
        N->PieceLoc_[inx] = dst;
        UpdPieceAttack(E, dst);
    }

    // Restore source piece attack
    UpdPieceAttack(E, src);
} /* RetractQueenMove */

/**************************************************************************************************/
/*                                                                                                */
/*                                           KING MOVES                                           */
/*                                                                                                */
/**************************************************************************************************/

/*-------------------------------------- Perform King Moves --------------------------------------*/

// Helper: Update king attacks by toggling king attack bits on 8 surrounding squares
// NOTE: No onBoard check — must match update_king_attack() in Attack.c (used by CalcAttackState).
static void UpdKingAttack(ENGINE *E, SQUARE sq, ATTACK *Attack)
{
    Attack[sq + E->Global->B.KingDir[0]] ^= kMask;
    Attack[sq + E->Global->B.KingDir[1]] ^= kMask;
    Attack[sq + E->Global->B.KingDir[2]] ^= kMask;
    Attack[sq + E->Global->B.KingDir[3]] ^= kMask;
    Attack[sq + E->Global->B.KingDir[4]] ^= kMask;
    Attack[sq + E->Global->B.KingDir[5]] ^= kMask;
    Attack[sq + E->Global->B.KingDir[6]] ^= kMask;
    Attack[sq + E->Global->B.KingDir[7]] ^= kMask;
}

static void PerformKingMove(ENGINE *E)
{
    NODE  *N        = E->S.currNode;
    NODE  *nextNode = N + 1;
    SQUARE src      = N->m.from;
    SQUARE dst      = N->m.to;
    PIECE  cap      = N->m.cap;
    INT    mobSum   = N->mobEval;

    // Handle castling
    if (N->m.type != mtype_Normal)
    {
        // Castling: temporarily modify move to be a rook move
        PIECE  origPiece = N->m.piece;
        SQUARE origFrom  = src;
        SQUARE origTo    = dst;

        // Determine rook squares based on castling type
        if (N->m.type == mtype_O_O) // Kingside castling
        {
            // Rook moves from h-file to f-file (dst+1 to dst-1)
            N->m.from = dst + 1;
            N->m.to   = dst - 1;
            N->m.dir  = -1;
        }
        else
        {
            // Rook moves from a-file to d-file (dst-2 to dst+1)
            N->m.from = dst - 2;
            N->m.to   = dst + 1;
            N->m.dir  = 1;
        }
        N->m.piece = N->player + rook;

        // Perform the rook move
        CastleRook(E);

        // Restore king move and get mobility from rook move result
        mobSum     = nextNode->mobEval;
        N->m.from  = origFrom;
        N->m.to    = origTo;
        N->m.piece = origPiece;
    }

    // Remove any captured piece and its attack
    if (cap == empty)
    {
        mobSum -= UpdBlockAttack(E, dst);
    }
    else
    {
        INT inx   = E->B.PLinx[dst];
        N->capInx = inx;
        E->B.pieceCount -= E->Global->B.PieceCountBit[cap];
        N->PieceLoc_[inx] = nullSq;
        mobSum -= UpdPieceAttack(E, dst);
    }

    // Move King on the board
    N->PieceLoc[0]  = dst; // King is always at index 0
    E->B.Board[src] = empty;
    E->B.Board[dst] = N->player + king;
    mobSum += UpdBlockAttack(E, src);

    // Update HasMovedTo counter (for castling rights)
    E->B.HasMovedTo[dst]++;

    // "Move" King Attack (toggle attack bits at both src and dst)
    UpdKingAttack(E, src, N->Attack);
    UpdKingAttack(E, dst, N->Attack);

    nextNode->mobEval = mobSum;
} /* PerformKingMove */

/*-------------------------------------- Retract King Moves --------------------------------------*/

static void RetractKingMove(ENGINE *E)
{
    NODE  *N   = E->S.currNode;
    SQUARE src = N->m.from;
    SQUARE dst = N->m.to;
    PIECE  cap = N->m.cap;

    // "Unmove" King Attack (toggle attack bits at both src and dst)
    UpdKingAttack(E, dst, N->Attack);
    UpdKingAttack(E, src, N->Attack);

    // Unmove King on the board
    UpdBlockAttack(E, src);
    N->PieceLoc[0]  = src; // King is always at index 0
    E->B.Board[src] = N->player + king;

    // Update HasMovedTo counter (for castling rights)
    E->B.HasMovedTo[dst]--;

    // Replace any captured piece and its attack
    E->B.Board[dst] = cap;
    if (cap == empty)
    {
        UpdBlockAttack(E, dst);
    }
    else
    {
        INT inx = N->capInx;
        E->B.pieceCount += E->Global->B.PieceCountBit[cap];
        E->B.PLinx[dst]   = inx;
        N->PieceLoc_[inx] = dst;
        UpdPieceAttack(E, dst);
    }

    // Handle castling retraction
    if (N->m.type != mtype_Normal)
    {
        // Castling: temporarily modify move to be a rook move
        PIECE  origPiece = N->m.piece;
        SQUARE origFrom  = src;
        SQUARE origTo    = dst;

        // Determine rook squares based on castling type
        if (N->m.type == mtype_O_O) // Kingside castling
        {
            // Rook moves from h-file to f-file (dst+1 to dst-1)
            N->m.from = dst + 1;
            N->m.to   = dst - 1;
            N->m.dir  = -1;
        }
        else // Queenside castling (mtype_O_O_O)
        {
            // Rook moves from a-file to d-file (dst-2 to dst+1)
            N->m.from = dst - 2;
            N->m.to   = dst + 1;
            N->m.dir  = 1;
        }
        N->m.piece = N->player + rook;

        // Retract the rook move
        UncastleRook(E);

        // Restore king move
        N->m.from  = origFrom;
        N->m.to    = origTo;
        N->m.piece = origPiece;
    }
} /* RetractKingMove */

/**************************************************************************************************/
/*                                                                                                */
/*                                      START UP INITIALIZATION                                   */
/*                                                                                                */
/**************************************************************************************************/

void InitPerformMoveModule(GLOBAL *Global)
{
    SQUARE CQDir[8] = {0x1, 0x11, 0x10, 0xF, -0x1, -0x11, -0x10, -0xF};
    SQUARE mdir, tdir;

    for (INT i = 0; i <= 7; i++)
    {
        //--- Initialize "RBUpdData[]" ---

        mdir       = Global->B.QueenDir[i];
        RBDATA *RB = &(Global->P.RBUpdData[mdir]);

        tdir      = Global->B.Turn90[mdir];
        RB->tdir  = 2 * tdir;
        RB->tbit  = Global->A.DirBit[tdir] & rbMask;
        RB->tbit_ = Global->A.DirBit[-tdir] & rbMask;

        RB->mbit  = Global->A.DirBit[mdir] & rbMask;
        RB->mbit_ = Global->A.DirBit[-mdir] & rbMask;

        RB->dm = (i <= 3 ? bishopMob : rookMob);

        RB->unused1 = 0;
        RB->unused2 = 0;

        //--- Initialize "QUpdData[]" ---

        mdir     = CQDir[i];
        QDATA *Q = &(Global->P.QUpdData[mdir]);

        tdir      = CQDir[(i + 1) % 8];
        Q->tdir0  = 2 * tdir;
        Q->tbit0  = Global->A.DirBit[tdir] & qMask;
        Q->tbit0_ = Global->A.DirBit[-tdir] & qMask;
        tdir      = CQDir[(i + 2) % 8];
        Q->tdir1  = 2 * tdir;
        Q->tbit1  = Global->A.DirBit[tdir] & qMask;
        Q->tbit1_ = Global->A.DirBit[-tdir] & qMask;
        tdir      = CQDir[(i + 3) % 8];
        Q->tdir2  = 2 * tdir;
        Q->tbit2  = Global->A.DirBit[tdir] & qMask;
        Q->tbit2_ = Global->A.DirBit[-tdir] & qMask;

        Q->mbit  = Global->A.DirBit[mdir] & qMask;
        Q->mbit_ = Global->A.DirBit[-mdir] & qMask;

        for (INT j = 0; j < 5; j++)
            Q->padding[j] = 0;
    }
} /* InitPerformMoveModule */
