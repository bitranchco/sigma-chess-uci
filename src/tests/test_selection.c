/*
 * test_selection.c - Tests for Selection functionality
 *
 * These tests verify the selective search functions:
 * - ComputeSelBaseVal() - Computes selection margin for forward pruning
 * - SelectMove() - Decides whether to search or prune a move
 */

#include <stdio.h>
#include <string.h>
#include "test_helpers.h"
#include "../Engine.h"
#include "../Searching/Selection.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                   HELPER FUNCTIONS                                             */
/*                                                                                                */
/**************************************************************************************************/

// Setup a basic position for selection testing
static void SetupBasicPosition(ENGINE *E)
{
    SetupEmptyBoard(E);

    // White: King on e1, Knight on d4
    PlacePiece(E, e1, wKing, 0);
    PlacePiece(E, d4, wKnight, 1);
    E->B.PieceLocW[2] = nullSq;

    // Black: King on e8, Pawn on e7
    PlacePiece(E, e8, bKing, 0);
    PlacePiece(E, e7, bPawn, 1);
    E->B.PieceLocB[2] = nullSq;

    CalcAttackState(E);
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   COMPUTESELBASEVAL TESTS                                      */
/*                                                                                                */
/**************************************************************************************************/

// Test: Basic selection margin calculation
TEST(compute_sel_base_val_basic)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupBasicPosition(&E);

    NODE *N     = E.S.currNode;
    N->player   = white;
    N->opponent = black;
    N->ply      = 3;

    ComputeSelBaseVal(&E);

    // Basic margin should be 4 * (ply - 1) = 4 * 2 = 8
    ASSERT(N->selMargin == 8, "Selection margin should be 4*(ply-1)");
}

// Test: Selection disabled when opponent has no officers and player has pawns
TEST(compute_sel_base_val_pawn_endgame)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White: King + Pawn
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, e4, wPawn, 1);
    E.B.PieceLocW[2] = nullSq;

    // Black: King only (no officers)
    PlacePiece(&E, e8, bKing, 0);
    E.B.PieceLocB[1] = nullSq;

    CalcAttackState(&E);

    NODE *N     = E.S.currNode;
    N->player   = white;
    N->opponent = black;
    N->ply      = 3;

    ComputeSelBaseVal(&E);

    // Selection should be disabled (maxVal)
    ASSERT(N->selMargin == maxVal, "Selection should be disabled in K+P vs K endgame");
}

// Test: Selection disabled when player has pawns on 7th rank
TEST(compute_sel_base_val_pawn_on_7th)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White: King + Pawn on 7th
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, e7, wPawn, 1);
    E.B.PieceLocW[2] = nullSq;

    // Black: King + Knight
    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, d6, bKnight, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    NODE *N     = E.S.currNode;
    N->player   = white;
    N->opponent = black;
    N->ply      = 3;

    ComputeSelBaseVal(&E);

    // Selection should be disabled (maxVal) because white has pawn on 7th
    ASSERT(N->selMargin == maxVal, "Selection should be disabled with pawn on 7th rank");
}

// Test: Threat eval added to selection margin
TEST(compute_sel_base_val_with_threat)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupBasicPosition(&E);

    NODE *N = E.S.currNode;

    N->player   = white;
    N->opponent = black;
    N->ply      = 3;

    // Setup threat scenario (PN is a macro defined as N-1)
    PN->escapeSq   = d4;
    PN->m.from     = e5; // Different from escapeSq
    PN->threatEval = 50;

    // Make sure Attack table shows d4 is attacked
    N->Attack       = E.A.AttackW;
    E.A.AttackW[d4] = 0x01000000; // King attacks it

    ComputeSelBaseVal(&E);

    // Margin should be base + threatEval = 8 + 50 = 58
    ASSERT(N->selMargin == 58, "Selection margin should include threat eval");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   SELECTMOVE TESTS                                             */
/*                                                                                                */
/**************************************************************************************************/

