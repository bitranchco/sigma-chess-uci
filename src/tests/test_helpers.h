/**************************************************************************************************/
/*                                                                                                */
/* Module  : TEST_HELPERS.H                                                                       */
/* Purpose : Helper functions and macros for testing the Chess Engine                            */
/*                                                                                                */
/**************************************************************************************************/

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Engine.h"
#include "../Data Structures/Attack.f"
#include "../Data Structures/Board.f"

// Test statistics
extern int tests_passed;
extern int tests_failed;

// Simple test macros
#define TEST(name) void test_##name()

#define RUN_TEST(name)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        printf("  Running %s... ", #name);                                                                             \
        fflush(stdout);                                                                                                \
        int failed_before = tests_failed;                                                                              \
        test_##name();                                                                                                 \
        if (tests_failed == failed_before)                                                                             \
        {                                                                                                              \
            printf("PASSED\n");                                                                                        \
            tests_passed++;                                                                                            \
        }                                                                                                              \
    } while (0)

#define ASSERT(condition, message)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("\n    FAILED: %s\n", message);                                                                     \
            printf("    at %s:%d\n", __FILE__, __LINE__);                                                              \
            tests_failed++;                                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define ASSERT_EQ(actual, expected, message)                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((actual) != (expected))                                                                                    \
        {                                                                                                              \
            printf("\n    FAILED: %s\n", message);                                                                     \
            printf("    Expected: %d, Got: %d\n", (int)(expected), (int)(actual));                                     \
            printf("    at %s:%d\n", __FILE__, __LINE__);                                                              \
            tests_failed++;                                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define ASSERT_NE(actual, not_expected, message)                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((actual) == (not_expected))                                                                                \
        {                                                                                                              \
            printf("\n    FAILED: %s\n", message);                                                                     \
            printf("    Should not be: %d\n", (int)(not_expected));                                                    \
            printf("    at %s:%d\n", __FILE__, __LINE__);                                                              \
            tests_failed++;                                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

// Helper functions for setting up engine state
void InitTestGlobal(GLOBAL *Global);
void InitTestEngine(ENGINE *E, GLOBAL *Global);
void SetupEmptyBoard(ENGINE *E);
void SetupInitialPosition(ENGINE *E);

// Test fixture functions for common board setups
// These properly initialize Board, PieceLoc, PLinx, and Attack tables

// Place a piece and update all necessary data structures
void PlacePiece(ENGINE *E, SQUARE sq, PIECE piece, int plinx);

// Setup a simple position with just kings (minimal legal position)
void SetupKingsOnly(ENGINE *E);

// Setup position with kings and one piece for white
void SetupWhitePiecePosition(ENGINE *E, SQUARE sq, PIECE piece);

// Setup position with kings and one piece for black
void SetupBlackPiecePosition(ENGINE *E, SQUARE sq, PIECE piece);

// Setup position for move generation testing (sets up NODE state)
void SetupMoveGenTest(ENGINE *E, COLOUR player, SQUARE *attackedPieces, SQUARE *safePieces);

// Setup position for sacrifice buffer testing
void SetupSacrificeTest(ENGINE *E);
