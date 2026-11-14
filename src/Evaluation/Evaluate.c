/**************************************************************************************************/
/*                                                                                                */
/* Module  : EVALUATE.C                                                                           */
/* Purpose : This is the main Engine Evaluation module. The piece values are added to the pawn    */
/*           structure evaluation and the special end game evaluation. Additionally, this module  */
/*           contains the main incremental evaluation routines.                                   */
/*                                                                                                */
/**************************************************************************************************/

#include "Evaluate.f"
#include "Engine.f"

// At the root node the total evaluation is computed from scratch by "CalcEvaluateState()". Then
// during the search, the evaluation for the next node NN is computed incrementally based on the
// following evaluation components from the current node N:
//
// pvSumEval (The piece value sum of the current board)
// ----------------------------------------------------
// NN->pvSumVal = N->pvSumVal + pvChange(N->m)
//
// pawnStructEval (The pawn structure evaluation)
// ----------------------------------------------
// If N->m is a pawn move, a pawn capture or a promotion (i.e. if N->m changes the pawn structure),
// NN->pawnStructEval is computed from scratch by the "EvalpawnStruct" routine. Otherwise we simply
// copy N->pawnStructEval to NN->pawnStructEval.
//
// endGameEval (The special endgame evaluation)
// --------------------------------------------
// For certain material configurations a special value is computed.
//
// This evaluation is always done BEFORE performing the move in order to speed up the search by
// pruning (selecting) moves without performing them.
// ### optimization : If move looks very "bad" and it's going to be skipped by the selection scheme
// anyway, make "fast" evaluation.

static INT  EvalMovePV(ENGINE *E);
static INT  EvalPawnStruct(ENGINE *E);
static INT  EvalEndGame(ENGINE *E);
static INT  EvalRuleOfSquare(ENGINE *E);
static INT  EvalKPK(ENGINE *E, COLOUR pawnColor);
static INT  OppColBishops(ENGINE *E);
static void GetPassedPawns(BYTE **Psq, INT _rank, INT _pass1);

static void EvalPawnStructRoot(ENGINE *E);

/**************************************************************************************************/
/*                                                                                                */
/*                                    ROOT EVALUATION FUNCTION                                    */
/*                                                                                                */
/**************************************************************************************************/

void CalcEvaluateState(ENGINE *E) // NOTE: Must be called AFTER "CalcPieceValState()".
{
    NODE *N = E->S.rootNode;

    // Calc PieceValue sum evaluation:
    N->pvSumEval = E->V.sumPV;

    // Calc pawn structure evaluation:
    EvalPawnStructRoot(E);
    EvalPawnStruct(E);

    // Calc end game evaluation (turn prevNode->m temporarily into capture to force recomputation!):
    NODE *prevNode = N - 1;
    prevNode->m.cap++;
    N->endGameEval = EvalEndGame(E);
    prevNode->m.cap--;

    // Finally calc total evaluation for root node:
    N->totalEval = N->pvSumEval + N->mobEval + N->pawnStructEval + N->endGameEval; //### TEMP: IGNORE MOBVAL

    if (N->player == black)
        N->totalEval = -N->totalEval;

    // Root mirror of UpdateDrawState's insufficient-material check (root has no
    // preceding move, so UpdateDrawState never runs here).
    {
        ULONG pc = E->B.pieceCount;
        if ((pc & 0xFE0F) == 0 && (pc & 0xFE0F0000) == 0)
        {
            INT sum = (INT)pc + ((INT)pc >> 16); // matches asm: srawi + add
            if ((sum & 0x0E00) == 0)
            {
                N->drawType  = drawType_InsuffMtrl;
                N->totalEval = 0;
            }
        }
    }

    N->capSelVal = 0;
} /* CalcEvaluateState */

/**************************************************************************************************/
/*                                                                                                */
/*                                EVALUATE SINGLE MOVE (INCREMENTALLY)                            */
/*                                                                                                */
/**************************************************************************************************/

// The "EvalMove" routine is the heart of the evaluation routine. It evaluates the current move
// incrementally and updates the evaluation components and total at the next node.

INT EvalMove(ENGINE *E)
{
    NODE *N        = E->S.currNode;
    NODE *nextNode = N + 1;

    INT eval = EvalMovePV(E);
    // eval += EvalMoveMob();                    // EvalMoveMob(); //### TEMP: IGNORE MOBVAL

    // PieceVal is stored in WHITE-POV (BaseVal[bPiece] = -BaseVal[wPiece]), so dPV from
    // EvalMovePV is in white-POV. NN->totalEval must be in NN's perspective (= opposite
    // of N) so that downstream callers — particularly SelectMove which uses `-NN->totalEval`
    // to convert to N's perspective — work correctly.
    //
    // To convert white-POV → NN's perspective:
    //   • N=white → NN=black: negate (white-POV → black-POV)
    //   • N=black → NN=white: don't negate (white-POV stays white-POV)
    // i.e. negate when N is WHITE.
    //
    // (The old code had `if (N->player == black) eval = -eval;` — sign inverted. The asm
    // *comment* on this line says "if player==black, negate" but the asm *code* does the
    // opposite (negates when player==white). The C author followed the misleading comment.
    // The asm code is correct: SelectMove's `-NN->totalEval` only makes sense if NN->totalEval
    // is in NN's perspective.)
    if (N->player == white)
        eval = -eval;

    nextNode->totalEval = eval;

    return nextNode->totalEval;
} /* EvalMove */

