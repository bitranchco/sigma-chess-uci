/**************************************************************************************************/
/*                                                                                                */
/* Module  : MOVEGEN.C                                                                            */
/* Purpose : This module contains all the move generation routines. Move generation is always     */
/*           performed implicitly in the current search node ("rNode").                           */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"

#include <stdio.h>
#include <stdlib.h>

#include "MoveGen.f"
#include "PerformMove.f"
#include "Search.f"
#include "Engine.f"
#include "Threats.f"

//### NB!!! Use Bucket Sort for normal non-captures (perhaps just 2 or 3 buckets). E.g. introduce a
// a semi-sacrifice buffer for poor positional moves (if PV[to] < PV[from])

// The move generation routines all expect register "rNode" to contain a pointer to the current
// node "N". Sacrifice moves are stored in the sacrifice buffer "SBuf". Non-sacrifices are stored
// in "N->m" and are processed immediately by ProcessMove(E). The move generation routines sets
// the "from", "to", "piece", "cap", "type", "dir" and "misc" (gen) fields of "N->m" - the "dply"
// field must be set by the calling search routines (with the exception of "SearchEscapes()" which
// sets "dply" itself). The move generation is divided into 12 phases (A-L):
//
// [A] SearchEnPriseCaptures()  Generates "en prise captures", i.e. captures of undefended or
//                              higher valued pieces. Queen promotions which are also captures
//                              are searched too (the under promotions are added to "SBuf").
//
// [B] SearchPromotions()       Generates non-capturing queen promotions. Again the under promo-
//                              tions are added to "SBuf".
//
// [C] SearchRecaptures()       Searches safe recaptures or en passant captures. In the former
//                              case non-safe recaptures are added to the sacrifice buffer and
//                              the square on which the captures are made is stored in the field
//                              "N->recapSq". If the last move was not a capture, "N->recapSq" is
//                              set to "nullSq" and en passsant captures are searched.
//
// [D] SearchSafeCaptures()     Searches safe captures which are not recaptures or en prise cap-
//                              tures. Non-safe captures are added to the sacrifice buffer.
//
// [E] SearchEscapes()          Searches escape moves, i.e. non-capturing moves of the highest
//                              valued piece which is threatened by a lower valued piece. Unsafe
//                              moves are added to the sacrifice buffer.
//
// [F] SearchKillers()          Searches the killer moves if they're pseudo-legal in the current
//                              position. The killer moves are defined as the best phase H or I
//                              moves so far in a previous line at the same depth.
//
// [G] SearchCastling()         Searches king-side and queen-side castling in that order.
//
// [H] SearchNonCaptures()      Searches safe, normal non-capturing moves which are not escape
//                              moves. Unsafe moves are added to "SBuf".
//
// [I] SearchSacrifices()       Searches the moves which have been stored in the sacrifice buffer
//                              by the previous move generation phases.
//
// [J] SearchSafeChecks()       Searches safe checks. Is only used in place of phase F, G and H
//                              in the quiescence search.
//
// [K] SearchFarPawns()         Searches all non-capturing pawn moves to the 6th and 7th rank. Like
//                              phase J, this is only done in the quiescence search.
//
// [L] SearchCheckEvasion()     Special routine that is called when the side to move is in check
//                              in place of all other phases.
//
// The move generators are basically used in three different scenarious:
//
// (1) If the side to move is check, only phase L is performed.
// (2) In the normal full width search, phases A - I are performed.
// (3) In the quiescence search, phases A - E, J, K and optionally I are performed.

static void ProcessMove(ENGINE *E);
static void AddSacrifice(ENGINE *E);
static void SearchEnPriseCaptures1(ENGINE *E, SQUARE sq, SQUARE sq4, ATTACK asq);
static void SearchPromotion1(ENGINE *E);
static void SearchEnPassant(ENGINE *E);
static void SearchSafeCaptures1(ENGINE *E, SQUARE sq, SQUARE sq4, ATTACK attp, ATTACK atto);
static void SearchSafeCapturesQRB(ENGINE *E, SQUARE dest, ATTACK bits, PIECE ptype, INT pieceMtrl, INT maxMtrl);
static void SearchNonCaptures1(ENGINE *E, SQUARE sq);
static void SearchKing(ENGINE *E, SQUARE sq, SQUARE sq2);
static void SearchQueen(ENGINE *E, SQUARE sq, SQUARE sq2);
static void SearchRook(ENGINE *E, SQUARE sq, SQUARE sq2);
static void SearchBishop(ENGINE *E, SQUARE sq, SQUARE sq2);
static void SearchKnight(ENGINE *E, SQUARE sq, SQUARE sq2);
static void SearchPawn(ENGINE *E, SQUARE sq, SQUARE sq2);
static void SearchCheckQRB(ENGINE *E, SQUARE ksq);
static void SearchCheckQRB1(ENGINE *E, SQUARE to, ATTACK ato, ATTACK bits);
static void SearchIndCheck(ENGINE *E, PIECE piece, SQUARE from, INT idir);
static void SearchCheckN(ENGINE *E, SQUARE ksq);
static void SearchCheckN1(ENGINE *E, SQUARE to, ATTACK asq, ATTACK bits);
static void SearchCheckP(ENGINE *E, SQUARE ksq);

/**************************************************************************************************/
/*                                                                                                */
/*                                     GENERATING ROOT MOVES                                      */
/*                                                                                                */
/**************************************************************************************************/

// Before the search can start, the GenRootMoves() routine must be called. This generates all
// strictly legal move for the "player" and stores these in the E->S.RootMoves[] table. Before
// this routine can be called, the E->S.RootNode pointer must have been initialized.

void GenRootMoves(ENGINE *E)
{
    NODE *N = E->S.rootNode;

    E->R.state = state_Root; // Instruct "ProcessMove(E)" to generate root moves.

    E->S.numRootMoves = 0;         // Clear RootMoves[] table.
    E->S.bufTop       = E->S.SBuf; // Reset sacrifice buffer.

    AnalyzeThreats(E); // Compute escapeSq, ALoc/SLoc etc.

    if (N->check)
    {
        N->m.dply = 1; // CHECK EVASION.
        SearchCheckEvasion(E);
    }
    else
    {
        N->m.dply = 0;            // [0] FORCED MOVES (dply = 0):
        SearchEnPriseCaptures(E); // Generate en prise captures, queen
        SearchPromotions(E);      // promotions and safe recaptures.
        SearchRecaptures(E);

        N->m.dply = 1;         // [1] NON-FORCED MOVES (dply = 1):
        SearchSafeCaptures(E); // Generate normal moves and (forced)
        N->eply = 0;
        SearchEscapes(E); // escapes.
        N->m.dply = 1;
        SearchCastling(E); // Generate castling if not in check.

        N->m.dply = 2;        // [2] GENERATE QUIET MOVES (dply = 2):
        SearchNonCaptures(E); // Generate non-captures.
        SearchSacrifices(E);  // Generate sacrifice moves.
    }

    E->S.bufTop = E->S.SBuf; // Reset the sacrifice buffer again!
    clrMove(N->m);
} /* GenRootMoves */

static void GenOneRootMove(register ENGINE *E);

static void GenOneRootMove(register ENGINE *E) // Stores the generated move in RootMoves[] if
{                                              // the move is strictly legal.
    SEARCH_STATE *S = &E->S;
    NODE         *N = E->S.rootNode;

    PerformMove(E);

    if (!N->Attack_[N->PieceLoc[0]]) // If move is strictly legal
    {
        S->RootMoves[S->numRootMoves]      = S->rootNode->m;   // then add it to the root moves
        S->RootMoves[S->numRootMoves].misc = S->rootNode->gen; // list and store move generator
        S->numRootMoves++;
    }

    RetractMove(E);
} /* GenOneRootMove */

/**************************************************************************************************/
/*                                                                                                */
/*                                     PROCESS GENERATED MOVES                                    */
/*                                                                                                */
/**************************************************************************************************/

// The ProcessMove(E) routine is called by the move generators for each generated move. Normally
// ProcessMove(E) searches the move in question. When the search is to be terminated (i.e. when
// E->R.state = state_Stopping is set), the routine simply does nothing. At the root node, before
// the search begins, ProcessMove(E) is used to generate all the strictly legal root moves (in the
// E->S.RootMoves[] table).

