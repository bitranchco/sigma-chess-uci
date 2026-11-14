/**************************************************************************************************/
/*                                                                                                */
/* Module  : TEST_TRANSTABLES.C                                                                   */
/* Purpose : Unit tests for transposition table functions                                        */
/*                                                                                                */
/**************************************************************************************************/

#include "../Engine.h"
#include "test_helpers.h"
#include "../Data Structures/Board.f"
#include "../Data Structures/Attack.f"
#include "../Searching/TransTables.f"
#include "../Misc/HashCode.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                      HELPER FUNCTIONS                                          */
/*                                                                                                */
/**************************************************************************************************/

// Helper to read TRANS entry fields (mirrors the inline functions in TransTables.c)
static inline HKEY GetTransHashKey(TRANS *t)
{
    return *((HKEY *)&t->data[0]);
}

static inline BYTE GetTransPly(TRANS *t)
{
    return t->data[4];
}

static inline BYTE GetTransMaxPly(TRANS *t)
{
    return t->data[5];
}

static inline INT GetTransScore(TRANS *t)
{
    return *((INT *)&t->data[6]);
}

static inline BYTE GetTransFrom(TRANS *t)
{
    return t->data[8];
}

static inline BYTE GetTransTo(TRANS *t)
{
    return t->data[9];
}

// Helper to setup trans tables for testing
void SetupTransTables(ENGINE *E, TRANS **transBuf)
{
    LONG minSize     = (1L << 13) * sizeof(TRANS);
    *transBuf        = (TRANS *)malloc(minSize);
    E->P.TransTables = *transBuf;
    E->P.transSize   = minSize;
    E->S.whiteNode   = NULL; // Skip whiteNode array initialization
    CalcTransState(E);
    ResetTransTab(E);

    // Manually set trans table pointers for current node
    E->S.currNode->TransTab1 = E->Tr.TransTab1W;
    E->S.currNode->TransTab2 = E->Tr.TransTab2W;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                  CALCTRANSSTATE TESTS                                          */
/*                                                                                                */
/**************************************************************************************************/

TEST(calc_trans_state_disabled_when_too_small)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Set up insufficient memory
    E.P.transSize = 1000; // Too small

    E.S.whiteNode = NULL; // Skip whiteNode initialization
    CalcTransState(&E);

    ASSERT(!E.Tr.transTabOn, "TransTab should be disabled with insufficient memory");
    ASSERT_EQ(E.Tr.transSize, 0, "transSize should be 0 when disabled");
    ASSERT_EQ(E.Tr.transUsed, 0, "transUsed should be 0 when disabled");
}

TEST(calc_trans_state_enabled_with_sufficient_memory)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Allocate actual memory for trans tables
    LONG   minSize  = (1L << 13) * sizeof(TRANS); // Minimum = 8192 entries
    TRANS *transBuf = (TRANS *)malloc(minSize);
    ASSERT(transBuf != NULL, "Failed to allocate trans table buffer");

    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;

    // Set whiteNode to NULL so CalcTransState doesn't try to initialize all nodes
    E.S.whiteNode = NULL;

    E.S.whiteNode = NULL; // Skip whiteNode initialization
    CalcTransState(&E);

    ASSERT(E.Tr.transTabOn, "TransTab should be enabled with sufficient memory");
    ASSERT(E.Tr.transSize >= 8192, "transSize should be at least 8192");
    ASSERT(E.Tr.TransTab1W != NULL, "TransTab1W should be allocated");
    ASSERT(E.Tr.TransTab1B != NULL, "TransTab1B should be allocated");
    ASSERT(E.Tr.TransTab2W != NULL, "TransTab2W should be allocated");
    ASSERT(E.Tr.TransTab2B != NULL, "TransTab2B should be allocated");

    // Manually set trans table pointers for current node (since whiteNode is NULL)
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    free(transBuf);
}

