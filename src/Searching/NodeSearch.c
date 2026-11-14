/**************************************************************************************************/
/*                                                                                                */
/* Module  : NODESEARCH.C                                                                         */
/* Purpose : This module controls all searching of non-root nodes.                                */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"

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

static void SearchNodeC(register ENGINE *E, register NODE *N);
static void SearchMoveC(register ENGINE *E, register NODE *N);

/**************************************************************************************************/
/*                                                                                                */
/*                                          SEARCH NODE                                           */
/*                                                                                                */
/**************************************************************************************************/

/*--------------------------------------- Search Node (C Version) ------------------------------------*/
// This routine recursively searches all non root nodes (i.e. depth > 0).
//
// Converted from PowerPC assembly wrapper. The original used global registers (rEngine, rNode)
// to avoid parameter passing. The C version passes ENGINE explicitly and tracks the current
// node via E->S.currNode.

void SearchNode(ENGINE *E)
{
    // Get current node from search state
    NODE *N = E->S.currNode;

    // Move to next node (N++)
    // The original assembly incremented rNode pointer
    N++;
    E->S.currNode = N;

    // Call the actual search logic
    SearchNodeC(E, N);

    // Move back to previous node (N--)
    // The original assembly decremented rNode pointer
    N--;
    E->S.currNode = N;
} /* SearchNode */

/*----------------------------------------- Search Node ------------------------------------------*/

