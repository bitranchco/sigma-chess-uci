/**************************************************************************************************/
/*                                                                                                */
/* Module  : SEARCH.C                                                                             */
/* Purpose : This is the main Engine Search module which controls the root search. Non-root nodes */
/*           are handled in "TreeSearch.c". The special Mate Search routines are defined in the   */
/*           "MateSearch.c" module.                                                               */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"
#include "uci_bridge.h"

#include "Search.f"
#include "SearchMisc.f"
#include "Selection.f"
#include "Threats.f"
#include "Board.f"
#include "Attack.f"
#include "PieceVal.f"
#include "Evaluate.f"
#include "MoveGen.f"
#include "PerformMove.f"
#include "Time.f"
#include "TransTables.f"
#include "Engine.f"
#include "HashCode.f"
#include "Tablebase.f"

//#define __dumpEloNps 1  //###

/**************************************************************************************************/
/*                                                                                                */
/*                                        MAIN SEARCH ROUTINE                                     */
/*                                                                                                */
/**************************************************************************************************/

static void PrepareSearch(ENGINE *E);
static void PrepareIteration(ENGINE *E);
static void SearchRootNode(ENGINE *E);
static void SearchRootNodeMate(ENGINE *E);
static void SearchIteration(ENGINE *E);
static void StorePrinVar(ENGINE *E);
static void EndIteration(ENGINE *E);
static BOOL AnotherIteration(ENGINE *E);
static void EndSearch(ENGINE *E);

void MainSearch(ENGINE *E)
{
    E->R.taskRunning = true;

    PrepareSearch(E);

    do
    {
        PrepareIteration(E);
        if (E->P.playingMode != mode_Mate)
            SearchIteration(E);
        else
            SearchRootNodeMate(E); // Mate mode has its own cook-discovery loop; no MultiPV.
        EndIteration(E);
    } while (AnotherIteration(E));

    EndSearch(E);

    E->R.taskRunning = false;
} /* MainSearch */

// Run SearchRootNode with aspiration around `seed` (prev iter's score for this
// slot). Below MIN_ASP_DEPTH or with a mate seed, falls through with full window.
static void SearchRootAspirated(ENGINE *E, INT seed)
{
    SEARCH_STATE *S = &E->S;
    NODE         *N = S->rootNode;

    const INT MIN_ASP_DEPTH = 5;
    const INT INIT_DELTA    = 17;
    BOOL      useAsp        = (E->P.aspiration && E->P.playingMode != mode_Mate && S->mainDepth >= MIN_ASP_DEPTH &&
                   seed > mateLoseVal && seed < mateWinVal);

    INT delta = INIT_DELTA;
    INT alpha = useAsp ? Max(seed - delta, 2 - maxVal) : 2 - maxVal;
    INT beta  = useAsp ? Min(seed + delta, maxVal - 1) : maxVal - 1;

    while (E->R.state == state_Running)
    {
        N[0].alpha   = alpha;
        N[0].beta    = beta;
        S->scoreType = scoreType_True; // clear stale fail-bound from prior loop iter
        SearchRootNode(E);
        if (E->R.state != state_Running)
            break;

        INT score = S->mainScore;
        if (useAsp && score <= alpha)
        {
            beta         = (alpha + beta) / 2;
            alpha        = Max(score - delta, 2 - maxVal);
            delta        = (delta > maxVal - delta / 2) ? maxVal : delta + delta / 2;
            S->scoreType = scoreType_UpperBound;
            UCI_SendInfo(E);
            continue;
        }
        if (useAsp && score >= beta)
        {
            beta         = Min(score + delta, maxVal - 1);
            delta        = (delta > maxVal - delta / 2) ? maxVal : delta + delta / 2;
            S->scoreType = scoreType_LowerBound;
            UCI_SendInfo(E);
            continue;
        }
        break;
    }
}

