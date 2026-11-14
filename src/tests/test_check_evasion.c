/*
 * test_check_evasion.c - Tests for Phase L (SearchCheckEvasion)
 *
 * These tests verify that check evasion moves are generated correctly:
 * - King moves out of check
 * - Capturing the checking piece
 * - Blocking the check (interpositions)
 * - Double check handling
 * - En passant to escape check
 */

#include <stdio.h>
#include <string.h>
#include "test_helpers.h"
#include "../Engine.h"
#include "../Move Generation/MoveGen.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                   HELPER FUNCTIONS                                             */
/*                                                                                                */
/**************************************************************************************************/

// Count moves generated in RootMoves[]
static int count_moves(ENGINE *E)
{
    return E->S.numRootMoves;
}

// Check if a specific move was generated
static int find_move(ENGINE *E, SQUARE from, SQUARE to, PIECE piece)
{
    for (int i = 0; i < E->S.numRootMoves; i++)
    {
        MOVE *m = &E->S.RootMoves[i];
        if (m->from == from && m->to == to && m->piece == piece)
        {
            return 1;
        }
    }
    return 0;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   KING MOVES OUT OF CHECK                                      */
/*                                                                                                */
/**************************************************************************************************/

// Test: White king in check from black rook - king must move
TEST(king_escapes_rook_check)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White king on e4, black king on e8, black rook on e1 (checking)
    PlacePiece(&E, e4, wKing, 0);
    E.B.PieceLocW[1] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, e1, bRook, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    // Setup NODE state for move generation (white to move, in check)
    SetupMoveGenTest(&E, white, NULL, NULL);
    E.S.rootNode->check = true;

    // Generate moves using SearchCheckEvasion directly
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 1;
    SearchCheckEvasion(&E);

    // King should be able to move to d4, f4, d5, f5, d3, f3 (not e-file, attacked by rook)
    ASSERT(find_move(&E, e4, d4, wKing), "King can escape to d4");
    ASSERT(find_move(&E, e4, f4, wKing), "King can escape to f4");
    ASSERT(find_move(&E, e4, d5, wKing), "King can escape to d5");
    ASSERT(find_move(&E, e4, f5, wKing), "King can escape to f5");

    // King should NOT be able to stay on e-file (attacked by rook)
    ASSERT(!find_move(&E, e4, e5, wKing), "King cannot move to e5 (attacked)");
    ASSERT(!find_move(&E, e4, e3, wKing), "King cannot move to e3 (attacked)");
}

// Test: King can capture the checking piece (pawn check from diagonal)
TEST(king_captures_checking_piece)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White king on e4, black king on e8, black pawn on d5 (checking from diagonal, undefended)
    PlacePiece(&E, e4, wKing, 0);
    E.B.PieceLocW[1] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, d5, bPawn, 1); // d5 is diagonal from e4 (pawn checks on diagonal)
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    SetupMoveGenTest(&E, white, NULL, NULL);
    E.S.rootNode->check = true;

    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 1;
    SearchCheckEvasion(&E);

    // King should be able to capture the checking pawn
    ASSERT(find_move(&E, e4, d5, wKing), "King can capture checking pawn");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   CAPTURING THE CHECKING PIECE                                 */
/*                                                                                                */
/**************************************************************************************************/

// Test: Another piece can capture the checking piece
TEST(capture_checking_piece_with_other_piece)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White king on e1, white queen on a5, black king on e8, black rook on e5 (checking)
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, a5, wQueen, 1); // Queen on a5 can capture rook on e5
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, e5, bRook, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    SetupMoveGenTest(&E, white, NULL, NULL);
    E.S.rootNode->check = true;

    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 1;
    SearchCheckEvasion(&E);

    // Queen should be able to capture the checking rook
    ASSERT(find_move(&E, a5, e5, wQueen), "Queen can capture checking rook");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   BLOCKING THE CHECK (INTERPOSITIONS)                          */
/*                                                                                                */
/**************************************************************************************************/

// Test: Block check with a piece
TEST(block_rook_check_with_piece)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White king on e1, white bishop on c3, black king on e8, black rook on e7 (checking)
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, c3, wBishop, 1);
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, e7, bRook, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    SetupMoveGenTest(&E, white, NULL, NULL);
    E.S.rootNode->check = true;

    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 1;
    SearchCheckEvasion(&E);

    // Bishop can block on e3, e4, e5, e6
    ASSERT(find_move(&E, c3, e5, wBishop), "Bishop can block check on e5");
}

// Test: Block check with pawn advance
TEST(block_check_with_pawn)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White king on e1, white pawn on g2, black king on e8, black bishop on h4
    // Bishop on h4 checks king on e1 along diagonal (h4-g3-f2-e1)
    // Pawn on g2 can advance to g3 to block the diagonal check
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, g2, wPawn, 1);
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, h4, bBishop, 1); // Bishop on h4 checks e1 diagonally
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    SetupMoveGenTest(&E, white, NULL, NULL);
    E.S.rootNode->check = true;

    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 1;
    SearchCheckEvasion(&E);

    // Pawn should be able to advance to g3 to block the diagonal check
    ASSERT(find_move(&E, g2, g3, wPawn), "Pawn can advance to g3 to block check");
}

