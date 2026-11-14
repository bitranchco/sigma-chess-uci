/**************************************************************************************************/
/*                                                                                                */
/* Module  : ENGINE.C                                                                             */
/* Purpose : This is the main Sigma Chess Engine module which provides routines for initializing, */
/*           starting and stopping the engine. The Engine Interface and the GUI modules deal with */
/*           this module only, and never with the other Engine Modules.                           */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"

#include "Engine.f"
#include "Board.f"
#include "Attack.f"
#include "MoveGen.f"
#include "PerformMove.f"
#include "Evaluate.f"
#include "PieceVal.f"
#include "Search.f"
#include "HashCode.f"
#include "uci_bridge.h"
#include "Time.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                      SIGMA CHESS Engine 3.00                                   */
/*                                                                                                */
/**************************************************************************************************/

// This is the main Sigma Chess engine interface module. It implements the API through which the
// UCI front-end drives the engine. All engine state (excluding the transposition table) lives in
// a single ENGINE struct, which the caller allocates and owns.

/**************************************************************************************************/
/*                                                                                                */
/*                                    ENGINE SYSTEM INITIALIZATION                                */
/*                                                                                                */
/**************************************************************************************************/

/*------------------------------------- Initialize Engine System ---------------------------------*/
// At startup the common "Global" engine data structure is initialized. However, allocation of
// this structure is left to the calling host application, in order to keep all memory allocation
// and global data structure definitions outside the engine.

void Engine_InitSystem(GLOBAL *Global, PTR kpkData)
{
    // Engine modules MUST be initialized in this order:
    InitBoardModule(Global);
    InitAttackModule(Global);
    InitMoveGenModule(Global);
    InitPerformMoveModule(Global);
    InitPieceValModule(Global);
    InitEvaluateModule(Global, kpkData);
    InitSearchModule(Global);
    InitHashCodeModule(Global);
} /* Engine_InitSystem */

/**************************************************************************************************/
/*                                                                                                */
/*                                      CREATE/DISPOSE ENGINE                                     */
/*                                                                                                */
/**************************************************************************************************/

/*----------------------------------------- Create Engine ----------------------------------------*/
// The engine struct must already have been allocated by the caller.

static void InitSearchParam(PARAM *P);

BOOL Engine_Create(GLOBAL *Global, ENGINE *E)
{
    if (!E)
        return false;

    E->Global = Global;

    E->R.taskID      = 0;
    E->R.taskRunning = false;
    E->R.state       = state_Stopped;

    InitSearchParam(&E->P);
    InitBoardState(&E->B);
    InitSearchState(E);

    return true;
} /* Engine_Create */

static void InitSearchParam(PARAM *P) // Initializes the search parameters with some happy
{                                     // defaults.
    P->backgrounding = false;

    // Set default engine search/eval parameters:
    P->pvSearch      = true;
    P->extensions    = true;
    P->selection     = true;
    P->deepSelection = false;
    P->nondeterm     = false;
    P->aspiration    = true;
    P->tbProbeDepth  = 1;
    P->tbRule50      = true;
    P->tbProbeLimit  = 7;

    // Mode/Level/Style parameters:
    P->playingMode    = mode_FixDepth; // Playing mode.
    P->movesLeft      = allMoves;      // Moves left to next time control.
    P->timeLeft       = 300000;        // Time left in ms to next time control (if time mode).
    P->timeInc        = 0;
    P->depth          = 1; // Depth if fixed depth or mate finder.
    P->playingStyle   = style_Normal;
    P->multiPVCount   = 1;
    P->reduceStrength = false;
    P->permanentBrain = false; // Driven by UCI Ponder option; affects AdjustELO only.
    P->engineELO      = 2400;

    P->searchMoveCount = 0; // Search all root moves.

    // Transposition Tables:
    P->TransTables = nil; // Transposition table memory block.
    P->transSize   = 0;
} /* InitSearchParam */

/*----------------------------------------- Destroy Engine ---------------------------------------*/
// Note: It's the caller's responsibility to deallocate the engine struct AFTER calling
// Engine_Destroy.

void Engine_Destroy(ENGINE *E)
{
    if (E->R.taskRunning)
        Engine_Abort(E);
} /* Engine_Destroy */

/**************************************************************************************************/
/*                                                                                                */
/*                                       START/STOP ENGINE                                        */
/*                                                                                                */
/**************************************************************************************************/

/*----------------------------------------- Start Engine -----------------------------------------*/
// This routine starts the engine, i.e. the engine starts analyzing the specified position
// given the specified search constraints/parameters (the PARAM record in the ENGINE data).

void Engine_Start(ENGINE *E)
{
    E->R.taskRunning = true;
    MainSearch(E);
} /* Engine_Start */

/*--------------------------------------- Stop/Abort Engine --------------------------------------*/

void Engine_Stop(ENGINE *E)
{
    if (E->R.state != state_Stopped)
        E->R.state = state_Stopping;

    E->R.aborted = false;
} /* Engine_Stop */

void Engine_Abort(ENGINE *E)
{
    E->R.state       = state_Stopped;
    E->R.aborted     = true;
    E->R.taskRunning = false;
} /* Engine_Abort */

/**************************************************************************************************/
/*                                                                                                */
/*                                               MISC                                             */
/*                                                                                                */
/**************************************************************************************************/

// Called every ~256 nodes from SearchNodeC (the periodicCounter gate there is a cheap node-count
// shortcut), and acts at most every 5 ms. The original polled every 333 ms and first after
// 167 ms (a converted 10-tick Mac delay), which made every shorter move overrun. Now() is cheap
// (~15 ns on Apple Silicon, ~155 ns on PPC Tiger); the original "VERY expensive" warning dated
// from classic Mac OS and never held here.

void Engine_Periodic(ENGINE *E)
{
    if (Now() < E->S.periodicTime)
        return;

    UCI_PollInput(E); // Honor stop/quit/isready that arrive during the search.

    if (E->Tr.transSize > 0)
        E->S.hashFull = (10 * E->Tr.transUsed) / MaxL(1, E->Tr.transSize / 100);

    if (TimeOut(E) && E->R.state == state_Running)
        Engine_Stop(E);

    E->S.periodicTime = Now() + 5; // Was 333ms; needs to be << subsecond budgets so TimeOut fires.
} /* Engine_Periodic */

/**************************************************************************************************/
/*                                                                                                */
/*                                      PPC ASSEMBLY WRAPPERS                                     */
/*                                                                                                */
/**************************************************************************************************/

// Many engine routines are written in assembly language only. These routines all expect the
// Note: Asm_Begin/Asm_End have been fully removed as all assembly has been converted to C
// All ENGINE* parameters are now passed explicitly through function signatures
