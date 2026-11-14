/**************************************************************************************************/
/*                                                                                                */
/* Module  : GENERAL.H                                                                            */
/* Purpose : Portable type definitions and utility functions for cross-platform compilation      */
/*                                                                                                */
/**************************************************************************************************/

#ifndef GENERAL_H
#define GENERAL_H

#include <stdint.h>
#include <stdbool.h>

/**************************************************************************************************/
/*                                                                                                */
/*                                   MACOS TYPE COMPATIBILITY                                     */
/*                                                                                                */
/**************************************************************************************************/

// MacOS Carbon framework integer types (for legacy compatibility)
typedef int64_t  SInt64;
typedef uint64_t UInt64;
typedef int32_t  SInt32;
typedef uint32_t UInt32;
typedef int16_t  SInt16;
typedef uint16_t UInt16;
typedef int8_t   SInt8;
typedef uint8_t  UInt8;

/**************************************************************************************************/
/*                                                                                                */
/*                                     SIGMA CLASS LIBRARY TYPES                                  */
/*                                                                                                */
/**************************************************************************************************/

// nil definition for null pointers
#ifndef nil
#define nil NULL
#endif

// Common macros
#define maxint       0x7FFF
#define maxlong      0x7FFFFFFF
#define bit(i)       (1 << (i))
#define bitL(i)      (1L << (i))
#define clrBit(i, a) (a &= ~bit(i))
#define even(x)      (((x) & 1) == 0)
#define odd(x)       ((x) & 1)

// Engine type aliases - MODERNIZED TO C11 STANDARD TYPES
// Old Mac-style types are mapped to standard C types for portability
typedef int16_t  INT;     // Was: short
typedef int32_t  LONG;    // Was: long
typedef bool     BOOL;    // Was: short (now proper bool from stdbool.h)
typedef char     CHAR;    // Unchanged
typedef uint8_t  BYTE;    // Was: unsigned char
typedef void    *PTR;     // Was: unsigned char * (now generic pointer)
typedef double   REAL;    // Unchanged
typedef uint16_t UINT;    // Was: unsigned short
typedef uint32_t ULONG;   // Was: unsigned long
typedef int64_t  LONG64;  // Was: SInt64
typedef uint64_t ULONG64; // Was: UInt64
typedef PTR     *HANDLE;  // Unchanged (pointer to pointer)

/**************************************************************************************************/
/*                                                                                                */
/*                                       UTILITY FUNCTIONS                                        */
/*                                                                                                */
/**************************************************************************************************/

// Utility functions from original General.h with portable implementations
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <sys/time.h>

// Min/Max functions
static inline INT Min(INT a, INT b)
{
    return (a < b) ? a : b;
}

static inline INT Max(INT a, INT b)
{
    return (a > b) ? a : b;
}

static inline LONG MinL(LONG a, LONG b)
{
    return (a < b) ? a : b;
}

static inline LONG MaxL(LONG a, LONG b)
{
    return (a > b) ? a : b;
}

static inline INT Abs(INT x)
{
    return (x < 0) ? -x : x;
}

// Sign function: returns -1, 0, or 1
static inline INT Sign(INT x)
{
    return (x > 0) ? 1 : ((x < 0) ? -1 : 0);
}

// Square function: returns x squared
static inline INT Sqr(INT x)
{
    return x * x;
}

// Helper functions for type-safe swap
static inline void _swap_int16(int16_t *a, int16_t *b)
{
    int16_t temp = *a;
    *a           = *b;
    *b           = temp;
}

static inline void _swap_ptr(void **a, void **b)
{
    void *temp = *a;
    *a         = *b;
    *b         = temp;
}

// Type-safe swap macro using C11 _Generic
// Automatically selects the correct swap implementation based on pointer type
// Usage: Swap(&a, &b) where a and b are int16_t/SQUARE/INT or any pointer type
#define Swap(a, b) _Generic((a), int16_t *: _swap_int16, default: _swap_ptr)(a, b)

// Legacy alias for compatibility with existing code
#define SwapSquare(a, b) Swap(a, b)

// Timer function (returns ticks since start)
// Milliseconds since process start. Anchored to the first call so the value stays
// within 32-bit range for the engine's lifetime — necessary because deadlines are
// computed as `Now() + offset` and stored in ULONG, which would wrap on epoch-ms.
static inline LONG Now(void)
{
    static LONG    anchor_sec = 0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (anchor_sec == 0)
        anchor_sec = (LONG)tv.tv_sec;
    return (LONG)((tv.tv_sec - anchor_sec) * 1000 + tv.tv_usec / 1000);
}

