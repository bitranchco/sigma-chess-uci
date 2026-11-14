/**************************************************************************************************/
/*                                                                                                */
/* Module  : TEST_SEARCHMISC.C                                                                    */
/* Purpose : Tests for SearchMisc helper functions (UpdateBestLine, PrepareKillers, etc.)        */
/*                                                                                                */
/**************************************************************************************************/

#include "test_helpers.h"
#include "../Searching/SearchMisc.f"
#include "../Data Structures/Move.f"
#include <string.h>

/**************************************************************************************************/
/*                                    UPDATE BEST LINE TESTS                                      */
/**************************************************************************************************/

TEST(update_best_line_copies_current_move)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N        = E.S.currNode;
    NODE *NextNode = N + 1;

    // Setup current move
    N->m.piece = wKnight;
    N->m.from  = d4;
    N->m.to    = f5;
    N->m.cap   = empty;
    N->m.type  = mtype_Normal;

    // Setup next node's best line with a continuation
    NextNode->BestLine[0].piece = bPawn;
    NextNode->BestLine[0].from  = e7;
    NextNode->BestLine[0].to    = e5;
    NextNode->BestLine[0].cap   = empty;
    clrMove(NextNode->BestLine[1]); // Null terminator

    // Call UpdateBestLine
    UpdateBestLine(&E);

    // Verify current move was copied to BestLine[0]
    ASSERT_EQ(N->BestLine[0].piece, wKnight, "BestLine[0].piece should be wKnight");
    ASSERT_EQ(N->BestLine[0].from, d4, "BestLine[0].from should be d4");
    ASSERT_EQ(N->BestLine[0].to, f5, "BestLine[0].to should be f5");

    // Verify continuation was copied
    ASSERT_EQ(N->BestLine[1].piece, bPawn, "BestLine[1].piece should be bPawn");
    ASSERT_EQ(N->BestLine[1].from, e7, "BestLine[1].from should be e7");
    ASSERT_EQ(N->BestLine[1].to, e5, "BestLine[1].to should be e5");

    // Verify null terminator
    ASSERT(isNull(N->BestLine[2]), "BestLine should be null-terminated");
}

TEST(update_best_line_handles_empty_continuation)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N        = E.S.currNode;
    NODE *NextNode = N + 1;

    // Setup current move
    N->m.piece = wQueen;
    N->m.from  = d1;
    N->m.to    = d8;
    N->m.cap   = bRook;

    // Setup empty continuation (just null move)
    clrMove(NextNode->BestLine[0]);

    // Call UpdateBestLine
    UpdateBestLine(&E);

    // Verify current move was copied
    ASSERT_EQ(N->BestLine[0].piece, wQueen, "BestLine[0] should have queen move");
    ASSERT_EQ(N->BestLine[0].to, d8, "BestLine[0].to should be d8");

    // Verify null terminator immediately after
    ASSERT(isNull(N->BestLine[1]), "BestLine[1] should be null");
}

/**************************************************************************************************/
/*                                    PREPARE KILLERS TESTS                                       */
/**************************************************************************************************/

TEST(prepare_killers_disables_in_check)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N = E.S.currNode;

    // Setup: in check with positive killer counts
    N->check         = 1;
    N->killer1Count  = 5;
    N->killer2Count  = 3;
    N->killer1Active = true; // Should be disabled
    N->killer2Active = true;

    PrepareKillers(&E);

    // Verify killers are disabled when in check
    ASSERT_EQ(N->killer1Active, false, "killer1Active should be false in check");
    ASSERT_EQ(N->killer2Active, false, "killer2Active should be false in check");
}

TEST(prepare_killers_activates_with_positive_counts)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N = E.S.currNode;

    // Setup: not in check, with positive killer counts
    N->check         = 0;
    N->killer1Count  = 5;
    N->killer2Count  = 3;
    N->killer1Active = false;
    N->killer2Active = false;

    PrepareKillers(&E);

    // Verify killers are activated
    ASSERT_EQ(N->killer1Active, true, "killer1Active should be true with count > 0");
    ASSERT_EQ(N->killer2Active, true, "killer2Active should be true with count > 0");
}

