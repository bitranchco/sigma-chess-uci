/**************************************************************************************************/
/*                                                                                                */
/* Module  : TEST_ATTACK.C                                                                        */
/* Purpose : Unit tests for Attack.c module                                                      */
/*                                                                                                */
/**************************************************************************************************/

#include "../Engine.h"
#include "test_helpers.h"

// Test initialization functions work
TEST(init_attack_module)
{
    GLOBAL Global;
    InitTestGlobal(&Global);

    // Check that bit masks were computed
    ASSERT(Global.A.QueenBit[0] == 1, "QueenBit[0] should be 1");
    ASSERT(Global.A.QueenBit[7] == 128, "QueenBit[7] should be 128");
    ASSERT(Global.A.KnightBit[0] == (1 << 16), "KnightBit[0] should be 1<<16");
}

// Test that CalcAttackState clears attack tables
TEST(calc_attack_state_clears_tables)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Pollute attack tables
    E.A.AttackW[e4] = 0xFFFFFFFF;
    E.A.AttackB[e4] = 0xFFFFFFFF;

    // Calculate for empty board
    CalcAttackState(&E);

    // Should be cleared
    ASSERT_EQ(E.A.AttackW[e4], 0, "AttackW should be cleared");
    ASSERT_EQ(E.A.AttackB[e4], 0, "AttackB should be cleared");
}

// Test UpdPieceAttack for white queen
TEST(upd_piece_attack_white_queen)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Place white queen on e4
    E.B.Board[e4] = wQueen;

    // Clear attack tables
    for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
        E.A.AttackW[sq] = E.A.AttackB[sq] = 0;

    // Call function
    INT mob = UpdPieceAttack(&E, e4);

    // Verify: queen should attack in 8 directions with queen bits (0-7)
    ASSERT(E.A.AttackW[e4 - 0x0F] & (1 << 0), "Queen should attack up-right");
    ASSERT(E.A.AttackW[e4 - 0x11] & (1 << 1), "Queen should attack up-left");
    ASSERT(E.A.AttackW[e4 + 0x11] & (1 << 2), "Queen should attack down-right");
    ASSERT(E.A.AttackW[e4 + 0x0F] & (1 << 3), "Queen should attack down-left");
    ASSERT(E.A.AttackW[e4 - 0x10] & (1 << 4), "Queen should attack up");
    ASSERT(E.A.AttackW[e4 + 0x10] & (1 << 5), "Queen should attack down");
    ASSERT(E.A.AttackW[e4 + 0x01] & (1 << 6), "Queen should attack right");
    ASSERT(E.A.AttackW[e4 - 0x01] & (1 << 7), "Queen should attack left");

    // Verify mobility is positive
    ASSERT(mob > 0, "Queen should have positive mobility");
}

// Test UpdPieceAttack for white knight
TEST(upd_piece_attack_white_knight)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Place white knight on e4
    E.B.Board[e4] = wKnight;

    // Clear attack tables
    for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
        E.A.AttackW[sq] = E.A.AttackB[sq] = 0;

    // Call function
    INT mob = UpdPieceAttack(&E, e4);

    // Verify: knight should attack 8 squares with knight bits (16-23)
    // Knight moves: 2 squares in one direction, 1 in perpendicular
    ASSERT(E.A.AttackW[e4 + Global.B.KnightDir[0]] & (1 << 16), "Knight attack 1");
    ASSERT(E.A.AttackW[e4 + Global.B.KnightDir[1]] & (1 << 17), "Knight attack 2");
    ASSERT(E.A.AttackW[e4 + Global.B.KnightDir[2]] & (1 << 18), "Knight attack 3");
    ASSERT(E.A.AttackW[e4 + Global.B.KnightDir[3]] & (1 << 19), "Knight attack 4");

    // Knight has no mobility score
    ASSERT_EQ(mob, 0, "Knight should return 0 mobility");
}

// Test UpdPieceAttack for white pawn
TEST(upd_piece_attack_white_pawn)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Place white pawn on e4
    E.B.Board[e4] = wPawn;

    // Clear attack tables and pawn structure
    for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
        E.A.AttackW[sq] = E.A.AttackB[sq] = 0;
    for (INT r = 0; r <= 7; r++)
        E.B.PawnStructW[r] = E.B.PawnStructB[r] = 0;

    // Call function
    INT mob = UpdPieceAttack(&E, e4);

    // Verify: pawn attacks diagonally forward (pMaskL and pMaskR)
    // Convention (matches upstream asm): pMaskL = pawn's left-forward diagonal (sq+0x0F for white).
    ASSERT(E.A.AttackW[e4 + 0x0F] & pMaskL, "White pawn should attack left-forward (sq+0x0F)");
    ASSERT(E.A.AttackW[e4 + 0x11] & pMaskR, "White pawn should attack right-forward (sq+0x11)");

    // Verify pawn structure updated
    ASSERT(E.B.PawnStructW[rank(e4)] & (1 << file(e4)), "Pawn structure should be set");

    // Pawn has no mobility score
    ASSERT_EQ(mob, 0, "Pawn should return 0 mobility");
}