TEST(calc_trans_state_sets_hash_mask)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Allocate memory
    LONG   minSize  = (1L << 13) * sizeof(TRANS);
    TRANS *transBuf = (TRANS *)malloc(minSize);
    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;

    E.S.whiteNode = NULL; // Skip node initialization

    E.S.whiteNode = NULL; // Skip whiteNode initialization
    CalcTransState(&E);

    // hashIndexMask should be (transSize/4) - 1
    HKEY expectedMask = (E.Tr.transSize / 4) - 1;
    ASSERT_EQ(E.Tr.hashIndexMask, expectedMask, "hashIndexMask calculation");

    free(transBuf);
}

/**************************************************************************************************/
/*                                                                                                */
/*                                  RESETTRANSTAB TESTS                                           */
/*                                                                                                */
/**************************************************************************************************/

TEST(reset_trans_tab_marks_all_unused)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup trans tables
    LONG   minSize  = (1L << 13) * sizeof(TRANS);
    TRANS *transBuf = (TRANS *)malloc(minSize);
    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;

    E.S.whiteNode = NULL; // Skip whiteNode initialization
    CalcTransState(&E);

    // Set some entries to non-0xFF to simulate usage
    E.Tr.TransTab1W[0].data[4]  = 5;
    E.Tr.TransTab1B[10].data[4] = 3;
    E.Tr.transUsed              = 100;

    ResetTransTab(&E);

    // Manually set trans table pointers for current node
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    ASSERT_EQ(E.Tr.transUsed, 0, "transUsed should be reset to 0");

    // Check that all entries are marked unused (ply = 0xFF)
    LONG count = E.Tr.transSize / 4;
    for (LONG i = 0; i < count; i++)
    {
        ASSERT_EQ(E.Tr.TransTab1W[i].data[4], 0xFF, "TransTab1W entry should be marked unused");
        ASSERT_EQ(E.Tr.TransTab1B[i].data[4], 0xFF, "TransTab1B entry should be marked unused");
        ASSERT_EQ(E.Tr.TransTab2W[i].data[4], 0xFF, "TransTab2W entry should be marked unused");
        ASSERT_EQ(E.Tr.TransTab2B[i].data[4], 0xFF, "TransTab2B entry should be marked unused");
    }

    free(transBuf);
}

TEST(reset_trans_tab_does_nothing_when_disabled)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    E.Tr.transTabOn = false;
    E.Tr.transUsed  = 42;

    ResetTransTab(&E);

    // Manually set trans table pointers for current node
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    // Should not crash and transUsed should remain unchanged
    ASSERT_EQ(E.Tr.transUsed, 42, "transUsed should not change when disabled");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                  PROBETRANSTAB TESTS                                           */
/*                                                                                                */
/**************************************************************************************************/

TEST(probe_trans_tab_returns_false_when_disabled)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N     = E.S.currNode;
    N->hashKey  = 0x12345678;
    N->drawType = drawType_None;
    N->pvNode   = false;

    // Disable trans tables
    E.R.rflags = 0; // rflag_TransTabOn not set

    BOOL result = ProbeTransTab(&E);

    ASSERT(!result, "ProbeTransTab should return false when disabled");
    ASSERT_EQ(N->rfm.piece, empty, "rfm should be cleared");
}

TEST(probe_trans_tab_returns_false_on_draw)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N     = E.S.currNode;
    N->hashKey  = 0x12345678;
    N->drawType = drawType_Rep2; // Draw by repetition
    N->pvNode   = false;

    E.R.rflags = rflag_TransTabOn;

    BOOL result = ProbeTransTab(&E);

    ASSERT(!result, "ProbeTransTab should return false on draw");
}

TEST(probe_trans_tab_returns_false_on_pv_node)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup trans tables
    LONG   minSize  = (1L << 13) * sizeof(TRANS);
    TRANS *transBuf = (TRANS *)malloc(minSize);
    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;
    E.S.whiteNode   = NULL; // Skip whiteNode initialization
    CalcTransState(&E);
    ResetTransTab(&E);

    // Manually set trans table pointers for current node
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    NODE *N     = E.S.currNode;
    N->hashKey  = 0x12345678;
    N->drawType = drawType_None;
    N->pvNode   = true; // PV node

    E.R.rflags = rflag_TransTabOn;

    BOOL result = ProbeTransTab(&E);

    ASSERT(!result, "ProbeTransTab should return false on PV node");

    free(transBuf);
}

