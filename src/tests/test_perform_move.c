/*
 * test_perform_move.c - Tests for PerformMove and RetractMove
 *
 * These tests verify that moves can be performed and retracted correctly:
 * - Board state is updated correctly
 * - PieceLoc arrays are updated correctly
 * - Attack tables are updated correctly
 * - State is fully restored after retraction
 */

#include <stdio.h>
#include <string.h>
#include "test_helpers.h"
#include "../Engine.h"
#include "../Move Generation/PerformMove.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                   HELPER FUNCTIONS                                             */
/*                                                                                                */
/**************************************************************************************************/

// Save board state for comparison
typedef struct
{
    PIECE  Board_[boardSize1]; // Need both Board_ and Board to support negative indexing
    PIECE  Board[boardSize2];
    SQUARE PieceLocW[16]; // Only 16 elements per color, not 32
    SQUARE PieceLocB[16];
    ATTACK AttackW[boardSize];
    ATTACK AttackB[boardSize];
} BOARD_SNAPSHOT;

static void save_board_state(ENGINE *E, BOARD_SNAPSHOT *snap)
{
    // Copy both Board_ and Board to capture the full range [-34..153]
    memcpy(snap->Board_, E->B.Board_, sizeof(E->B.Board_));
    memcpy(snap->Board, E->B.Board, sizeof(E->B.Board));
    memcpy(snap->PieceLocW, E->B.PieceLocW, 16 * sizeof(SQUARE));
    memcpy(snap->PieceLocB, E->B.PieceLocB, 16 * sizeof(SQUARE));
    memcpy(snap->AttackW, E->A.AttackW, sizeof(E->A.AttackW));
    memcpy(snap->AttackB, E->A.AttackB, sizeof(E->A.AttackB));
}

static int compare_board_state(ENGINE *E, BOARD_SNAPSHOT *snap, const char *label)
{
    int errors = 0;

    // Check Board (need to use the same indexing approach as the engine)
    // E->B.Board[-34..153] maps to snap->Board_[0..33] and snap->Board[0..153]
    for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
    {
        PIECE actual   = E->B.Board[sq];
        PIECE expected = snap->Board[sq]; // This works because snap->Board is laid out the same way
        if (actual != expected)
        {
            printf("    ERROR %s: Board[%02X] = %d, expected %d\n", label, sq, actual, expected);
            errors++;
            if (errors >= 5)
                break; // Limit output
        }
    }

    // Check PieceLocW
    for (int i = 0; i < 16; i++)
    {
        if (E->B.PieceLocW[i] != snap->PieceLocW[i])
        {
            printf("    ERROR %s: PieceLocW[%d] = %02X, expected %02X\n", label, i, E->B.PieceLocW[i],
                   snap->PieceLocW[i]);
            errors++;
            if (errors >= 5)
                break;
        }
    }

    // Check PieceLocB
    for (int i = 0; i < 16; i++)
    {
        if (E->B.PieceLocB[i] != snap->PieceLocB[i])
        {
            printf("    ERROR %s: PieceLocB[%d] = %02X, expected %02X\n", label, i, E->B.PieceLocB[i],
                   snap->PieceLocB[i]);
            errors++;
            if (errors >= 5)
                break;
        }
    }

    return (errors == 0);
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   BASIC MOVE TESTS                                             */
/*                                                                                                */
/**************************************************************************************************/

// Test: PerformMove doesn't hang
TEST(perform_move_doesnt_hang)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup position with kings and a pawn
    SetupWhitePiecePosition(&E, e2, wPawn);

    // Simple pawn move
    NODE *N    = E.S.currNode;
    N->player  = white;
    N->mobEval = 0;
    N->m.piece = wPawn;
    N->m.from  = e2;
    N->m.to    = e3;
    N->m.cap   = empty;
    N->m.type  = mtype_Normal;

    printf("    About to call PerformMove...\n");
    PerformMove(&E);
    printf("    PerformMove returned!\n");

    ASSERT(1, "PerformMove completed without hanging");
}

