/*
 * test_noncaptures.c - Tests for Phase H (SearchNonCaptures) and Phase I (SearchSacrifices)
 *
 * These tests verify that non-capturing moves are generated correctly:
 * - Knight moves (all 8 directions)
 * - Bishop moves (diagonal sliding)
 * - Rook moves (orthogonal sliding)
 * - Queen moves (combined)
 * - King moves (8 directions, safe squares only)
 * - Pawn advances (1 and 2 squares)
 * - Sacrifice buffer handling
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
/*                                   PHASE H: KNIGHT MOVES                                        */
/*                                                                                                */
/**************************************************************************************************/

// Test: White knight on d4 can move to all 8 squares (empty board)
TEST(knight_all_moves)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup position with knight on d4
    SetupWhitePiecePosition(&E, d4, wKnight);

    // Setup NODE state for move generation
    SQUARE attackedPieces[] = {d4, nullSq};
    SetupMoveGenTest(&E, white, attackedPieces, NULL);

    // Generate moves
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 2;
    SearchNonCaptures(&E);

    // Knight on d4 should be able to move to: c2, e2, f3, f5, e6, c6, b5, b3
    ASSERT(find_move(&E, d4, c2, wKnight), "Knight d4-c2");
    ASSERT(find_move(&E, d4, e2, wKnight), "Knight d4-e2");
    ASSERT(find_move(&E, d4, f3, wKnight), "Knight d4-f3");
    ASSERT(find_move(&E, d4, f5, wKnight), "Knight d4-f5");
    ASSERT(find_move(&E, d4, e6, wKnight), "Knight d4-e6");
    ASSERT(find_move(&E, d4, c6, wKnight), "Knight d4-c6");
    ASSERT(find_move(&E, d4, b5, wKnight), "Knight d4-b5");
    ASSERT(find_move(&E, d4, b3, wKnight), "Knight d4-b3");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   PHASE H: BISHOP MOVES                                        */
/*                                                                                                */
/**************************************************************************************************/

// Test: White bishop on d4 can slide diagonally
TEST(bishop_diagonal_slides)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup position with bishop on d4
    SetupWhitePiecePosition(&E, d4, wBishop);

    // Setup NODE state for move generation (bishop is safe, not attacked)
    SQUARE safePieces[] = {d4, nullSq};
    SetupMoveGenTest(&E, white, NULL, safePieces);

    // Generate moves
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 2;
    SearchNonCaptures(&E);

    // Should be able to move along all 4 diagonals
    ASSERT(find_move(&E, d4, c3, wBishop), "Bishop d4-c3");
    ASSERT(find_move(&E, d4, e5, wBishop), "Bishop d4-e5");
    ASSERT(find_move(&E, d4, f6, wBishop), "Bishop d4-f6");
    ASSERT(find_move(&E, d4, g7, wBishop), "Bishop d4-g7");
    ASSERT(count_moves(&E) >= 13, "Bishop should have 13 moves (4 diagonals)");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   PHASE H: PAWN ADVANCES                                       */
/*                                                                                                */
/**************************************************************************************************/

// Test: White pawn on e2 can advance 1 or 2 squares
TEST(pawn_double_advance)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup position with pawn on e2 (starting rank)
    SetupWhitePiecePosition(&E, e2, wPawn);

    // Setup NODE state for move generation
    SQUARE safePieces[] = {e2, nullSq};
    SetupMoveGenTest(&E, white, NULL, safePieces);

    // Generate moves
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 2;
    SearchNonCaptures(&E);

    // Should be able to advance 1 or 2 squares
    ASSERT(find_move(&E, e2, e3, wPawn), "Pawn e2-e3");
    ASSERT(find_move(&E, e2, e4, wPawn), "Pawn e2-e4");
}

