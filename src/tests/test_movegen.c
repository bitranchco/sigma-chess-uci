/*
 * test_movegen_simple.c - Basic smoke tests for move generation functions
 *
 * These tests verify that the move generation functions compile and can be called
 * without crashing. More comprehensive positional tests will be added later.
 */

#include <stdio.h>
#include <string.h>
#include "test_helpers.h"
#include "../Engine.h"
#include "../Move Generation/MoveGen.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                   SMOKE TESTS                                                  */
/*                                                                                                */
/**************************************************************************************************/

// Test: SearchEnPriseCaptures doesn't crash on empty board
TEST(en_prise_empty_board)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Set up minimal node state
    E.S.currNode->player     = white;
    E.S.currNode->opponent   = black;
    E.S.currNode->lastPiece_ = 0; // No opponent pieces

    // This should not crash (no pieces to capture)
    SearchEnPriseCaptures(&E);

    ASSERT(1, "SearchEnPriseCaptures survived empty board");
}

// Test: SearchPromotions doesn't crash when no pawns on 7th rank
TEST(promotions_no_pawns)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Set up minimal node state
    E.S.currNode->player  = white;
    E.S.currNode->pawnDir = 0x10;

    // No pawns on 7th rank
    E.B.PawnStructW[6] = 0;

    // This should not crash
    SearchPromotions(&E);

    ASSERT(1, "SearchPromotions survived with no pawns");
}

// Test: SearchRecaptures doesn't crash when no previous capture
TEST(recaptures_no_capture)
{
    GLOBAL Global;
    ENGINE E;
    NODE  *N, *PrevNode;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Move currNode to position 1 so (N-1) is valid
    E.S.currNode = E.S.rootNode + 1;
    N            = E.S.currNode;
    PrevNode     = N - 1; // Previous node (rootNode)

    // Set up minimal node state
    N->player    = white;
    N->opponent  = black;
    N->pawnDir   = 0x10;
    N->Attack    = E.A.AttackW;
    N->Attack_   = E.A.AttackB;
    N->PieceLoc  = E.B.PieceLocW;
    N->PieceLoc_ = E.B.PieceLocB;

    // Previous move was not a capture - set up in previous node
    PrevNode->m.cap   = empty;
    PrevNode->m.piece = wPawn;
    PrevNode->m.from  = e2;
    PrevNode->m.to    = e4;

    // This should call SearchEnPassant but not crash
    SearchRecaptures(&E);

    ASSERT(1, "SearchRecaptures survived with no capture");
}

// Test: SearchSafeCaptures doesn't crash on empty board
TEST(safe_captures_empty_board)
{
    GLOBAL Global;
    ENGINE E;
    NODE  *N;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    N = E.S.currNode;

    // Set up minimal node state
    N->player     = white;
    N->opponent   = black;
    N->pawnDir    = 0x10;
    N->lastPiece_ = 0; // No opponent pieces
    N->recapSq    = nullSq;

    // This should not crash (no pieces to capture)
    SearchSafeCaptures(&E);

    ASSERT(1, "SearchSafeCaptures survived empty board");
}

// Test: SearchEscapes doesn't crash when escapeSq is nullSq
TEST(escapes_no_escape_sq)
{
    GLOBAL Global;
    ENGINE E;
    NODE  *N;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    N = E.S.currNode;

    // Set up minimal node state
    N->player   = white;
    N->opponent = black;
    N->pawnDir  = 0x10;
    N->escapeSq = nullSq; // No escape square
    N->eply     = 0;

    // This should return immediately
    SearchEscapes(&E);

    ASSERT(1, "SearchEscapes survived with no escape square");
}

// Test: SearchKillers doesn't crash when no killers are active
TEST(killers_no_active)
{
    GLOBAL Global;
    ENGINE E;
    NODE  *N;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    N = E.S.currNode;

    // Set up minimal node state
    N->player        = white;
    N->opponent      = black;
    N->pawnDir       = 0x10;
    N->killer1Active = false; // No active killers
    N->killer2Active = false;

    // This should not crash (no killers to search)
    SearchKillers(&E);

    ASSERT(1, "SearchKillers survived with no active killers");
}

