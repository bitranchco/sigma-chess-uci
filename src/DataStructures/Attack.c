/**************************************************************************************************/
/*                                                                                                */
/* Module  : ATTACK.C                                                                             */
/* Purpose : This module implements the "Attack" data structure and various operations. Two       */
/*           attack tables are maintained - one for each side - which for each square describes   */
/*           how this is attacked by the various piece types.                                     */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"

#include "Attack.f"
#include "Engine.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                        RESET ATTACK TABLE                                      */
/*                                                                                                */
/**************************************************************************************************/

// Before the engine can start searching, the attack state must first be rebuilt from scratch from
// the current board position (Engine->B.Board).

void CalcAttackState(ENGINE *E)
{
    for (SQUARE sq = -boardSize1; sq < boardSize2; sq++) // Clear attack table.
        E->A.AttackW[sq] = E->A.AttackB[sq] = 0;

    INT mobSum = 0;
    for (SQUARE sq = a1; sq <= h8; sq++)     // Compute attack table, pawn
        if (onBoard(sq) && E->B.Board[sq])   // structure and mobility.
            mobSum += UpdPieceAttack(E, sq); // Now takes ENGINE* parameter
    E->S.rootNode->mobEval = mobSum;
} /* CalcAttackState */

/**************************************************************************************************/
/*                                                                                                */
/*                                     UPDATING ATTACK TABLE                                      */
/*                                                                                                */
/**************************************************************************************************/

/*--------------------------------- Adding/Removing Piece Attack ---------------------------------*/
// When a piece is added to a square, we must additionally add attack information for that piece.
// Similarily, when removing a piece from a square we must also remove attack information. This
// is handled by the "UpdPieceAttack(sq)" routine which simply inverts the relevant attack bits for
// the affected squares.

// Helper function: Update sliding piece (queen/rook/bishop) attack in one direction.
// Based on the upd_qrb_attack macro — XOR first, store always, then check whether to
// continue. IMPORTANT: this matches UpdBlockAttack (and the upstream asm) which XOR at
// the terminating cell — including the edge sentinel. CalcAttackState and incremental
// updates must write to the same cells; an earlier "check edge first" version left the
// edge cell unwritten in the rebuild path while UpdBlockAttack wrote it incrementally,
// causing phantom attack bits to accumulate during deep searches.
static inline INT update_sliding_attack(ENGINE *E, SQUARE sq, SQUARE dir, ULONG bit, INT mob_change,
                                        ATTACK *attack_table)
{
    INT    dmob       = 0;
    SQUARE current_sq = sq;

    do
    {
        current_sq += dir;
        attack_table[current_sq] ^= bit; // Toggle attack bit (always, including at edge)
        dmob += mob_change;
    } while (E->B.Board[current_sq] == empty);

    return dmob;
}

// Helper function: Update knight attack (based on upd_knight_attack macro)
// NOTE: The assembly uses hardcoded byte offsets from the attack table pointer.
// These correspond to the 8 knight directions with specific bit patterns.
static inline void update_knight_attack(ENGINE *E, ATTACK *attack_table, SQUARE sq)
{
    // Knight attacks use bits 16-23 (nMask = 0x00FF0000)
    // The 8 knight moves with their corresponding bits:
    attack_table[sq + E->Global->B.KnightDir[0]] ^= (1 << 16);
    attack_table[sq + E->Global->B.KnightDir[1]] ^= (1 << 17);
    attack_table[sq + E->Global->B.KnightDir[2]] ^= (1 << 18);
    attack_table[sq + E->Global->B.KnightDir[3]] ^= (1 << 19);
    attack_table[sq + E->Global->B.KnightDir[4]] ^= (1 << 20);
    attack_table[sq + E->Global->B.KnightDir[5]] ^= (1 << 21);
    attack_table[sq + E->Global->B.KnightDir[6]] ^= (1 << 22);
    attack_table[sq + E->Global->B.KnightDir[7]] ^= (1 << 23);
}

// Helper function: Update king attack (based on upd_king_attack macro)
static inline void update_king_attack(ENGINE *E, ATTACK *attack_table, SQUARE sq)
{
    // King uses kMask = 0x01000000 (bit 24)
    attack_table[sq + E->Global->B.KingDir[0]] ^= kMask;
    attack_table[sq + E->Global->B.KingDir[1]] ^= kMask;
    attack_table[sq + E->Global->B.KingDir[2]] ^= kMask;
    attack_table[sq + E->Global->B.KingDir[3]] ^= kMask;
    attack_table[sq + E->Global->B.KingDir[4]] ^= kMask;
    attack_table[sq + E->Global->B.KingDir[5]] ^= kMask;
    attack_table[sq + E->Global->B.KingDir[6]] ^= kMask;
    attack_table[sq + E->Global->B.KingDir[7]] ^= kMask;
}