void SearchNodeC(register ENGINE *E, register NODE *N)
{

    /* - - - - - - - - - - - - - - - - - - - Periodics - - - - - - - - - - - - - - - - - - - - */
    // Engine_Periodic self-throttles via S.periodicTime; poll every ~256 nodes so
    // subsecond budgets fire on time, more often when reduced strength runs slower.

    if (--E->S.periodicCounter <= 0)
    {
        Engine_Periodic(E);
        if (E->P.nodeLimit > 0 && E->S.moveCount >= E->P.nodeLimit && E->R.state == state_Running)
            Engine_Stop(E);
        E->S.periodicCounter = (INT)MinL(E->S.npsTarget >> 6, 256);

        if (E->P.reduceStrength) // Reduce ELO strength: spin until our move count
        {                        // is below npsTarget (moves/sec) * elapsed time,
                                 // or the search is stopped (time out, "stop").
            while (E->R.state == state_Running &&
                   E->S.moveCount * 1000 >= (LONG64)E->S.npsTarget * (Now() - E->S.startTime))
                Engine_Periodic(E);
        }
    }

    // Track max recursion depth for UCI seldepth.
    {
        INT depthFromRoot = N->gameDepth - E->P.lastMoveNo;
        if (depthFromRoot > E->S.selDepth)
            E->S.selDepth = depthFromRoot;
    }

    /* - - - - - - - - - - - - - - - - - Compute Parameters - - - - - - - - - - - - - - - - - -*/

    N->alpha = N->alpha0;
    if (N->ply < 0)
        N->ply = 0; // Adjust "ply" counter to the
    else if (N->ply > N->maxPly)
        N->ply = N->maxPly; // interval [0..maxPly].

    /* - - - - - - - - - - - - - - - - - - Preprocess Node - - - - - - - - - - - - - - - - - - */

    clrMove(N->BestLine[0]); // No move has been found yet.

    UpdateDrawState(E);               // Return in case of a true draw or
    if (N->drawType != drawType_None) // first repetition (unless depth = 2).
    {
        N->score = drawVal;
        if (N->depth != 2 || N->drawType >= drawType_Rep2)
            goto exit;
    }

    if (ProbeTransTab(E)) // Probe transposition table.
    {
        goto exit;
    }

    INT tbScore;
    if (TB_ProbeNode(E, N, &tbScore)) // Syzygy WDL after captures and pawn moves.
    {
        N->score = tbScore;
        goto exit;
    }

    N->check = (N->Attack_[N->PieceLoc[0]] > 0); // Is the player in check?

    // The undo tests threatEval, so analyze threats first.
    BOOL analyzed = false;
    if (!N->check && N->ply > 0)                               // Undo futile extensions.
    {                                                          // Must be done here, because it may
        if (PN->m.dply == 0 && (N - 2)->gen == gen_I &&        // change the "ply" counter (and
            PN->m.to == (N - 2)->m.to && /*! (N-2)->m.cap &&*/ // hence "N->quies" and threat category...)
                                             PN->threatEval == 0)
            N->ply--;
        else if (N->depth >= E->S.mainDepth)
        {
            N->quies = false;
            AnalyzeThreats(E);
            analyzed = true;
            if (N->totalEval - N->threatEval >= N->beta + 50)
                N->ply--, analyzed = (N->ply > 0);
        }
    }

    N->quies = (N->ply <= 0 && !N->check); // Is this a quiescence node?
    if (!analyzed)
        AnalyzeThreats(E); // Analyze threats.
    Evaluate(E);           // Compute static evalutation.

    // Safety depth limit to prevent stack overflow from unbounded check extensions
    if (N->depth >= maxSearchDepth - 2)
    {
        N->score = N->totalEval;
        goto exit;
    }

    if (N->bottomNode || (N->isMateDepth && !N->check))
    {
        N->score = N->totalEval; // Return if bottom node.
        goto exit;
    }

    if (N->quies) // Compute worst case evaluation.
    {
        if (N->drawType == drawType_None)
            N->score = N->totalEval - N->threatEval;
        if (N->score > N->alpha)
            if (N->score >= N->beta)
                goto exit;
            else
                N->alpha = N->score;
        N->alphaPly = N->betaPly = (E->P.selection ? 0 : 1000); // Always select in quiescence node.
    }
    else
    {
        if (N->drawType == drawType_None)
            N->score = N->loseVal;
        else if (drawVal > N->alpha) // In case of first repetition, give
            if (drawVal >= N->beta)
                goto exit; // program opportunity to improve the
            else
                N->alpha = drawVal; // draw score "drawVal".

        N->alphaPly = PN->betaPly - Min(PN->m.dply, 1);
        N->betaPly  = PN->alphaPly - PN->m.dply;
        if (PN->m.dply == 2 && (PN->m.cap || PN->m.type != mtype_Normal || N->threatEval > 0))
            N->betaPly++;
    }

    NN->beta    = -N->alpha;        // Set ß value at next ply.
    N->bufStart = E->S.bufTop;      // Remember old state of "SBuf".
    N->gen = N->bestGen = gen_None; // Reset current/best move generator.
    N->firstMove        = true;     // We are about to search first move
    N->canMove          = false;    // and none has been searched yet.
    N->cutoff           = false;
    PrepareKillers(E); // Prepare killer table.
    if (N->alphaPly <= 0)
        ComputeSelBaseVal(E); // Compute selective base value.

    // Being mated already reaches beta: exit (as a mate if in check).
    if (N->score >= N->beta)
    {
        if (N->check && PN->beta > -N->loseVal)
            PN->beta = -N->loseVal;
        goto node_store;
    }

/* - - - - - - - - - - - - - - - - - - - - Search Node - - - - - - - - - - - - - - - - - - */

// Exit after a cutoff (killers and table already updated) or when the score reaches beta.
#define CUTOFF_CHECK                                                                                                   \
    if (N->cutoff)                                                                                                     \
        goto node_exit;                                                                                                \
    if (N->score >= N->beta)                                                                                           \
    goto node_store

    if (!isNull(N->rfm)) // Search refutation move (if any).
    {
        SearchMoveC(E, N);
        CUTOFF_CHECK;
    }

    if (N->check) // --- CHECK EVASION ---
    {
        N->m.dply     = 0;
        N->storeSacri = true;         // Search and extend check evasion
        SearchCheckEvasion(E);        // moves (including sacrifices).
        if (N->score == N->loseVal && // Force cutoff at previous node in
            PN->beta > -N->loseVal)   // case of mate.
            PN->beta = -N->loseVal;
    }
    else
    {
        N->m.dply     = 1;               // Forced moves (dply = 0).
        N->storeSacri = (N->maxPly > 0); // Store sacrifices?
        SearchEnPriseCaptures(E);        // Search forced captures and queen
        CUTOFF_CHECK;
        N->m.dply = 0;       // Forced moves (dply = 0).
        SearchPromotions(E); // promotions.
        CUTOFF_CHECK;
        SearchRecaptures(E);
        CUTOFF_CHECK;
        N->m.dply = 1;         // Non-forced moves (dply = 1).
        SearchSafeCaptures(E); // Search non-forced, safe captures.
        CUTOFF_CHECK;

        if (!N->quies) // --- FULL WIDTH SEARCH ---
        {
            if (N->escapeSq != nullSq) // Search escapes.
            {
                if (N->totalEval < N->alpha - 30 ||              // Do not extend search, if escapes
                    N->totalEval - N->threatEval > N->beta + 30) // are not forced/interesting.
                    N->eply = 1;
                SearchEscapes(E);
                CUTOFF_CHECK;
            }

            N->m.dply = 1; // Castling (dply = 1):
            SearchCastling(E);
            CUTOFF_CHECK;
            N->m.dply = 2; // Quiet moves (dply = 2):
            SearchKillers(E);
            CUTOFF_CHECK;
            SearchNonCaptures(E);
            CUTOFF_CHECK;
            N->selMargin -= 50;  // Search sacrifices and punish use-
            SearchSacrifices(E); // less sacrifices during selection.

            if (!N->canMove)
                N->score = drawVal; // If we are stalemate return drawVal.
        }
        else if (N->maxPly > 0) // --- QUIESCENCE SEARCH (SHALLOW) ---
        {
            if (N->escapeSq != nullSq) // Search forced/interesting escapes.
            {
                if (N->totalEval < N->alpha - 30)
                    N->escapeSq = nullSq;
                else
                    N->storeSacri = false, // Don't search sacrifice escapes.
                        SearchEscapes(E);
                CUTOFF_CHECK;
            }

            SearchSafeChecks(E); // Search safe checks
            CUTOFF_CHECK;
            SearchFarPawns(E); // Pawn moves to the 6th or 7th rank.
            CUTOFF_CHECK;
            if (N->depth - E->S.mainDepth <= 1) // Search “shallow” sacrifices.
            {
                if (N->program)
                    N->selMargin -= 50; // Punish the program's sacrifices.
                SearchSacrifices(E);
            }
        }
        else // --- QUIESCENCE SEARCH (DEEP) ---
        {
            if (N->depth < E->S.checkDepth) //### Experiment
                SearchSafeChecks(E);
            //       SearchFarPawns(E);                               // Pawn moves to the 6th or 7th rank.
        }
    }

#undef CUTOFF_CHECK

    /* - - - - - - - - - - - - - - - - - - - - Exit Node - - - - - - - - - - - - - - - - - - - */
node_store:
    UpdateKillers(E); // Update killer table.
    if (N->score != 0)
        StoreTransTab(E); // Update transposition table.

node_exit:
    // E->S.genMoveCount += (E->S.bufTop - N->bufStart);
    E->S.bufTop = N->bufStart; // Restore old state of "SBuf".

exit:
    N->pvNode = false;     // This is no longer a PV node.
    PN->val   = -N->score; // "Return" score.
} /* SearchNodeC */