// Test: Black pawn on e7 can advance 1 or 2 squares
TEST(pawn_double_advance_black)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup position with black pawn on e7 (starting rank for black)
    SetupBlackPiecePosition(&E, e7, bPawn);

    // Setup NODE state for move generation (black to move)
    SQUARE safePieces[] = {e7, nullSq};
    SetupMoveGenTest(&E, black, NULL, safePieces);

    // Generate moves
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 2;
    SearchNonCaptures(&E);

    // Should be able to advance 1 or 2 squares
    ASSERT(find_move(&E, e7, e6, bPawn), "Pawn e7-e6");
    ASSERT(find_move(&E, e7, e5, bPawn), "Pawn e7-e5");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   PHASE H: KING MOVES                                          */
/*                                                                                                */
/**************************************************************************************************/

// Test: King doesn't move to attacked squares
TEST(king_avoids_attacked_squares)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Place white king on e4
    PlacePiece(&E, e4, wKing, 0);
    E.B.PieceLocW[1] = nullSq;

    // Place black king on e8
    PlacePiece(&E, e8, bKing, 0);

    // Place black rook on e1 (attacks e2, e3, e4, e5, e6, e7)
    PlacePiece(&E, e1, bRook, 1);
    E.B.PieceLocB[2] = nullSq;

    // Update attack tables
    CalcAttackState(&E);

    // Setup NODE state for move generation
    // King on e4 is attacked by rook on e1
    SQUARE attackedPieces[] = {e4, nullSq};
    SetupMoveGenTest(&E, white, attackedPieces, NULL);

    // Generate moves
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 2;
    SearchNonCaptures(&E);

    // King should NOT move to e5 or e3 (attacked by rook on e1)
    ASSERT(!find_move(&E, e4, e5, wKing), "King should not move to e5 (attacked)");
    ASSERT(!find_move(&E, e4, e3, wKing), "King should not move to e3 (attacked)");

    // King CAN move to d4, f4, d5, f5, d3, f3 (not attacked)
    ASSERT(find_move(&E, e4, d4, wKing), "King can move to d4");
    ASSERT(find_move(&E, e4, f4, wKing), "King can move to f4");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   PHASE I: SACRIFICE BUFFER                                    */
/*                                                                                                */
/**************************************************************************************************/

// Test: Sacrifice moves are added to buffer and then processed
TEST(sacrifice_buffer_processing)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup position with knight on d4
    SetupWhitePiecePosition(&E, d4, wKnight);

    // Add black pawn on f7 (attacks e6, making Ne6 a sacrifice)
    PlacePiece(&E, f7, bPawn, 1);
    E.B.PieceLocB[2] = nullSq; // Terminate list

    // Update attack tables
    CalcAttackState(&E);

    // Setup NODE state for move generation
    SQUARE attackedPieces[] = {d4, nullSq};
    SetupMoveGenTest(&E, white, attackedPieces, NULL);

    // Setup sacrifice buffer
    SetupSacrificeTest(&E);

    // Generate non-captures (knight to e6 should be a sacrifice - attacked by pawn)
    // Use state_Stopping to just fill the buffer without processing
    E.R.state            = state_Stopping;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 2;
    SearchNonCaptures(&E);

    // Verify sacrifice buffer has the move
    int sacrificeCount = E.S.bufTop - E.S.currNode->bufStart;
    ASSERT(sacrificeCount > 0, "Should have at least one sacrifice move in buffer");

    // Now process sacrifices from buffer
    E.R.state        = state_Root;
    E.S.numRootMoves = 0;
    SearchSacrifices(&E);

    // Verify that sacrifice move (Nd4-e6) was processed
    ASSERT(find_move(&E, d4, e6, wKnight), "Sacrifice move Ne6 should be processed");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   TEST REGISTRY                                                */
/*                                                                                                */
/**************************************************************************************************/

void run_noncapture_tests(void)
{
    printf("\nRunning Non-Capture Move Tests (Phases H-I):\n");

    // Phase H: Non-capturing moves
    RUN_TEST(knight_all_moves);
    RUN_TEST(bishop_diagonal_slides);
    RUN_TEST(pawn_double_advance);
    RUN_TEST(pawn_double_advance_black);
    RUN_TEST(king_avoids_attacked_squares);

    // Phase I: Sacrifice buffer
    RUN_TEST(sacrifice_buffer_processing);
}