// Main function: Update piece attack for a square
// Converted from PowerPC assembly - follows pseudocode in assembly comments
INT UpdPieceAttack(ENGINE *E, SQUARE sq)
{
    PIECE piece = E->B.Board[sq];
    INT   dmob  = 0;

    switch (piece)
    {
    case wQueen:
        dmob = 0;
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[0], 1 << 0, queenMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[1], 1 << 1, queenMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[2], 1 << 2, queenMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[3], 1 << 3, queenMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[4], 1 << 4, queenMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[5], 1 << 5, queenMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[6], 1 << 6, queenMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[7], 1 << 7, queenMob, E->A.AttackW);
        break;

    case bQueen:
        dmob = 0;
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[0], 1 << 0, -queenMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[1], 1 << 1, -queenMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[2], 1 << 2, -queenMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[3], 1 << 3, -queenMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[4], 1 << 4, -queenMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[5], 1 << 5, -queenMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[6], 1 << 6, -queenMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.QueenDir[7], 1 << 7, -queenMob, E->A.AttackB);
        break;

    case wRook:
        dmob = 0;
        dmob += update_sliding_attack(E, sq, E->Global->B.RookDir[0], 1 << 12, rookMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.RookDir[1], 1 << 13, rookMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.RookDir[2], 1 << 14, rookMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.RookDir[3], 1 << 15, rookMob, E->A.AttackW);
        break;

    case bRook:
        dmob = 0;
        dmob += update_sliding_attack(E, sq, E->Global->B.RookDir[0], 1 << 12, -rookMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.RookDir[1], 1 << 13, -rookMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.RookDir[2], 1 << 14, -rookMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.RookDir[3], 1 << 15, -rookMob, E->A.AttackB);
        break;

    case wBishop:
        dmob = 0;
        dmob += update_sliding_attack(E, sq, E->Global->B.BishopDir[0], 1 << 8, bishopMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.BishopDir[1], 1 << 9, bishopMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.BishopDir[2], 1 << 10, bishopMob, E->A.AttackW);
        dmob += update_sliding_attack(E, sq, E->Global->B.BishopDir[3], 1 << 11, bishopMob, E->A.AttackW);
        break;

    case bBishop:
        dmob = 0;
        dmob += update_sliding_attack(E, sq, E->Global->B.BishopDir[0], 1 << 8, -bishopMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.BishopDir[1], 1 << 9, -bishopMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.BishopDir[2], 1 << 10, -bishopMob, E->A.AttackB);
        dmob += update_sliding_attack(E, sq, E->Global->B.BishopDir[3], 1 << 11, -bishopMob, E->A.AttackB);
        break;

    case wKnight:
        update_knight_attack(E, E->A.AttackW, sq);
        break;

    case bKnight:
        update_knight_attack(E, E->A.AttackB, sq);
        break;

    case wKing:
        update_king_attack(E, E->A.AttackW, sq);
        break;

    case bKing:
        update_king_attack(E, E->A.AttackB, sq);
        break;

    // Pawn-attack convention (matches upstream PPC asm):
    //   pMaskL = pawn's left-forward diagonal target
    //   White pawn at sq: AttackW[sq+0x0F] ^= pMaskL, AttackW[sq+0x11] ^= pMaskR
    //   Black pawn at sq: AttackB[sq-0x11] ^= pMaskL, AttackB[sq-0x0F] ^= pMaskR
    case wPawn:
        E->A.AttackW[sq + 0x0F] ^= pMaskL;
        E->A.AttackW[sq + 0x11] ^= pMaskR;
        E->B.PawnStructW[rank(sq)] ^= (1 << file(sq));
        break;

    case bPawn:
        E->A.AttackB[sq - 0x11] ^= pMaskL;
        E->A.AttackB[sq - 0x0F] ^= pMaskR;
        E->B.PawnStructB[rank(sq)] ^= (1 << file(sq));
        break;
    }

    return dmob;
} /* UpdPieceAttack */