// One ID iteration. For MultiPV=1 this is just SearchRootNode. For k>1 we run
// k times, Ignore'ing each previously-found best move, and snapshot/restore
// engine state around slots 2..k so iter N+1's slot 1 inherits state as if
// only slot 1 had run — matching single-PV continuity.
static void SearchIteration(ENGINE *E)
{
    SEARCH_STATE *S = &E->S;

    if (S->multiPVCount <= 1)
    {
        S->multiPVCurrent = 1;
        SearchRootAspirated(E, S->prevScore);
        return;
    }

    BOOL IgnoreSnapshot[maxLegalMoves];
    for (INT i = 0; i < S->numRootMoves; i++)
        IgnoreSnapshot[i] = S->Ignore[i];

    // Slot 2..k mutate the NODE tree, RootTab, and (via PerformMove/RetractMove
    // accumulators) BOARD_STATE / ATTACK_STATE. Snapshot all four after slot 1
    // and restore at the end.
    NODE         NodeSnapshot[maxSearchDepth + 3];
    ROOTTAB      RootTabSnapshot[maxLegalMoves];
    BOARD_STATE  BoardSnapshot;
    ATTACK_STATE AttackSnapshot;
    BOOL         haveSnapshot = false;

    // Snapshot prior-iter validity/scores BEFORE the per-iter Valid reset and
    // the search loop's overwrites. Slots not run last iter get mateWinVal,
    // making SearchRootAspirated fall through to full window.
    INT slotSeed[maxMultiPV];
    for (INT k = 0; k < maxMultiPV; k++)
        slotSeed[k] = S->MultiPVValid[k] ? S->MultiPVScores[k] : mateWinVal;

    for (INT k = 0; k < maxMultiPV; k++)
        S->MultiPVValid[k] = false;

    // Cap k at the number of moves actually available (Ignore[] may already
    // exclude some via book pruning).
    INT searchable = 0;
    for (INT i = 0; i < S->numRootMoves; i++)
        if (!S->Ignore[i])
            searchable++;
    INT effective = S->multiPVCount;
    if (effective > searchable)
        effective = searchable;
    if (effective < 1)
        effective = 1;

    for (INT slot = 1; slot <= effective; slot++)
    {
        S->multiPVCurrent = slot;
        clrMove(S->MainLine[0]); // Fresh PV for this slot.

        SearchRootAspirated(E, slotSeed[slot - 1]);

        if (slot == 1)
        {
            NODE *Nodes = S->rootNode;
            for (INT d = 0; d <= maxSearchDepth; d++)
                NodeSnapshot[d] = Nodes[d];
            for (INT i = 0; i < S->numRootMoves; i++)
                RootTabSnapshot[i] = S->RootTab[i];
            BoardSnapshot  = E->B;
            AttackSnapshot = E->A;
            haveSnapshot   = true;
        }

        // Save the slot's result even if SearchRootNode exited early — a
        // fast-mate or external stop still produces a valid line.
        if (!isNull(S->MainLine[0]))
        {
            for (INT d = 0; d < maxSearchDepth + 3; d++)
                S->MultiPVLines[slot - 1][d] = S->MainLine[d];
            S->MultiPVScores[slot - 1]    = S->bestScore;
            S->MultiPVScoreType[slot - 1] = S->scoreType;
            S->MultiPViMain[slot - 1]     = S->iMain;
            S->MultiPVValid[slot - 1]     = true;
        }

        if (E->R.state != state_Running)
            break;

        if (slot < effective)
            S->Ignore[S->iMain] = true;
    }

    for (INT i = 0; i < S->numRootMoves; i++)
        S->Ignore[i] = IgnoreSnapshot[i];

    if (haveSnapshot)
    {
        NODE *Nodes = S->rootNode;
        for (INT d = 0; d <= maxSearchDepth; d++)
            Nodes[d] = NodeSnapshot[d];
        for (INT i = 0; i < S->numRootMoves; i++)
            S->RootTab[i] = RootTabSnapshot[i];
        E->B = BoardSnapshot;
        E->A = AttackSnapshot;
    }

    // Surface slot 1 as the official iteration result for EndIteration,
    // SortRootTab on next iteration, EndSearch, and the bestmove emission.
    if (S->MultiPVValid[0])
    {
        for (INT d = 0; d < maxSearchDepth + 3; d++)
            S->MainLine[d] = S->MultiPVLines[0][d];
        S->iMain     = S->MultiPViMain[0];
        S->mainScore = S->MultiPVScores[0];
        S->bestScore = S->MultiPVScores[0];
        S->scoreType = S->MultiPVScoreType[0];
    }
    S->multiPVCurrent = 1;
} /* SearchIteration */

/**************************************************************************************************/
/*                                                                                                */
/*                                           ROOT SEARCH                                          */
/*                                                                                                */
/**************************************************************************************************/

/*----------------------------------------- Prepare Search ---------------------------------------*/

static void CalcRunFlags(ENGINE *E);
static void PrepareSearchTree(ENGINE *E);
static void PrepareMisc(ENGINE *E);
static void ResetRootTab(ENGINE *E);
static void SetPlayingStrength(ENGINE *E);
static void RestrictRootMoves(ENGINE *E);