// Test: Never prune special moves
TEST(select_move_never_prune_special)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupBasicPosition(&E);

    NODE *N = E.S.currNode;

    // Setup a castling move (special move type)
    N->m.type  = mtype_O_O;
    N->m.piece = wKing;
    N->gen     = gen_G;

    N->selMargin  = 10;
    N->alpha      = 100;
    NN->totalEval = 200; // Very bad evaluation (NN is a macro = N+1)

    BOOL result = SelectMove(&E);

    ASSERT(result == true, "Special moves should never be pruned");
}

// Test: Never prune gen_J moves (safe checks)
TEST(select_move_never_prune_gen_j)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupBasicPosition(&E);

    NODE *N = E.S.currNode;

    // Setup a gen_J move (safe check)
    N->m.type  = mtype_Normal;
    N->m.piece = wKnight;
    N->gen     = gen_J;

    N->selMargin  = 10;
    N->alpha      = 100;
    NN->totalEval = 200; // Very bad evaluation (NN is a macro = N+1)

    BOOL result = SelectMove(&E);

    ASSERT(result == true, "gen_J moves should never be pruned");
}

// Test: Select move with positive diff
TEST(select_move_positive_diff)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupBasicPosition(&E);

    NODE *N = E.S.currNode;

    N->m.type  = mtype_Normal;
    N->m.piece = wKnight;
    N->m.cap   = empty;
    N->gen     = gen_H;

    N->selMargin  = 100;
    N->alpha      = 50;
    NN->totalEval = -30; // Good evaluation (from opponent's perspective, NN = N+1)

    // diff = -(-30) + 100 - 50 = 30 + 100 - 50 = 80 (positive, should select)

    BOOL result = SelectMove(&E);

    ASSERT(result == true, "Move with positive diff should be selected");
}

// Test: Capture bonus affects selection
TEST(select_move_capture_bonus)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupBasicPosition(&E);

    NODE *N = E.S.currNode;

    N->m.type    = mtype_Normal;
    N->m.piece   = wKnight;
    N->m.cap     = bPawn;
    N->capSelVal = 10;
    N->gen       = gen_D;

    N->selMargin  = 100;
    N->alpha      = 120;
    NN->totalEval = 0; // NN = N+1

    // diff = 0 + 100 - 120 = -20 (negative without bonus)
    // diff += 40 - 10 = 30, so total diff = 10 (positive with bonus)

    BOOL result = SelectMove(&E);

    ASSERT(result == true, "Capture bonus should help selection");
}

// Test: Pawn move bonus
TEST(select_move_pawn_bonus)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupBasicPosition(&E);

    NODE *N = E.S.currNode;

    N->m.type  = mtype_Normal;
    N->m.piece = wPawn;
    N->m.cap   = empty;
    N->gen     = gen_H;

    N->selMargin  = 100;
    N->alpha      = 130;
    NN->totalEval = 0; // NN = N+1

    // diff = 0 + 100 - 130 = -30 (negative without bonus)
    // diff += 35, so total diff = 5 (positive with pawn bonus)

    BOOL result = SelectMove(&E);

    ASSERT(result == true, "Pawn move bonus should help selection");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   TEST REGISTRY                                                */
/*                                                                                                */
/**************************************************************************************************/

void run_selection_tests(void)
{
    printf("\nRunning Selection Tests:\n");

    // ComputeSelBaseVal tests
    RUN_TEST(compute_sel_base_val_basic);
    RUN_TEST(compute_sel_base_val_pawn_endgame);
    RUN_TEST(compute_sel_base_val_pawn_on_7th);
    RUN_TEST(compute_sel_base_val_with_threat);

    // SelectMove tests
    RUN_TEST(select_move_never_prune_special);
    RUN_TEST(select_move_never_prune_gen_j);
    RUN_TEST(select_move_positive_diff);
    RUN_TEST(select_move_capture_bonus);
    RUN_TEST(select_move_pawn_bonus);
}