/**************************************************************************************************/
/*                                                                                                */
/*                                      EVALUATE POSITION                                         */
/*                                                                                                */
/**************************************************************************************************/

void Evaluate(ENGINE *E)
{
    NODE *N        = E->S.currNode;
    NODE *prevNode = N - 1;

    //--- Evaluate pawn structure ---
    // If last move did NOT affect the pawn structure, we simply copy the pawn structure evaluation
    // of the current node (including the PassSq[] tables).

    BOOL needsRecalc = false;

    if (pieceType(prevNode->m.piece) == pawn)
        needsRecalc = true;
    else if (prevNode->m.cap != empty && pieceType(prevNode->m.cap) == pawn)
        needsRecalc = true;
    else if (prevNode->m.cap && (E->B.pieceCount & 0xFFF0) == 0 && (E->B.pieceCount & 0xFFF00000) == 0)
        needsRecalc = true;

    if (needsRecalc)
    {
        EvalPawnStruct(E);
    }
    else
    { // else
        // Copy pawn structure data from previous node
        for (INT i = 0; i < 20; i++)
            N->PassSqW[i] = prevNode->PassSqW[i];
        N->pawnStructEval = prevNode->pawnStructEval;
    }

    //--- Evaluate end game ---
    // In certain endgames with limited material left, the score is reduced (based on pvSumEval)

    N->endGameEval = EvalEndGame(E);
    INT capSelVal  = N->endGameEval;

    //--- Total Evaluation ---
    INT totalEval = N->pvSumEval + N->mobEval + N->pawnStructEval + N->endGameEval;

    if (N->player == black)
    {
        totalEval = -totalEval;
        capSelVal = -capSelVal;
    }

    N->totalEval = totalEval;
    N->capSelVal = (capSelVal > 0 ? 0 : capSelVal);
} /* Evaluate */

/**************************************************************************************************/
/*                                                                                                */
/*                                   PIECE VALUE CHANGE EVALUATION                                */
/*                                           NN->pvSumEval                                        */
/*                                                                                                */
/**************************************************************************************************/

// Piece values form the main part of the evaluation. The "Piece Value Sum" is computed
// incrementally before each move is performed:
//
//    NN->pvSumEval = N->pvSumEval + deltaPV(N->m)

static INT EvalMovePV(ENGINE *E)
{
    NODE *N        = E->S.currNode;
    NODE *nextNode = N + 1;
    MOVE *m        = &N->m;

    INT dPV;

    // Calculate piece value at destination minus piece value at source
    dPV = E->V.PieceVal[m->piece][m->to];
    dPV -= E->V.PieceVal[m->piece][m->from];

    if (m->cap)
        dPV -= E->V.PieceVal[m->cap][m->to];

    // Handle special move types
    switch (m->type)
    {
    case mtype_O_O:
        dPV = E->V.o_oPV[N->player];
        break;

    case mtype_O_O_O:
        dPV = E->V.o_o_oPV[N->player];
        break;

    case mtype_EP: {
        PIECE  enemyPawn = (N->player == white ? bPawn : wPawn);
        SQUARE behindSq  = m->to - N->pawnDir;
        dPV -= E->V.PieceVal[enemyPawn][behindSq];
        break;
    }

    default:
        if (m->type & mtype_Promotion)
            dPV += E->V.PieceVal[m->type][m->to];
        break;
    }

    N->dPV              = dPV;
    nextNode->pvSumEval = N->pvSumEval + dPV;

    return nextNode->pvSumEval;
} /* EvalMovePV */

/**************************************************************************************************/
/*                                                                                                */
/*                                    PAWN STRUCTURE EVALUATION                                   */
/*                                        N->pawnStructEval                                       */
/*                                                                                                */
/**************************************************************************************************/

// Pawn structure evaluation (f = game phase [0...9]):

#define isoBack_Closed  2           //  -8 if isolated/backward on closed file.
#define isoBack_Open(f) (6 - f / 2) // -28...-16 if isolated/backward on semi-open file.
#define iso_Any         3           // -12 extra if isolated.
#define dob_Any         4           // -16 if doubled on any file.
#define dobIso_Any      18          // -72 if doubled and isolated on any file.
#define passVal(f)      (7 + f / 2) // +20...+36 if Passed pawn (isoBack_Open is subtracted during eval).

/*--------------------------------- Root Pawn Structure Evaluation -------------------------------*/
// First we compute the various pawn structure evaluation tables (some of which depend on the
// game phase). The bonus must be high enough to outweigh the penalty
// of isolated pawns. This also automatically gives an extra bonus for connected passed pawns.