static void PrepareSearch(ENGINE *E)
{
    // Set state to "root" while preparing search (e.g. Generating root moves)
    E->R.state = state_Root;

    // Latch and clamp the requested PV count from PARAM.
    E->S.multiPVCount = E->P.multiPVCount;
    if (E->S.multiPVCount < 1)
        E->S.multiPVCount = 1;
    if (E->S.multiPVCount > maxMultiPV)
        E->S.multiPVCount = maxMultiPV;
    E->S.multiPVCurrent = 1;

    // Clear per-slot scratch so aspiration seeds in the first iter aren't
    // stale data from the previous `go` (different position).
    for (INT k = 0; k < maxMultiPV; k++)
    {
        E->S.MultiPVScores[k] = 0;
        E->S.MultiPVValid[k]  = false;
    }

    if (E->P.playingMode == mode_Mate)
    {
        E->P.selection = false;
        E->P.pvSearch  = false;
    }

    // MultiPV>1 needs selection off globally — the quiescence alphaPly logic
    // in NodeSearch keys off P.selection, and slot 1 must run the same search
    // algorithm a Selection=false single-PV would.
    if (E->S.multiPVCount > 1)
        E->P.selection = false;

    // First compute the root node (based on side to move):
    E->S.rootNode = (E->P.player == white ? E->S.whiteNode : E->S.blackNode);
    E->S.currNode = E->S.rootNode;

    // Next calc board, attack and transposition table state from search parameters:
    CalcBoardState(E);
    CalcAttackState(E); // Also resets mobility.
    CalcTransState(E);

    CalcRunFlags(E); // Must be done AFTER "CalcTransState" but BEFORE "GenRootMoves"

    // Then reset search and evaluation state:
    PrepareSearchTree(E);
    PrepareMisc(E);
    CalcPieceValState(E); // <--- MUST be done here since it is needed in the root tab (??)

    // Reduce selection in simple endgames (needs the phase from CalcPieceValState).
    if (E->P.selection)
    {
        INT incPly = Min(3, Max(E->V.phase - 6, 0));
        E->S.rootNode->alphaPly += incPly;
        E->S.rootNode->betaPly += incPly;
    }
    CalcEvaluateState(E);
    // CalcMobilityState(E);
    GenRootMoves(E);
    RestrictRootMoves(E);
    TB_FilterRootMoves(E);
    ResetRootTab(E);
    AllocateTime(E);
    SetPlayingStrength(E); // Must be done after time allocation.

    // Display initial search results:
    UCI_SendInfo(E);

    // Finally switch to "running" state (must be done after GenRootMoves).
    E->R.state = state_Running;
} /* PrepareSearch */

static void CalcRunFlags(ENGINE *E)
{
    ULONG f = E->R.state;

    if (E->P.pvSearch)
        f |= rflag_PVSearch;
    if (E->P.extensions)
        f |= rflag_Extensions;
    if (E->P.selection)
        f |= rflag_Selection;
    if (E->P.deepSelection)
        f |= rflag_DeepSel;
    if (E->P.reduceStrength)
        f |= rflag_ReduceStrength;
    if (E->Tr.transTabOn)
        f |= rflag_TransTabOn;

    E->R.rflags = f;
} /* CalcRunState */

static void PrepareSearchTree(ENGINE *E) // Initializes the nodes of the search tree.
{
    NODE *N = E->S.rootNode;

    // Initialize the "previous" node:

    N[-1].m          = E->P.lastMove; // Set previous move.
    N[-1].gen        = gen_None;
    N[-1].lastPiece_ = E->B.LastPiece[E->B.player];
    N[-1].lastPiece  = E->B.LastPiece[E->B.opponent];

    // Perform common initialization of search tree (i.e. all nodes):

    for (INT d = 0; d <= maxSearchDepth; d++)
    {
        N[d].depth         = d;
        N[d].alphaPly      = 1000;
        N[d].betaPly       = 1000;
        N[d].maxPly        = 0;
        N[d].gameDepth     = E->P.lastMoveNo + d;
        N[d].drawData      = &E->P.DrawData[N[d].gameDepth];
        N[d].loseVal       = d - maxVal;
        N[d].program       = even(d);
        N[d].isMateDepth   = false;
        N[d].bottomNode    = false;
        N[d].hungVal1      = (N[d].program ? 15 : 10);
        N[d].hungVal2      = (N[d].program ? 50 : 30);
        N[d].lastPiece     = N[d - 1].lastPiece_;
        N[d].lastPiece_    = N[d - 1].lastPiece;
        N[d].killer1Count  = 0;
        N[d].killer2Count  = 0;
        N[d].killer1Active = false;
        N[d].killer2Active = false;
        clrMove(N[d].rfm);
        clrMove(N[d].killer1);
        clrMove(N[d].killer2);
    }

    N[maxSearchDepth - 1].bottomNode = false;
    N[maxSearchDepth].bottomNode     = true;

    // Finally initialize the root node (in particular those fields needed for the generation of the
    // root moves):

    N[0].ply = 0;

    if (!E->P.selection)
    {
        N[0].alphaPly = N[0].betaPly = 1000;
    }
    else
    {
        if (E->P.playingMode == mode_Infinite)
        {
            N[0].alphaPly = -2; //-1;
            N[0].betaPly  = -3; //-2;
        }
        else
        {
            N[0].alphaPly = -4; //-2;
            N[0].betaPly  = -3; //-1;
            if (E->P.deepSelection)
                N[0].alphaPly--;
        }
    }

    clrMove(N[0].m);
    N[0].gen        = gen_None;
    N[0].check      = (N->Attack_[N->PieceLoc[0]] > 0);
    N[0].quies      = false;
    N[0].bufStart   = E->S.SBuf;
    N[0].storeSacri = true;
    N[0].selMargin  = 0;

    N[0].hashKey = N[0].drawData->hashKey;
} /* PrepareSearchTree */