static void ProcessMove(ENGINE *E)
{
    NODE *N = E->S.currNode;

    // Build with -DSIGMA_GEN_BUG_GUARD to filter malformed generator moves;
    // set SIGMA_GEN_BUG_NOISY=1 to log them.
#ifdef SIGMA_GEN_BUG_GUARD
    {
        BOOL fromOn  = (N->m.from >= 0 && N->m.from < boardSize && onBoard(N->m.from));
        BOOL toOn    = (N->m.to >= 0 && N->m.to < boardSize && onBoard(N->m.to));
        BOOL pieceOk = fromOn && (E->B.Board[N->m.from] == N->m.piece);
        if (!fromOn || !toOn || !pieceOk)
        {
            static int s_noisyChecked = 0;
            static int s_noisy        = 0;
            if (!s_noisyChecked)
            {
                const char *env = getenv("SIGMA_GEN_BUG_NOISY");
                s_noisy         = (env && env[0] && env[0] != '0');
                s_noisyChecked  = 1;
            }
            if (s_noisy)
            {
                PIECE actual = (fromOn ? E->B.Board[N->m.from] : (PIECE)-1);
                fprintf(stderr,
                        "GEN-BUG ply=%d misc=0x%x from=0x%02x to=0x%02x "
                        "piece=%d boardAtFrom=%d fromOn=%d toOn=%d player=%d\n",
                        (int)N->ply, (unsigned)N->m.misc, (unsigned)(N->m.from & 0xFF), (unsigned)(N->m.to & 0xFF),
                        (int)N->m.piece, (int)actual, (int)fromOn, (int)toOn, (int)N->player);
            }
            return;
        }
    }
#endif

    switch (E->R.state)
    {
    case state_Running:
        // Once the score reaches beta no further move is searched (see CUTOFF_CHECK).
        if (N->cutoff || N->score >= N->beta)
        {
            E->S.cutoffSkips++;
            return;
        }
        E->S.searchMoves++;
        SearchMove(E);
        break;
    case state_Root:
        GenOneRootMove(E);
        break;
    default:
        return;
    }
} /* ProcessMove */

/**************************************************************************************************/
/*                                                                                                */
/*                                      [A] EN PRISE CAPTURES                                     */
/*                                                                                                */
/**************************************************************************************************/

// En Prise captures are generated by scanning the opponent PieceLoc table from top to bottom
// (i.e. Queens first, followed by rooks e.t.c.). If an opponent piece is en prise (i.e. attacked
// by a lower valued piece and/or undefended), then capture moves are generated/searched.

/*---------------------------------- Search All En Prise Captures --------------------------------*/

void SearchEnPriseCaptures(ENGINE *E) // Generates and searches all en prise captures, i.e.
{                                     // captures of undefended and/or higher valued pieces.
    NODE *N = E->S.currNode;

    N->gen    = gen_A;
    N->m.type = mtype_Normal;

    for (INT i = N->lastPiece_; i >= 1; i--)
    {
        SQUARE sq = N->PieceLoc_[i];

        if (sq >= a1 && N->Attack[sq])
        {
            SearchEnPriseCaptures1(E, sq, sq * 4, N->Attack[sq]);
        }
    }
} /* SearchEnPriseCaptures */

/*---------------------------- Search En Prise Captures of 1 Piece -------------------------------*/
// Searches en prise captures of one piece on the square "sq". On entry:
//    sq : capture square
//    sq4 : unused (kept for compatibility)
//    asq : AttackP[sq]

// Helper for QRB en prise captures
static void SearchEnPriseQRB(ENGINE *E, SQUARE dest, ATTACK bits, PIECE ptype)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    if (!bits)
        return;

    N->m.piece     = N->player + ptype;
    QRB_DATA *Data = G->M.QRBdata;

    while (bits)
    {
        INT idx  = bits & 0xFF;
        N->m.dir = Data[idx].dir;

        SQUARE from = dest;
        do
        {
            from -= N->m.dir;
        } while (E->B.Board[from] == empty);

        if (onBoard(from))
        {
            N->m.from = from;
            ProcessMove(E);
        }

        bits = Data[idx].offset;
    }
}

static void SearchEnPriseCaptures1(ENGINE *E, SQUARE sq, SQUARE sq4, ATTACK asq)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    ATTACK d    = N->Attack_[sq];
    ATTACK a    = asq;
    SQUARE dest = sq;

    N->m.to    = dest;
    N->m.cap   = E->B.Board[sq];
    PIECE pcap = pieceType(N->m.cap);

    //--- CAPTURE WITH PAWNS (INCL PROMOTIONS) ---
    BOOL isRank8 = (dest + N->pawnDir) & 0x88;

    // Pawn capture direction reversal. Convention: pMaskL on dest means attacker
    // sat on dest's left-forward diagonal source (white: dest-0x0F, black: dest+0x11).
    // capDirL = pawnDir - 1 yields +0x0F for white, -0x11 for black; capDirR = +0x11/-0x0F.
    INT capDirL = N->pawnDir - 1; // +0x0F (white) or -0x11 (black)
    INT capDirR = N->pawnDir + 1; // +0x11 (white) or -0x0F (black)

    if (isRank8)
    {
        if (a & pMaskL)
        {
            N->m.piece = N->player + pawn;
            N->m.from  = dest - capDirL;
            SearchPromotion1(E);
        }

        if (a & pMaskR)
        {
            N->m.piece = N->player + pawn;
            N->m.from  = dest - capDirR;
            SearchPromotion1(E);
        }
    }
    else if (d == 0 || pcap > pawn)
    {
        if (a & pMaskL)
        {
            N->m.piece = N->player + pawn;
            N->m.from  = dest - capDirL;
            ProcessMove(E);
        }

        if (a & pMaskR)
        {
            N->m.piece = N->player + pawn;
            N->m.from  = dest - capDirR;
            ProcessMove(E);
        }
    }

    //--- CAPTURE WITH KNIGHTS ---
    if (d && pcap <= bishop)
        return;

    ATTACK nBits = (a & nMask) >> 16;
    if (nBits)
    {
        N->m.piece   = N->player + knight;
        N_DATA *NDir = G->M.Ndata;

        while (nBits)
        {
            INT idx   = nBits & 0xFF;
            INT j     = G->A.LowBit[idx]; //       Get lowest bit index
            N->m.from = dest - NDir[idx].dir;
            ProcessMove(E);
            nBits &= ~(1 << j);
        }
    }

    //--- CAPTURE WITH BISHOPS ---
    ATTACK bBits = (a & bMask) >> 8;
    SearchEnPriseQRB(E, dest, bBits, bishop);

    //--- CAPTURE WITH ROOKS ---
    if (d && pcap <= rook)
        return;

    ATTACK rBits = (a & rMask) >> 8;
    SearchEnPriseQRB(E, dest, rBits, rook);

    //--- CAPTURE WITH QUEENS ---
    if (d)
        return;

    ATTACK qBits = (a & qMask);
    SearchEnPriseQRB(E, dest, qBits, queen);

    //--- CAPTURE WITH KING ---
    if (a & kMask)
    {
        N->m.piece = N->player + king;
        N->m.from  = N->PieceLoc[0];
        ProcessMove(E);
    }
} /* SearchEnPriseCaptures1 */

/**************************************************************************************************/
/*                                                                                                */
/*                                         [B] PROMOTIONS                                         */
/*                                                                                                */
/**************************************************************************************************/

/*----------------------------- Search All Non-capturing Promotions ------------------------------*/
// The "SearchPromotions()" routine generates and searches all non-capturing promotions. Queen
// promotions are searched immediately, whereas under-promotions are added to the sacrifice
// buffer.

void SearchPromotions(ENGINE *E)
{
    NODE *N = E->S.currNode;

    N->gen = gen_B;

    // Get pawn structure bits for promotion rank
    BYTE   bits;
    SQUARE rankBase;

    if (N->player == white)
    {
        bits     = E->B.PawnStructW[6];
        rankBase = 0x70; // White pawns promote from rank 7 to rank 8
    }
    else
    {
        bits     = E->B.PawnStructB[1];
        rankBase = 0x00; // Black pawns promote from rank 2 to rank 1
    }

    if (bits == 0)
        return;

    // For each pawn on the promotion rank
    for (INT f = 0; f < 8; f++)
    {
        if (!(bits & (1 << f)))
            continue;

        SQUARE to = rankBase + f;

        if (E->B.Board[to] != empty)
            continue;

        N->m.from  = to - N->pawnDir;
        N->m.to    = to;
        N->m.piece = N->player + pawn;
        N->m.cap   = empty;

        SearchPromotion1(E);
    }
} /* SearchPromotions */