// It's also very important to reset the PassSqW[] and PassSqB[] tables at the root node before
// calling the general pawn structure evaluation.

static void EvalPawnStructRoot(ENGINE *E)
{
    EVAL_STATE *e = &(E->E);
    INT         f = E->V.phase;

    for (INT i = 0; i <= 0xFF; i++)
    {
        INT n = E->Global->A.NumBits[i];

        e->IsoBackVal[i]  = n * isoBack_Closed;
        e->IsoBackVal_[i] = n * (isoBack_Open(f) - isoBack_Closed);
        e->IsoVal[i]      = n * iso_Any;
        e->DobVal[i]      = n * dob_Any;
        e->DobIsoVal[i]   = n * (dobIso_Any - dob_Any - iso_Any);

        e->PassedVal[i] = n * passVal(f);
        if (n > 1 && (i & 0x03) && (i & 0xC0))
            e->PassedVal[i] += f / 2 + 2;
    }

    E->S.rootNode->PassSqW[0] = E->S.rootNode->PassSqB[0] = 0;
} /* EvalPawnStructRoot */

/*------------------------------- General Pawn Structure Evaluation ------------------------------*/
// EvalPawnStruct() returns a pawn structure evaluation of doubled, isolated, backward and
// passed pawns. Also the lists N->PassSqW[] and N->PassSqB[] of white and black passed pawns
// are computed if the rule of the square is applicable. These lists are used by the end game
// routines EvalRuleOfSquare() and EvalKPK().
// The evaluation values (punishments) below are multiplied by 4 (<< 2) during evaluation.

#define sum  rLocal1 // Friendly occupied files so far (bit list).
#define back rLocal2 // Files with friendly backward pawns so far (bit list).
#define dob  rLocal3 // Files with friendly doubled pawns so far (bit list).
#define pass rLocal4 // Files with enemy passed pawns so far (bit list).
#define iso  rLocal5 // Files with friendly isolated pawns (bit list).
#define sum_ rTmp10  // Enemy occupied files (bit list). For evaluation only

#define Blk rTmp9 // Base pointers to PawnBlk[] and IsoPawns[].
#define Iso rTmp8
#define Psq rTmp7 // List of passed pawns (excl. on 7th), stored in N->PassSqX[]

#define peval  rTmp6 // Pawn struct evaluation sum
#define val    rTmp5 // Pawn struct evaluation sum
#define ValTab rTmp4 // Pawn struct evaluation sum

#define pwn rTmp5 // White pawns on current rank (temp)
#define pbn rTmp4 // Black pawns on current rank (temp)
#define blk rTmp3 // Block bits (temp)

#define pw(n) (ENGINE.B.PawnStructW + n - 1)(rEngine) // E->B.PawnStructW[n]
#define pb(n) (ENGINE.B.PawnStructB + n - 1)(rEngine) // E->B.PawnStructB[n]

// Helper function to evaluate a single rank for white pawns.
// `rank` is the chess rank (1..8); the array is indexed by row (rank - 1) to match
// the upstream asm `pw(n)` macro which evaluates to PawnStructW[n - 1].
static inline void EvalRankWhite(ENGINE *E, INT rank, INT pasRank, BYTE *sum, BYTE *dob, BYTE *back, BYTE *pass,
                                 BYTE *sum_, BYTE **Psq, BYTE *BlkTab, BYTE *IsoTab)
{
    NODE *N   = E->S.currNode;
    BYTE  pwn = E->B.PawnStructW[rank - 1];
    BYTE  pbn = E->B.PawnStructB[rank - 1];

    if (pwn)
    {
        BYTE iso = IsoTab[pwn];
        *dob |= (*sum) & pwn;
        *sum |= pwn;
        BYTE blk = BlkTab[*sum];
        *back    = (iso & blk) | (*back & ~pwn);
    }

    BYTE blk = BlkTab[*sum];
    BYTE ps  = (pbn & blk) & ~(*sum);
    *sum_ |= pbn;

    if (ps)
    {
        *pass |= ps;
        if ((E->B.pieceCount & 0xFFF0) == 0)
        {
            GetPassedPawns(Psq, pasRank, ps);
        }
    }
}

// Helper function to evaluate a single rank for black pawns.
// `rank` is the chess rank (1..8); the array is indexed by row (rank - 1).
static inline void EvalRankBlack(ENGINE *E, INT rank, INT pasRank, BYTE *sum, BYTE *dob, BYTE *back, BYTE *pass,
                                 BYTE *sum_, BYTE **Psq, BYTE *BlkTab, BYTE *IsoTab)
{
    NODE *N   = E->S.currNode;
    BYTE  pbn = E->B.PawnStructB[rank - 1];
    BYTE  pwn = E->B.PawnStructW[rank - 1];

    if (pbn)
    {
        BYTE iso = IsoTab[pbn];
        *dob |= (*sum) & pbn;
        *sum |= pbn;
        BYTE blk = BlkTab[*sum];
        *back    = (iso & blk) | (*back & ~pbn);
    }

    BYTE blk = BlkTab[*sum];
    BYTE ps  = (pwn & blk) & ~(*sum);
    *sum_ |= pwn;

    if (ps)
    {
        *pass |= ps;
        if ((E->B.pieceCount & 0xFFF00000) == 0)
        {
            GetPassedPawns(Psq, pasRank, ps);
        }
    }
}