TEST(probe_trans_tab_finds_exact_score)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup trans tables
    LONG   minSize  = (1L << 13) * sizeof(TRANS);
    TRANS *transBuf = (TRANS *)malloc(minSize);
    memset(transBuf, 0, minSize); // Zero out to avoid garbage data
    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;
    E.S.whiteNode   = NULL; // Skip whiteNode initialization
    CalcTransState(&E);
    // Don't reset - we'll manually create entries
    // ResetTransTab(&E);

    // Manually set trans table pointers for current node
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    NODE *N     = E.S.currNode;
    N->hashKey  = 0x12345000; // Low 11 bits = 0 for simplicity
    N->drawType = drawType_None;
    N->pvNode   = false;
    N->ply      = 5;
    N->maxPly   = 10;
    N->depth    = 3;
    N->alpha    = -100;
    N->alpha0   = -100; // Must set alpha0 for ProbeTransTab
    N->beta     = 100;

    E.R.rflags = rflag_TransTabOn;

    // Manually create a matching entry in trans1
    LONG   index = (N->hashKey & E.Tr.hashIndexMask);
    TRANS *entry = N->TransTab1 + index;

    // Set hash key with TrueScoreBit
    HKEY hashkey                   = (N->hashKey & trans_HashLockMask) | trans_TrueScoreBit;
    *((uint32_t *)&entry->data[0]) = (uint32_t)hashkey; // Write only 4 bytes
    entry->data[4]                 = N->ply;            // Same ply
    entry->data[5]                 = N->maxPly;         // Same maxPly
    *((INT *)&entry->data[6])      = 50;                // Score

    BOOL result = ProbeTransTab(&E);

    ASSERT(result, "ProbeTransTab should return true for exact match");
    ASSERT_EQ(N->score, 50, "Score should be retrieved from trans table");

    free(transBuf);
}

TEST(probe_trans_tab_adjusts_mate_scores)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup trans tables
    LONG   minSize  = (1L << 13) * sizeof(TRANS);
    TRANS *transBuf = (TRANS *)malloc(minSize);
    memset(transBuf, 0, minSize); // Zero out to avoid garbage data
    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;
    E.S.whiteNode   = NULL; // Skip whiteNode initialization
    CalcTransState(&E);
    // Don't reset - we'll manually create entries
    // ResetTransTab(&E);

    // Manually set trans table pointers for current node
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    NODE *N     = E.S.currNode;
    N->hashKey  = 0x12345000;
    N->drawType = drawType_None;
    N->pvNode   = false;
    N->ply      = 5;
    N->maxPly   = 10;
    N->depth    = 3;
    N->alpha    = -10000;
    N->beta     = 10000;

    E.R.rflags = rflag_TransTabOn;

    // Create entry with mate score
    LONG   index = (N->hashKey & E.Tr.hashIndexMask);
    TRANS *entry = N->TransTab1 + index;

    HKEY hashkey                   = (N->hashKey & trans_HashLockMask) | trans_TrueScoreBit;
    *((uint32_t *)&entry->data[0]) = (uint32_t)hashkey; // Write only 4 bytes
    entry->data[4]                 = N->ply;
    entry->data[5]                 = N->maxPly;
    *((INT *)&entry->data[6])      = mateWinVal + 100; // Mate score

    BOOL result = ProbeTransTab(&E);

    ASSERT(result, "ProbeTransTab should find mate score");
    // Score should be adjusted by depth: (mateWinVal + 100) - 3
    ASSERT_EQ(N->score, mateWinVal + 100 - N->depth, "Mate score should be adjusted");

    free(transBuf);
}