TEST(prepare_killers_deactivates_with_zero_counts)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N = E.S.currNode;

    // Setup: not in check, with zero killer counts
    N->check         = 0;
    N->killer1Count  = 0;
    N->killer2Count  = 0;
    N->killer1Active = true;
    N->killer2Active = true;

    PrepareKillers(&E);

    // Verify killers are deactivated with zero counts
    ASSERT_EQ(N->killer1Active, false, "killer1Active should be false with count = 0");
    ASSERT_EQ(N->killer2Active, false, "killer2Active should be false with count = 0");
}

/**************************************************************************************************/
/*                                 KILLER REF COLLISION TESTS                                     */
/**************************************************************************************************/

TEST(killer_collision_detects_killer1_match)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N = E.S.currNode;

    // Setup killer1
    N->killer1.piece = wKnight;
    N->killer1.from  = d4;
    N->killer1.to    = f5;
    N->killer1.cap   = empty;
    N->killer1Active = true;
    N->gen           = gen_H; // Not gen_F1

    // Setup current move to match killer1
    N->m.piece = wKnight;
    N->m.from  = d4;
    N->m.to    = f5;
    N->m.cap   = empty;

    BOOL result = KillerRefCollision(&E);

    // Verify collision detected and killer1 deactivated
    ASSERT(result == true, "Should detect killer1 collision");
    ASSERT_EQ(N->killer1Active, false, "killer1Active should be deactivated");
}

TEST(killer_collision_no_match_different_square)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N = E.S.currNode;

    // Setup killer1
    N->killer1.piece = wKnight;
    N->killer1.from  = d4;
    N->killer1.to    = f5;
    N->killer1.cap   = empty;
    N->killer1Active = true;

    // Setup current move different from killer1
    N->m.piece = wKnight;
    N->m.from  = d4;
    N->m.to    = e6; // Different destination
    N->m.cap   = empty;

    // Setup rfm to not match
    N->rfm.piece = wPawn;
    N->rfm.from  = e2;
    N->rfm.to    = e4;
    N->rfm.cap   = empty;

    BOOL result = KillerRefCollision(&E);

    // Verify no collision
    ASSERT(result == false, "Should not detect collision with different square");
    ASSERT_EQ(N->killer1Active, true, "killer1Active should remain active");
}

TEST(killer_collision_skips_if_gen_matches)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N = E.S.currNode;

    // Setup killer1 matching move but gen = gen_F1
    N->killer1.piece = wKnight;
    N->killer1.from  = d4;
    N->killer1.to    = f5;
    N->killer1.cap   = empty;
    N->killer1Active = true;
    N->gen           = gen_F1; // Same as killer1Gen

    N->m.piece = wKnight;
    N->m.from  = d4;
    N->m.to    = f5;
    N->m.cap   = empty;

    // Setup rfm to not match
    N->rfm.piece = wPawn;
    N->rfm.from  = e2;
    N->rfm.to    = e4;
    N->rfm.cap   = empty;

    BOOL result = KillerRefCollision(&E);

    // Verify no collision when gen matches
    ASSERT(result == false, "Should not detect collision when gen = gen_F1");
    ASSERT_EQ(N->killer1Active, true, "killer1Active should remain active");
}

TEST(refutation_move_collision_detected)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N = E.S.currNode;

    // Setup inactive killers
    N->killer1Active = false;
    N->killer2Active = false;

    // Setup refutation move
    N->rfm.piece = wBishop;
    N->rfm.from  = c1;
    N->rfm.to    = f4;
    N->rfm.cap   = empty;

    // Setup current move to match rfm
    N->m.piece = wBishop;
    N->m.from  = c1;
    N->m.to    = f4;
    N->m.cap   = empty;

    BOOL result = KillerRefCollision(&E);

    // Verify refutation collision detected
    ASSERT(result == true, "Should detect refutation move collision");
}

