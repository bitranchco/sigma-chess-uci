#pragma once

#undef check // Needed because this macro is used in Carbon headers!!

#include "General.h"
#include "Board.h"
#include "Move.h"
#include "HashCode.h"
#include "TransTables.h"
#include "Attack.h"
#include "Search.h"
#include "MoveGen.h"
#include "PerformMove.h"
#include "Evaluate.h"
#include "PieceVal.h"
#include "Time.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                       CONSTANTS & MACROS                                       */
/*                                                                                                */
/**************************************************************************************************/

/*--------------------------------------- Engine Run State ---------------------------------------*/

enum ENGINE_STATE
{
    state_Stopped  = 0,
    state_Root     = 1,
    state_Running  = 2,
    state_Stopping = 3
};

/*---------------------------------------- Runtime Flags -----------------------------------------*/

enum ENGINE_RFLAGS // Engine run flags stored in the "rFlags" register during search.
{
    rflag_RunState       = 0x0003, // Bits 0..1 : (ENGINE_STATE: Stopped, root, running, stopping)
    rflag_PVSearch       = 0x0004, // Bit  2    : Principal variation search?
    rflag_Extensions     = 0x0008, // Bit  3    : Apply depth extensions for forced/dangerous moves?
    rflag_Selection      = 0x0010, // Bit  4    : Apply selection of "poor" moves?
    rflag_DeepSel        = 0x0020, // Bit  5    : Start selection earlier (### determined at root?).
    rflag_ReduceStrength = 0x0040, // Bit  6    : Reduce playing strength?
    rflag_TransTabOn     = 0x0080  // Bit  7    : Are transposition tables on?
};

/*----------------------------------------- Score Types ------------------------------------------*/

enum ENGINE_SCORETYPE // Engine score types for E->S.bestScore:
{
    scoreType_True,       // S->bestScore is a true score (i.e. inside alpha-beta window)
    scoreType_LowerBound, // real score >= S->bestScore (fail-high; UCI "lowerbound")
    scoreType_UpperBound, // real score <= S->bestScore (fail-low;  UCI "upperbound")
    scoreType_Temp,       // S->bestScore is a temporary score (returned from PV line at non root node)
    scoreType_Unknown     // S->bestScore is a useless value which should not be displayed
};

/*------------------------------------ Playing Modes/Styles --------------------------------------*/

enum ENGINE_PLAYING_MODE
{
    mode_Time     = 1,
    mode_FixDepth = 2,
    mode_Infinite = 3,
    mode_Mate     = 5
};

enum ENGINE_PLAYING_STYLE
{
    style_Chicken    = 1,
    style_Defensive  = 2,
    style_Normal     = 3,
    style_Aggressive = 4,
    style_Desperado  = 5
};

#define allMoves 10000

/**************************************************************************************************/
/*                                                                                                */
/*                                        TYPE DEFINITIONS                                        */
/*                                                                                                */
/**************************************************************************************************/

/*------------------------------ The Search Parameters Data Structure ----------------------------*/
// Once the engine has been created (via Engine_Create()), all that is needed to start the
// engine is to initialize the PARAM structure below and then call Engine_Start.