/*--------------------------------- Search One Promotion Move ------------------------------------*/
// "Turns" the current promotion move N->m into a queen promotion and searches that move. Under
// promotions are stored in the sacrifice buffer. On exit, N->m.type is restored to mtype_Normal.

static void SearchPromotion1(ENGINE *E)
{
    NODE *N = E->S.currNode;

    // Process queen promotion immediately
    N->m.type = N->player + queen;
    ProcessMove(E);

    N->m.type = mtype_Normal;

    // Add under-promotions to sacrifice buffer
    MOVE *buf = E->S.bufTop;

    *buf      = N->m;
    buf->type = N->player + rook;
    buf++;

    *buf      = N->m;
    buf->type = N->player + knight;
    buf++;

    *buf      = N->m;
    buf->type = N->player + bishop;
    buf++;

    E->S.bufTop = buf;
} /* SearchPromotion1 */

/**************************************************************************************************/
/*                                                                                                */
/*                                          [C] RECAPTURES                                        */
/*                                                                                                */
/**************************************************************************************************/

// If the previous move was a capture on a square "sq", then safe captures (which are not also en
// prise captures) of the piece on "sq" are searched and N->recapSq is set to "sq". Otherwise
// N->recapSq is set to "nullSq" and en passant moves (if any) are searched.

void SearchRecaptures(ENGINE *E)
{
    NODE *N        = E->S.currNode;
    NODE *PrevNode = N - 1; // Previous node for pm

    N->gen = gen_C;

    if (PrevNode->m.cap)
    {
        N->recapSq = PrevNode->m.to;

        if (N->Attack[N->recapSq] && N->Attack_[N->recapSq])
        {
            N->m.type = mtype_Normal;
            SearchSafeCaptures1(E, N->recapSq, N->recapSq * 4, N->Attack[N->recapSq], N->Attack_[N->recapSq]);
        }
    }
    else
    {
        N->recapSq = nullSq;
        SearchEnPassant(E);
    }
} /* SearchRecaptures */

/*-------------------------------------- Search En Passant ---------------------------------------*/
// Searches en passant captures. Need only be called if the previous move was not a capture.

static void SearchEnPassant(ENGINE *E)
{
    NODE *N        = E->S.currNode;
    NODE *PrevNode = N - 1; // Previous node for pm

    // Check if previous move was enemy pawn double advance
    PIECE pmPiece = pieceType(PrevNode->m.piece);
    if (pmPiece != pawn)
        return;

    SQUARE pmFrom   = PrevNode->m.from;
    SQUARE pmTo     = PrevNode->m.to;
    INT    pawnDir2 = N->pawnDir + N->pawnDir;

    if (pmFrom - pmTo != pawnDir2)
        return;

    PIECE myPawn = N->player + pawn;

    // Check left square
    SQUARE leftSq = pmTo - 1;
    if (E->B.Board[leftSq] == myPawn)
    {
        N->m.from  = leftSq;
        N->m.to    = pmTo + N->pawnDir;
        N->m.piece = myPawn;
        N->m.cap   = empty;
        N->m.type  = mtype_EP;
        ProcessMove(E);
    }

    // Check right square
    SQUARE rightSq = pmTo + 1;
    if (E->B.Board[rightSq] == myPawn)
    {
        N->m.from  = rightSq;
        N->m.to    = pmTo + N->pawnDir;
        N->m.piece = myPawn;
        N->m.cap   = empty;
        N->m.type  = mtype_EP;
        ProcessMove(E);
    }
} /* SearchEnPassant */

/**************************************************************************************************/
/*                                                                                                */
/*                                         [D] SAFE CAPTURES                                      */
/*                                                                                                */
/**************************************************************************************************/

/*------------------------------------- Search all Safe Captures ---------------------------------*/
// Searches safe captures which are neither recaptures or en prise captures. The former case is
// handled by not searching captures of the piece (if any) on N->recapSq. The latter case is
// checked individually for each potential capture. Also, sacrifice captures are stored in the
// sacrifice buffer.

void SearchSafeCaptures(ENGINE *E)
{
    NODE *N = E->S.currNode;

    N->gen    = gen_D;
    N->m.type = mtype_Normal;

    for (INT i = N->lastPiece_; i >= 1; i--)
    {
        SQUARE sq = N->PieceLoc_[i];

        if (sq >= a1 && sq != N->recapSq && N->Attack[sq] && N->Attack_[sq])
        {
            SearchSafeCaptures1(E, sq, sq * 4, N->Attack[sq], N->Attack_[sq]);
        }
    }
} /* SearchSafeCaptures */

/*--------------------------------- Search Safe Captures of 1 Piece ------------------------------*/
// Searches all safe captures of one piece on the specified square. It is assumed that m.type =
// mtype_Normal.
// On entry:
//    rTmp1 : sq
//    rTmp2 : 4*sq
//    rTmp3 : Attack[sq] (<> 0)
//    rTmp4 : Attack_[sq] (<> 0)

static void SearchSafeCaptures1(ENGINE *E, SQUARE sq, SQUARE sq4, ATTACK attp, ATTACK atto)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    N->m.to  = sq;
    N->m.cap = E->B.Board[sq];

    ATTACK ap = attp;
    ATTACK d  = atto;

    INT capMtrl = G->B.Mtrl[N->m.cap];

    // Calculate maxMtrl = capMtrl + SmattMtrl(d)
    INT smattMtrl;
    if (d & pMask)
        smattMtrl = pawnMtrl;
    else if (d & nMask)
        smattMtrl = knightMtrl;
    else if (d & bMask)
        smattMtrl = bishopMtrl;
    else if (d & rMask)
        smattMtrl = rookMtrl;
    else
        smattMtrl = queenMtrl;

    INT maxMtrl = capMtrl + smattMtrl;

    switch (capMtrl)
    {
    case pawnMtrl: {
        // Pawn capture directions depend on colour:
        //   White: pMaskL set at pawn+0x0F, pMaskR at pawn+0x11 (pawnDir=+0x10)
        //   Black: pMaskL set at pawn-0x11, pMaskR at pawn-0x0F (pawnDir=-0x10)
        // To reverse: from = sq - capDir.
        INT capDirL = N->pawnDir - 1; // +0x0F (white) or -0x11 (black)
        INT capDirR = N->pawnDir + 1; // +0x11 (white) or -0x0F (black)

        if (ap & pMaskL)
        {
            N->m.piece = N->player + pawn;
            N->m.from  = sq - capDirL;
            ProcessMove(E);
        }

        if (ap & pMaskR)
        {
            N->m.piece = N->player + pawn;
            N->m.from  = sq - capDirR;
            ProcessMove(E);
        }
        // Fall through to knight
    }

    case knightMtrl: {
        // Knight captures
        ATTACK nBits = (ap & nMask) >> 16;
        if (nBits)
        {
            N->m.piece   = N->player + knight;
            N_DATA *NDir = G->M.Ndata;

            while (nBits)
            {
                INT idx   = nBits & 0xFF;
                N->m.from = sq - NDir[idx].dir;

                // Bit count check for sacrifice vs safe
                INT    bitcount = 0;
                ATTACK temp     = ap;
                while (temp)
                {
                    bitcount += (temp & 1);
                    temp >>= 1;
                }

                if (capMtrl == knightMtrl || (maxMtrl >= knightMtrl && bitcount > 1))
                {
                    ProcessMove(E);
                }
                else
                {
                    AddSacrifice(E);
                }

                nBits = NDir[idx].offset;
            }
        }

        // Bishop captures (routes to AddSacrifice when capMtrl < bishopMtrl)
        {
            ATTACK bBits2 = (ap & bMask) >> 8;
            SearchSafeCapturesQRB(E, sq, bBits2, bishop, bishopMtrl, maxMtrl);
        }
        // Rook captures (routes to AddSacrifice when capMtrl < rookMtrl)
        {
            ATTACK rBits2 = (ap & rMask) >> 8;
            SearchSafeCapturesQRB(E, sq, rBits2, rook, rookMtrl, maxMtrl);
        }
        // Queen captures (routes to AddSacrifice when capMtrl < queenMtrl)
        {
            ATTACK qBits2 = (ap & qMask);
            SearchSafeCapturesQRB(E, sq, qBits2, queen, queenMtrl, maxMtrl);
        }
        break;
    }

    case rookMtrl: {
        ATTACK rBits = (ap & rMask) >> 8;
        SearchSafeCapturesQRB(E, sq, rBits, rook, rookMtrl, maxMtrl);
    }
        // fall through - queen captures of rook-value pieces
    case queenMtrl: {
        ATTACK qBits = (ap & qMask);
        SearchSafeCapturesQRB(E, sq, qBits, queen, queenMtrl, maxMtrl);
        break;
    }
    }
} /* SearchSafeCaptures1 */