static INT EvalPawnStruct(ENGINE *E)
{
    NODE *N      = E->S.currNode;
    BYTE *BlkTab = E->Global->E.BlkPawnsN;
    BYTE *IsoTab = E->Global->E.IsoPawns;
    INT   peval  = 0;

    // --- WHITE Pawn Structure ---
    // Match asm: pw(2) = PawnStructW[1] = chess rank 2 (white starting pawns).
    BYTE sum  = E->B.PawnStructW[1];
    BYTE pass = E->B.PawnStructB[1];
    BYTE dob  = 0;
    BYTE sum_ = 0;
    BYTE back = IsoTab[sum];

    BYTE *Psq = N->PassSqB - 1;

    if (pass && (E->B.pieceCount & 0xFFF0) == 0)
    {
        GetPassedPawns(&Psq, 0x10, pass);
    }

    // Process ranks 3-7
    EvalRankWhite(E, 3, 0x20, &sum, &dob, &back, &pass, &sum_, &Psq, BlkTab, IsoTab);
    EvalRankWhite(E, 4, 0x30, &sum, &dob, &back, &pass, &sum_, &Psq, BlkTab, IsoTab);
    EvalRankWhite(E, 5, 0x40, &sum, &dob, &back, &pass, &sum_, &Psq, BlkTab, IsoTab);
    EvalRankWhite(E, 6, 0x50, &sum, &dob, &back, &pass, &sum_, &Psq, BlkTab, IsoTab);
    EvalRankWhite(E, 7, 0x50, &sum, &dob, &back, &pass, &sum_, &Psq, BlkTab, IsoTab);

    *(++Psq) = 0;

    // --- ISOLATED PAWNS ---
    BYTE iso = IsoTab[sum];
    peval -= E->E.IsoVal[iso];

    // --- DOUBLED PAWNS ---
    if (dob)
        peval -= E->E.DobVal[dob];

    // --- DOUBLED & ISOLATED PAWNS ---
    if (dob & iso)
    {
        peval -= E->E.DobIsoVal[dob & iso];
        back ^= (dob & iso);
    }

    // --- BACKWARD/ISOLATED PAWNS ---
    if (back)
    {
        peval -= E->E.IsoBackVal[back];
        BYTE bits = back & ~sum_;
        if (bits)
            peval -= E->E.IsoBackVal_[bits];
    }

    // --- PASSED BLACK PAWNS ---
    if (pass)
        peval -= E->E.PassedVal[pass];

    // --- BLACK Pawn Structure ---
    // Match asm: pb(7) = PawnStructB[6] = chess rank 7 (black starting pawns).
    sum  = E->B.PawnStructB[6];
    pass = E->B.PawnStructW[6];
    dob  = 0;
    sum_ = 0;
    back = IsoTab[sum];

    Psq = N->PassSqW - 1;

    if (pass && (E->B.pieceCount & 0xFFF00000) == 0)
    {
        GetPassedPawns(&Psq, 0x60, pass);
    }

    // Process ranks 6-2 (black perspective)
    EvalRankBlack(E, 6, 0x50, &sum, &dob, &back, &pass, &sum_, &Psq, BlkTab, IsoTab);
    EvalRankBlack(E, 5, 0x40, &sum, &dob, &back, &pass, &sum_, &Psq, BlkTab, IsoTab);
    EvalRankBlack(E, 4, 0x30, &sum, &dob, &back, &pass, &sum_, &Psq, BlkTab, IsoTab);
    EvalRankBlack(E, 3, 0x20, &sum, &dob, &back, &pass, &sum_, &Psq, BlkTab, IsoTab);
    EvalRankBlack(E, 2, 0x20, &sum, &dob, &back, &pass, &sum_, &Psq, BlkTab, IsoTab);

    *(++Psq) = 0;

    // --- ISOLATED PAWNS ---
    iso = IsoTab[sum];
    peval += E->E.IsoVal[iso];

    // --- DOUBLED PAWNS ---
    if (dob)
        peval += E->E.DobVal[dob];

    // --- DOUBLED & ISOLATED PAWNS ---
    if (dob & iso)
    {
        peval += E->E.DobIsoVal[dob & iso];
        back ^= (dob & iso);
    }

    // --- BACKWARD/ISOLATED PAWNS ---
    if (back)
    {
        peval += E->E.IsoBackVal[back];
        BYTE bits = back & ~sum_;
        if (bits)
            peval += E->E.IsoBackVal_[bits];
    }

    // --- PASSED WHITE PAWNS ---
    if (pass)
        peval += E->E.PassedVal[pass];

    N->pawnStructEval = peval * 4;

    return N->pawnStructEval;
} /* EvalPawnStruct */