typedef struct
{
    //--- Game state ---
    PIECE     Board[boardSize];      // Current board configuration of game (CGame->Board).
    INT       HasMovedTo[boardSize]; // Castling detection (CGame->HasMovedTo).
    COLOUR    player;                // Side to start in this position (CGame->player).
    MOVE      lastMove;              // Last move played (CGame->Record[currMove]).
    INT       lastMoveNo;            // Half moves played from initial position (CGame->currMove).
    INT       fenHalfmoves;          // Halfmove clock of the setup FEN (adds to the 50-move count).
    DRAWDATA *DrawData;              // Draw information table (CGame->DrawData).

    //--- Analysis Category ---
    BOOL backgrounding; // Is this a background analysis (in opponents time)?

    //--- Search/eval parameters ---
    BOOL pvSearch;      // Principal variation search?
    BOOL extensions;    // Apply depth extensions for forced/dangerous moves?
    BOOL selection;     // Apply selection of "poor" moves?
    BOOL deepSelection; // Start selection earlier.
    BOOL nondeterm;     // Non-deterministic (i.e. add small random value)?
    BOOL aspiration;    // Use aspiration windows at root.

    //--- Syzygy tablebases ---
    INT  tbProbeDepth; // Minimum remaining depth for in-search probes.
    BOOL tbRule50;     // Count cursed wins / blessed losses as draws.
    INT  tbProbeLimit; // Probe positions with at most this many pieces.

    //--- Mode/Level/Style parameters ---
    INT  playingMode;    // Playing mode.
    INT  movesLeft;      // Moves left to next time control.
    LONG timeLeft;       // Time left in milliseconds to next time control (if time mode).
    LONG timeInc;        // Time increment (in ms) per move if Fischer. 0 otherwise.
    LONG fixedTimeMs;    // If > 0 (mode_Time only): use exactly this budget per move
                         // (in milliseconds) and skip AllocateTime's clock math.
                         // Used by UCI "go movetime".
    LONG64 nodeLimit;    // If > 0: stop once S.moveCount reaches this (UCI "go nodes").
    INT    depth;        // Depth if fixed depth or mate finder.
    INT    playingStyle; // The playing style.
    INT    multiPVCount; // UCI MultiPV (1 = single PV; 2..maxMultiPV = analysis).

    INT  searchMoveCount; // If > 0: search only these root moves (UCI "go searchmoves").
    MOVE searchMoves[maxLegalMoves];

    //--- Mode/Level/Style parameters ---
    BOOL reduceStrength;  // Reduce strength of engine (ie reduce nps)?
    BOOL permanentBrain;  // Used by the ELO to NPS calculation.
    INT  engineELO;       // Engine "target" ELO if "reduceStrength".
    INT  actualEngineELO; // The actual/achieved strength during the search.

    //--- Transposition Tables ---
    TRANS *TransTables; // Transposition table buffer (nil if disabled).
    LONG   transSize;   // Size in bytes of transposition tables (0 if disabled).
} PARAM;

typedef struct
{
    LONG taskID;
    BOOL taskRunning;
    INT  state;   // Stopped, root, running, stopping, paused
    BOOL aborted; // Was search aborted?

    ULONG rflags; // Global flags stored in the rFlags register for faster access.
} RUN_STATE;

/*----------------------------------- Main ENGINE Data Structure ---------------------------------*/
// The ENGINE data structure contains all information about the state of an engine, such as search
// parameters, board state, attack state, search nodes state etc.

typedef struct
{
    struct _GLOBAL *Global; // Pointer to global struct (so we only need to pass engine struct to
                            // the various routines).
    PARAM     P;            // Engine search parameters (level/time/mode, flags...)
    RUN_STATE R;            // Engine run state information.

    BOARD_STATE    B;  // Board state. Incrementally updated during search.
    ATTACK_STATE   A;  // Attack state. Incrementally update during search.
    EVAL_STATE     E;  // Evaluate State.
    PIECEVAL_STATE V;  // Piece value tables e.t.c. for root position.
    TIME_STATE     T;  // Time allocation state.
    TRANS_STATE    Tr; // Transposition tables.
    SEARCH_STATE   S;  // Nodes of current branch in search tree.

    CHAR debugStr[1000];
} ENGINE;

/*------------------------------------ Global COMMON Data Structure ------------------------------*/
// The global common structure contains various read-only utility data shared by the engine
// (movegen tables, eval tables, Zobrist keys, etc.).

typedef struct _GLOBAL
{
    BOARD_COMMON       B;
    ATTACK_COMMON      A;
    MOVEGEN_COMMON     M;
    PERFORMMOVE_COMMON P;
    HASHCODE_COMMON    H;
    PIECEVAL_COMMON    V;
    EVAL_COMMON        E; // Must be last (because of KPKData and 32K limitation)
} GLOBAL;

/**************************************************************************************************/
/*                                                                                                */
/*                                    FUNCTION DECLARATIONS                                       */
/*                                                                                                */
/**************************************************************************************************/

// Include function prototypes
#include "Engine.f"