// Helper for sliding piece captures in SearchSafeCaptures1
static void SearchSafeCapturesQRB(ENGINE *E, SQUARE dest, ATTACK bits, PIECE ptype, INT pieceMtrl, INT maxMtrl)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    if (!bits)
        return;

    N->m.piece     = N->player + ptype;
    QRB_DATA *Data = G->M.QRBdata;

    while (bits)
    {
        INT idx  = bits & 0xFF;
        N->m.dir = Data[idx].dir;

        SQUARE from = dest;
        do
        {
            from -= N->m.dir;
        } while (E->B.Board[from] == empty);

        if (!onBoard(from))
        {
            bits = Data[idx].offset;
            continue;
        }

        N->m.from = from;

        if (N->m.piece == E->B.Board[from])
        {
            if (maxMtrl < pieceMtrl)
            {
                AddSacrifice(E);
            }
            else
            {
                ProcessMove(E);
            }
        }

        bits = Data[idx].offset;
    }
}

/**************************************************************************************************/
/*                                                                                                */
/*                                         [E] ESCAPE MOVES                                       */
/*                                                                                                */
/**************************************************************************************************/

// Generates escape moves for the highest valued threatened piece. The "escapeSq" and "eply"
// fields must have been set by the calling routine (typically AnalyzeThreats).

void SearchEscapes(ENGINE *E)
{
    NODE *N = E->S.currNode;

    if (N->escapeSq == nullSq)
        return;

    N->gen    = gen_E;
    N->m.cap  = empty;
    N->m.type = mtype_Normal;
    N->m.dply = N->eply;

    SearchNonCaptures1(E, N->escapeSq);
} /* SearchEscapes */

/**************************************************************************************************/
/*                                                                                                */
/*                                         [F] KILLER MOVES                                       */
/*                                                                                                */
/**************************************************************************************************/

static void SearchKiller(ENGINE *E, MOVE *killer, INT gen);

// Searches the 2 killer moves if they are active and pseudo-legal.
// Only called at normal full width nodes (i.e. not quiescence or check):

void SearchKillers(ENGINE *E)
{
    NODE *N = E->S.currNode;

    if (N->killer1Active)
        SearchKiller(E, &N->killer1, gen_F1);

    if (N->killer2Active)
        SearchKiller(E, &N->killer2, gen_F2);

} /* SearchKillers */

// Searches a single killer move if it's pseudo-legal in the current position.

static void SearchKiller(ENGINE *E, MOVE *killer, INT gen)
{
    NODE *N = E->S.currNode;

    SQUARE from  = killer->from;
    SQUARE to    = killer->to;
    PIECE  piece = killer->piece;

    if (piece != E->B.Board[from])
        return;
    // Killer move's capture target must still match (the captured piece may
    // have been removed by an earlier move in the search tree). Matches the
    // upstream asm's second board-validation check.
    if (killer->cap != E->B.Board[to])
        return;

    N->gen = gen;

    // Verify move is pseudo-legal
    if (killer->type != mtype_Normal)
        goto SEARCH;

    { // Scope block to allow goto to cross variable initialization
        PIECE ptype = pieceType(piece);
        if (ptype == knight)
            goto SEARCH;

        // Verify path is clear for sliding pieces and pawns
        if (ptype < knight)
        {
            if (to != from + 2 * N->pawnDir)
                goto SEARCH;
            if (E->B.Board[from + N->pawnDir] != empty)
                return;
        }
        else if (ptype == king)
        {
            if (N->Attack_[to])
                return;
        }
        else
        {
            INT dir = killer->dir;
            for (SQUARE sq = from + dir; sq != to; sq += dir)
            {
                if (E->B.Board[sq] != empty)
                    return;
            }
        }
    }

SEARCH:
    // Copy all but dply: the killer is searched at the current phase's dply.
    N->m.piece = killer->piece;
    N->m.from  = killer->from;
    N->m.to    = killer->to;
    N->m.cap   = killer->cap;
    N->m.type  = killer->type;
    N->m.dir   = killer->dir;
    ProcessMove(E);
} /* SearchKiller */

/**************************************************************************************************/
/*                                                                                                */
/*                                         [G] CASTLING MOVES                                     */
/*                                                                                                */
/**************************************************************************************************/

void SearchCastling(ENGINE *E)
{
    NODE  *N          = E->S.currNode;
    PIECE *Board      = E->B.Board;
    PIECE *HasMovedTo = Board + boardSize2;

    N->gen   = gen_G;
    N->m.cap = empty;

    if (N->player == white)
    {
        // White castling
        if (Board[e1] != wKing || HasMovedTo[e1])
            return;

        // King-side castling (O-O)
        if (Board[h1] == wRook && (*(LONG *)&Board[f1] == 0) && !HasMovedTo[h1] && !N->Attack_[e1] && !N->Attack_[f1] &&
            !N->Attack_[g1])
        {
            N->m.from  = e1;
            N->m.to    = g1;
            N->m.piece = wKing;
            N->m.type  = mtype_O_O;
            ProcessMove(E);
        }

        // Queen-side castling (O-O-O)
        if (Board[a1] == wRook && Board[b1] == empty && (*(LONG *)&Board[c1] == 0) && !HasMovedTo[a1] &&
            !N->Attack_[e1] && !N->Attack_[d1] && !N->Attack_[c1])
        {
            N->m.from  = e1;
            N->m.to    = c1;
            N->m.piece = wKing;
            N->m.type  = mtype_O_O_O;
            ProcessMove(E);
        }
    }
    else
    {
        // Black castling
        if (Board[e8] != bKing || HasMovedTo[e8])
            return;

        // King-side castling (O-O)
        if (Board[h8] == bRook && (*(LONG *)&Board[f8] == 0) && !HasMovedTo[h8] && !N->Attack_[e8] && !N->Attack_[f8] &&
            !N->Attack_[g8])
        {
            N->m.from  = e8;
            N->m.to    = g8;
            N->m.piece = bKing;
            N->m.type  = mtype_O_O;
            ProcessMove(E);
        }

        // Queen-side castling (O-O-O)
        if (Board[a8] == bRook && Board[b8] == empty && (*(LONG *)&Board[c8] == 0) && !HasMovedTo[a8] &&
            !N->Attack_[e8] && !N->Attack_[d8] && !N->Attack_[c8])
        {
            N->m.from  = e8;
            N->m.to    = c8;
            N->m.piece = bKing;
            N->m.type  = mtype_O_O_O;
            ProcessMove(E);
        }
    }
} /* SearchCastling */

/**************************************************************************************************/
/*                                                                                                */
/*                                         [H] NON-CAPTURING MOVES                                */
/*                                                                                                */
/**************************************************************************************************/

// Generates normal, safe non-capturing moves (which are not escape moves). The moves are
// generated in the following order: ALoc[], SLoc[] and then the rest of the pieces.

void SearchNonCaptures(ENGINE *E)
{
    NODE *N = E->S.currNode;

    N->gen    = gen_H;
    N->m.cap  = empty;
    N->m.type = mtype_Normal;

    // Generate moves for attacked pieces first (ALoc)
    for (INT i = 0; N->ALoc[i] != nullSq; i++)
        SearchNonCaptures1(E, N->ALoc[i]);

    // Then safe pieces (SLoc)
    for (INT i = 0; N->SLoc[i] != nullSq; i++)
        SearchNonCaptures1(E, N->SLoc[i]);

    // Generate king non-capture moves (king is not in ALoc/SLoc)
    SearchNonCaptures1(E, N->PieceLoc[0]);

} /* SearchNonCaptures */

static void SearchNonCaptures1(ENGINE *E, SQUARE sq)
{
    NODE *N = E->S.currNode;

    N->m.from  = sq;
    N->m.piece = E->B.Board[sq];

    PIECE ptype = pieceType(N->m.piece);
    switch (ptype)
    {
    case pawn:
        SearchPawn(E, sq, sq * 2);
        break;

    case knight:
        SearchKnight(E, sq, sq * 2);
        break;

    case bishop:
        SearchBishop(E, sq, sq * 2);
        break;

    case rook:
        SearchRook(E, sq, sq * 2);
        break;

    case queen:
        SearchQueen(E, sq, sq * 2);
        break;

    case king:
        SearchKing(E, sq, sq * 2);
        break;
    }
} /* SearchNonCaptures1 */