static void PrepareMisc(ENGINE *E) // Initialize miscellaneous variables.
{
    E->S.mainDepth = 0; // Reset nominal search depth (= rootNode->ply).
    E->S.currMove  = 0; // Reset current root move (index).
    E->S.mainScore = 0; // Reset main score.
    E->S.bestScore = 0;
    E->S.prevScore = mateWinVal;              // Reset previous iteration score.
    E->S.MainLine  = E->S.rootNode->BestLine; // Reset main line (= best line of root node).
    clrMove(E->S.MainLine[0]);

    E->S.nodeCount       = 0; // Reset node count.
    E->S.moveCount       = 0; // Reset move count.
    E->S.hashFull        = 0; // = 1000*E->Tr.transUsed/E->Tr.transSize
    E->S.selDepth        = 0; // Max physical recursion depth reached this search.
    E->S.tbHits          = 0;
    E->S.startTime       = Now();
    E->S.searchTime      = 0;
    E->S.mainTime        = 0;
    E->S.periodicTime    = Now(); // Poll from the start; subsecond budgets need it.
    E->S.periodicCounter = 0;

    E->S.bufTop    = E->S.SBuf; // Reset sacrifice buffer.
    E->R.aborted   = false;
    E->S.mateDepth = 2 * E->P.depth - 1;
    E->S.mateFound = false;
    E->S.mateTime  = Now();

    ResetTransTab(E);
    if (E->P.playingMode == mode_Mate)
        E->S.rootNode[E->S.mateDepth].isMateDepth = true;
} /* PrepareMisc */

// Keep only the root moves in P.searchMoves (if any). If none of them is legal, all moves are kept.

static void RestrictRootMoves(ENGINE *E)
{
    SEARCH_STATE *S = &E->S;
    INT           n = 0;

    S->rootRestricted = false;
    if (E->P.searchMoveCount == 0)
        return;

    for (INT i = 0; i < S->numRootMoves; i++)
        for (INT j = 0; j < E->P.searchMoveCount; j++)
        {
            MOVE *m = &S->RootMoves[i], *sm = &E->P.searchMoves[j]; // Squares and promotion piece
            if (m->from == sm->from && m->to == sm->to && (!isPromotion(*m) || m->type == sm->type))
            {
                S->RootMoves[n++] = *m;
                break;
            }
        }

    if (n > 0)
    {
        S->rootRestricted = (n < S->numRootMoves);
        S->numRootMoves   = n;
    }
} /* RestrictRootMoves */

static void ResetRootTab(ENGINE *E)
{
    SEARCH_STATE *S = &E->S;
    ROOTTAB      *R = S->RootTab;

    for (INT i = 0; i < S->numRootMoves; i++)
    {
        R[i].i   = i;
        R[i].val = 0;
    }

    if (E->P.playingMode == mode_Infinite)
        for (INT i = 0; i < S->numRootMoves; i++)
            S->RootMoves[i].dply = 1;

    for (INT i = 0; i < S->numRootMoves; i++)
        E->S.Ignore[i] = false;

    S->iMain = 0;
} /* ResetRootTab */

/*---------------------------------------- Prepare Iteration -------------------------------------*/

static void SortRootTab(ENGINE *E);
static void StorePrinVar(ENGINE *E);

static void PrepareIteration(ENGINE *E)
{
    NODE *N = E->S.rootNode;

    if (E->P.playingMode == mode_Mate)
    {
        N[0].alpha = maxVal - E->S.mateDepth - 1;
        N[0].beta  = maxVal - N->ply;
    }
    else
    {
        N[0].alpha = 2 - maxVal; // Set initial alpha-beta window.
        N[0].beta  = maxVal - 1;
    }

    N[0].ply++;      // Increase nominal search depth and
    N[0].alphaPly++; // selective search depth.
    N[0].betaPly++;

    E->S.mainDepth = N[0].ply;
    E->S.currMove  = 0;

    INT phase  = Max(5, Min(9, E->V.phase));           // Set the maximum quiescence depth
    INT maxPly = ((20 - phase) * E->S.mainDepth) / 10; // (i.e. maximum number of plies to
    if (E->P.playingMode == mode_Infinite)             // restricted quiescence search).
    {
        maxPly++;
    }
    else
    {
        maxPly--;
        if (odd(maxPly))
            maxPly--;
    }

    maxPly = Max(maxPly, E->S.mainDepth + 3); // Restrict maxPly to the interval
    maxPly = Min(maxPly, maxSearchDepth);     // [ply + 3...maxSearchDepth].

    E->S.checkDepth = maxPly + (E->P.playingMode == mode_Infinite ? 3 : 1); //###Testing

    for (INT d = 0; maxPly >= 0; d++)
        N[d].maxPly = maxPly--;

    if (N[0].ply > 1)
    {
        SortRootTab(E);
        StorePrinVar(E);
    }

} /* PrepareIteration */