/**************************************************************************************************/
/*                                    UPDATE KILLERS TESTS                                        */
/**************************************************************************************************/

TEST(update_killers_increments_killer1_count)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N = E.S.currNode;

    // Setup: not in check, bestGen = gen_F1 (killer1)
    N->check        = 0;
    N->bestGen      = gen_F1;
    N->killer1Count = 5;

    UpdateKillers(&E);

    // Verify killer1Count incremented
    ASSERT_EQ(N->killer1Count, 6, "killer1Count should be incremented");
}

TEST(update_killers_increments_killer2_count)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N = E.S.currNode;

    // Setup: not in check, bestGen = gen_F2 (killer2)
    N->check        = 0;
    N->bestGen      = gen_F2;
    N->killer2Count = 3;

    UpdateKillers(&E);

    // Verify killer2Count incremented
    ASSERT_EQ(N->killer2Count, 4, "killer2Count should be incremented");
}

TEST(update_killers_returns_early_in_check)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N = E.S.currNode;

    // Setup: in check
    N->check        = 1;
    N->bestGen      = gen_F1;
    N->killer1Count = 5;

    UpdateKillers(&E);

    // Verify no update happened
    ASSERT_EQ(N->killer1Count, 5, "killer1Count should not change in check");
}

TEST(update_killers_stores_new_killer)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N          = E.S.currNode;
    NODE *ParentNode = N - 1;

    // Setup: bestGen = gen_H (non-capture)
    N->check        = 0;
    N->bestGen      = gen_H;
    N->killer1Count = 0;
    N->killer2Count = 0;

    // Setup BestLine with new move
    N->BestLine[0].piece = wKnight;
    N->BestLine[0].from  = g1;
    N->BestLine[0].to    = f3;
    N->BestLine[0].cap   = empty;

    // Setup ParentNode->m.to different so we don't skip
    ParentNode->m.to = e4;

    UpdateKillers(&E);

    // Verify new killer stored in killer1
    ASSERT_EQ(N->killer1.piece, wKnight, "killer1 should store new move piece");
    ASSERT_EQ(N->killer1.from, g1, "killer1 should store new move from");
    ASSERT_EQ(N->killer1.to, f3, "killer1 should store new move to");
    ASSERT_EQ(N->killer1Count, 1, "killer1Count should be 1");
}

/**************************************************************************************************/
/*                                   UPDATE DRAW STATE TESTS                                      */
/**************************************************************************************************/

TEST(update_draw_state_sets_draw_type_none)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N          = E.S.currNode;
    NODE *ParentNode = N - 1;

    // Setup simple move
    ParentNode->m.piece = wKnight;
    ParentNode->m.from  = g1;
    ParentNode->m.to    = f3;
    ParentNode->m.cap   = empty;
    ParentNode->m.type  = mtype_Normal;

    // Setup draw data
    static DRAWDATA drawData[2];
    memset(drawData, 0, sizeof(drawData));
    N->drawData  = &drawData[1];
    N->gameDepth = 1;

    N->drawType = drawType_Rep2; // Set non-None initially

    UpdateDrawState(&E);

    // Verify drawType reset to None
    ASSERT_EQ(N->drawType, drawType_None, "drawType should be set to None");
}