/*------------------------------------------ King Moves ------------------------------------------*/
// Searches non capturing moves for the king on "From". On entry: rTmp1 = kingSq, rTmp2 = 2*kingSq,
// and all but the "to"-field of "N->m" must already be set.

static void SearchKing(ENGINE *E, SQUARE sq, SQUARE sq2)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    INT *KDir = G->B.KingDir;

    // Try all 8 king directions
    for (INT i = 0; i <= 7; i++)
    {
        INT    dir = KDir[i];
        SQUARE to  = sq + dir;

        if (offBoard(to))
            continue;
        if (E->B.Board[to] != empty)
            continue;
        if (N->Attack_[to])
            continue;

        N->m.to = to;
        ProcessMove(E);
    }
} /* SearchKing */

/*----------------------------------- Queen/Rook/Bishop Moves ------------------------------------*/
// Generates non capturing moves for the queen/rook/bishop on "sq". On entry: rTmp1 = sq,
// rTmp2 = 2*sq, and all but the "to"- and "dir"-fields of "N->m" must have been set.

// Helper for sliding pieces (queen, rook, bishop)
static void SearchSliding(ENGINE *E, SQUARE sq, INT dir, ATTACK xraybit, ATTACK piecebit, ATTACK smattMask)
{
    NODE *N = E->S.currNode;

    if (E->B.Board[sq + dir] != empty)
        return; // if (Board[m.from + dir] != empty) continue;

    N->m.dir     = dir;           // m.dir = dir;
    ATTACK aFrom = N->Attack[sq]; // aFrom = AttackP[src];
    SQUARE dst   = sq + dir;      // dst = src + dir;

    while (E->B.Board[dst] == empty) // while (Board[dst] == empty)
    {                                // {
        N->m.to  = dst;              //    m.to = dst;
        ATTACK d = N->Attack_[dst];  //    d = AttackO[dst];

        if ((d & smattMask) ||            //    if ((d & smattMask) ||
            (d &&                         //        (d &&
             !(aFrom & xraybit) &&        //         ! (aFrom & xraybit) &&
             N->Attack[dst] == piecebit)) //         bitcount(AttackP[dst]) < 1
                                          //        )
        {
            AddSacrifice(E); //       AddSacrifice(E);
        }
        else //    else
        {
            ProcessMove(E); //       ProcessMove(E);
        }

        dst += dir; //    dst += dir;
    } // }
}

static void SearchQueen(ENGINE *E, SQUARE sq, SQUARE sq2)
{
    NODE *N = E->S.currNode;

    ATTACK smattMask = 0x06FFFF00; // smattMask for queen (pawn + all pieces)

    // All 8 queen directions (diagonal + orthogonal)
    SearchSliding(E, sq, -0x0F, 0x0101, 0x0001, smattMask);
    SearchSliding(E, sq, -0x11, 0x0202, 0x0002, smattMask);
    SearchSliding(E, sq, +0x11, 0x0404, 0x0004, smattMask);
    SearchSliding(E, sq, +0x0F, 0x0808, 0x0008, smattMask);
    SearchSliding(E, sq, -0x10, 0x1010, 0x0010, smattMask);
    SearchSliding(E, sq, +0x10, 0x2020, 0x0020, smattMask);
    SearchSliding(E, sq, +0x01, 0x4040, 0x0040, smattMask);
    SearchSliding(E, sq, -0x01, 0x8080, 0x0080, smattMask);
} /* SearchQueen */

static void SearchRook(ENGINE *E, SQUARE sq, SQUARE sq2)
{
    NODE *N = E->S.currNode;

    ATTACK smattMask = 0x06FF0F00;

    // 4 rook directions (orthogonal only)
    SearchSliding(E, sq, -0x10, 0x1010, 0x1000, smattMask);
    SearchSliding(E, sq, +0x10, 0x2020, 0x2000, smattMask);
    SearchSliding(E, sq, +0x01, 0x4040, 0x4000, smattMask);
    SearchSliding(E, sq, -0x01, 0x8080, 0x8000, smattMask);
} /* SearchRook */

static void SearchBishop(ENGINE *E, SQUARE sq, SQUARE sq2)
{
    NODE *N = E->S.currNode;

    ATTACK smattMask = 0x06FFF000;

    // 4 bishop directions (diagonal only)
    SearchSliding(E, sq, -0x0F, 0x0101, 0x0100, smattMask);
    SearchSliding(E, sq, -0x11, 0x0202, 0x0200, smattMask);
    SearchSliding(E, sq, +0x11, 0x0404, 0x0400, smattMask);
    SearchSliding(E, sq, +0x0F, 0x0808, 0x0800, smattMask);
} /* SearchBishop */

/*------------------------------------------- Knight Moves ---------------------------------------*/
// Generates non capturing moves for the knight on "sq". On entry: rTmp1 = sq, rTmp2 = 2*sq,
// and all but the "to"-field of "N->m" must be set.

static void SearchKnight(ENGINE *E, SQUARE sq, SQUARE sq2)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    INT *NDir = G->B.KnightDir;
    // KnightBit[i] = 1 << (16+i); compare against the full ATTACK word.
    // (Old code used `nbit = 1` which can never match because knight bits
    // live at positions 16-23.)
    ATTACK nbit = 1u << 16;

    // Try all 8 knight moves
    for (INT i = 0; i <= 7; i++)
    {
        INT    dir = NDir[i];
        SQUARE to  = sq + dir;

        if (E->B.Board[to] != empty)
            continue;
        N->m.to = to;

        ATTACK d = N->Attack_[to];

        if (d && ((d & pMask) || N->Attack[to] == nbit))
        {
            AddSacrifice(E);
        }
        else
        {
            ProcessMove(E);
        }

        nbit <<= 1;
    }
} /* SearchKnight */

/*------------------------------------------ Pawn Moves ------------------------------------------*/

static void SearchPawn(ENGINE *E, SQUARE sq, SQUARE sq2)
{
    NODE *N = E->S.currNode;

    SQUARE front = sq + N->pawnDir;

    if (E->B.Board[front] != empty)
        return;

    BOOL isRank7 = (N->player == white) ? (sq >= 0x60) : (sq < 0x20);
    if (isRank7)
        return;

    ATTACK xRay = N->Attack[sq];

    // Forward orthogonal attack-mask (matches upstream wForwardMask/bForwardMask).
    // Detects a friendly queen/rook attacking sq from directly behind the pawn,
    // i.e. one whose ray goes through the pawn in the forward direction.
    // (Old code used 0x0F0F/0xF0F0 = 8 bits including diagonals from ahead — too broad.)
    if (N->player == white)
    {
        xRay &= 0x2020; // QueenBit[5] | RookBit[5-4] = south-orthogonal forward (white)
    }
    else
    {
        xRay &= 0x1010; // QueenBit[4] | RookBit[4-4] = north-orthogonal forward (black)
    }

    // Check pawn double advance (only from starting rank: rank 1 for white, rank 6 for black)
    BOOL isRank2 = (N->player == white) ? (sq < 0x20) : (sq >= 0x60);
    if (isRank2)
    {
        SQUARE front2 = sq + 2 * N->pawnDir;
        if (E->B.Board[front2] == empty)
        {
            N->m.to = front2;

            if (!xRay && N->Attack_[front2] && !N->Attack[front2])
            {
                AddSacrifice(E);
            }
            else
            {
                ProcessMove(E);
            }
        }
    }

    // Single pawn advance
    N->m.to = front;

    if (!xRay && N->Attack_[front] && !N->Attack[front])
    {
        AddSacrifice(E);
    }
    else
    {
        BOOL isRank5Plus = (N->player == white) ? (sq >= 0x40) : (sq < 0x40);
        if (isRank5Plus)
        {
            N->m.dply = 0;
        }
        ProcessMove(E);
        N->m.dply = 2; // <-- Important to restore
    }
} /* SearchPawn */

/**************************************************************************************************/
/*                                                                                                */
/*                                          [I] SACRIFICES                                        */
/*                                                                                                */
/**************************************************************************************************/

/*---------------------------------- Search All Sacrifice Moves ----------------------------------*/
// Searches all moves of "SBuf" pertaining to the current node. N->m.dply must already have been
// set appropriately (it will not be changed).

void SearchSacrifices(ENGINE *E)
{
    NODE *N = E->S.currNode;

    N->gen = gen_I;

    for (MOVE *sm = N->bufStart; sm < E->S.bufTop; sm++)
    {
        N->m = *sm;
        ProcessMove(E);
    }
} /* SearchSacrifices */