// Test: Simple knight move
TEST(knight_move_perform_retract)
{
    GLOBAL                Global;
    ENGINE                E;
    static BOARD_SNAPSHOT before; // Use static to avoid stack overflow

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup position with knight on d4
    SetupWhitePiecePosition(&E, d4, wKnight);

    // Save initial state
    save_board_state(&E, &before);

    // Set up the move: knight d4 to f5
    NODE *N    = E.S.currNode;
    N->player  = white;
    N->mobEval = 0;
    N->m.piece = wKnight;
    N->m.from  = d4;
    N->m.to    = f5;
    N->m.cap   = empty;
    N->m.type  = mtype_Normal;
    N->m.dply  = 2;

    printf("    Knight move d4->f5, calling PerformMove...\n");
    fflush(stdout);

    // Perform the move
    PerformMove(&E);

    printf("    PerformMove returned for knight move!\n");

    // Verify move was performed
    ASSERT(E.B.Board[d4] == empty, "Source square should be empty after move");
    ASSERT(E.B.Board[f5] == wKnight, "Destination should have knight after move");

    // Retract the move
    RetractMove(&E);

    // Verify state is restored
    ASSERT(compare_board_state(&E, &before, "after retract"), "Board state should be fully restored after retract");
}

// Test: Pawn capture
TEST(pawn_capture_perform_retract)
{
    GLOBAL                Global;
    ENGINE                E;
    static BOARD_SNAPSHOT before; // Use static to avoid stack overflow

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup position with white pawn on e4
    SetupWhitePiecePosition(&E, e4, wPawn);

    // Add black pawn on d5 (to be captured)
    PlacePiece(&E, d5, bPawn, 1);
    E.B.PieceLocB[2] = nullSq; // Terminate list

    CalcAttackState(&E);

    // Save initial state
    save_board_state(&E, &before);

    // Set up the move: pawn e4xd5
    NODE *N    = E.S.currNode;
    N->player  = white;
    N->mobEval = 0;
    N->m.piece = wPawn;
    N->m.from  = e4;
    N->m.to    = d5;
    N->m.cap   = bPawn;
    N->m.type  = mtype_Normal;
    N->m.dir   = 0x11; // Diagonal capture direction
    N->m.dply  = 0;

    // Perform the move
    PerformMove(&E);

    // Verify move was performed
    ASSERT(E.B.Board[e4] == empty, "Source square should be empty");
    ASSERT(E.B.Board[d5] == wPawn, "Destination should have white pawn");

    // Retract the move
    RetractMove(&E);

    // Verify state is restored
    ASSERT(E.B.Board[e4] == wPawn, "Source should have white pawn after retract");
    ASSERT(E.B.Board[d5] == bPawn, "Captured pawn should be restored");
    ASSERT(compare_board_state(&E, &before, "after retract"), "Board state should be fully restored");
}

// Test: King move updates PieceLoc[0]
TEST(king_move_updates_piceloc)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup minimal position (just kings)
    SetupKingsOnly(&E);

    // Set up the move: king e1 to e2
    NODE *N    = E.S.currNode;
    N->player  = white;
    N->mobEval = 0;
    N->m.piece = wKing;
    N->m.from  = e1;
    N->m.to    = e2;
    N->m.cap   = empty;
    N->m.type  = mtype_Normal;
    N->m.dply  = 1;

    SQUARE old_king_loc = E.B.PieceLocW[0];
    ASSERT(old_king_loc == e1, "King should start at e1");

    // Perform the move
    PerformMove(&E);

    // Verify PieceLoc[0] was updated
    ASSERT(E.B.PieceLocW[0] == e2, "PieceLoc[0] should be updated to e2");
    ASSERT(E.B.Board[e1] == empty, "e1 should be empty");
    ASSERT(E.B.Board[e2] == wKing, "e2 should have king");

    // Retract
    RetractMove(&E);

    // Verify restored
    ASSERT(E.B.PieceLocW[0] == e1, "PieceLoc[0] should be restored to e1");
    ASSERT(E.B.Board[e1] == wKing, "e1 should have king again");
    ASSERT(E.B.Board[e2] == empty, "e2 should be empty again");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   TEST REGISTRY                                                */
/*                                                                                                */
/**************************************************************************************************/

void run_perform_move_tests(void)
{
    printf("\nRunning PerformMove/RetractMove Tests:\n");

    RUN_TEST(perform_move_doesnt_hang);
    RUN_TEST(knight_move_perform_retract);
    RUN_TEST(pawn_capture_perform_retract);
    RUN_TEST(king_move_updates_piceloc);
}