// Test: SearchCastling doesn't crash on empty board
TEST(castling_empty_board)
{
    GLOBAL Global;
    ENGINE E;
    NODE  *N;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    N = E.S.currNode;

    // Set up minimal node state
    N->player   = white;
    N->opponent = black;
    N->pawnDir  = 0x10;
    N->Attack_  = E.A.AttackB;

    // This should not crash (no kings to castle)
    SearchCastling(&E);

    ASSERT(1, "SearchCastling survived empty board");
}

// Test: All phase functions can be called
TEST(all_phases_callable)
{
    GLOBAL Global;
    ENGINE E;
    NODE  *N, *PrevNode;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Move currNode to position 1 so (N-1) is valid
    E.S.currNode = E.S.rootNode + 1;
    N            = E.S.currNode;
    PrevNode     = N - 1; // Previous node (rootNode)

    // Set up minimal state
    N->player        = white;
    N->opponent      = black;
    N->pawnDir       = 0x10;
    N->lastPiece_    = 0;
    N->recapSq       = nullSq;
    N->escapeSq      = nullSq;
    N->eply          = 0;
    N->killer1Active = false;
    N->killer2Active = false;
    N->Attack        = E.A.AttackW;
    N->Attack_       = E.A.AttackB;
    N->PieceLoc      = E.B.PieceLocW;
    N->PieceLoc_     = E.B.PieceLocB;
    PrevNode->m.cap  = empty;

    // Call all completed phases - should not crash
    SearchEnPriseCaptures(&E);
    SearchPromotions(&E);
    SearchRecaptures(&E);
    SearchSafeCaptures(&E);
    SearchEscapes(&E);
    SearchKillers(&E);
    SearchCastling(&E);

    ASSERT(1, "All move generation phases are callable");
}

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

// Check if a promotion move was generated
static int find_promotion(ENGINE *E, SQUARE from, SQUARE to, PIECE promo_type)
{
    for (int i = 0; i < E->S.numRootMoves; i++)
    {
        MOVE *m = &E->S.RootMoves[i];
        if (m->from == from && m->to == to && m->type == promo_type)
        {
            return 1;
        }
    }
    return 0;
}

// Check if en passant capture was generated
static int find_ep_move(ENGINE *E, SQUARE from, SQUARE to)
{
    for (int i = 0; i < E->S.numRootMoves; i++)
    {
        MOVE *m = &E->S.RootMoves[i];
        if (m->from == from && m->to == to && m->type == mtype_EP)
        {
            return 1;
        }
    }
    return 0;
}

/**************************************************************************************************/
/*                                                                                                */
/*                               EN PRISE CAPTURES (DETAILED TESTS)                               */
/*                                                                                                */
/**************************************************************************************************/

// Test: Capture undefended queen with pawn
TEST(enprise_capture_queen_with_pawn)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White pawn on e4, black queen on d5 (undefended)
    printf("\n    Debug: e4 constant = 0x%02x, c4 = 0x%02x\n", e4, c4);
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, e4, wPawn, 1);
    E.B.PieceLocW[2] = nullSq;
    printf("    After PlacePiece: Board[e4=0x%02x] = 0x%02x, PieceLocW[1] = 0x%02x\n", e4, E.B.Board[e4],
           E.B.PieceLocW[1]);

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, d5, bQueen, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    // Setup move generation
    SQUARE attacked[] = {nullSq};
    SetupMoveGenTest(&E, white, attacked, NULL);

    // Generate en prise captures
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 0;

    // Debug: Check attack state
    NODE *N = E.S.currNode;
    printf("    Debug: White attacks d5 = 0x%08lx, lastPiece_ = %d\n", N->Attack[d5], N->lastPiece_);
    printf("    Debug: Board[c4=0x32] = 0x%02x, Board[e4=0x34] = 0x%02x\n", E.B.Board[c4], E.B.Board[e4]);
    printf("    Debug: PieceLocW[0] = 0x%02x, PieceLocW[1] = 0x%02x, PieceLocW[2] = 0x%02x\n", E.B.PieceLocW[0],
           E.B.PieceLocW[1], E.B.PieceLocW[2]);

    SearchEnPriseCaptures(&E);

    printf("    Generated %d root moves\n", E.S.numRootMoves);
    if (E.S.numRootMoves > 0)
    {
        MOVE *m = &E.S.RootMoves[0];
        printf("    Move 0: from=0x%02x to=0x%02x piece=0x%02x type=0x%04x\n", m->from, m->to, m->piece, m->type);
    }

    // Should capture the undefended queen
    ASSERT(find_move(&E, e4, d5, wPawn), "Pawn should capture undefended queen");
}