/*--------------------------------- Add Move to Sacrifice Buffer ---------------------------------*/
// Adds the move "N->m" to "SBuf". ### SHOULD CHECK IF SACRIFICE BUFFER FULL. IF SO -> SEARCH MOVE
// DIRECTLY ANYWAY...

static void AddSacrifice(ENGINE *E)
{
    NODE *N = E->S.currNode;

    if (N->storeSacri)
    {
        *E->S.bufTop = N->m;
        E->S.bufTop++;
    }
} /* AddSacrifice */

/**************************************************************************************************/
/*                                                                                                */
/*                                          [J] SAFE CHECKS                                       */
/*                                                                                                */
/**************************************************************************************************/

// Helper for scanning one direction for QRB checks
static void ScanCheckDir(ENGINE *E, SQUARE ksq, INT dir, ATTACK dirMask, ATTACK rayBit)
{
    NODE *N = E->S.currNode;

    SQUARE to = ksq;

    // Scan along direction from king
    while (true)
    {
        to -= dir;
        if (offBoard(to))
            break;

        PIECE piece = E->B.Board[to];

        if (piece == empty)
        {
            ATTACK a = N->Attack[to];
            if (a)
            {
                ATTACK bits = a & dirMask;
                if (bits)
                {
                    SearchCheckQRB1(E, to, a, bits);
                }
            }
            continue;
        }

        // Hit a piece - check for indirect checks
        if (pieceColour(piece) == N->player && (N->Attack[to] & rayBit))
        {
            SearchIndCheck(E, piece, to, dir);
        }
        break;
    }
}

/*---------------------------------------- Search Safe Checks ------------------------------------*/
// Searches safe non-capturing checks (both direct and indirect check). Pawns to 7th or 8th rank are
// not searched.

void SearchSafeChecks(ENGINE *E)
{
    NODE *N = E->S.currNode;

    SQUARE ksq = N->PieceLoc_[0];

    N->gen    = gen_J;
    N->m.cap  = empty;
    N->m.type = mtype_Normal;

    // Set up context for check search helpers
    SearchCheckQRB(E, ksq);
    SearchCheckN(E, ksq);
    SearchCheckP(E, ksq);
} /* SearchSafeChecks */

/*---------------------- Search Queen/Rook/Bishop Checks (direct and indirect) -------------------*/

// For each of the 8 directions emanating from the opponent king, we check if the squares are
// attacked by sliding pieces. If so we generate direct QRB checks. If the direction emanating from
// the king ends on a square occupied by a friendly piece, we check for indirect/discovered checks.
//
// On entry: rLocal1, rLocal2 and rLocal3 must have been set by the calling routine.

static void SearchCheckQRB(ENGINE *E, SQUARE ksq)
{
    // Scan all 8 queen directions from opponent king
    ScanCheckDir(E, ksq, -0x0F, qbMask, 0x0101); // Diagonal up-left
    ScanCheckDir(E, ksq, -0x11, qbMask, 0x0202); // Diagonal up-right
    ScanCheckDir(E, ksq, +0x11, qbMask, 0x0404); // Diagonal down-right
    ScanCheckDir(E, ksq, +0x0F, qbMask, 0x0808); // Diagonal down-left
    ScanCheckDir(E, ksq, -0x10, qrMask, 0x1010); // Up
    ScanCheckDir(E, ksq, +0x10, qrMask, 0x2020); // Down
    ScanCheckDir(E, ksq, +0x01, qrMask, 0x4040); // Right
    ScanCheckDir(E, ksq, -0x01, qrMask, 0x8080); // Left
} /* SearchCheckQRB */

// The "SearchCheckQRB1()" routine searches all safe checks to a single empty square "to". On entry,
// rTmp1 contains the relevant QRB attackbits of the target square "to". Additionally, rLocal4 =
// &Attack[to], and rLocal5 = &Board[to]. These registers are NOT changed.

static void SearchCheckQRB1(ENGINE *E, SQUARE to, ATTACK ato, ATTACK bits)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    ATTACK d = N->Attack_[to];

    if (d & pMask)
        return;

    if (d & (nMask | bMask))
    {
        bits &= bMask;
        if (!bits)
            return;
    }
    else if (d & rMask)
    {
        bits &= (bMask | rMask);
        if (!bits)
            return;
    }

    N->m.to        = to;
    ATTACK    a    = ato;
    QRB_DATA *Data = G->M.QRBdata;

    while (bits)
    {
        INT idx = (bits | (bits >> 8)) & 0xFF;
        INT dir = Data[idx].dir;

        SQUARE from = to;
        do
        {
            from -= dir;
        } while (E->B.Board[from] == empty);

        if (!onBoard(from))
        {
            bits = Data[idx].offset;
            continue;
        }

        if (from != N->escapeSq && (d == 0 || (a & ~Data[idx].rayBit) || (N->Attack[from] & Data[idx].rayBit)))
        {
            N->m.from  = from;
            N->m.dir   = dir;
            N->m.piece = E->B.Board[from];
            ProcessMove(E);
        }

        bits = Data[idx].offset;
    }
} /* SearchCheckQRB1 */

static void SearchIndCheck(ENGINE *E, PIECE piece, SQUARE from, INT idir)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    SQUARE ksq = N->PieceLoc_[0];

    N->m.piece = piece;
    N->m.from  = from;

    PIECE ptype = pieceType(piece);

    switch (ptype)
    {
    case king: {
        INT *KDir = G->B.KingDir;
        for (INT i = 0; i <= 7; i++)
        {
            INT dir = KDir[i];
            if (dir == idir || dir == -idir)
                continue;

            SQUARE to = from + dir;
            if (E->B.Board[to] != empty)
                continue;
            if (N->Attack_[to])
                continue;

            N->m.to = to;
            ProcessMove(E);
        }
        return;
    }

    case knight: {
        INT *NDir = G->B.KnightDir;
        for (INT i = 0; i <= 7; i++)
        {
            INT    dir = NDir[i];
            SQUARE to  = from + dir;
            if (E->B.Board[to] != empty)
                continue;

            // Skip direct checks (already searched)
            if (G->A.AttackDir[to - ksq] & nDirMask)
                continue;

            N->m.to = to;
            ProcessMove(E);
        }
        return;
    }

    case pawn: {
        if (idir == N->pawnDir || idir == -N->pawnDir)
            return;

        SQUARE to = from + N->pawnDir;
        if (E->B.Board[to] != empty)
            return;

        // Skip promotions (searched elsewhere)
        if ((to + 2 * N->pawnDir) & 0x88)
            return;

        N->m.to = to;
        ProcessMove(E);

        // Double advance from rank 2
        if ((from - 2 * N->pawnDir) & 0x88)
        {
            to += N->pawnDir;
            if (E->B.Board[to] != empty)
                return;

            N->m.to = to;
            ProcessMove(E);
        }
        return;
    }

    case bishop:
    case rook: {
        INT   *Dir     = (ptype == bishop) ? G->B.BishopDir : G->B.RookDir;
        ATTACK dirMask = (ptype == bishop) ? bDirMask : rDirMask;

        for (INT i = 0; i <= 3; i++)
        {
            INT mdir = Dir[i];
            N->m.dir = mdir;

            SQUARE to = from;
            while (true)
            {
                to += mdir;
                if (E->B.Board[to] != empty)
                    break;

                // Check for discovered check
                INT fdir = G->A.AttackDir[ksq - to];
                if (fdir & dirMask)
                {
                    // Skip direct checks
                    fdir >>= 5;
                    SQUARE sq    = to;
                    PIECE  eKing = (N->player ^ 0x10) + king;
                    do
                    {
                        sq += fdir;
                    } while (E->B.Board[sq] == empty);

                    if (E->B.Board[sq] == eKing)
                        continue;
                }

                N->m.to = to;
                ProcessMove(E);
            }
        }
        return;
    }

    default:
        return;
    }
} /* SearchIndCheck */

/*--------------------------------- Search Safe Knight Checks ------------------------------------*/

static void SearchCheckN(ENGINE *E, SQUARE ksq)
{
    NODE *N = E->S.currNode;

    INT *NDir = E->Global->B.KnightDir;

    // Try all 8 knight move directions from opponent king
    for (INT i = 0; i <= 7; i++) // for each knight direction
    {
        INT    dir = NDir[i];
        SQUARE to  = ksq + dir;

        if (E->B.Board[to] != empty)
            continue;

        ATTACK a    = N->Attack[to];
        ATTACK bits = a & nMask;

        if (bits)
        {
            SearchCheckN1(E, to, a, bits);
        }
    }
} /* SearchCheckN */