// Stores the bit list "_pass1" of passed pawns on current rank "_rank" in "PassSq?[]".
// Psq is a pointer to the current position in the PassSq array (passed by reference)

static void GetPassedPawns(BYTE **Psq, INT _rank, INT _pass1)
{
    while (_pass1)
    {
        INT  _file  = 31 - __builtin_clz(_pass1);
        BYTE sq     = _rank | _file;
        *(++(*Psq)) = sq;
        _pass1 &= ~(1 << _file);
    }
} /* GetPassedPawns */

#undef sum
#undef back
#undef dob
#undef pass
#undef iso

#undef Blk
#undef Iso
#undef Psq

#undef peval
#undef val
#undef ValTab

#undef pwn
#undef pbn
#undef blk

#undef pw
#undef pb

/**************************************************************************************************/
/*                                                                                                */
/*                                         END GAME EVALUATION                                    */
/*                                           N->endGameEval                                       */
/*                                                                                                */
/**************************************************************************************************/

// EvalEndGame() computes an evaluation in special end games where the normal evaluation (piece
// values, mobility and pawn structure) would be insufficient: If the rule of the square is
// applicable EvalRuleOfSquare() or EvalKPK() is called. Otherwise a material configuration
// modification value N->mtrlEndVal is computed and added to N->E to deal with cases such
// as KNNK, KNKP, KQKR e.t.c. This value is computed incrementally, that is if the previous
// move was not a capture we simply set N->mtrlEndVal = PN->mtrlEndVal. Otherwise we compute
// N->mtrlEndVal from scratch.

// Distinct endgame groups:
//
// (1) BOTH WHITE AND BLACK HAS PAWNS
//     Check opposite coloured bishops
//
// (2) ONLY WHITE HAS PAWNS
//     Exit if too much material (i.e. any Queens or Rooks or too many Bishop/Knight/Pawns)
//     KP:K(N|B)      +175    White cannot lose even though 200 points behind
//     KPP:K(N|B)     +125    White cannot lose (and may even win!) even though 100 points behind
//     KP(N|B):K(N|B) -100    The extra pawn isn't worth much. Black just needs to sacrifice his B/N
//     KP*(N|B):KNN   +200    The extra Black knight isn't worth much
//     KP:KNN         +450    Usually a draw (because K:KNN drawn), except in rare zugswang cases.
//     KPP:KNN        +400    Usually a draw (because K:KNN drawn), except in rare zugswang cases.
//     KPPP:KNN       +350    Probably slightly advantagous to white!!
//
// (3) ONLY BLACK HAS PAWNS
//     Exit if too much material (i.e. any Queens or Rooks or too many Bishop/Knight/Pawns)
//     K(N|B):KP      -175    Black cannot lose even though 200 points behind
//     K(N|B):KPP     -125    Black cannot lose (and may even win!) even though 100 points behind
//     K(N|B):KP(N|B) +100    The extra pawn isn't worth much. White just needs to sacrifice his B/N
//     KNN:KP         -450    Usually a draw (because KNN:K drawn), except in rare zugswang cases.
//     KNN:KPP        -400    Usually a draw (because KNN:K drawn), except in rare zugswang cases.
//     KNN:KPPP       -350    Probably slightly advantagous to black!!
//
// (4) NO PAWNS AT ALL
//     (a) BOTH WHITE AND BLACK HAS OFFICERS
//         Reduce advantage of leading side (drawish)
//     (b) ONLY WHITE HAS OFFICERS
//         if KNNK it's a draw
//         otherwise it's a forced win (the cases KNK and KBK are handled by the draw check).
//     (c) ONLY WHITE HAS OFFICERS
//         if KKNN it's a draw
//         otherwise it's a forced win (the cases KKN and KKB are handled by the draw check).