/*---------------------------------- Adding/Removing Block Attack --------------------------------*/
// During the search, as moves are performed, the moving pieces may block/unblock attack of
// other pieces (queens, rooks and bishop). The "UpdBlockAttack(sq)" routine inverts the
// relevant attack bits for the block/unblock on square "sq".
// Converted from PowerPC assembly - follows pseudocode in assembly comments

// Helper: Fold attack bits for BlockTab lookup
// Takes low 16 bits and combines high and low bytes to produce a bit pattern
static inline INT fold_attack_bits(ATTACK At)
{
    INT high   = (At >> 8) & 0xFF;
    INT low    = At & 0xFF;
    INT folded = high | low;
    return folded;
}

INT UpdBlockAttack(ENGINE *E, SQUARE sq)
{
    INT    dmob = 0;
    ATTACK At;

    //--- Updating PLAYER Block Attack ---

    At = E->A.AttackW[sq];
    if (At & qrbMask)
    {
        INT bits = fold_attack_bits(At & qrbMask);

        do
        {
            BLOCKTAB *BD  = &E->Global->A.BlockTab[bits];
            SQUARE    dir = BD->dir / 2;
            // dA must be ATTACK-wide (32-bit). BLOCKTAB.rayBits is INT (int16_t in this
            // build); when masked with At and stored into a 16-bit signed local, values
            // like 0x8000 become negative, sign-extending in the later XOR into the
            // 32-bit attack table — corrupting bits 16-31 with 0xFFFF0000. Use ATTACK.
            ATTACK dA = (ATTACK)(unsigned short)BD->rayBits & At;
            bits      = BD->nextBits / sizeof(BLOCKTAB);

            // Determine mobility delta based on piece type
            INT dm;
            if (dA & bMask)
                dm = bishopMob;
            else if (dA & rMask)
                dm = rookMob;
            else
                dm = queenMob;

            // Update attack along the ray until we hit a piece
            SQUARE current_sq = sq;
            do
            {
                current_sq += dir;
                dmob += dm;
                E->A.AttackW[current_sq] ^= dA;
            } while (E->B.Board[current_sq] == empty);
        } while (bits != 0);
    }

    //--- Updating OPPONENT Block Attack ---

    At = E->A.AttackB[sq];
    if (At & qrbMask)
    {
        INT bits = fold_attack_bits(At & qrbMask);

        do
        {
            BLOCKTAB *BD  = &E->Global->A.BlockTab[bits];
            SQUARE    dir = BD->dir / 2;
            ATTACK    dA  = (ATTACK)(unsigned short)BD->rayBits & At;
            bits          = BD->nextBits / sizeof(BLOCKTAB);

            // Determine mobility delta (negative for black)
            INT dm;
            if (dA & bMask)
                dm = -bishopMob;
            else if (dA & rMask)
                dm = -rookMob;
            else
                dm = -queenMob;

            // Update attack along the ray until we hit a piece
            SQUARE current_sq = sq;
            do
            {
                current_sq += dir;
                dmob += dm;
                E->A.AttackB[current_sq] ^= dA;
            } while (E->B.Board[current_sq] == empty);
        } while (bits != 0);
    }

    // Return mobility change in white-POV.
    //
    // Convention: this C version hardcodes AttackW for the "player block" loop and AttackB for
    // the "opponent block" loop. So `dmob` is unconditionally accumulated as
    //   (white's mobility gain) - (black's mobility gain)
    // which is already in white-POV, regardless of which side is to move at the calling node.
    //
    // The asm version uses N->Attack (per-node pointer) for "player" so its dmob is in current
    // mover's POV, then it negates when N->player == black to convert to white-POV. Same final
    // semantics: caller always receives white-POV deltas.
    //
    // (Old code wrote `(E->B.player == white ? dmob : -dmob)` — that was correct only when the
    // root player is white; for root=black searches it returned the negated value of what the
    // asm returns, breaking evaluation symmetry.)
    return dmob;
} /* UpdBlockAttack */

/**************************************************************************************************/
/*                                                                                                */
/*                                      START UP INITIALIZATION                                   */
/*                                                                                                */
/**************************************************************************************************/

// The InitAttackModule() function must be called exactly once at startup. The function resets
// various global (const) data structures which are used (read only) by all engine processes.
// Must be initialized AFTER the Board module.