TEST(probe_trans_tab_returns_refutation_move)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup trans tables
    LONG   minSize  = (1L << 13) * sizeof(TRANS);
    TRANS *transBuf = (TRANS *)malloc(minSize);
    memset(transBuf, 0, minSize); // Zero out to avoid garbage data
    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;
    E.S.whiteNode   = NULL; // Skip whiteNode initialization
    CalcTransState(&E);
    // Don't reset - we'll manually create entries
    // ResetTransTab(&E);

    // Manually set trans table pointers for current node
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    NODE *N     = E.S.currNode;
    N->hashKey  = 0x12345000;
    N->drawType = drawType_None;
    N->pvNode   = false;
    N->ply      = 5;
    N->maxPly   = 10;
    N->player   = white;

    E.R.rflags = rflag_TransTabOn;

    // Create entry with a stored move (knight piece type = 1)
    TRANS *entry = N->TransTab1 + (N->hashKey & E.Tr.hashIndexMask);

    HKEY hashkey                   = (N->hashKey & trans_HashLockMask) | knight; // Store piece type
    *((uint32_t *)&entry->data[0]) = (uint32_t)hashkey;                          // Write only 4 bytes
    entry->data[4]                 = N->ply - 1; // Older ply (not deep enough for score)
    entry->data[5]                 = N->maxPly - 1;
    entry->data[8]                 = d4; // From square
    entry->data[9]                 = f5; // To square

    BOOL result = ProbeTransTab(&E);

    ASSERT(!result, "Should not return true (score not deep enough)");
    ASSERT_EQ(N->rfm.piece, wKnight, "Should extract refutation move piece");
    ASSERT(N->tmove == entry, "tmove should point to trans entry");

    free(transBuf);
}

/**************************************************************************************************/
/*                                                                                                */
/*                                  GETTRANSMOVE TESTS                                            */
/*                                                                                                */
/**************************************************************************************************/

TEST(get_trans_move_validates_knight_move)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup board with knight on d4
    SetupWhitePiecePosition(&E, d4, wKnight);

    NODE *N   = E.S.currNode;
    N->player = white;
    N->quies  = false;
    N->maxPly = 10; // Must be non-zero for non-quiescence search

    // Create a trans entry with knight move d4-f5
    TRANS transMoveEntry;
    HKEY  hashkey = (knight) | (0 << 3) | trans_ShQuiesBit;     // knight piece, no capture, set ShQuiesBit
    *((uint32_t *)&transMoveEntry.data[0]) = (uint32_t)hashkey; // Write only 4 bytes
    transMoveEntry.data[8]                 = d4;                // from
    transMoveEntry.data[9]                 = f5;                // to

    N->tmove     = &transMoveEntry;
    N->rfm.piece = wKnight;

    BOOL result = GetTransMove(&E);

    ASSERT(result, "Knight move should be valid");
    ASSERT_EQ(N->rfm.from, d4, "Move from square");
    ASSERT_EQ(N->rfm.to, f5, "Move to square");
    ASSERT_EQ(N->rfm.cap, empty, "No capture");
}

TEST(get_trans_move_rejects_illegal_position)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup board with knight on d4
    SetupWhitePiecePosition(&E, d4, wKnight);

    NODE *N   = E.S.currNode;
    N->player = white;
    N->quies  = false;
    N->maxPly = 10; // Must be non-zero for non-quiescence search

    // Create a trans entry claiming knight is on e4 (wrong!)
    TRANS transMoveEntry;
    HKEY  hashkey                          = knight | trans_ShQuiesBit;
    *((uint32_t *)&transMoveEntry.data[0]) = (uint32_t)hashkey; // Write only 4 bytes
    transMoveEntry.data[8]                 = e4;                // from (but knight is on d4!)
    transMoveEntry.data[9]                 = f6;                // to

    N->tmove     = &transMoveEntry;
    N->rfm.piece = wKnight;

    BOOL result = GetTransMove(&E);

    ASSERT(!result, "Move should be rejected (piece not on from square)");
    ASSERT_EQ(N->rfm.piece, empty, "rfm should be cleared");
}