static INT EvalEndGame(ENGINE *E)
{
    NODE *N          = E->S.currNode;
    NODE *prevNode   = N - 1;
    ULONG pieceCount = E->B.pieceCount;

    // Check for KPK endgames or rule of the square applicability
    if (N->PassSqW[0] || N->PassSqB[0])    // if (N->PassSqW[0] || N->PassSqB[0])
    {                                      // {
        if (pieceCount == 0x00000001)      //    if (pieceCount == 0x00000001)
            return EvalKPK(E, white);      //       return EvalKPK(white);
        else if (pieceCount == 0x00010000) //    else if (pieceCount == 0x00010000)
            return EvalKPK(E, black);      //       return EvalKPK(black);
        else                               //    else
            return EvalRuleOfSquare(E);    //       return EvalRuleOfSquare();
    } // }

    // Check for too much material (any queens/rooks or many officers)
    if ((pieceCount & 0xEC00EC00) != 0) // if ("Too much material")
        return 0;                       //    return 0;

    // If no capture and not a promotion or EP, use cached value.
    // (Original C port mask was 0x0F00 — bits 8-11 — but m.type uses bits 0-7,
    // so the promotion test was always true and the cache was wrongly hit on
    // promotion moves, propagating stale endGameEval after material change.)
    if (prevNode->m.cap == empty && (prevNode->m.type & (mtype_Promotion | mtype_EP)) == 0)
        return prevNode->endGameEval;

    // Extract white and black material counts
    UINT pWhite = pieceCount & 0xFFFF;         // pWhite = pieceCount & 0xFFFF;
    UINT pBlack = (pieceCount >> 16) & 0xFFFF; // pBlack = (pieceCount >> 16) & 0xFFFF;

    // Check if both sides have pawns
    if ((pWhite & 0x000F) && (pBlack & 0x000F)) // if (White has pawns AND Black has pawns)
    {                                           // {
        // Check for opposite colored bishops (KBP* vs KBP*)
        if ((pieceCount & 0xFFF0FFF0) == 0x01000100) //    if ("KBP*KBP*")
            return OppColBishops(E);                 //       return OppColBishops();
        else                                         //    else
            return 0;                                //       return 0;
    } // }

    // Check if only white has pawns
    if ((pWhite & 0x000F) && !(pBlack & 0x000F)) // else if (Only white has pawns)
    {                                            // {
        // Too much white material?
        if (pWhite & 0xFE0C) //    if (Any w Q/R or > 1 w B/N or > 3 w P)
            return 0;        //       return 0;
        // Too much black material?
        if (pBlack & 0xFC00) //    else if (Any b Q/R or > 3 b B/N)
            return 0;        //       return 0;

        // Special case: Black has two knights (KNN)
        if (pBlack == 0x0220)    //    else if (Black only has 2 N)
        {                        //    {
            if (pWhite & 0xFFF0) //       if (KP*(B|N)KNN) return 200;
                return 200;
            if (pWhite == 0x0001) //       if (KPKNN) return 450;
                return 450;
            if (pWhite == 0x0002) //       if (KPPKNN) return 400;
                return 400;
            return 350; //       if (KPPPKNN) return 350;
        } //    }

        // Check if black has exactly one officer
        if ((pBlack & 0x0F00) != 0x0100) //    else if (Black does NOT have exactly 1 B/N)
            return 0;                    //       return 0;

        // White has one officer.
        if (pWhite & 0x0100) //    else if (White has 1 B/N)
            return (pWhite & 0x0002) ? 0 : -100;

        // White has no officers.
        if ((pWhite & 0x0002) == 0) //    else if (KPKN or KPKB) return 175;
            return 175;
        if ((pWhite & 0x0001) == 0) //    else if (KPPKN or KPPKB) return 125;
            return 125;
        return 0; //    return 0;
    } // }

    // Check if only black has pawns
    if (!(pWhite & 0x000F) && (pBlack & 0x000F)) // else if (Only black has pawns)
    {                                            // {
        // Too much black material?
        if (pBlack & 0xFE0C) //    if (Any b Q/R or > 1 b B/N or > 3 b P)
            return 0;        //       return 0;
        // Too much white material?
        if (pWhite & 0xFC00) //    else if (Any w Q/R or > 3 w B/N)
            return 0;        //       return 0;

        // Special case: White has two knights (KNN)
        if (pWhite == 0x0220)    //    else if (White only has 2 N)
        {                        //    {
            if (pBlack & 0xFFF0) //       if (KNNKP*(B|N)) return -200;
                return -200;
            if (pBlack == 0x0001) //       if (KNNKP) return -450;
                return -450;
            if (pBlack == 0x0002) //       if (KNNKPP) return -400;
                return -400;
            return -350; //       if (KNNKPPP) return -350;
        } //    }

        // Check if white has exactly one officer
        if ((pWhite & 0x0F00) != 0x0100) //    else if (White does NOT have exactly 1 B/N)
            return 0;                    //       return 0;

        // Black has one officer (mirror of the symmetric white branch above).
        if (pBlack & 0x0100) //    else if (Black has 1 B/N)
            return (pBlack & 0x0002) ? 0 : 100;

        // Black has no officers (mirror of the symmetric white branch above).
        if ((pBlack & 0x0002) == 0) //    else if (KNKP or KBKP) return -175;
            return -175;
        if ((pBlack & 0x0001) == 0) //    else if (KNKPP or KBKPP) return -125;
            return -125;
        return 0; //    return 0;
    } // }

    // No pawns at all - check officers
    if ((pWhite & 0xFFF0) && (pBlack & 0xFFF0)) // else if (White has officers AND Black has officers")
    {                                           // {
        INT val = -N->pvSumEval / 2;            //    INT val = -N->pvSumEval/2;
        if (val < -75)                          //    if (val < -75) val = -75;
            val = -75;
        else if (val > 75) //    else if (val > 75) val = 75;
            val = 75;
        return val; //    return val;
    } // }

    // Only white has officers
    if ((pWhite & 0xFFF0) && !(pBlack & 0xFFF0)) // else if (Only White has officers)
    {                                            // {
        if (pWhite == 0x0220)                    //    if ("KNNK")
            return 30 - N->pvSumEval;            //       return 30 - N->pvSumEval;
        else                                     //    else
            return 200;                          //       return 200;
    } // }

    // Only black has officers
    if (!(pWhite & 0xFFF0) && (pBlack & 0xFFF0)) // else if (Only Black has officers)
    {                                            // {
        if (pBlack == 0x0220)                    //    if ("KNNK")
            return -30 - N->pvSumEval;           //       return -30 - N->pvSumEval;
        else                                     //    else
            return -200;                         //       return -200;
    } // }

    return 0;
} /* EvalEndGame */

