/**************************************************************************************************/
/*                                                                                                */
/* Module  : TRANSTABLES.C                                                                        */
/* Purpose : This module implements the transposition tables.                                     */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"
#include <stdint.h>

#include "TransTables.f"

/**************************************************************************************************/
/*                                                                                                */
/*                               TRANS ENTRY ACCESS HELPERS                                       */
/*                                                                                                */
/**************************************************************************************************/

// Helper functions to read/write TRANS entry fields
// Note: TRANS entries store only 4 bytes for the hash key (bytes 0-3),
// but HKEY is 8 bytes on 64-bit platforms. We must read/write only 4 bytes.
static inline HKEY GetTransHashKey(TRANS *t)
{
    // Read 4 bytes and zero-extend to HKEY
    return (HKEY)(*((uint32_t *)&t->data[0]));
}

static inline void SetTransHashKey(TRANS *t, HKEY key)
{
    // Write only lower 4 bytes
    *((uint32_t *)&t->data[0]) = (uint32_t)key;
}

static inline BYTE GetTransPly(TRANS *t)
{
    return t->data[4];
}

static inline void SetTransPly(TRANS *t, BYTE ply)
{
    t->data[4] = ply;
}

static inline BYTE GetTransMaxPly(TRANS *t)
{
    return t->data[5];
}

static inline void SetTransMaxPly(TRANS *t, BYTE maxPly)
{
    t->data[5] = maxPly;
}

static inline INT GetTransScore(TRANS *t)
{
    return *((INT *)&t->data[6]);
}

static inline void SetTransScore(TRANS *t, INT score)
{
    *((INT *)&t->data[6]) = score;
}

static inline BYTE GetTransFrom(TRANS *t)
{
    return t->data[8];
}

static inline void SetTransFrom(TRANS *t, BYTE from)
{
    t->data[8] = from;
}

static inline BYTE GetTransTo(TRANS *t)
{
    return t->data[9];
}