static void SortRootTab(ENGINE *E) // Sort the root move table on the values
{                                  // returned by the search of each move in the
    SEARCH_STATE *S = &E->S;       // previous iteration (val > -maxVal).
    ROOTTAB       R[maxLegalMoves];

    for (INT i = 0; i < S->numRootMoves; i++)
        R[i] = S->RootTab[i];

    for (INT i = 0; i < S->numRootMoves; i++)
    {
        INT vmax = -maxVal; // Find best move "jmax" among
        INT jmax = 0;       // remaining moves.

        for (INT j = 0; j < S->numRootMoves; j++)
            if (R[j].val > vmax)
                vmax = R[jmax = j].val;
        R[jmax].val   = -maxVal;
        S->RootTab[i] = R[jmax];
    }
} /* SortRootTab */

static void StorePrinVar(ENGINE *E) // Stores the principal variation in the search tree
{                                   // so that it will be searched first.
    if (E->P.playingMode == mode_Mate)
        return; // Mate mode does not use principal variation.

    SEARCH_STATE *S = &E->S;
    NODE         *N = S->rootNode;
    INT           d = 0;

    if (!isNull(S->MainLine[0])) //###
        while (!isNull(S->MainLine[++d]))
        {
            N[d].pvNode = true;
            N[d].rfm    = E->S.MainLine[d];
        }

    while (d < maxSearchDepth)
        N[d++].pvNode = false;
} /* StorePrinVar */

/*-------------------------- Perform Iteration - Normal Root Node Search -------------------------*/
// Performs one iteration by searching all the root moves.

static void SearchRootNode(ENGINE *E)
{
    register SEARCH_STATE *S = &E->S;
    register NODE         *N = S->rootNode;
    register ROOTTAB      *R = S->RootTab;

    S->nodeCount++;

    S->mainScore = N->score = N->loseVal; //(E->P.playingMode == mode_Infinite ? -1 : N->loseVal); //###
    NN->beta                = -N->alpha;

    //--- For each root move ---

    for (S->currMove = 0; S->currMove < S->numRootMoves && E->R.state == state_Running; S->currMove++, R++)
    {
        S->moveCount++;
        Engine_Periodic(E);

        N->m   = S->RootMoves[R->i]; // Retrieve next root move to be searched
        N->gen = N->m.misc;

        // Skip without clobbering R->val: under MultiPV, slot N's iteration
        // would otherwise wipe the score recorded by an earlier slot.
        if (S->Ignore[R->i])
            continue;
        R->val = 1 - maxVal;
        UCI_SendCurrMove(E);

        NN->ply = N->ply - Min(N->m.dply, 1); // Decrement ply-counter at next node.

        INT moveEval = EvalMove(E); // Compute move evaluation

        //--- Perform, Search & Retract Move ---

        PerformMove(E);

        if (S->currMove == 0 || !E->P.pvSearch) // If first move or not PV node
        {
            NN->alpha0 = -N->beta; // search with full window...
            SearchNode(E);
        }
        else // ...otherwise search with minimal
        {
            NN->alpha0 = -N->alpha - 1; // window and re-search if fail high.
            SearchNode(E);
            if (N->val > N->alpha)
            {
                NN->alpha0    = -N->beta;
                NN->ply       = N->ply - Min(N->m.dply, 1);
                NN->totalEval = moveEval;
                SearchNode(E);
            }
        }

        RetractMove(E);

        //--- Update Score ---
        if (N->val > N->score && E->R.state == state_Running) // Update alpha values and main
        {                                                     // line if necessary...
            if (E->P.nondeterm &&                             // Apply nondeterministic content
                N->val > mateLoseVal && N->val < mateWinVal)  // factor (if not mate win/lose).
                N->val += Rand(5);
            S->mainScore = S->bestScore = N->score = N->alpha = R->val = N->val;
            UpdateBestLine(E);
            NN->beta     = -N->alpha;
            S->iMain     = R->i;
            S->scoreType = scoreType_True;

            if (S->currMove > 0 || S->mainDepth == 1)
                E->S.mainTime = Now() - E->S.startTime;

            if (N->score >= maxVal - 1 - N->ply) // Terminate search if a fast mate
            {
                E->R.state   = state_Stopping; // is found.
                E->R.aborted = false;
            }

            UCI_SendInfo(E);
            AdjustTimeLimit(E);

            if (N->score >= N->beta) // Fail high (aspiration window).
                break;
        }
    }
} /* SearchRootNode */