TEST(get_trans_move_validates_pawn_advance)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup board with white pawn on e2
    SetupWhitePiecePosition(&E, e2, wPawn);

    NODE *N    = E.S.currNode;
    N->player  = white;
    N->pawnDir = 0x10;
    N->quies   = false;
    N->maxPly  = 10; // Must be non-zero for non-quiescence search

    // Create trans entry with pawn move e2-e4
    TRANS transMoveEntry;
    HKEY  hashkey                          = pawn | trans_ShQuiesBit;
    *((uint32_t *)&transMoveEntry.data[0]) = (uint32_t)hashkey; // Write only 4 bytes
    transMoveEntry.data[8]                 = e2;
    transMoveEntry.data[9]                 = e4;

    N->tmove     = &transMoveEntry;
    N->rfm.piece = wPawn;

    BOOL result = GetTransMove(&E);

    ASSERT(result, "Pawn advance should be valid");
    ASSERT_EQ(N->rfm.from, e2, "Pawn from square");
    ASSERT_EQ(N->rfm.to, e4, "Pawn to square");
}

TEST(get_trans_move_validates_sliding_piece)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup board with bishop on c1
    SetupWhitePiecePosition(&E, c1, wBishop);

    NODE *N   = E.S.currNode;
    N->player = white;
    N->quies  = false;
    N->maxPly = 10; // Must be non-zero for non-quiescence search

    // Create trans entry with bishop move c1-f4 (diagonal)
    TRANS transMoveEntry;
    HKEY  hashkey                          = bishop | trans_ShQuiesBit;
    *((uint32_t *)&transMoveEntry.data[0]) = (uint32_t)hashkey; // Write only 4 bytes
    transMoveEntry.data[8]                 = c1;
    transMoveEntry.data[9]                 = f4;

    N->tmove     = &transMoveEntry;
    N->rfm.piece = wBishop;
    N->rfm.dir   = E.Global->A.AttackDir[f4 - c1] >> 5;

    BOOL result = GetTransMove(&E);

    ASSERT(result, "Bishop diagonal move should be valid");
    ASSERT_EQ(N->rfm.from, c1, "Bishop from square");
    ASSERT_EQ(N->rfm.to, f4, "Bishop to square");
}

TEST(get_trans_move_rejects_blocked_sliding_piece)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup board with bishop on c1 and pawn on d2 blocking
    SetupWhitePiecePosition(&E, c1, wBishop);
    PlacePiece(&E, d2, wPawn, 2);
    E.B.PieceLocW[3] = nullSq;

    NODE *N   = E.S.currNode;
    N->player = white;
    N->quies  = false;
    N->maxPly = 10; // Must be non-zero for non-quiescence search

    // Try to move bishop c1-f4 (blocked by d2)
    TRANS transMoveEntry;
    HKEY  hashkey                          = bishop | trans_ShQuiesBit;
    *((uint32_t *)&transMoveEntry.data[0]) = (uint32_t)hashkey; // Write only 4 bytes
    transMoveEntry.data[8]                 = c1;
    transMoveEntry.data[9]                 = f4;

    N->tmove     = &transMoveEntry;
    N->rfm.piece = wBishop;
    N->rfm.dir   = E.Global->A.AttackDir[f4 - c1] >> 5;

    BOOL result = GetTransMove(&E);

    ASSERT(!result, "Blocked bishop move should be rejected");
    ASSERT_EQ(N->rfm.piece, empty, "rfm should be cleared");
}

/**************************************************************************************************/
/*                                                                                                */
/*                                  STORETRANSTAB TESTS                                           */
/*                                                                                                */
/**************************************************************************************************/

TEST(store_trans_tab_does_nothing_when_disabled)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N     = E.S.currNode;
    N->drawType = drawType_None;

    E.R.rflags = 0; // Not enabled

    // Should not crash
    StoreTransTab(&E);
}

TEST(store_trans_tab_does_nothing_on_draw)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    NODE *N     = E.S.currNode;
    N->drawType = drawType_Rep2;

    E.R.rflags = rflag_TransTabOn;

    // Should not crash
    StoreTransTab(&E);
}

