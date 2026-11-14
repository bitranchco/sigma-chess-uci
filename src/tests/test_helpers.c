/**************************************************************************************************/
/*                                                                                                */
/* Module  : TEST_HELPERS.C                                                                       */
/* Purpose : Helper functions for testing the Chess Engine                                       */
/*                                                                                                */
/**************************************************************************************************/

#include "../Engine.h"
#include "test_helpers.h"
#include "../Data Structures/Board.f"
#include "../Data Structures/Attack.f"
#include "../Move Generation/MoveGen.f"
#include "../Evaluation/Evaluate.f"

// Test statistics
int tests_passed = 0;
int tests_failed = 0;

// Initialize a minimal GLOBAL structure for testing
void InitTestGlobal(GLOBAL *Global)
{
    memset(Global, 0, sizeof(GLOBAL));

    // Initialize all required modules (order matters!)
    InitBoardModule(Global);       // Must be first
    InitAttackModule(Global);      // Must be after Board
    InitMoveGenModule(Global);     // Must be after Attack and Board
    InitEvaluateModule(Global, 0); // Initialize evaluation tables (kpkData=NULL for tests)
    // Add other Init calls as needed
}

// Initialize a minimal ENGINE structure for testing
void InitTestEngine(ENGINE *E, GLOBAL *Global)
{
    memset(E, 0, sizeof(ENGINE));

    E->Global = Global;

    // Initialize board state
    InitBoardState(&E->B);

    // Set up nodes for testing (need at least 3: parent, current, next)
    // Start at testNodes[1] so N-1 (parent) is valid
    static NODE testNodes[10]; // Array of nodes for test moves
    memset(testNodes, 0, sizeof(testNodes));
    E->S.rootNode = &testNodes[1]; // Root at index 1 (index 0 is for parent)
    E->S.currNode = &testNodes[1]; // Current also at index 1

    // Initialize node pointers to point to board arrays
    for (int i = 0; i < 10; i++)
    {
        testNodes[i].PieceLoc  = E->B.PieceLocW;
        testNodes[i].PieceLoc_ = E->B.PieceLocB;
        testNodes[i].Attack    = E->A.AttackW;
        testNodes[i].Attack_   = E->A.AttackB;
    }
}

// Set up an empty board (edges only)
void SetupEmptyBoard(ENGINE *E)
{
    // Board already has edges from InitBoardState
    // Just clear the playable squares
    for (SQUARE sq = a1; sq <= h8; sq++)
    {
        if (onBoard(sq))
        {
            E->B.Board[sq] = empty;
        }
    }
    E->B.player     = white;
    E->B.pieceCount = 0;
}