static void SearchRootNodeMate(ENGINE *E)
{
    register SEARCH_STATE *S = &E->S;
    register NODE         *N = S->rootNode;
    register ROOTTAB      *R = S->RootTab;

    S->nodeCount++;

    S->mainScore = N->score = 0;
    NN->beta                = -N->alpha;

    //--- For each root move ---

    for (S->currMove = 0; S->currMove < S->numRootMoves && E->R.state == state_Running; S->currMove++, R++)
    {
        S->moveCount++;
        Engine_Periodic(E);

        N->m   = S->RootMoves[R->i]; // Retrieve next root move to be searched
        N->gen = N->m.misc;

        if (S->Ignore[R->i])
            continue;
        R->val = 1 - maxVal;

        UCI_SendCurrMove(E);

        NN->ply = N->ply - Min(N->m.dply, 1); // Decrement ply-counter at next node.

        EvalMove(E); // Compute move evaluation

        //--- Perform, Search & Retract Move ---

        PerformMove(E);

        NN->alpha0 = -N->beta; // search with full window...
        SearchNode(E);

        RetractMove(E);

        //--- Update Score ---

        if (N->val > N->alpha && E->R.state == state_Running) // Update alpha values and main
        {                                                     // line if necessary...
            S->mainScore = S->bestScore = N->score = N->alpha = R->val = N->val;
            UpdateBestLine(E);
            NN->beta     = -N->alpha;
            S->iMain     = R->i;
            S->scoreType = scoreType_True;

            R->val            = 1 - maxVal;
            E->S.Ignore[R->i] = true;

            S->mateFound    = true;
            S->mateTime     = Now() - S->mateTime;
            S->mateContinue = false;

            UCI_SendInfo(E);

            // Set by host if user/host wants to continue looking for cooks
            if (S->mateContinue)
            {
                N->alpha = maxVal - E->S.mateDepth - 1;
                N->beta  = maxVal - N->ply;
                clrMove(S->MainLine[0]);
                S->mainScore = N->score = 0;
                NN->beta                = -N->alpha;
                S->mateTime             = Now();

                UCI_SendInfo(E);
            }
        }
    }
} /* SearchRootNodeMate */

/*------------------------------------------ End Iteration ---------------------------------------*/

static void EndIteration(ENGINE *E)
{
    E->S.prevScore = E->S.mainScore;

    // Re-publish the iteration's final PV(s). MultiPV>1: replay each saved
    // slot, swapping MainLine/score/scoreType into S so UCI_SendInfo sees it.
    if (E->S.multiPVCount <= 1)
    {
        E->S.multiPVCurrent = 1;
        UCI_SendInfo(E);
        return;
    }

    SEARCH_STATE *S = &E->S;
    MOVE          backup[maxSearchDepth + 3];
    for (INT d = 0; d < maxSearchDepth + 3; d++)
        backup[d] = S->MainLine[d];
    INT savedScore = S->bestScore, savedType = S->scoreType;

    for (INT slot = 1; slot <= S->multiPVCount; slot++)
    {
        if (!S->MultiPVValid[slot - 1])
            continue;
        for (INT d = 0; d < maxSearchDepth + 3; d++)
            S->MainLine[d] = S->MultiPVLines[slot - 1][d];
        S->bestScore      = S->MultiPVScores[slot - 1];
        S->scoreType      = S->MultiPVScoreType[slot - 1];
        S->multiPVCurrent = slot;
        UCI_SendInfo(E);
    }

    for (INT d = 0; d < maxSearchDepth + 3; d++)
        S->MainLine[d] = backup[d];
    S->bestScore      = savedScore;
    S->scoreType      = savedType;
    S->multiPVCurrent = 1;
} /* EndIteration */

static BOOL AnotherIteration(ENGINE *E) // Should we perform one more iteration?
{
    if (E->S.numRootMoves == 1 && !E->S.rootRestricted) // Stop if only one legal move
        return (E->S.mainDepth < 2 &&                   // and a reply is found.
                isNull(E->S.MainLine[1]));

    if (E->P.playingMode != mode_Mate)       // If not mate mode, don't continue if mate is
        if (E->S.mainScore <= mateLoseVal || // unavoidable.
            E->S.mainScore >= mateWinVal)
            return false;

    if (E->S.mainDepth == maxSearchDepth) // Stop if maximum search depth has been reached.
        return false;

    if (E->P.backgrounding)
        return !E->R.aborted;
    else if (E->R.state == state_Stopping || E->R.state == state_Stopped)
        return false;

    switch (E->P.playingMode)
    {
    case mode_Time:
        return TimeForAnotherIteration(E);
    case mode_FixDepth:
        return E->S.mainDepth < E->P.depth;
    case mode_Mate:
        return E->S.mainDepth < E->S.mateDepth; //### -1 when real mate finder done at some point
    case mode_Infinite:
        return true;
    default:
        return false; // We should never get here!
    }
} /* AnotherIteration */