TEST(update_draw_state_detects_insufficient_material_KK)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N          = E.S.currNode;
    NODE *ParentNode = N - 1;

    // Setup capture move (irreversible)
    ParentNode->m.piece = wKnight;
    ParentNode->m.from  = f3;
    ParentNode->m.to    = e5;
    ParentNode->m.cap   = bPawn;
    ParentNode->m.type  = mtype_Normal;

    // Setup draw data
    static DRAWDATA drawData[2];
    memset(drawData, 0, sizeof(drawData));
    N->drawData  = &drawData[1];
    N->gameDepth = 1;

    // Setup pieceCount for KK (only kings remain)
    E.B.pieceCount = 0x01000100; // King on both sides, nothing else

    UpdateDrawState(&E);

    // Verify insufficient material draw detected
    ASSERT_EQ(N->drawType, drawType_InsuffMtrl, "Should detect insufficient material (KK)");
}

TEST(update_draw_state_detects_50_move_rule)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N          = E.S.currNode;
    NODE *ParentNode = N - 1;

    // Setup reversible move (non-capture, non-pawn)
    ParentNode->m.piece = wKnight;
    ParentNode->m.from  = f3;
    ParentNode->m.to    = e5;
    ParentNode->m.cap   = empty;
    ParentNode->m.type  = mtype_Normal;

    // Setup draw data with 50-move countdown
    static DRAWDATA drawData[2];
    memset(drawData, 0, sizeof(drawData));
    drawData[0].irr = 0; // Last irreversible move at ply 0
    N->drawData     = &drawData[1];
    N->gameDepth    = 100; // 100 plies since last irreversible = 50 moves

    UpdateDrawState(&E);

    // Verify 50-move rule draw detected
    ASSERT_EQ(N->drawType, drawType_50, "Should detect 50-move rule draw");
}

TEST(update_draw_state_sets_irreversible_for_capture)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N          = E.S.currNode;
    NODE *ParentNode = N - 1;

    // Setup capture move (irreversible)
    ParentNode->m.piece = wQueen;
    ParentNode->m.from  = d1;
    ParentNode->m.to    = d8;
    ParentNode->m.cap   = bRook;
    ParentNode->m.type  = mtype_Normal;

    // Setup draw data
    static DRAWDATA drawData[2];
    memset(drawData, 0, sizeof(drawData));
    drawData[0].hashKey = 0x12345678;
    N->drawData         = &drawData[1];
    N->gameDepth        = 10;

    // Setup pieces so no insufficient material
    E.B.pieceCount = 0x12001200; // Multiple pieces

    UpdateDrawState(&E);

    // Verify irr set to current gameDepth
    ASSERT_EQ(drawData[1].irr, 10, "irr should be set to current gameDepth for capture");
    ASSERT_EQ(drawData[1].repCount, 0, "repCount should be 0 for irreversible move");
}

/**************************************************************************************************/
/*                                         TEST RUNNER                                            */
/**************************************************************************************************/

void run_searchmisc_tests()
{
    printf("Running SearchMisc Module Tests:\n");

    // UpdateBestLine tests
    RUN_TEST(update_best_line_copies_current_move);
    RUN_TEST(update_best_line_handles_empty_continuation);

    // PrepareKillers tests
    RUN_TEST(prepare_killers_disables_in_check);
    RUN_TEST(prepare_killers_activates_with_positive_counts);
    RUN_TEST(prepare_killers_deactivates_with_zero_counts);

    // KillerRefCollision tests
    RUN_TEST(killer_collision_detects_killer1_match);
    RUN_TEST(killer_collision_no_match_different_square);
    RUN_TEST(killer_collision_skips_if_gen_matches);
    RUN_TEST(refutation_move_collision_detected);

    // UpdateKillers tests
    RUN_TEST(update_killers_increments_killer1_count);
    RUN_TEST(update_killers_increments_killer2_count);
    RUN_TEST(update_killers_returns_early_in_check);
    RUN_TEST(update_killers_stores_new_killer);

    // UpdateDrawState tests
    RUN_TEST(update_draw_state_sets_draw_type_none);
    RUN_TEST(update_draw_state_detects_insufficient_material_KK);
    RUN_TEST(update_draw_state_detects_50_move_rule);
    RUN_TEST(update_draw_state_sets_irreversible_for_capture);

    printf("\n");
}