// Set up initial chess position
void SetupInitialPosition(ENGINE *E)
{
    SetupEmptyBoard(E);

    // White pieces
    E->B.Board[a1] = wRook;
    E->B.Board[b1] = wKnight;
    E->B.Board[c1] = wBishop;
    E->B.Board[d1] = wQueen;
    E->B.Board[e1] = wKing;
    E->B.Board[f1] = wBishop;
    E->B.Board[g1] = wKnight;
    E->B.Board[h1] = wRook;

    for (SQUARE sq = a2; sq <= h2; sq++)
    {
        E->B.Board[sq] = wPawn;
    }

    // Black pieces
    E->B.Board[a8] = bRook;
    E->B.Board[b8] = bKnight;
    E->B.Board[c8] = bBishop;
    E->B.Board[d8] = bQueen;
    E->B.Board[e8] = bKing;
    E->B.Board[f8] = bBishop;
    E->B.Board[g8] = bKnight;
    E->B.Board[h8] = bRook;

    for (SQUARE sq = a7; sq <= h7; sq++)
    {
        E->B.Board[sq] = bPawn;
    }

    E->B.player = white;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   TEST FIXTURE FUNCTIONS                                       */
/*                                                                                                */
/**************************************************************************************************/

// Helper to place a piece and properly initialize all data structures
void PlacePiece(ENGINE *E, SQUARE sq, PIECE piece, int plinx)
{
    COLOUR color = pieceColour(piece);

    // Set the piece on the board
    E->B.Board[sq] = piece;

    // Update PieceLoc array
    if (color == white)
    {
        E->B.PieceLocW[plinx] = sq;
    }
    else
    {
        E->B.PieceLocB[plinx] = sq;
    }

    // Update PLinx (reverse mapping from square to PieceLoc index)
    E->B.PLinx[sq] = plinx;

    // Update pieceCount
    E->B.pieceCount += E->Global->B.PieceCountBit[piece];
}

// Setup a minimal legal position (just kings)
void SetupKingsOnly(ENGINE *E)
{
    SetupEmptyBoard(E);

    // White king on e1
    PlacePiece(E, e1, wKing, 0);
    E->B.PieceLocW[1] = nullSq; // Terminate list

    // Black king on e8
    PlacePiece(E, e8, bKing, 0);
    E->B.PieceLocB[1] = nullSq; // Terminate list

    // Calculate attack tables
    CalcAttackState(E);
}

// Setup position with kings and one white piece
void SetupWhitePiecePosition(ENGINE *E, SQUARE sq, PIECE piece)
{
    SetupEmptyBoard(E);

    // Kings
    PlacePiece(E, e1, wKing, 0);
    PlacePiece(E, e8, bKing, 0);

    // White piece
    PlacePiece(E, sq, piece, 1);
    E->B.PieceLocW[2] = nullSq; // Terminate list

    E->B.PieceLocB[1] = nullSq; // Terminate black list

    // Calculate attack tables
    CalcAttackState(E);
}

// Setup position with kings and one black piece
void SetupBlackPiecePosition(ENGINE *E, SQUARE sq, PIECE piece)
{
    SetupEmptyBoard(E);

    // Kings
    PlacePiece(E, e1, wKing, 0);
    PlacePiece(E, e8, bKing, 0);

    // Black piece
    PlacePiece(E, sq, piece, 1);
    E->B.PieceLocB[2] = nullSq; // Terminate list

    E->B.PieceLocW[1] = nullSq; // Terminate white list

    // Calculate attack tables
    CalcAttackState(E);
}

// Setup NODE state for move generation testing
// attackedPieces: null-terminated array of squares with attacked pieces (or NULL)
// safePieces: null-terminated array of squares with safe pieces (or NULL)
void SetupMoveGenTest(ENGINE *E, COLOUR player, SQUARE *attackedPieces, SQUARE *safePieces)
{
    NODE *N = E->S.currNode;

    // Setup player
    N->player   = player;
    N->opponent = player ^ black;
    N->pawnDir  = (player == white) ? 0x10 : -0x10;

    // Setup PieceLoc pointers
    if (player == white)
    {
        N->PieceLoc  = E->B.PieceLocW;
        N->PieceLoc_ = E->B.PieceLocB;
        N->Attack    = E->A.AttackW;
        N->Attack_   = E->A.AttackB;
    }
    else
    {
        N->PieceLoc  = E->B.PieceLocB;
        N->PieceLoc_ = E->B.PieceLocW;
        N->Attack    = E->A.AttackB;
        N->Attack_   = E->A.AttackW;
    }

    // Setup ALoc (attacked pieces)
    int i = 0;
    if (attackedPieces != NULL)
    {
        while (attackedPieces[i] != nullSq && i < 32)
        {
            N->ALoc[i] = attackedPieces[i];
            i++;
        }
    }
    N->ALoc[i] = nullSq; // Terminate

    // Setup SLoc (safe pieces)
    i = 0;
    if (safePieces != NULL)
    {
        while (safePieces[i] != nullSq && i < 32)
        {
            N->SLoc[i] = safePieces[i];
            i++;
        }
    }
    N->SLoc[i] = nullSq; // Terminate

    // Setup other NODE fields commonly needed
    N->check         = false;
    N->recapSq       = nullSq;
    N->escapeSq      = nullSq;
    N->killer1Active = false;
    N->killer2Active = false;
    N->storeSacri    = true; // Enable sacrifice storage by default

    // Initialize sacrifice buffer
    E->S.bufTop = E->S.SBuf;
    N->bufStart = E->S.SBuf;

    // Count opponent pieces for move generation
    N->lastPiece_ = 0;
    for (int j = 1; j < 16; j++)
    {
        if (N->PieceLoc_[j] >= a1)
        {
            N->lastPiece_ = j;
        }
        else
        {
            break;
        }
    }
}

// Setup for sacrifice buffer testing
void SetupSacrificeTest(ENGINE *E)
{
    // Reset sacrifice buffer
    E->S.bufTop               = E->S.SBuf;
    E->S.currNode->bufStart   = E->S.SBuf;
    E->S.currNode->storeSacri = true;
}