/*------------------------------------------- End Search -----------------------------------------*/

static void RecalcPlayingStrength(ENGINE *E);

static void EndSearch(ENGINE *E)
{
    if (isNull(E->S.MainLine[0])) // In the rare case where no move was found we simply
    {
        E->S.MainLine[0] = E->S.RootMoves[0]; // return the first legal move (and clear the reply).
        clrMove(E->S.MainLine[1]);
    }

    E->S.MainLine[0].misc = 0; // Clear move generator, so we avoid strange glyphs

    E->S.Ignore[E->S.iMain] = true; // Update ignore list so next best can be applied.

    E->S.searchTime = Now() - E->S.startTime; // Calc elapsed search time (in ms).
    RecalcPlayingStrength(E);

    E->R.state = state_Stopped; // Go to "stopped" state
} /* EndSearch */

/**************************************************************************************************/
/*                                                                                                */
/*                               PLAYING STRENGTH - ELO/NPS CONVERSION                            */
/*                                                                                                */
/**************************************************************************************************/

// ELO/Nps conversion formulas:
//
//    eloNpsFactor = exp(100*ln(npsMax/npsMin)/(eloMax - eloMin))
//
//    nps(elo) = npsMin*eloNpsFactor^((elo - eloMin)/100)
//
//    elo(nps) = eloMin + 100*ln(nps/npsMin)/ln(eloNpsFactor)

// ELO/Nps conversion constants for the standard/base configuration:
//
// • 40 moves in 2 hours (i.e. 180 secs per move)
// • permanent brain on
// • normal playing style
// • 2.5 Mb Hash

#define eloMin 1200
//#define npsMin       15           // Corresponding to eloMin in standard configuration
#define npsMin 25 // Corresponding to eloMin in standard configuration
//#define eloNpsFactor 2.350      // nps1200 = 15, nps2500 = 1000000
#define eloNpsFactor  2.259  // nps1200 = 25, nps2500 = 1000000
#define eloBaseMoveMs 180000 // 40 moves in 2 hours

static LONG ELO_to_Nps(INT elo);
static INT  Nps_to_ELO(LONG nps);
static INT  AdjustELO(ENGINE *E);

/*-------------------------------- Set Playing Strength (Max NPS) --------------------------------*/
// The playing strength is controlled by reducing the NPS according to the specified ELO rating.

static void SetPlayingStrength(ENGINE *E)
{
    E->S.eloAdjust = AdjustELO(E); // Adjust for blitz, hash tables etc.
    E->S.npsTarget = 1000000;

    if (!E->P.reduceStrength)
        return;

    E->S.eloTarget = E->P.engineELO - E->S.eloAdjust;
    E->S.npsTarget = ELO_to_Nps(E->S.eloTarget);

    // The conversion is calibrated at eloBaseMoveMs per move. Under a clock, scale the rate so a
    // move searches the nodes it would get there; otherwise faster games play far weaker.
    if (E->P.playingMode == mode_Time)
    {
        LONG   moveMs  = MaxL(1, (LONG)(E->T.NormalTime[timer_Normal] - Now()));
        LONG64 nps     = (LONG64)E->S.npsTarget * eloBaseMoveMs / moveMs;
        E->S.npsTarget = (ULONG)(nps < 1000000000 ? nps : 1000000000);
    }

#ifdef __dumpEloNps
    CHAR s[100];
    Format(s, "ELO Target : %d, NPS : %ld", E->S.eloTarget, E->S.npsTarget);
    DebugWriteNL(s);

    for (INT elo = eloMin; elo <= 2500; elo += 100)
    {
        Format(s, "ELO : %d, NPS : %ld", elo, ELO_to_Nps(elo));
        DebugWriteNL(s);
    }
    for (LONG nps = npsMin; nps <= ELO_to_Nps(2500); nps *= 2)
    {
        Format(s, "NPS : %6ld, ELO : %d", nps, Nps_to_ELO(nps));
        DebugWriteNL(s);
    }
#endif
    /*
   E->S.eloTimer     = Now() + 8;
   E->S.eloTMoves    = (nps*8)/60;
   E->S.eloTMCounter = E->S.eloTMoves;
*/
} /* SetPlayingStrength */