static inline void SetTransTo(TRANS *t, BYTE to)
{
    t->data[9] = to;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                ALLOCATE/RESET TRANSPOSITION TABLES                             */
/*                                                                                                */
/**************************************************************************************************/

// Allocates and initializes the transposition tables based on the amount of memory set aside for
// the specified engine (in the engine parameter block). Additionally, the transposition table
// pointers in the search nodes are initialized.

void CalcTransState(ENGINE *E)
{
    TRANS_STATE *T = &E->Tr;

    T->transSize = (1L << 13);                     // Minimum number of entries.
    ULONG free   = E->P.transSize / sizeof(TRANS); // Total number of free entries.

    if (T->transSize > free) // A minimum of 4*2^11 = 8192 entries are required.
    {
        T->transTabOn = false;

        T->transSize     = 0;
        T->transUsed     = 0;
        T->hashIndexMask = 0;

        T->TransTab1W = nil;
        T->TransTab1B = nil;
        T->TransTab2W = nil;
        T->TransTab2B = nil;

        // Don't initialize node pointers when disabled
        return;
    }

    T->transTabOn = true;

    while (2 * T->transSize <= free)
        T->transSize *= 2; // Calc the total number of used entries

    T->transUsed     = 0;
    ULONG singleSize = T->transSize / 4; // Size of a single transposition table
    T->hashIndexMask = singleSize - 1;   // Compute the hash index mask.

    T->TransTab1W = E->P.TransTables; // "Allocate" the 4 transpostion tables
    T->TransTab1B = T->TransTab1W + singleSize;
    T->TransTab2W = T->TransTab1B + singleSize;
    T->TransTab2B = T->TransTab2W + singleSize;

    // Update search nodes' trans table pointers
    // Note: Only initialize if whiteNode array is properly allocated
    if (E->S.whiteNode != NULL)
    {
        for (INT d = 0; d <= maxSearchDepth + 2; d++) // Finally update search nodes.
        {
            E->S.whiteNode[d - 2].TransTab1 = (even(d) ? T->TransTab1W : T->TransTab1B);
            E->S.whiteNode[d - 2].TransTab2 = (even(d) ? T->TransTab2W : T->TransTab2B);
        }
    }
} /* CalcTransState */

/*---------------------------------- Reset Transposition Tables ----------------------------------*/
// Resets the transposition table by setting the "ply" and "maxPly" fields of all entries to -1,
// as well as "piece" = empty.

void ResetTransTab(ENGINE *E)
{
    TRANS_STATE *T = &E->Tr;

    if (!T->transTabOn)
        return;

    T->transUsed = 0;

    // Reset all entries: invalidate hashkey (so probe never finds a stale match) AND
    // mark ply field 0xFF (so the StoreTransTab "previously unused?" check works).
    // The asm uses 0x0000FFFF stored across bytes 2-5 of each entry; this C version is
    // slightly more thorough, also zeroing bytes 0-1 of the hashkey to fully prevent
    // false probe matches against garbage data left over from prior allocations.
    LONG count = T->transSize / 4; // Number of entries per table

    for (LONG i = 0; i < count; i++)
    {
        SetTransHashKey(&T->TransTab1W[i], 0);
        SetTransHashKey(&T->TransTab1B[i], 0);
        SetTransHashKey(&T->TransTab2W[i], 0);
        SetTransHashKey(&T->TransTab2B[i], 0);
        T->TransTab1W[i].data[4] = 0xFF; // ply = -1 (signed-char) → never satisfies tply >= N->ply
        T->TransTab1B[i].data[4] = 0xFF;
        T->TransTab2W[i].data[4] = 0xFF;
        T->TransTab2B[i].data[4] = 0xFF;
    }
} /* ResetTransTab */

/**************************************************************************************************/
/*                                                                                                */
/*                                      PROBE TRANSPOSITION TABLE                                 */
/*                                                                                                */
/**************************************************************************************************/

/*---------------------------------------- Probe Trans Table -------------------------------------*/
// On entry to each node (but AFTER having checked draw info and computed the hashkey for the
// current position), we probe the transposition table: If the position is found, we first check
// if the score can be used directly. If it can't we instead return the stored "refutation" move
// (if any), which can then be search subsequently.
//
// NOTE: "ProbeTransTab" sets the rfm.piece field (even if it returns false). If rfm.piece = empty,
// then obviously there is NO refutation move (it's a null move). If on the other hand, a refutation
// move exists, then rfm.piece = the moving piece, and the reset of the rfm fields are subsequently
// set by "GetTransMove" (which may then NOT be called if rfm.piece = empty).

BOOL ProbeTransTab(ENGINE *E)
{
    NODE *N        = E->S.currNode;
    HKEY  hashkey  = N->hashKey;
    HKEY  hashmask = E->Tr.hashIndexMask;

    // Compute trans table entry addresses
    LONG   index = (hashkey & hashmask);
    TRANS *trn1  = N->TransTab1 + index;
    TRANS *trn2  = N->TransTab2 + index;

    N->trans1 = trn1;
    N->trans2 = trn2;

    if (!(E->R.rflags & rflag_TransTabOn) || N->drawType != drawType_None)
    {
        if (!N->pvNode)
            N->rfm.piece = empty;
        return false;
    }

    if (N->pvNode)
        return false;

    N->rfm.piece = empty;

    // Probe conditional hash table (trans1)
    HKEY thashkey = GetTransHashKey(trn1);
    if (((hashkey ^ thashkey) >> 11) == 0)
    {
        BYTE tply    = GetTransPly(trn1);
        BYTE tmaxply = GetTransMaxPly(trn1);

        if ((INT)(signed char)tply >= N->ply && (INT)(signed char)tmaxply >= N->maxPly)
        {
            INT tscore = GetTransScore(trn1);

            if (tscore >= mateWinVal)
                tscore -= N->depth;
            else if (tscore <= mateLoseVal)
                tscore += N->depth;

            N->score = tscore;

            if (thashkey & trans_TrueScoreBit)
                return true;
            else if (thashkey & trans_CutoffBit)
            {
                if (tscore >= N->beta)
                    return true;
            }
            else if (tscore <= N->alpha0)
                return true;
        }

        PIECE tpiece = thashkey & trans_PieceMask;
        if (tpiece != empty)
        {
            N->tmove     = trn1;
            N->rfm.piece = tpiece + N->player;
        }
    }

    // Probe unconditional hash table (trans2)
    thashkey = GetTransHashKey(trn2);
    if (((hashkey ^ thashkey) >> 11) == 0)
    {
        BYTE tply    = GetTransPly(trn2);
        BYTE tmaxply = GetTransMaxPly(trn2);

        if ((INT)(signed char)tply >= N->ply && (INT)(signed char)tmaxply >= N->maxPly)
        {
            INT tscore = GetTransScore(trn2);

            if (tscore >= mateWinVal)
                tscore -= N->depth;
            else if (tscore <= mateLoseVal)
                tscore += N->depth;

            N->score = tscore;

            if (thashkey & trans_TrueScoreBit)
                return true;
            else if (thashkey & trans_CutoffBit)
            {
                if (tscore >= N->beta)
                    return true;
            }
            else if (tscore <= N->alpha0)
                return true;
        }

        PIECE tpiece = thashkey & trans_PieceMask;
        if (tpiece != empty)
        {
            N->tmove     = trn2;
            N->rfm.piece = tpiece + N->player;
        }
    }

    return false;
} /* ProbeTransTab */

/*----------------------------------------- Get Trans Move ---------------------------------------*/
// The "GetTransMove" routine indicates if the transposition table move (found by "ProbeTransTab")
// may be searched (i.e. if it is both pseudo-legal and applicable at the current node). If so, the
// move is stored in "rfm". May NOT be called if rfm is a null move (i.e. if rfm.piece = empty).

BOOL GetTransMove(ENGINE *E)
{
    NODE  *N      = E->S.currNode;
    TRANS *tm     = N->tmove;
    HKEY   tflags = GetTransHashKey(tm);
    SQUARE tfrom  = GetTransFrom(tm);
    SQUARE tto    = GetTransTo(tm);
    PIECE  tcap   = (tflags & trans_CapMask) >> 3;

    if (tcap == empty)
    {
        if (N->quies)
        {
            if (N->maxPly == 0 || !(tflags & trans_ShQuiesBit))
            {
                N->rfm.piece = empty;
                return false;
            }
        }
    }
    else
    {
        tcap += black;
        tcap = tcap - N->player;
    }

    N->rfm.cap  = tcap;
    N->rfm.dply = (tflags & trans_DPlyMask) >> 6;
    N->rfm.from = tfrom;
    N->rfm.to   = tto;
    N->rfm.type = mtype_Normal;

    PIECE tpiece = (tflags & trans_PieceMask) + N->player;
    N->rfm.dir   = E->Global->A.AttackDir[tto - tfrom] >> 5;

    // Verify move is pseudo-legal
    if (E->B.Board[tfrom] != tpiece || E->B.Board[tto] != tcap)
    {
        N->rfm.piece = empty;
        return false;
    }

    PIECE ptype = pieceType(tpiece);

    switch (ptype)
    {
    case knight:
        return true;

    case pawn:
        if (tcap != empty)
            return true;
        if (E->B.Board[tfrom + N->pawnDir] == empty)
            return true;
        break;

    case king:
        if (N->Attack_[tto] == 0)
            return true;
        break;

    default: {
        SQUARE sq = tfrom + N->rfm.dir;
        while (sq != tto)
        {
            if (E->B.Board[sq] != empty)
            {
                N->rfm.piece = empty;
                return false;
            }
            sq += N->rfm.dir;
        }
        return true;
    }
    }

    N->rfm.piece = empty;
    return false;
} /* GetTransMove */

/**************************************************************************************************/
/*                                                                                                */
/*                                       STORE TRANSPOSITION TABLE                                */
/*                                                                                                */
/**************************************************************************************************/

// Stores the current position (score, ply counters, flags, best move) in the transposition table,
// thus overriding the previous entry.

void StoreTransTab(ENGINE *E)
{
    NODE *N = E->S.currNode;

    if (!(E->R.rflags & rflag_TransTabOn) || N->drawType != drawType_None)
        return;

    TRANS *trn     = N->trans1;
    BYTE   trn1ply = GetTransPly(N->trans1);

    if (N->ply < (INT)(signed char)trn1ply)
        trn = N->trans2;

    BYTE oldply = GetTransPly(trn);
    if (oldply == 0xFF)
        E->Tr.transUsed++;

    HKEY hashkey = N->hashKey & trans_HashLockMask;
    HKEY tflags  = 0;

    SetTransPly(trn, N->ply);
    SetTransMaxPly(trn, N->maxPly);

    INT tscore = N->score;

    if (tscore >= N->beta)
    {
        tflags |= trans_CutoffBit;
        tscore = N->beta;
    }
    else if (tscore > N->alpha0)
    {
        tflags |= trans_TrueScoreBit;
    }
    else
    {
        tscore = N->alpha0;
    }

    if (tscore >= mateWinVal)
        tscore += N->depth;
    else if (tscore <= mateLoseVal)
        tscore -= N->depth;

    SetTransScore(trn, tscore);

    // Store move
    MOVE *m      = &N->BestLine[0];
    PIECE mpiece = pieceType(m->piece);

    if (mpiece == empty || m->type != mtype_Normal)
    {
        SetTransHashKey(trn, hashkey | tflags);
        return;
    }

    SetTransFrom(trn, m->from);
    SetTransTo(trn, m->to);

    tflags |= mpiece;
    tflags |= (pieceType(m->cap) << 3);
    tflags |= ((m->dply & 0x03) << 6);

    if (N->bestGen == gen_E || N->bestGen == gen_J)
        tflags |= trans_ShQuiesBit;

    SetTransHashKey(trn, hashkey | tflags);
} /* StoreTransTab */