/**************************************************************************************************/
/*                                                                                                */
/*                                          SEARCH MOVE                                           */
/*                                                                                                */
/**************************************************************************************************/

/*----------------------------------------- Search Move ------------------------------------------*/
// This routine searches one move. Is called directly by the various move generator routines.
//
// Converted from PowerPC assembly wrapper. The original just passed global register values
// to SearchMoveC. In C, we get ENGINE from the search state.

void SearchMove(ENGINE *E)
{
    SearchMoveC(E, E->S.currNode);
} /* SearchMove */

static void SearchMoveC(register ENGINE *E, register NODE *N)
{
    E->S.moveCount++;

    /* - - - - - - - - - - - - - - - - Prepare Move Search - - - - - - - - - - - - - - - - - -*/

    if (N->firstMove && !isNull(N->rfm))
        if (!N->pvNode && !GetTransMove(E))
            return; // Retrieve refutation move
        else
            N->m = N->rfm;
    else if (KillerRefCollision(E)) // Handle killer/refutation move
        return;                     // collision.

    INT moveEval = EvalMove(E);                // Compute move evaluation (pvSum and mobility only).
    NN->ply      = N->ply - Min(N->m.dply, 1); // Decrement ply-counter at next node.

    // Check selection/forward pruning (Note: It's very important that we NEVER prune the moves
    // in the PV line (otherwise the NN->pvNode is not cleared properly).

    if (N->alphaPly <= 0 && N->depth >= 2 && !(N->pvNode && N->firstMove) && !SelectMove(E))
    {
        // Score a legal pruned move as at most alpha, so a node whose every move is pruned
        // doesn't return loseVal. Only king moves and pinnable pieces can be illegal.
        INT est      = -NN->totalEval;
        N->firstMove = false;
        if (!N->check && pieceType(N->m.piece) == king)
        {
            if (N->Attack_[N->m.to])
                return;
        }
        else if (N->check ||
                 ((N->Attack_[N->m.from] & qrbMask) && (E->Global->A.AttackDir[N->PieceLoc[0] - N->m.from] & qDirMask)))
        {
            PerformMove(E);
            BOOL legal = !N->Attack_[N->PieceLoc[0]];
            RetractMove(E);
            if (!legal)
                return;
        }
        if (est > N->alpha && N->alpha > mateLoseVal)
            est = N->alpha;
        if (est > N->score)
            N->score = est;
        N->canMove = true;
        return;
    }

    /* - - - - - - - - - - - - - - Perform, Search and Retract Move - - - - - - - - - - - - - */

    PerformMove(E); // Perform move.

    if (N->Attack_[N->PieceLoc[0]]) // Skip search if it's illegal.
    {
        RetractMove(E);
        N->firstMove = false;
        return;
    }

    N->canMove = true; // Strictly legal move -> Now we REALLY can move

    if (N->isMateDepth)     // If mate finder and the "loosing" side
    {                       // is not mate at the mate depth, exit
        N->score = N->beta; // and cutoff
    }
    else if (N->firstMove || !E->P.pvSearch || N->beta == N->alpha0 + 1 || !N->pvNode) // If first move or not PV node
    {
        NN->alpha0 = -N->beta; // search with full window...
        SearchNode(E);
        N->firstMove = false;
    }
    else // ...otherwise search with minimal
    {
        NN->alpha0 = -N->alpha - 1; // window and re-search if fail high.
        SearchNode(E);

        if (N->val > N->alpha) // Re-search from the same move evaluation.
        {
            NN->alpha0    = -N->beta;
            NN->ply       = N->ply - Min(N->m.dply, 1);
            NN->totalEval = moveEval;
            SearchNode(E);
        }
    }

    RetractMove(E); // Retract move.

    /* - - - - - - - - - - - - - - - - - - End Move Search - - - - - - - - - - - - - - - - - -*/

    if (N->val > N->score) // Update alpha/beta values and the
    {                      // best line.
        N->score   = N->val;
        N->bestGen = N->gen;
        UpdateBestLine(E);

        if (N->score >= N->beta) // If cutoff then return score and exit node:
        {
            UpdateKillers(E);          //    Update killer table.
            StoreTransTab(E);          //    Update transposition table.
            E->S.bufTop = N->bufStart; //    Restore old state of "SBuf".
            N->pvNode   = false;       //    This is no longer a PV node.
            PN->val     = -N->score;   //    "Return" score.
            N->cutoff   = true;

            // The asm long jumped out of SearchNodeC here; N->cutoff ends the node instead.
            return;
        }
        else if (N->score > N->alpha) // else if better than alpha simply update alpha and beta
        {
            N->alpha = N->score;
            NN->beta = -N->alpha;
        }

        if (N->pvNode && E->R.state == state_Running) // Update main value if this  is a PV node.
        {
            E->S.mainScore = (N->program ? N->score : -N->score);
        }
    }
} /* SearchMoveC */

/*----------------------------------------- Cut off Handling -------------------------------------*/
// NOTE: Cutoff handling has been simplified in the C version.
//
// In the original PowerPC assembly, cutoffs used a long jump (setjmp/longjmp style) to exit
// directly from SearchMoveC back to the exit point of SearchNodeC, bypassing the normal call
// stack. This was an optimization to avoid unwinding through the move generation recursion.
//
// In the C version, we use normal function returns instead. When a beta cutoff occurs in
// SearchMoveC (line ~391), we simply return, which allows the move generation loop in
// SearchNodeC to continue. The cleanup code that was at the __exit_node label in the assembly
// is now at the normal exit: label in SearchNodeC (line ~304), which gets executed whether
// we had a cutoff or not.
//
// This is slightly less optimal than the assembly longjmp approach, but it's much simpler,
// more portable, and the performance difference is negligible in modern C compilers.