static void SearchCheckN1(ENGINE *E, SQUARE to, ATTACK asq, ATTACK bits)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    ATTACK d = N->Attack_[to];

    if (d != 0)
    {
        if (d & pMask)
            return;

        // Bitcount check
        INT    bitcount = 0;
        ATTACK temp     = asq;
        while (temp)
        {
            bitcount += (temp & 1);
            temp >>= 1;
        }

        if (!(asq & ~nMask) && bitcount == 1)
            return;
    }

    N->m.piece = N->player + knight;
    N->m.to    = to;

    N_DATA *NDir = G->M.Ndata;
    bits >>= 16; // Normalize bits

    while (bits)
    {
        INT    idx  = bits & 0xFF;
        INT    dir  = NDir[idx].dir;
        SQUARE from = to - dir;

        if (from != N->escapeSq)
        {
            N->m.from = from;
            ProcessMove(E);
        }

        bits = NDir[idx].offset;
    }
} /* SearchCheckN1 */

/*------------------------------------ Search Safe Pawn Checks -----------------------------------*/
// For simplicity it is sufficient to search all pawn checks - even unsafe ones.
// However pawn moves to the 6th and 7th rank are NOT searched here, because they are searched
// by another move generator [K].

static void SearchCheckP(ENGINE *E, SQUARE ksq)
{
    NODE *N = E->S.currNode;

    // Skip if king too far from pawn check range
    INT rankCheck = ksq + N->player;
    if (rankCheck < 0x30 || rankCheck > 0x57)
        return;

    PIECE myPawn = N->player + pawn;

    // Check left diagonal
    for (INT hdir = -1; hdir <= 1; hdir += 2) // Check both diagonals
    {
        SQUARE to = ksq - N->pawnDir + hdir;
        if (E->B.Board[to] != empty)
            continue;

        SQUARE from = to - N->pawnDir;

        if (E->B.Board[from] == myPawn)
        {
            N->m.piece = myPawn;
            N->m.from  = from;
            N->m.to    = to;
            ProcessMove(E);
        }
        else if (E->B.Board[from] == empty)
        {
            from -= N->pawnDir;

            // Check if pawn double advance is possible
            INT rank2 = (ksq & 0x70) + N->player;
            if (rank2 == 0x40 && E->B.Board[from] == myPawn)
            {
                N->m.piece = myPawn;
                N->m.from  = from;
                N->m.to    = to;
                ProcessMove(E);
            }
        }
    }
} /* SearchCheckP */

/**************************************************************************************************/
/*                                                                                                */
/*                                [K] PAWN MOVES TO 6th & 7TH RANK                                */
/*                                                                                                */
/**************************************************************************************************/

// This routines searches all non-capturing pawn moves to the 6th and 7th rank (including
// sacrifices).

void SearchFarPawns(ENGINE *E)
{
    NODE *N = E->S.currNode;

    N->gen    = gen_K;
    N->m.cap  = empty;
    N->m.type = mtype_Normal;

    // Process far pawns (rank 5/6 for white, rank 2/3 for black)
    BYTE  *PawnStruct;
    SQUARE rankBase;
    INT    rank1, rank2;

    if (N->player == white)
    {
        PawnStruct = E->B.PawnStructW;
        rank1      = 5;
        rankBase   = 0x60;
        rank2      = 4;
    }
    else
    {
        PawnStruct = E->B.PawnStructB;
        rank1      = 2;
        rankBase   = 0x10;
        rank2      = 3;
    }

    // Process first rank
    BYTE bits = PawnStruct[rank1];
    if (bits)
    {
        for (INT f = 0; f < 8; f++)
        {
            if (bits & (1 << f))
            {
                SQUARE to = rankBase + f;
                if (E->B.Board[to] == empty)
                {
                    N->m.to    = to;
                    N->m.from  = to - N->pawnDir;
                    N->m.piece = N->player + pawn;
                    ProcessMove(E);
                }
            }
        }
    }

    // Process second rank. The rank2 destination is one square further from the
    // promotion edge than rank1's destination, i.e. one step AGAINST pawnDir
    // (white pawnDir=+0x10 → rankBase decreases; black pawnDir=-0x10 → rankBase
    // increases). The original C used `-= 0x10` for both colors, which gave black
    // the wrong destination (chess rank 1 instead of rank 3) and produced bogus
    // bPawn moves with from-squares that didn't actually contain a black pawn.
    rankBase -= N->pawnDir;
    bits = PawnStruct[rank2];
    if (bits)
    {
        for (INT f = 0; f < 8; f++)
        {
            if (bits & (1 << f))
            {
                SQUARE to = rankBase + f;
                if (E->B.Board[to] == empty)
                {
                    N->m.to    = to;
                    N->m.from  = to - N->pawnDir;
                    N->m.piece = N->player + pawn;
                    ProcessMove(E);
                }
            }
        }
    }
} /* SearchFarPawns */

/**************************************************************************************************/
/*                                                                                                */
/*                                       [L] CHECK EVASION                                        */
/*                                                                                                */
/**************************************************************************************************/

static void SearchAllKingMoves(ENGINE *E, SQUARE _ksq, SQUARE _csq, INT _cdir);
static void SearchInterpositions(ENGINE *E, SQUARE _ksq, SQUARE _csq, INT _cdir);
static void SearchInterpositions1(ENGINE *E, SQUARE _isq);

// The "SearchCheckEvasion()" routine should be called if the side to move is check. It generates
// and searches all strictly legal (except ep which are always tried) check evasion moves
// (including sacrifices).

void SearchCheckEvasion(ENGINE *E)
{
    NODE   *N      = E->S.currNode;
    PIECE  *Board  = E->B.Board;
    GLOBAL *Global = E->Global;

    N->gen    = gen_L;
    N->m.type = mtype_Normal;

    SQUARE ksq  = N->PieceLoc[0];
    SQUARE cdir = 0;

    /*- - - - - - - - - - - - - - - - Find Checking Piece - - - - - - - - - - - - - - - - - -*/
    // First we locate the checking piece(s) and check if it's a double check. If so we only
    // search king moves, otherwise we search normal check evasion:

    ATTACK a   = N->Attack_[ksq];
    SQUARE csq = ksq;

    // Check if attacked by pawn
    if (a & 0x06000000)
    {
        if (a & 0xFFFF)
            goto DOUBLE_CHECK;

        // Find checking pawn (post-flip convention).
        //   White king (opponent black): black pawn places pMaskL at sq-0x11 → attacker at ksq+0x11.
        //   Black king (opponent white): white pawn places pMaskL at sq+0x0F → attacker at ksq-0x0F.
        // In both cases csq = ksq + pawnDir + (pMaskL ? +1 : -1) — sign cancels out.
        {
            INT pawnOffset = (a & pMaskL) ? +1 : -1;
            csq            = ksq + N->pawnDir + pawnOffset;
        }
        goto NORMAL_CHECK;
    }

    // Check if attacked by knight
    if (a & 0x00FF0000)
    {
        if (a & 0xFFFF)
            goto DOUBLE_CHECK;

        // csq -= KnightDir[LowBit[nBits(a)]]);
        INT bits   = (a >> 16) & 0xFF;
        INT lowBit = Global->A.LowBit[bits];
        csq        = ksq - Global->B.KnightDir[lowBit];
        goto NORMAL_CHECK;
    }

    // Defensive guard for king-vs-king (kings adjacent). Cannot legally occur in chess
    // and perft confirms it never triggers from standard play, but the search may evaluate
    // pseudo-legal hypothetical positions where it does. Upstream asm has no equivalent —
    // it falls through to QRB with cdir=0, csq=ksq, which works correctly because
    // SearchAllKingMoves still generates king escapes; the explicit branch here is just
    // a clearer/safer expression of the same behavior. Originally added in commit 8ab8a05.
    if (a & kMask)
        goto DOUBLE_CHECK;

    // Check if attacked by QRB (queen/rook/bishop)
    {
        // Extract QRB bits: combine rook nibble (bits 12-15) and bishop nibble (bits 8-11)
        // Assembly: srawi rTmp3,rTmp2,8; or rTmp4,rTmp2,rTmp3; rlwinm rTmp4,rTmp4,4,20,27
        // After OR: combined = 0xHHHHRRBB where RR=rook nibble, BB=bishop nibble (H=high bits)
        // Rotate left 4: 0xHHHRRBB0, then extract bits [11:4] -> 0xRR (rook nibble as 8-bit index)
        INT combined = a | (a >> 8); // OR rook and bishop nibbles
        INT bits     = ((combined << 4) & 0x0FF0) >> 4;

        // Access QRBdata element by array index
        QRB_DATA *Data = &Global->M.QRBdata[bits];

        // Read dir and offset directly from structure (handles endianness correctly)
        cdir            = Data->dir;
        INT data_offset = Data->offset;

        // Check for double check (multiple QRB directions)
        if (data_offset != 0)
            goto DOUBLE_CHECK;

        // Follow the direction backwards to find the checking piece
        csq = ksq;
        do
        {
            csq -= cdir;
        } while (Board[csq] == empty);

        if (!onBoard(csq))
            goto DONE; // Sentinel was corrupted; skip check evasion

        goto NORMAL_CHECK;
    }

    /*- - - - - - - - - - - - - - - - - Double Check Evasion - - - - - - - - - - - - - - - - - */

DOUBLE_CHECK:
    SearchAllKingMoves(E, ksq, ksq, 0);
    goto DONE;

    /*- - - - - - - - - - - - - - - - - Normal Check Evasion - - - - - - - - - - - - - - - - - */
    // The first thing we do here is to try and capture the checking piece. Then we search king
    // moves, en passant, interpositions and finally sacrifices:

NORMAL_CHECK:

    // Try to capture the checking piece
    if (N->Attack[csq] != 0)
    {
        SQUARE csq4 = csq * 4;
        SearchEnPriseCaptures1(E, csq, csq4, N->Attack[csq]);

        if (N->Attack_[csq] != 0)
        {
            SearchSafeCaptures1(E, csq, csq4, N->Attack[csq], N->Attack_[csq]);
        }
    }

    SearchAllKingMoves(E, ksq, csq, cdir);
    SearchEnPassant(E);

    if (cdir != 0)
        SearchInterpositions(E, ksq, csq, cdir);

    SearchSacrifices(E);

DONE:;
} /* SearchCheckEvasion */