// Random number function (0 to n-1)
static inline INT Rand(INT n)
{
    return rand() % n;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                  DEFENSIVE PROGRAMMING MACROS                                  */
/*                                                                                                */
/**************************************************************************************************/

/*
 * These macros provide runtime safety checks that are enabled in DEBUG builds
 * and compile away to nothing in RELEASE builds (when NDEBUG is defined).
 *
 * This gives you:
 * - Zero overhead in production (release) builds
 * - Early bug detection during development (debug) builds
 * - Self-documenting code (assertions document assumptions)
 */

// Debug assertion - checks condition and aborts with message if false
// Compiles to nothing in release builds (when NDEBUG is defined)
#ifndef NDEBUG
#define ASSERT(condition, message)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            fprintf(stderr, "ASSERTION FAILED: %s\n", message);                                                        \
            fprintf(stderr, "  File: %s, Line: %d\n", __FILE__, __LINE__);                                             \
            fprintf(stderr, "  Condition: %s\n", #condition);                                                          \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
#else
#define ASSERT(condition, message) ((void)0)
#endif

// Pointer validation - ensures pointer is not NULL before dereferencing
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL, "Unexpected NULL pointer: " #ptr)

// Square validation - ensures square is on the board (not off-board 0x88 representation)
#define ASSERT_VALID_SQUARE(sq) ASSERT(!((sq) & 0x88), "Invalid square (off-board): " #sq)

// Array bounds checking - ensures index is within valid range [min, max)
#define ASSERT_IN_RANGE(value, min, max)                                                                               \
    ASSERT((value) >= (min) && (value) < (max), "Index out of bounds: " #value " must be in [" #min ", " #max ")")

// Specific bounds checks for common arrays
#define ASSERT_VALID_BYTE_INDEX(idx)   ASSERT_IN_RANGE(idx, 0, 256)            // For 256-element lookup tables
#define ASSERT_NONZERO_BYTE_INDEX(idx) ASSERT_IN_RANGE(idx, 1, 256)            // For tables where index 0 is invalid
#define ASSERT_VALID_PIECE(piece)      ASSERT_IN_RANGE(pieceType(piece), 0, 7) // For piece type validation
#define ASSERT_VALID_DIRECTION(dir)    ASSERT((dir) != 0, "Direction cannot be zero: " #dir)

// Board state invariant checking (can be extended with actual checks)
// These are more expensive so they're only enabled with DEBUG_INVARIANTS
#ifdef DEBUG_INVARIANTS
#define CHECK_BOARD_INVARIANTS(E)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        /* Future: Add comprehensive board consistency checks here */                                                  \
        /* - Piece counts match board configuration */                                                                 \
        /* - PieceLoc matches Board positions */                                                                       \
        /* - Attack tables are consistent */                                                                           \
    } while (0)
#else
#define CHECK_BOARD_INVARIANTS(E) ((void)0)
#endif

// Verify that a value matches expected value (useful for detecting corruption)
#define ASSERT_EQUALS(actual, expected) ASSERT((actual) == (expected), #actual " should equal " #expected)

// Warn in debug builds but don't abort (for recoverable issues)
#ifndef NDEBUG
#define DEBUG_WARN(condition, message)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            fprintf(stderr, "DEBUG WARNING: %s\n", message);                                                           \
            fprintf(stderr, "  File: %s, Line: %d\n", __FILE__, __LINE__);                                             \
        }                                                                                                              \
    } while (0)
#else
#define DEBUG_WARN(condition, message) ((void)0)
#endif

/**************************************************************************************************/
/*                                                                                                */
/*                                  SAFE ARRAY ACCESS HELPERS                                     */
/*                                                                                                */
/**************************************************************************************************/

// Safe array access with bounds checking in debug builds
// In release builds, these compile to simple array access with zero overhead

// Safe access to 256-element lookup tables (QRBdata, BlockTab, etc.)
// These tables should never be accessed with index 0 in normal operation
#define SAFE_LOOKUP_TABLE_ACCESS(table, idx, field) (ASSERT_NONZERO_BYTE_INDEX(idx), (table)[(idx)].field)

// Safe access to Board array (checks square is valid)
#define SAFE_BOARD_ACCESS(board, sq) (ASSERT_VALID_SQUARE(sq), (board)[sq])

// Safe access to any array with explicit bounds
#define SAFE_ARRAY_ACCESS(array, idx, size) (ASSERT_IN_RANGE(idx, 0, size), (array)[idx])

#endif // GENERAL_H