// Test: Knight captures undefended bishop
TEST(enprise_knight_captures_bishop)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White knight on d4, black bishop on f5 (undefended)
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, d4, wKnight, 1);
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, f5, bBishop, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    // Setup move generation
    SetupMoveGenTest(&E, white, NULL, NULL);

    // Generate en prise captures
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 0;
    SearchEnPriseCaptures(&E);

    // Should capture undefended bishop
    ASSERT(find_move(&E, d4, f5, wKnight), "Knight should capture undefended bishop");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                  PROMOTIONS (DETAILED TESTS)                                   */
/*                                                                                                */
/**************************************************************************************************/

// Test: White pawn promotes to queen on 8th rank
TEST(promotion_to_queen)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White pawn on e7 (ready to promote)
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, e7, wPawn, 1);
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, a8, bKing, 0); // Black king on a8 (not blocking promotion)
    E.B.PieceLocB[1] = nullSq;

    // CalcAttackState will set up PawnStructW automatically
    CalcAttackState(&E);

    // Setup move generation
    SetupMoveGenTest(&E, white, NULL, NULL);

    // Generate promotions
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 0;
    SearchPromotions(&E);

    // Should promote to queen
    ASSERT(find_promotion(&E, e7, e8, wQueen), "Pawn should promote to queen");
}

// Test: Black pawn promotes to queen on 1st rank
TEST(promotion_to_queen_black)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Black pawn on e2 (ready to promote)
    PlacePiece(&E, a1, wKing, 0); // White king on a1 (not blocking promotion)
    E.B.PieceLocW[1] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, e2, bPawn, 1);
    E.B.PieceLocB[2] = nullSq;

    // CalcAttackState will set up PawnStructB automatically
    CalcAttackState(&E);

    // Setup move generation
    SetupMoveGenTest(&E, black, NULL, NULL);

    // Generate promotions
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 0;
    SearchPromotions(&E);

    // Should promote to queen
    ASSERT(find_promotion(&E, e2, e1, bQueen), "Black pawn should promote to queen");
}

// Test: Pawn capture-promotes (tested via EnPriseCaptures)
TEST(capture_promotion)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White pawn on e7, black knight on d8
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, e7, wPawn, 1);
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, d8, bKnight, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    // Setup move generation
    SetupMoveGenTest(&E, white, NULL, NULL);

    // Generate en prise captures (includes capture-promotions)
    E.R.state            = state_Root;
    E.S.numRootMoves     = 0;
    E.S.currNode->m.dply = 0;
    SearchEnPriseCaptures(&E);

    // Should capture and promote to queen
    ASSERT(find_promotion(&E, e7, d8, wQueen), "Pawn should capture-promote to queen");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                 EN PASSANT (DETAILED TESTS)                                    */
/*                                                                                                */
/**************************************************************************************************/