TEST(store_trans_tab_stores_exact_score)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup trans tables
    LONG   minSize  = (1L << 13) * sizeof(TRANS);
    TRANS *transBuf = (TRANS *)malloc(minSize);
    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;
    E.S.whiteNode   = NULL; // Skip whiteNode initialization
    CalcTransState(&E);
    ResetTransTab(&E);

    // Manually set trans table pointers for current node
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    NODE *N              = E.S.currNode;
    N->hashKey           = 0x12345000;
    N->drawType          = drawType_None;
    N->ply               = 5;
    N->maxPly            = 10;
    N->depth             = 3;
    N->score             = 50;
    N->alpha0            = -100;
    N->beta              = 100;
    N->BestLine[0].piece = wKnight;
    N->BestLine[0].from  = d4;
    N->BestLine[0].to    = f5;
    N->BestLine[0].cap   = empty;
    N->BestLine[0].type  = mtype_Normal;
    N->BestLine[0].dply  = 0;
    N->bestGen           = gen_H;

    E.R.rflags = rflag_TransTabOn;

    // Manually set trans1/trans2 (normally set by ProbeTransTab)
    LONG index = (N->hashKey & E.Tr.hashIndexMask);
    N->trans1  = N->TransTab1 + index;
    N->trans2  = N->TransTab2 + index;

    StoreTransTab(&E);

    // Verify entry was stored
    TRANS *entry = N->trans1; // Should use trans1 for new entry

    HKEY storedKey = GetTransHashKey(entry);
    ASSERT((storedKey & trans_TrueScoreBit) != 0, "Should have TrueScoreBit set");
    ASSERT_EQ(GetTransPly(entry), 5, "Stored ply");
    ASSERT_EQ(GetTransMaxPly(entry), 10, "Stored maxPly");
    ASSERT_EQ(GetTransScore(entry), 50, "Stored score");
    ASSERT_EQ(GetTransFrom(entry), d4, "Stored from square");
    ASSERT_EQ(GetTransTo(entry), f5, "Stored to square");

    free(transBuf);
}

TEST(store_trans_tab_stores_cutoff)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup trans tables
    LONG   minSize  = (1L << 13) * sizeof(TRANS);
    TRANS *transBuf = (TRANS *)malloc(minSize);
    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;
    E.S.whiteNode   = NULL; // Skip whiteNode initialization
    CalcTransState(&E);
    ResetTransTab(&E);

    // Manually set trans table pointers for current node
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    NODE *N              = E.S.currNode;
    N->hashKey           = 0x12345000;
    N->drawType          = drawType_None;
    N->ply               = 5;
    N->maxPly            = 10;
    N->depth             = 3;
    N->score             = 150; // >= beta
    N->alpha0            = -100;
    N->beta              = 100;
    N->BestLine[0].piece = empty; // No move
    N->bestGen           = gen_H;

    E.R.rflags = rflag_TransTabOn;

    // Manually set trans1/trans2 (normally set by ProbeTransTab)
    LONG index = (N->hashKey & E.Tr.hashIndexMask);
    N->trans1  = N->TransTab1 + index;
    N->trans2  = N->TransTab2 + index;

    StoreTransTab(&E);

    TRANS *entry     = N->trans1;
    HKEY   storedKey = GetTransHashKey(entry);

    ASSERT((storedKey & trans_CutoffBit) != 0, "Should have CutoffBit set");
    ASSERT_EQ(GetTransScore(entry), 100, "Score should be capped at beta");

    free(transBuf);
}

TEST(store_trans_tab_adjusts_mate_scores)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup trans tables
    LONG   minSize  = (1L << 13) * sizeof(TRANS);
    TRANS *transBuf = (TRANS *)malloc(minSize);
    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;
    E.S.whiteNode   = NULL; // Skip whiteNode initialization
    CalcTransState(&E);
    ResetTransTab(&E);

    // Manually set trans table pointers for current node
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    NODE *N              = E.S.currNode;
    N->hashKey           = 0x12345000;
    N->drawType          = drawType_None;
    N->ply               = 5;
    N->maxPly            = 10;
    N->depth             = 3;
    N->score             = mateWinVal + 50; // Mate score within alpha-beta window
    N->alpha0            = -30000;
    N->beta              = 30000; // Wide window so score won't be clamped
    N->BestLine[0].piece = empty;
    N->bestGen           = gen_H;

    E.R.rflags = rflag_TransTabOn;

    // Manually set trans1/trans2 (normally set by ProbeTransTab)
    LONG index = (N->hashKey & E.Tr.hashIndexMask);
    N->trans1  = N->TransTab1 + index;
    N->trans2  = N->TransTab2 + index;

    StoreTransTab(&E);

    TRANS *entry       = N->trans1;
    INT    storedScore = GetTransScore(entry);

    // Score should be adjusted: (mateWinVal + 50) + depth
    ASSERT_EQ(storedScore, mateWinVal + 50 + N->depth, "Mate score should be adjusted");

    free(transBuf);
}

