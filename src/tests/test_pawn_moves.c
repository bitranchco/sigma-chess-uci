/**************************************************************************************************/
/*                                                                                                */
/* Module  : TEST_PAWN_MOVES.C                                                                   */
/* Purpose : Unit tests for pawn move execution                                                  */
/*                                                                                                */
/**************************************************************************************************/

#include "../Engine.h"
#include "../Move Generation/PerformMove.f"
#include "test_helpers.h"

// Test simple white pawn move forward one square
TEST(perform_white_pawn_simple_move)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Place white pawn on e2
    E.B.Board[e2]             = wPawn;
    E.B.PLinx[e2]             = 0; // First piece
    E.S.currNode->PieceLoc[0] = e2;

    // Setup move: e2-e3
    E.S.currNode->m.from  = e2;
    E.S.currNode->m.to    = e3;
    E.S.currNode->m.piece = wPawn;
    E.S.currNode->m.cap   = empty;
    E.S.currNode->m.type  = mtype_Normal;
    E.S.currNode->player  = white;
    E.S.currNode->mobEval = 0;

    // Perform the move
    PerformMove(&E);

    // Verify board state
    ASSERT(E.B.Board[e2] == empty, "e2 should be empty after move");
    ASSERT(E.B.Board[e3] == wPawn, "e3 should have white pawn");
    ASSERT(E.B.PLinx[e3] == 0, "PLinx should be updated");
    ASSERT(E.S.currNode->PieceLoc[0] == e3, "PieceLoc should point to new square");
}

// Test simple black pawn move forward one square
TEST(perform_black_pawn_simple_move)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Place black pawn on e7
    E.B.Board[e7]              = bPawn;
    E.B.PLinx[e7]              = 0;
    E.S.currNode->PieceLoc_[0] = e7;

    // Setup move: e7-e6
    E.S.currNode->m.from  = e7;
    E.S.currNode->m.to    = e6;
    E.S.currNode->m.piece = bPawn;
    E.S.currNode->m.cap   = empty;
    E.S.currNode->m.type  = mtype_Normal;
    E.S.currNode->player  = black;
    E.S.currNode->mobEval = 0;

    // Perform the move
    PerformMove(&E);

    // Verify board state
    ASSERT(E.B.Board[e7] == empty, "e7 should be empty after move");
    ASSERT(E.B.Board[e6] == bPawn, "e6 should have black pawn");
}

// Run all pawn move tests
void run_pawn_move_tests()
{
    printf("\nRunning Pawn Move Tests:\n");

    RUN_TEST(perform_white_pawn_simple_move);
    RUN_TEST(perform_black_pawn_simple_move);
}