// Test UpdBlockAttack basic functionality
TEST(upd_block_attack_basic)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Place white queen on a1 and white rook on a8
    E.B.Board[a1] = wQueen;
    E.B.Board[a8] = wRook;

    // Clear attack tables and set up attacks
    for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
        E.A.AttackW[sq] = E.A.AttackB[sq] = 0;

    UpdPieceAttack(&E, a1);
    UpdPieceAttack(&E, a8);

    // Check initial state: a4 should be attacked by both queen and rook
    ULONG a4_before = E.A.AttackW[a4];
    ASSERT(a4_before != 0, "a4 should be attacked initially");

    // Place a pawn on a4 - this blocks some of the attacks
    E.B.Board[a4] = wPawn;

    // Call UpdBlockAttack - this toggles the attack bits for rays passing through a4
    INT mob1 = UpdBlockAttack(&E, a4);

    // Remove the pawn
    E.B.Board[a4] = empty;

    // Call UpdBlockAttack again - should toggle back
    INT mob2 = UpdBlockAttack(&E, a4);

    // After toggling twice, attack should be restored
    ASSERT(E.A.AttackW[a4] == a4_before, "Attack should be restored after two toggles");

    // Mobility values are returned (we just check function doesn't crash)
    // The exact values depend on player perspective and are tested elsewhere
    (void)mob1;
    (void)mob2; // Suppress unused warnings
}

// Test UpdBlockAttack with different ray directions
TEST(upd_block_attack_multiple_rays)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // Set up: White queen on e4 (attacks in 8 directions)
    E.B.Board[e4] = wQueen;

    // Clear attack tables
    for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
        E.A.AttackW[sq] = E.A.AttackB[sq] = 0;

    UpdPieceAttack(&E, e4);

    // Save attack state for e6 (should be attacked by queen)
    ULONG e6_attacks = E.A.AttackW[e6];
    ASSERT(e6_attacks != 0, "e6 should be attacked by queen");

    // Place a piece on e6
    E.B.Board[e6] = wPawn;

    // Call UpdBlockAttack - should toggle attacks beyond e6
    UpdBlockAttack(&E, e6);

    // e7 and e8 should no longer be attacked (blocked by pawn)
    ASSERT(E.A.AttackW[e7] == 0, "e7 should not be attacked (blocked)");
    ASSERT(E.A.AttackW[e8] == 0, "e8 should not be attacked (blocked)");
}

// Test that UpdBlockAttack handles both colors correctly
TEST(upd_block_attack_both_colors)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);
    SetupEmptyBoard(&E);

    // White queen on a1, black queen on h8
    E.B.Board[a1] = wQueen;
    E.B.Board[h8] = bQueen;

    // Clear attack tables
    for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
        E.A.AttackW[sq] = E.A.AttackB[sq] = 0;

    UpdPieceAttack(&E, a1);
    UpdPieceAttack(&E, h8);

    // d4 should be attacked by white queen
    ASSERT(E.A.AttackW[d4] != 0, "d4 should be attacked by white queen");

    // e5 should be attacked by black queen
    ASSERT(E.A.AttackB[e5] != 0, "e5 should be attacked by black queen");

    // Place a piece on d4
    E.B.Board[d4] = wPawn;

    // Call UpdBlockAttack - should update white attacks
    UpdBlockAttack(&E, d4);

    // Squares beyond d4 should not be attacked by queen's diagonal
    ASSERT(E.A.AttackW[e5] == 0, "e5 should not be attacked (blocked by pawn on d4)");
}

// Run all attack tests
void run_attack_tests()
{
    printf("\nRunning Attack Module Tests:\n");

    RUN_TEST(init_attack_module);
    RUN_TEST(calc_attack_state_clears_tables);
    RUN_TEST(upd_piece_attack_white_queen);
    RUN_TEST(upd_piece_attack_white_knight);
    RUN_TEST(upd_piece_attack_white_pawn);
    RUN_TEST(upd_block_attack_basic);
    RUN_TEST(upd_block_attack_multiple_rays);
    RUN_TEST(upd_block_attack_both_colors);

    // More tests will be added here as we convert functions
}