/*-------------------------------------- The Rule of the Square ----------------------------------*/
// Evaluates the rule of the square by scanning N->PassSqW[] and N->PassSqB[] and returns an
// endgame modification value if necessary.

static INT EvalRuleOfSquare(ENGINE *E)
{
    NODE  *N    = E->S.currNode;
    SQUARE wksq = E->B.PieceLoc[white];
    SQUARE bksq = E->B.PieceLoc[black];
    BYTE  *RTab = E->Global->E.RuleOfSquareTab; // addi RTab, rGlobal,GLOBAL.E.RuleOfSquareTab

    // --- Check WHITE Passed Pawns ---
    SQUARE sqMax = h1 - 0x10;
    BYTE  *Psq   = N->PassSqW - 1;
    SQUARE sq;

    while ((sq = *(++Psq)) != 0)
    {
        // Check king block/support of prom. sq.
        CHAR diff = (CHAR)RTab[sq - wksq + 0x10];

        if (diff < 0)
        {
            sq -= 0x10;
            diff++;
        }

        if (N->player == black)
            sq -= 0x10;

        if (sq > sqMax && ((diff == 0 && wksq >= 0x60) || sq >= (CHAR)RTab[sq - bksq]))
        {
            sqMax = sq | 0x07;
        }
    }

    // --- Check BLACK Passed Pawns ---
    SQUARE sqMin = a8 + 0x10;
    Psq          = N->PassSqB - 1;

    while ((sq = *(++Psq)) != 0)
    {
        // Check king block/support of prom. sq.
        CHAR diff = (CHAR)RTab[sq - bksq + 0x10];

        if (diff < 0)
        {
            sq += 0x10;
            diff++;
        }

        if (N->player == white)
            sq += 0x10;

        INT rankSq = (sq & 0x70); // Calculate 7-rank for comparison
        INT rank7  = 0x70 - rankSq;

        if (sq < sqMin && ((diff == 0 && bksq <= h2) || rank7 >= (CHAR)RTab[wksq - sq]))
        {
            sqMin = sq & 0x70;
        }
    }

    // --- Evaluate/Compare WHITE & BLACK
    INT rankW = sqMax & 0xFFF0;
    INT rankB = sqMin - 0x70;
    INT diff  = rankW + rankB;

    if (diff == 0)
        return 0;

    if (diff > 0)
    {
        if (rankW > rankB + 0x10 || N->player == black)
            return 500 + rankW;
        else
            return 0;
    }
    else
    {
        if (rankB > rankW + 0x10 || N->player == white)
            return -500 + rankB;
        else
            return 0;
    }
} /* EvalRuleOfSquare */

/*----------------------------------------- KPK Evaluation ---------------------------------------*/
// The KPK data base (size: 192 Kbit = 24 Kb). Is indexed by an 18 bit position descriptor (white
// pawn on the a-d files):
//
//    • Bit 17..15 : Rank ([1..6]) of white pawn.
//    • Bit 14..13 : File ([a..d] = [0..3]) of white pawn.
//    • Bit 12..10 : Rank of white king.
//    • Bit 09..07 : File of white king.
//    • Bit 06..04 : Rank of black king.
//    • Bit 12..10 : Rank of white king.
//    • Bit 03..01 : File of black king.
//    • Bit 00     : Side to move (0 = white; 1 = black).
//
// Bits 17..3 is used to index a byte in "kpkData", the remaining 3 bits is a bit index in the
// byte.
// Evaluates the KPK end game. On entry: rTmp1 indicates the colour of the pawn. The
// evaluation is stored returned in rTmp1.