// Test: White captures black pawn en passant
TEST(en_passant_capture_white)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White pawn on d5, black pawn just moved from e7 to e5
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, d5, wPawn, 1);
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, e5, bPawn, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    // Setup move generation for white at root node
    NODE *N      = E.S.rootNode;
    N->player    = white;
    N->opponent  = black;
    N->pawnDir   = 0x10;
    N->Attack    = E.A.AttackW;
    N->Attack_   = E.A.AttackB;
    N->PieceLoc  = E.B.PieceLocW;
    N->PieceLoc_ = E.B.PieceLocB;
    N->recapSq   = nullSq;

    // Setup previous move in rootNode: black pawn double advance e7-e5
    // (N-1 will be used by SearchEnPassant to check previous move)
    NODE *PrevNode    = N - 1;
    PrevNode->m.piece = bPawn;
    PrevNode->m.from  = e7;
    PrevNode->m.to    = e5;
    PrevNode->m.cap   = empty;

    // currNode must equal rootNode for root move generation
    E.S.currNode = E.S.rootNode;

    // Generate recaptures (which calls SearchEnPassant)
    E.R.state        = state_Root;
    E.S.numRootMoves = 0;
    N->m.dply        = 0;
    SearchRecaptures(&E);

    // Should have en passant capture
    ASSERT(find_ep_move(&E, d5, e6), "White pawn should capture en passant");
}

// Test: Black captures white pawn en passant
TEST(en_passant_capture_black)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Black pawn on d4, white pawn on e4 (just moved from e2)
    PlacePiece(&E, e1, wKing, 0);
    PlacePiece(&E, e4, wPawn, 1);
    E.B.PieceLocW[2] = nullSq;

    PlacePiece(&E, e8, bKing, 0);
    PlacePiece(&E, d4, bPawn, 1);
    E.B.PieceLocB[2] = nullSq;

    CalcAttackState(&E);

    // Setup move generation for black at root node
    NODE *N      = E.S.rootNode;
    N->player    = black;
    N->opponent  = white;
    N->pawnDir   = -0x10;
    N->Attack    = E.A.AttackB;
    N->Attack_   = E.A.AttackW;
    N->PieceLoc  = E.B.PieceLocB;
    N->PieceLoc_ = E.B.PieceLocW;
    N->recapSq   = nullSq;

    // Setup previous move in rootNode: white pawn double advance e2-e4
    // (N-1 will be used by SearchEnPassant to check previous move)
    NODE *PrevNode    = N - 1;
    PrevNode->m.piece = wPawn;
    PrevNode->m.from  = e2;
    PrevNode->m.to    = e4;
    PrevNode->m.cap   = empty;

    // currNode must equal rootNode for root move generation
    E.S.currNode = E.S.rootNode;

    // Generate recaptures
    E.R.state        = state_Root;
    E.S.numRootMoves = 0;
    N->m.dply        = 0;
    SearchRecaptures(&E);

    // Should have en passant capture
    ASSERT(find_ep_move(&E, d4, e3), "Black pawn should capture en passant");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                   TEST REGISTRY                                                */
/*                                                                                                */
/**************************************************************************************************/

void run_movegen_tests(void)
{
    printf("\nRunning Move Generation Tests:\n");

    // Smoke tests
    printf("  Basic Smoke Tests:\n");
    RUN_TEST(en_prise_empty_board);
    RUN_TEST(promotions_no_pawns);
    RUN_TEST(recaptures_no_capture);
    RUN_TEST(safe_captures_empty_board);
    RUN_TEST(escapes_no_escape_sq);
    RUN_TEST(killers_no_active);
    RUN_TEST(castling_empty_board);
    RUN_TEST(all_phases_callable);

    // Detailed functionality tests
    printf("  En Prise Captures:\n");
    RUN_TEST(enprise_capture_queen_with_pawn);
    RUN_TEST(enprise_knight_captures_bishop);

    printf("  Promotions:\n");
    RUN_TEST(promotion_to_queen);
    RUN_TEST(promotion_to_queen_black);
    RUN_TEST(capture_promotion);

    printf("  En Passant:\n");
    RUN_TEST(en_passant_capture_white);
    RUN_TEST(en_passant_capture_black);
}