/*--------------------------------- Search All Legal King Moves ----------------------------------*/
// Although defined as having no parameters, this routine assumes that rLocal1, rLocal2 and rLocal3
// has been set properly by the calling routine (ksq, csq, cdir). It is also assumed that m.type
// is 0 (mtype_Normal).
// First all capture moves are examined, followed by non-captures.
// NOTE: Captures to the location of the checking piece (csq = rLocal2) are skipped in case of
// single checks, since these moves will be searched by the capture routines.

static void SearchAllKingMoves(ENGINE *E, SQUARE ksq, SQUARE csq, INT cdir)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    INT *KDir = G->B.KingDir; // Table of king directions

    N->m.from  = ksq;
    N->m.piece = N->player + king;

    /* - - - - - - - - - - - - - - - - - - - Captures - - - - - - - - - - - - - - - - - - - - - - */

    for (INT i = 0; i <= 7; i++)
    {
        INT    dir = KDir[i];
        SQUARE to  = ksq + dir;

        if (dir == cdir || to == csq)
            continue;

        if (offBoard(to))
            continue;

        PIECE cap = E->B.Board[to];

        if (cap == empty)
            continue;

        if (pieceColour(cap) == N->player)
            continue;

        if (N->Attack_[to] != 0)
            continue;

        N->m.to  = to;
        N->m.cap = cap;
        N->m.dir = dir;
        ProcessMove(E);
    }

    /* - - - - - - - - - - - - - - - - - - - Non Captures - - - - - - - - - - - - - - - - - - - - */

    N->m.cap = empty;

    for (INT i = 0; i <= 7; i++)
    {
        INT    dir = KDir[i];
        SQUARE to  = ksq + dir;

        if (dir == cdir)
            continue;

        if (offBoard(to))
            continue;

        if (E->B.Board[to] != empty)
            continue;

        if (N->Attack_[to] != 0)
            continue;

        N->m.to  = to;
        N->m.dir = dir;
        ProcessMove(E);
    }

} /* SearchAllKingMoves */

/*--------------------------------- Search All Interpositions ------------------------------------*/
// If the checking piece is a QRB we also have to search "interposition moves": I.e. for each
// empty square between the king and the checking piece, we try to move a piece to that square.
// The registers rLocal1..rLocal3 must be set up by the caller.

// SearchInterpositions - searches interposition moves between king and checking piece
// For each empty square between the king and checking piece, try to move a piece there
// Parameters: ksq, csq, cdir (passed from caller)
static void SearchInterpositions(ENGINE *E, SQUARE _ksq, SQUARE _csq, INT _cdir)
{
    NODE *N = E->S.currNode;

    N->m.cap  = empty;
    N->m.type = mtype_Normal;

    for (SQUARE isq = _ksq - _cdir; isq != _csq; isq -= _cdir)
    {
        SearchInterpositions1(E, isq);
    }

} /* SearchInterpositions */

/*--------------------------- Search Interpositions to a Single Square ---------------------------*/
// The "SearchInterpositions1" routine searches all interposition moves to the (empty) square
// specified in the rLocal1 register.

static void SearchInterpositions1(ENGINE *E, SQUARE isq)
{
    NODE   *N = E->S.currNode;
    GLOBAL *G = E->Global;

    N->m.to = isq;

    //--- PAWN INTERPOSITIONS ---
    PIECE  myPawn = N->player + pawn;
    SQUARE from   = isq - N->pawnDir;

    if (E->B.Board[from] == empty)
    {
        from -= N->pawnDir;
        INT rank2 = (N->player == white) ? 0x20 : 0x60;

        if ((isq & 0x70) == (N->player + 0x30) && E->B.Board[from] == myPawn)
        {
            N->m.piece = myPawn;
            N->m.from  = from;
            ProcessMove(E);
        }
    }
    else if (E->B.Board[from] == myPawn)
    {
        N->m.piece = myPawn;
        N->m.from  = from;

        if (!((isq + N->pawnDir) & 0x88))
        {
            ProcessMove(E);
        }
        else
        {
            SearchPromotion1(E);
        }
    }

    //--- KNIGHT INTERPOSITIONS ---
    ATTACK a     = N->Attack[isq];
    ATTACK nBits = (a & nMask) >> 16;

    if (nBits)
    {
        N->m.piece   = N->player + knight;
        N_DATA *NDir = G->M.Ndata;

        while (nBits)
        {
            INT idx   = nBits & 0xFF;
            INT j     = G->A.LowBit[idx];
            N->m.from = isq - NDir[idx].dir;
            ProcessMove(E);
            nBits &= ~(1 << j);
        }
    }

    //--- SLIDING PIECE INTERPOSITIONS ---
    ATTACK qrbBits = ((a >> 8) | a) & 0xFF;

    if (qrbBits)
    {
        QRB_DATA *Data = G->M.QRBdata;

        while (qrbBits)
        {
            INT idx = qrbBits & 0xFF;
            INT dir = Data[idx].dir;

            from = isq;
            do
            {
                from -= dir;
            } while (E->B.Board[from] == empty);

            if (onBoard(from))
            {
                N->m.piece = E->B.Board[from];
                N->m.dir   = dir;
                N->m.from  = from;
                ProcessMove(E);
            }

            qrbBits = Data[idx].offset;
        }
    }
} /* SearchInterpositions1 */

/**************************************************************************************************/
/*                                                                                                */
/*                                      START UP INITIALIZATION                                   */
/*                                                                                                */
/**************************************************************************************************/

void InitMoveGenModule(GLOBAL *Global) // Must be initialized AFTER the Attack and Board modules.
{
    ATTACK_COMMON  *A = &(Global->A);
    MOVEGEN_COMMON *M = &(Global->M);
    N_DATA         *n;
    QRB_DATA       *q;

    for (INT bits = 1; bits < 256; bits++)
    {
        INT j = A->LowBit[bits];

        n         = &(M->Ndata[bits]);
        n->dir    = Global->B.KnightDir[j];
        n->Nbit   = A->KnightBit[j];
        n->offset = (bits == bit(j) ? 0 : bits & ~bit(j));

        q         = &(M->QRBdata[bits]);
        q->dir    = Global->B.QueenDir[j];
        q->RBbit  = A->RookBit[j] | A->BishopBit[j];
        q->Qbit   = A->QueenBit[j];
        q->rayBit = A->RayBit[j];
        q->offset = (bits == bit(j) ? 0 : bits & ~bit(j));
    }
} /* InitMoveGenModule */