static INT EvalKPK(ENGINE *E, COLOUR pawnColor)
{
    NODE  *N = E->S.currNode;
    SQUARE psq, ksq, ksq_;

    // Get piece locations based on pawn color
    if (pawnColor == white)
    {
        psq = N->PassSqW[0];

        // Check for double pawn move (pawn on 2nd rank that moved to 4th)
        if (E->B.Board[psq] != wPawn)
            psq -= 0x10;

        ksq  = E->B.PieceLoc[white];
        ksq_ = E->B.PieceLoc[black];
    }
    else
    {
        psq = N->PassSqB[0];

        // Check for double pawn move
        if (E->B.Board[psq] != bPawn)
            psq += 0x10;

        // Flip board for black pawns (normalize to white pawn)
        ksq  = E->B.PieceLoc[black] ^ 0x70;
        ksq_ = E->B.PieceLoc[white] ^ 0x70;
        psq ^= 0x70;
    }

    // Normalize file (mirror if pawn on e-h files)
    INT file = psq & 0x07;
    if (file > 3)
    {
        psq ^= 0x07;
        ksq ^= 0x07;
        ksq_ ^= 0x07;
    }

    // Calculate initial evaluation based on pawn advancement and king closeness
    INT rank      = (psq & 0x70) >> 2;
    INT closeness = E->Global->V.Closeness[psq + 0x10 - ksq_];
    INT val       = rank - (closeness / 2);

    // Pack position into 18-bit descriptor for tablebase lookup
    LONG n = 0; // 18-bit descriptor: must not be truncated to 16 bits.

    // Bits 17..15: Pawn rank (1..6)
    // Bits 14..13: Pawn file (a..d = 0..3)
    INT pawnRank = (psq & 0x70) >> 4;
    INT pawnFile = psq & 0x07;
    n            = ((pawnRank - 1) << 2) | pawnFile;
    n <<= 6;

    // Bits 12..10: White king rank
    // Bits 09..07: White king file
    INT kingRank = (ksq & 0x70) >> 1;
    INT kingFile = ksq & 0x07;
    n |= kingRank | kingFile;
    n <<= 6;

    // Bits 06..04: Black king rank
    // Bits 03..01: Black king file
    INT king2Rank = (ksq_ & 0x70) >> 1;
    INT king2File = ksq_ & 0x07;
    n |= king2Rank | king2File;
    n <<= 1;

    // Bit 0: Side to move (0=white, 1=black)
    if (N->player != pawnColor)
        n |= 1;

    // Lookup in KPK tablebase
    LONG byteIndex = n >> 3;
    INT  bitIndex  = n & 0x07;
    BYTE tableByte = E->Global->E.kpkData[byteIndex];

    if (tableByte & (1 << bitIndex))
        val += 600;

    // Negate for black pawns
    if (pawnColor == black)
        val = -val;

    // Adjust for current piece values and pawn structure
    val -= N->pvSumEval;
    val -= N->pawnStructEval;

    return val;
} /* EvalKPK */

/*------------------------------------ Opposite Coloured Bishops ---------------------------------*/
// Punishes the leading side if we have an opposite coloured bishop ending.

static INT OppColBishops(ENGINE *E)
{
    NODE *N = E->S.currNode;

    // Find first white bishop square
    SQUARE wsq;
    for (INT i = 1; (wsq = E->B.PieceLocW[i]) < a1; i++)
        ;

    // Find first black bishop square
    SQUARE bsq;
    for (INT i = 1; (bsq = E->B.PieceLocB[i]) < a1; i++)
        ;

    // Check if bishops are on opposite colors
    if (((wsq ^ bsq ^ (wsq >> 4) ^ (bsq >> 4)) & 0x01) == 0)
        return 0;
    else
        return -N->pvSumEval / 2;
} /* OppColBishops */

/**************************************************************************************************/
/*                                                                                                */
/*                                      START UP INITIALIZATION                                   */
/*                                                                                                */
/**************************************************************************************************/

void InitEvaluateModule(GLOBAL *Global, PTR kpkData)
{
    EVAL_COMMON *E = &(Global->E);

    //--- Initialize bit tables ---

    for (INT i = 0; i <= 255; i++)
    {
        E->BlkPawnsN[i] = ~((i << 1) | (i >> 1)); // Negated!
        E->IsoPawns[i]  = i & ~((i << 1) | (i >> 1));
    }

    //--- Initialize "Rule of Square" table ---

    BYTE *R = E->RuleOfSquareTab;
    R[0]    = 0x70;

    for (INT f = 1; f <= 7; f++)
    {
        for (INT r = 1; r <= 7; r++)
        {
            R[f + (r << 4)] = 1;
            R[f - (r << 4)] = (r >= f ? 0x70 : (8 - f) << 4);
            R[f + 0]        = R[f - 0x10];
        }

        SQUARE sq = f << 4;
        R[-sq]    = 0x70;

        if (sq > 0x30)
            R[sq] = -2;
        else
            R[sq] = -1, R[left(sq)] = R[right(sq)] = 0;

        for (INT r = -0x70; r <= 0x70; r += 0x10)
            R[r - f] = R[r + f];
    }

    //--- Initialize "KPK" database ---

    if (kpkData)
    {
        BYTE *kpkBytes = (BYTE *)kpkData;
        for (LONG i = 0; i < kpkDataSize; i++)
            E->kpkData[i] = kpkBytes[i];
    }
    else
    {
        for (LONG i = 0; i < kpkDataSize; i++)
            E->kpkData[i] = 0;
    }
} /* InitEvaluateModule */