/*------------------------------------ Actual Playing Strength -----------------------------------*/
// When the search completes we compute the actual/effective playing strength by converting back
// from the actual NPS to ELO rating. If the actual ELO rating is lower (within a margin) than the
// specified ELO rating, the host application could e.g. notify the user that the machine is too
// slow.

static void RecalcPlayingStrength(ENGINE *E)
{
    ULONG nps = (1000 * E->S.moveCount) / E->S.searchTime;

    if (E->S.moveCount < 100000 || E->S.searchTime < 2000) // Ignore if too brief search (< 2s)
        E->P.actualEngineELO = E->P.engineELO;
    else
        E->P.actualEngineELO = Nps_to_ELO(nps) + E->S.eloAdjust;

#ifdef __dumpEloNps
    CHAR s[100];
    Format(s, "Actual/achieved ELO : %d (%d), NPS : %ld (%ld)", E->P.actualEngineELO, Nps_to_ELO(nps), nps,
           ELO_to_Nps(E->P.actualEngineELO));
    DebugWriteNL(s);
#endif
} /* RecalcPlayingStrength */

/*-------------------------------------- ELO/NPS Conversion --------------------------------------*/

LONG ELO_to_Nps(INT elo)
{
    return (LONG)(npsMin * pow(eloNpsFactor, (REAL)(elo - eloMin) / 100.0));
} /* ELO_to_Nps */

INT Nps_to_ELO(LONG nps)
{
    return eloMin + 100 * log((REAL)nps / (REAL)npsMin) / log(eloNpsFactor);
} /* Nps_to_ELO */

/*----------------- ELO Adustment caused by deviation from standard configuration  ---------------*/
// Computes an ELO adjustment offset based on the current search parameters. For the standard/base
// configuration the adjustment is 0. Deviations are handled as follows:
//
// • For shorter time controls the effective ELO against human opponents increases (up to 200 ELO
//   in blitz). This adjustment is only done in the mode_Time playing mode.
// • If permanent brain is off, the ELO is decreased by 50 ELO points.
// • Transposition tables: Doubling (resp. halving) the size corresponds to 5 (resp -5) ELO points.
//   If the transposition tables are off an additional penalty of 50 ELO points is included.

#define transDoubleVal 8 // ELO increase for doubling trans tables.

INT AdjustELO(ENGINE *E)
{
    INT diff = 0;

    // Reduce ELO strength if permanent brain off:
    if (!E->P.permanentBrain)
        diff -= 30;

    // Reduce strength if small hash tables (default setting 10 MB hash)
    diff -= 5 * transDoubleVal;
    LONG t = E->P.transSize / 1024;
    while (t > 80)
        t >>= 1, diff += transDoubleVal;

    return Min(diff, 200);
} /* AdjustELO */

/**************************************************************************************************/
/*                                                                                                */
/*                                     SEARCH STATE INITIALIZATION                                */
/*                                                                                                */
/**************************************************************************************************/

// When a new engine is created/allocated, the InitSearchState() routine must be called. It sets
// up various "read only" values in the search nodes e.t.c.

void InitSearchState(ENGINE *E)
{
    SEARCH_STATE *S = &E->S;
    BOARD_STATE  *B = &E->B;
    ATTACK_STATE *A = &E->A;
    NODE         *N = S->_Nodes;

    // Initialize main line e.t.c.:

    S->numRootMoves = 0;
    S->whiteNode    = &N[2];
    S->blackNode    = &N[1];
    S->mainDepth    = 0;
    S->currMove     = 0;
    S->mainScore    = 0;
    S->bestScore    = 0;
    S->MainLine     = S->whiteNode->BestLine;
    clrMove(S->MainLine[0]);
    clrMove(S->RootMoves[0]);

    // Initialize the search nodes:

    for (INT d = 0; d <= maxSearchDepth + 2; d++, N++)
    {
        N->pvNode = N->bottomNode = false;
        N->killer1Active          = false;
        N->killer2Active          = false;

        if (even(d))
        {
            N->player    = white;
            N->opponent  = black;
            N->pawnDir   = +0x10;
            N->Attack    = A->AttackW;
            N->Attack_   = A->AttackB;
            N->PieceLoc  = B->PieceLocW;
            N->PieceLoc_ = B->PieceLocB;
        }
        else
        {
            N->player    = black;
            N->opponent  = white;
            N->pawnDir   = -0x10;
            N->Attack    = A->AttackB;
            N->Attack_   = A->AttackW;
            N->PieceLoc  = B->PieceLocB;
            N->PieceLoc_ = B->PieceLocW;
        }
    }
} /* InitSearchState */

/**************************************************************************************************/
/*                                                                                                */
/*                                      START UP INITIALIZATION                                   */
/*                                                                                                */
/**************************************************************************************************/

void InitSearchModule(GLOBAL *G)
{
    // At the moment no initialization needed!!
} /* InitSearchModule */
