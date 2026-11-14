#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                     UCI BRIDGE - INTERFACE                                     */
/*                                                                                                */
/**************************************************************************************************/

/*
 * This module provides a bridge between the UCI (Universal Chess Interface) protocol
 * and the Sigma Chess engine's internal API. It handles:
 * - Parsing UCI move notation (e.g., "e2e4", "e7e8q")
 * - Formatting engine moves to UCI notation
 * - Managing position state (board, move history, draw detection)
 * - Setting up engine parameters from UCI commands
 */

#define MAX_MOVE_HISTORY 1024

/*----------------------------------- UCI Position State Structure -------------------------------*/

typedef struct
{
    PIECE  Board[boardSize];      // Current board position (0x88 representation)
    INT    HasMovedTo[boardSize]; // Castling rights tracking
    COLOUR player;                // Side to move

    MOVE     moveHistory[MAX_MOVE_HISTORY]; // Full move history for draw detection
    DRAWDATA drawData[MAX_MOVE_HISTORY];    // Hash keys and repetition data
    INT      moveCount;                     // Number of moves in history
    INT      plyCount;                      // Ply count (half-moves from start)
    INT      fenHalfmoves;                  // Halfmove clock of the FEN (0 for startpos)

    // Optional GLOBAL pointer for hash-key computation. When set, UCI_ApplyMove
    // and the setup functions populate drawData[].hashKey with proper Zobrist
    // keys, enabling repetition detection and correct TT keying. NULL = legacy
    // mode (hashKey left at 0 — used by tests that don't need draw detection).
    GLOBAL *Global;
} UCI_State;

/*----------------------------------- Square/Piece String Utilities ------------------------------*/

// Convert square enum to UCI notation string (e.g., 0x11 -> "e2")
void UCI_SquareToString(SQUARE sq, char *str);

// Parse UCI square notation to square enum (e.g., "e2" -> 0x11)
// Returns true on success, false on parse error
BOOL UCI_ParseSquare(const char *str, SQUARE *sq);

// Convert piece to character (wPawn -> 'P', bKnight -> 'n', etc.)
char UCI_PieceToChar(PIECE p);

// Parse piece character to piece enum ('P' -> wPawn, 'n' -> bKnight, etc.)
// Returns empty if invalid
PIECE UCI_CharToPiece(char c);

/*----------------------------------- Move Formatting/Parsing ------------------------------------*/

// Format a MOVE struct to UCI notation (e.g., "e2e4", "e7e8q", "e1g1")
void UCI_FormatMove(const MOVE *m, char *str);

// Parse UCI move notation to a MOVE struct (pure string parsing, no validation)
// Returns true on successful parse, false on syntax error
// This does NOT validate legality - just parses the string format
// Example: "e2e4" -> from=0x14, to=0x34, "e7e8q" -> promotion to queen
BOOL UCI_ParseMove(UCI_State *state, const char *str, MOVE *m);

/*----------------------------------- Position Setup/Management ----------------------------------*/

// Bind a GLOBAL for hash-key computation. Call once after Engine_InitSystem
// before any UCI_SetupStartPosition / UCI_ApplyMove call that needs proper
// Zobrist keys (i.e. anything past test code).
void UCI_BindGlobal(UCI_State *state, GLOBAL *G);

// Initialize UCI_State to starting position
void UCI_SetupStartPosition(UCI_State *state);

// Setup UCI_State from FEN string
// Returns true on success, false on parse error
BOOL UCI_SetupFromFEN(UCI_State *state, const char *fen);

// Convert UCI_State to FEN string (for debugging)
// Buffer should be at least 128 bytes
void UCI_StateToFEN(const UCI_State *state, char *fen);

// Clear/reset the UCI_State
void UCI_ClearState(UCI_State *state);

// Apply a move to the UCI_State (updates Board, moveHistory, drawData, etc.)
// This does NOT validate legality - move must be legal!
void UCI_ApplyMove(UCI_State *state, const MOVE *m);

// Setup engine PARAM structure from UCI_State
void UCI_StateToParam(const UCI_State *state, PARAM *param);

/*----------------------------------- Engine Control Helpers -------------------------------------*/

// Extract best move from engine after search completes
// Returns false if no move found
BOOL UCI_GetBestMove(ENGINE *E, MOVE *bestMove);

/*------------------------------------- Debug / Verification -------------------------------------*/

// Verify incremental attack state matches full CalcAttackState recalculation.
// Prints mismatches to stderr. Returns number of mismatched squares.
INT UCI_VerifyAttacks(ENGINE *E, UCI_State *state);

// Compute and print the static evaluation for the current UCI position.
// Output format (single line, parseable):
//   eval total <T> pvSum <P> mob <M> pawnStruct <S> endGame <E> player <p>
// All values are in centipawns (white-positive). The total is reported in the
// side-to-move's perspective (UCI convention).
void UCI_DumpEval(ENGINE *E, UCI_State *state);

/*------------------------------------- Mid-Search Input Polling --------------------------------*/

// Non-blocking poll of stdin for commands the GUI may send while a search is running
// (stop, quit, isready, ponderhit). Called from Engine_Periodic; safe to call frequently.
void UCI_PollInput(ENGINE *E);

/*------------------------------------- UCI Info Output ------------------------------------------*/

// Output UCI "info" line with current search state (depth, score, pv, nodes, time, nps).
void UCI_SendInfo(ENGINE *E);

// Emit "info currmove ... currmovenumber ..." for the root move currently being
// searched. Throttled internally to avoid flooding at low depths / fast moves.
void UCI_SendCurrMove(ENGINE *E);

/*----------------------------------- Perft (Move Generation Testing) ---------------------------*/

// Run perft to the given depth using PerformMove/RetractMove (incremental updates).
// Prints per-move breakdown and total. Returns total leaf node count.
LONG64 UCI_Perft(ENGINE *E, UCI_State *state, INT depth);
LONG64 UCI_PerftIncremental(ENGINE *E, UCI_State *state, INT depth);