static void ComputeBitMasks(GLOBAL *Global);
static void ComputeBitTables(GLOBAL *Global);
static void ComputeSmattMasks(GLOBAL *Global);
static void ComputeAttackDir(GLOBAL *Global);

void InitAttackModule(GLOBAL *Global)
{
    ComputeBitMasks(Global);
    ComputeBitTables(Global);
    ComputeSmattMasks(Global);
    ComputeAttackDir(Global);
} /* InitAttackModule */

static void ComputeBitMasks(GLOBAL *Global) // Computes the 5 bit mask tables.
{
    ATTACK_COMMON *A = &(Global->A);

    for (INT i = 0; i <= 7; i++)
    {
        A->QueenBit[i]                   = 1L << i;
        A->RookBit[i]                    = (i < 4 ? 0 : 1L << (i + 8));
        A->BishopBit[i]                  = (i >= 4 ? 0 : 1L << (i + 8));
        A->KnightBit[i]                  = 1L << (i + 16);
        A->RayBit[i]                     = A->QueenBit[i] + A->RookBit[i] + A->BishopBit[i];
        A->DirBit[Global->B.QueenDir[i]] = A->RayBit[i];
    }
} /* ComputeBitMasks */

static void ComputeBitTables(GLOBAL *Global) // Computes the "LowBit", "HighBit" and "NumBits" tables.
{
    ATTACK_COMMON *A = &(Global->A);

    A->LowBit[0]  = -1;
    A->HighBit[0] = 8;
    A->NumBits[0] = 0;

    for (INT bits = 1; bits < 256; bits++)
    {
        A->NumBits[bits] = 0;
        for (INT i = 7; i >= 0; i--)
            if (bits & bit(i))
                A->LowBit[bits] = i, A->NumBits[bits]++;
        for (INT i = 0; i <= 7; i++)
            if (bits & bit(i))
                A->HighBit[bits] = i;
        A->NumBitsB[bits] = A->NumBits[bits];

        INT j = A->LowBit[bits];

        A->BlockTab[bits].rayBits  = 0x0101 << j;
        A->BlockTab[bits].dir      = 2 * Global->B.QueenDir[j];
        A->BlockTab[bits].nextBits = (bits - bit(j)) * sizeof(BLOCKTAB);
        A->BlockTab[bits].padding  = 0;
    }
} /* ComputeBitTables */

static void ComputeSmattMasks(GLOBAL *Global)
{
    ATTACK_COMMON *A = &(Global->A);

    A->SmattMask[wPawn] = A->SmattMask[bPawn] = 0L;
    A->SmattMask[wKnight] = A->SmattMask[bKnight] = pMask;
    A->SmattMask[wBishop] = A->SmattMask[bBishop] = pMask;
    A->SmattMask[wRook] = A->SmattMask[bRook] = bnpMask;
    A->SmattMask[wQueen] = A->SmattMask[bQueen] = rbnpMask;
} /* ComputeSmattMasks */

static void ComputeAttackDir(GLOBAL *Global)
{
    ATTACK_COMMON *A = &(Global->A);

    for (SQUARE sq = -h8; sq <= h8; sq++)
        A->AttackDir[sq] = 0;

    for (INT i = 0; i < 8; i++)
    {
        SQUARE dir = Global->B.QueenDir[i];
        for (SQUARE sq = dir, j = 1; j <= 7; sq += dir, j++)
            A->AttackDir[sq] = (dir << 5) | (i < 4 ? bDirMask : rDirMask);
        A->AttackDir[Global->B.KnightDir[i]] = nDirMask;
    }

    A->AttackDir[0x11] |= wPawnDirMask;
    A->AttackDir[0xF] |= wPawnDirMask;
    A->AttackDir[-0x11] |= bPawnDirMask;
    A->AttackDir[-0xF] |= bPawnDirMask;

    A->AttackDirMask[wQueen]  = qDirMask;
    A->AttackDirMask[wRook]   = rDirMask;
    A->AttackDirMask[wBishop] = bDirMask;
    A->AttackDirMask[wKnight] = nDirMask;
    A->AttackDirMask[wPawn]   = wPawnDirMask;

    A->AttackDirMask[bQueen]  = qDirMask;
    A->AttackDirMask[bRook]   = rDirMask;
    A->AttackDirMask[bBishop] = bDirMask;
    A->AttackDirMask[bKnight] = nDirMask;
    A->AttackDirMask[bPawn]   = bPawnDirMask;
} /* ComputeAttackDir */