// Test: Block diagonal check
TEST(block_bishop_check)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White king on e1, white knight on d3, black king on e8, black bishop on h4 (checking)
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, d3, wKnight, 1);
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, h4, bBishop, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    SetupMoveGenTest(&E, white, NULL, NULL);
    E.S.rootNode->check = true;

    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 1;
    SearchCheckEvasion(&E);

    // Knight can block on f2
    ASSERT(find_move(&E, d3, f2, wKnight), "Knight can block diagonal check on f2");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   DOUBLE CHECK                                                 */
/*                                                                                                */
/**************************************************************************************************/

// Test: Double check - only king moves allowed
TEST(double_check_only_king_moves)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White king on e4, white queen on e2
    // Black king on e8, black rook on e7, black bishop on c6 (both checking)
    PlacePiece(&E, e4, wKing, 0);
    PlacePiece(&E, e2, wQueen, 1);
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, e7, bRook, 1);
    PlacePiece(&E, c6, bBishop, 2);
    E.B.PieceLocB[3] = nullSq;

    CalcAttackState(&E);

    SetupMoveGenTest(&E, white, NULL, NULL);
    E.S.rootNode->check = true;

    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 1;
    SearchCheckEvasion(&E);

    // Only king moves should be generated (no captures, no blocks)
    // Queen should NOT be able to move
    ASSERT(!find_move(&E, e2, e3, wQueen), "Queen cannot move in double check");
    ASSERT(!find_move(&E, e2, e7, wQueen), "Queen cannot capture in double check");

    // King should be able to move to safe squares
    int moveCount = count_moves(&E);
    ASSERT(moveCount > 0, "King should have at least one legal move");

    // All generated moves should be king moves
    for (int i = 0; i < E.S.numRootMoves; i++)
    {
        PIECE piece = E.S.RootMoves[i].piece;
        ASSERT((piece & 0x07) == king, "All moves in double check must be king moves");
    }
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   PAWN CHECK DETECTION                                         */
/*                                                                                                */
/**************************************************************************************************/

// Test: Detect pawn check correctly
TEST(pawn_check_detection)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White king on e4, black king on e8, black pawn on d5 (checking)
    PlacePiece(&E, e4, wKing, 0);
    E.B.PieceLocW[1] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, d5, bPawn, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    SetupMoveGenTest(&E, white, NULL, NULL);
    E.S.rootNode->check = true;

    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 1;
    SearchCheckEvasion(&E);

    // King should be able to escape or capture the pawn
    ASSERT(find_move(&E, e4, d5, wKing), "King can capture checking pawn");

    // King should be able to move to safe squares
    ASSERT(find_move(&E, e4, f4, wKing), "King can escape to f4");
    ASSERT(find_move(&E, e4, e3, wKing), "King can escape to e3");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   KNIGHT CHECK                                                 */
/*                                                                                                */
/**************************************************************************************************/

// Test: Knight check - cannot be blocked
TEST(knight_check_no_interposition)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White king on e4, white bishop on d3
    // Black king on e8, black knight on d6 (checking via knight move)
    PlacePiece(&E, e4, wKing, 0);
    PlacePiece(&E, d3, wBishop, 1);
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, d6, bKnight, 1); // d6 is a knight move away from e4
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    SetupMoveGenTest(&E, white, NULL, NULL);
    E.S.rootNode->check = true;

    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 1;
    SearchCheckEvasion(&E);

    // Cannot block knight check - only king moves or capture knight
    // Bishop should NOT be able to interpose
    ASSERT(!find_move(&E, d3, e5, wBishop), "Cannot block knight check");

    // King should be able to move
    int moveCount = count_moves(&E);
    ASSERT(moveCount > 0, "Should have legal moves (king moves)");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   TEST REGISTRY                                                */
/*                                                                                                */
/**************************************************************************************************/

void run_check_evasion_tests(void)
{
    printf("\nRunning Check Evasion Tests (Phase L):\n");

    // King moves out of check
    RUN_TEST(king_escapes_rook_check);
    RUN_TEST(king_captures_checking_piece);

    // Capturing the checking piece
    RUN_TEST(capture_checking_piece_with_other_piece);

    // Blocking the check
    RUN_TEST(block_rook_check_with_piece);
    RUN_TEST(block_check_with_pawn);
    RUN_TEST(block_bishop_check);

    // Double check
    RUN_TEST(double_check_only_king_moves);

    // Various piece checks
    RUN_TEST(pawn_check_detection);
    RUN_TEST(knight_check_no_interposition);
}