TEST(store_trans_tab_increments_used_count)
{
    GLOBAL Global;
    ENGINE E;

    InitTestGlobal(&Global);
    InitTestEngine(&E, &Global);

    // Setup trans tables
    LONG   minSize  = (1L << 13) * sizeof(TRANS);
    TRANS *transBuf = (TRANS *)malloc(minSize);
    E.P.TransTables = transBuf;
    E.P.transSize   = minSize;
    E.S.whiteNode   = NULL; // Skip whiteNode initialization
    CalcTransState(&E);
    ResetTransTab(&E);

    // Manually set trans table pointers for current node
    E.S.currNode->TransTab1 = E.Tr.TransTab1W;
    E.S.currNode->TransTab2 = E.Tr.TransTab2W;

    ASSERT_EQ(E.Tr.transUsed, 0, "transUsed should start at 0");

    NODE *N              = E.S.currNode;
    N->hashKey           = 0x12345000;
    N->drawType          = drawType_None;
    N->ply               = 5;
    N->maxPly            = 10;
    N->score             = 50;
    N->alpha0            = -100;
    N->beta              = 100;
    N->BestLine[0].piece = empty;
    N->bestGen           = gen_H;

    E.R.rflags = rflag_TransTabOn;

    // Manually set trans1/trans2 (normally set by ProbeTransTab)
    LONG index = (N->hashKey & E.Tr.hashIndexMask);
    N->trans1  = N->TransTab1 + index;
    N->trans2  = N->TransTab2 + index;

    StoreTransTab(&E);

    ASSERT_EQ(E.Tr.transUsed, 1, "transUsed should increment");

    // Store again to same position (should not increment)
    StoreTransTab(&E);

    ASSERT_EQ(E.Tr.transUsed, 1, "transUsed should not increment for reuse");

    free(transBuf);
}

/**************************************************************************************************/
/*                                                                                                */
/*                                      TEST SUITE RUNNER                                         */
/*                                                                                                */
/**************************************************************************************************/

void run_transtables_tests()
{
    printf("\nRunning TransTables Tests:\n");

    // CalcTransState tests
    RUN_TEST(calc_trans_state_disabled_when_too_small);
    RUN_TEST(calc_trans_state_enabled_with_sufficient_memory);
    RUN_TEST(calc_trans_state_sets_hash_mask);

    // ResetTransTab tests
    RUN_TEST(reset_trans_tab_marks_all_unused);
    RUN_TEST(reset_trans_tab_does_nothing_when_disabled);

    // ProbeTransTab tests
    RUN_TEST(probe_trans_tab_returns_false_when_disabled);
    RUN_TEST(probe_trans_tab_returns_false_on_draw);
    RUN_TEST(probe_trans_tab_returns_false_on_pv_node);
    RUN_TEST(probe_trans_tab_finds_exact_score);
    RUN_TEST(probe_trans_tab_adjusts_mate_scores);
    RUN_TEST(probe_trans_tab_returns_refutation_move);

    // GetTransMove tests
    RUN_TEST(get_trans_move_validates_knight_move);
    RUN_TEST(get_trans_move_rejects_illegal_position);
    RUN_TEST(get_trans_move_validates_pawn_advance);
    RUN_TEST(get_trans_move_validates_sliding_piece);
    RUN_TEST(get_trans_move_rejects_blocked_sliding_piece);

    // StoreTransTab tests
    RUN_TEST(store_trans_tab_does_nothing_when_disabled);
    RUN_TEST(store_trans_tab_does_nothing_on_draw);
    RUN_TEST(store_trans_tab_stores_exact_score);
    RUN_TEST(store_trans_tab_stores_cutoff);
    RUN_TEST(store_trans_tab_adjusts_mate_scores);
    RUN_TEST(store_trans_tab_increments_used_count);
}
