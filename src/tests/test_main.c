/**************************************************************************************************/
/*                                                                                                */
/* Module  : TEST_MAIN.C                                                                          */
/* Purpose : Main test runner for Chess Engine tests                                             */
/*                                                                                                */
/**************************************************************************************************/

#include <stdio.h>
#include "test_helpers.h"

// Test suites
void run_attack_tests();
void run_pawn_move_tests();
void run_perform_move_tests();
void run_movegen_tests();
void run_noncapture_tests();
void run_check_evasion_tests();
void run_searchmisc_tests();
void run_nodesearch_tests();
void run_selection_tests();
void run_transtables_tests();
void run_evaluate_tests();

int main()
{
    printf("========================================\n");
    printf("  Chess Engine Unit Tests\n");
    printf("========================================\n");

    // Run all test suites
    run_attack_tests();
    run_pawn_move_tests();
    run_perform_move_tests(); // Test PerformMove/RetractMove before higher-level tests
    run_movegen_tests();
    run_noncapture_tests();
    run_check_evasion_tests();
    run_searchmisc_tests();  // Test SearchMisc helper functions
    run_nodesearch_tests();  // Test NodeSearch functionality
    run_selection_tests();   // Test Selection (forward pruning)
    run_transtables_tests(); // Test Transposition Tables
    run_evaluate_tests();    // Test Evaluate module

    // Print summary
    printf("\n========================================\n");
    printf("  Test Summary\n");
    printf("========================================\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("========================================\n");

    return (tests_failed == 0) ? 0 : 1;
}
