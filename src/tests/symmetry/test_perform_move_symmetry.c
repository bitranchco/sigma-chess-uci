// Symmetry tests for PerformMove / RetractMove plus perft regression check.
// Round-trip invariant: Perform then Retract restores state byte-for-byte.
// Perft (depth 4 = 197281, depth 5 = 4865609) verifies MoveGen end-to-end.

#include "Engine.h"
#include "state_snapshot.h"
#include "uci_bridge.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

void PerformMove(ENGINE *E);
void RetractMove(ENGINE *E);
void InitPerformMoveModule(GLOBAL *Global);
void InitBoardState(BOARD_STATE *B);
void InitBoardModule(GLOBAL *Global);
void InitAttackModule(GLOBAL *Global);
void InitMoveGenModule(GLOBAL *Global);
void InitEvaluateModule(GLOBAL *Global, void *kpkData);
void CalcAttackState(ENGINE *E);
void InitSearchState(ENGINE *E);
void InitTransModule(ENGINE *E);
void InitSearchModule(GLOBAL *G);
void SearchKillers(ENGINE *E);
BOOL SelectMove(ENGINE *E);
void SearchFarPawns(ENGINE *E);

// uci_main.c provides UCI_PollInput for the real engine binary. The symmetry
// test driver doesn't read stdin, so stub it out.
void UCI_PollInput(ENGINE *E)
{
    (void)E;
}

static GLOBAL g_global;
static ENGINE g_engine;
static NODE   g_nodes[8];

static int total_failures = 0;

static void place(ENGINE *E, SQUARE sq, PIECE piece, INDEX plinx)
{
    E->B.Board[sq] = piece;
    E->B.PLinx[sq] = plinx;
    if (pieceColour(piece) == white)
        E->B.PieceLocW[plinx] = sq;
    else
        E->B.PieceLocB[plinx] = sq;
    E->B.pieceCount += E->Global->B.PieceCountBit[piece];
}

static void terminate_lists(ENGINE *E, INDEX whiteEnd, INDEX blackEnd)
{
    for (int i = whiteEnd; i < 16; i++)
        E->B.PieceLocW[i] = nullSq;
    for (int i = blackEnd; i < 16; i++)
        E->B.PieceLocB[i] = nullSq;
    E->B.LastOffi[white]  = whiteEnd - 1;
    E->B.LastOffi[black]  = blackEnd - 1;
    E->B.LastPiece[white] = whiteEnd - 1;
    E->B.LastPiece[black] = blackEnd - 1;
}

static void reset_engine(void)
{
    memset(&g_engine, 0, sizeof(g_engine));
    memset(g_nodes, 0, sizeof(g_nodes));
    g_engine.Global = &g_global;

    InitBoardState(&g_engine.B);

    for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
        g_engine.B.Board[sq] = edge;
    for (SQUARE sq = 0; sq < boardSize; sq++)
        if (onBoard(sq))
            g_engine.B.Board[sq] = empty;
    for (int i = 0; i < boardSize; i++)
        g_engine.B.HasMovedTo[i] = 0;
    for (int i = 0; i < 32; i++)
        g_engine.B.PieceLoc[i] = nullSq;
    for (int i = 0; i < 8; i++)
        g_engine.B.PawnStructW[i] = g_engine.B.PawnStructB[i] = 0;
    g_engine.B.pieceCount = 0;

    g_engine.S.rootNode = &g_nodes[1];
    g_engine.S.currNode = &g_nodes[1];
    for (int i = 0; i < 8; i++)
    {
        g_nodes[i].PieceLoc  = g_engine.B.PieceLocW;
        g_nodes[i].PieceLoc_ = g_engine.B.PieceLocB;
        g_nodes[i].Attack    = g_engine.A.AttackW;
        g_nodes[i].Attack_   = g_engine.A.AttackB;
        g_nodes[i].mobEval   = 0;
    }
}

static void set_player(NODE *N, COLOUR player, ENGINE *E)
{
    N->player   = player;
    N->opponent = player ^ black;
    N->pawnDir  = (player == white) ? 0x10 : -0x10;
    if (player == white)
    {
        N->PieceLoc  = E->B.PieceLocW;
        N->PieceLoc_ = E->B.PieceLocB;
        N->Attack    = E->A.AttackW;
        N->Attack_   = E->A.AttackB;
    }
    else
    {
        N->PieceLoc  = E->B.PieceLocB;
        N->PieceLoc_ = E->B.PieceLocW;
        N->Attack    = E->A.AttackB;
        N->Attack_   = E->A.AttackW;
    }
}

// ------------------------------------------------------------------------
// Test fixtures
// ------------------------------------------------------------------------

static void test_king_normal_move(void)
{
    printf("test_king_normal_move\n");
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    terminate_lists(&g_engine, 1, 1);
    CalcAttackState(&g_engine);

    NODE *N = g_engine.S.currNode;
    set_player(N, white, &g_engine);
    N->m.piece = wKing;
    N->m.from  = e1;
    N->m.to    = e2;
    N->m.cap   = empty;
    N->m.type  = mtype_Normal;
    N->mobEval = 0;

    EngineSnapshot before, after_perform, after_retract;
    Snapshot(&g_engine, &before);

    PerformMove(&g_engine);
    Snapshot(&g_engine, &after_perform);

    if (g_engine.B.Board[e1] != empty)
    {
        printf("  [FAIL] e1 not empty after perform\n");
        total_failures++;
        return;
    }
    if (g_engine.B.Board[e2] != wKing)
    {
        printf("  [FAIL] e2 not wKing after perform\n");
        total_failures++;
        return;
    }
    if (g_engine.B.PieceLocW[0] != e2)
    {
        printf("  [FAIL] PieceLocW[0] != e2\n");
        total_failures++;
        return;
    }
    if (g_engine.B.HasMovedTo[e2] != 1)
    {
        printf("  [FAIL] HasMovedTo[e2] != 1\n");
        total_failures++;
        return;
    }

    RetractMove(&g_engine);
    Snapshot(&g_engine, &after_retract);

    total_failures += AssertSnapshotEqual(&before, &after_retract, "Perform+Retract round trip (king e1->e2)");
}

static void test_king_capture(void)
{
    printf("test_king_capture\n");
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, e2, bRook, 1); // black rook at e2, capturable by wK
    terminate_lists(&g_engine, 1, 2);
    CalcAttackState(&g_engine);

    NODE *N = g_engine.S.currNode;
    set_player(N, white, &g_engine);
    N->m.piece = wKing;
    N->m.from  = e1;
    N->m.to    = e2;
    N->m.cap   = bRook;
    N->m.type  = mtype_Normal;
    N->mobEval = 0;

    EngineSnapshot before, after_retract;
    Snapshot(&g_engine, &before);

    PerformMove(&g_engine);

    if (g_engine.B.Board[e2] != wKing)
    {
        printf("  [FAIL] e2 not wKing after capture\n");
        total_failures++;
        return;
    }
    if (g_engine.B.PieceLocB[1] != nullSq)
    {
        printf("  [FAIL] PieceLocB[1] not nullSq after capture\n");
        total_failures++;
        return;
    }

    RetractMove(&g_engine);
    Snapshot(&g_engine, &after_retract);

    total_failures += AssertSnapshotEqual(&before, &after_retract, "Perform+Retract round trip (king e1xe2 capture)");
}

static void test_king_kingside_castle(void)
{
    printf("test_king_kingside_castle\n");
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, h1, wRook, 1);
    place(&g_engine, e8, bKing, 0);
    terminate_lists(&g_engine, 2, 1);
    CalcAttackState(&g_engine);

    NODE *N = g_engine.S.currNode;
    set_player(N, white, &g_engine);
    N->m.piece = wKing;
    N->m.from  = e1;
    N->m.to    = g1;
    N->m.cap   = empty;
    N->m.type  = mtype_O_O;
    N->mobEval = 0;

    EngineSnapshot before, after_retract;
    Snapshot(&g_engine, &before);

    PerformMove(&g_engine);

    if (g_engine.B.Board[e1] != empty)
    {
        printf("  [FAIL] e1 not empty after castle\n");
        total_failures++;
        return;
    }
    if (g_engine.B.Board[g1] != wKing)
    {
        printf("  [FAIL] g1 not wKing after castle\n");
        total_failures++;
        return;
    }
    if (g_engine.B.Board[f1] != wRook)
    {
        printf("  [FAIL] f1 not wRook after castle\n");
        total_failures++;
        return;
    }
    if (g_engine.B.Board[h1] != empty)
    {
        printf("  [FAIL] h1 not empty after castle\n");
        total_failures++;
        return;
    }

    RetractMove(&g_engine);
    Snapshot(&g_engine, &after_retract);

    total_failures += AssertSnapshotEqual(&before, &after_retract, "Perform+Retract round trip (kingside castle)");
}

static void test_king_queenside_castle(void)
{
    printf("test_king_queenside_castle\n");
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, a1, wRook, 1);
    place(&g_engine, e8, bKing, 0);
    terminate_lists(&g_engine, 2, 1);
    CalcAttackState(&g_engine);

    NODE *N = g_engine.S.currNode;
    set_player(N, white, &g_engine);
    N->m.piece = wKing;
    N->m.from  = e1;
    N->m.to    = c1;
    N->m.cap   = empty;
    N->m.type  = mtype_O_O_O;
    N->mobEval = 0;

    EngineSnapshot before, after_retract;
    Snapshot(&g_engine, &before);

    PerformMove(&g_engine);

    if (g_engine.B.Board[e1] != empty)
    {
        printf("  [FAIL] e1 not empty after castle\n");
        total_failures++;
        return;
    }
    if (g_engine.B.Board[c1] != wKing)
    {
        printf("  [FAIL] c1 not wKing after castle\n");
        total_failures++;
        return;
    }
    if (g_engine.B.Board[d1] != wRook)
    {
        printf("  [FAIL] d1 not wRook after castle\n");
        total_failures++;
        return;
    }
    if (g_engine.B.Board[a1] != empty)
    {
        printf("  [FAIL] a1 not empty after castle\n");
        total_failures++;
        return;
    }

    RetractMove(&g_engine);
    Snapshot(&g_engine, &after_retract);

    total_failures += AssertSnapshotEqual(&before, &after_retract, "Perform+Retract round trip (queenside castle)");
}

// ------------------------------------------------------------------------
// Queen tests
// ------------------------------------------------------------------------

static void run_symmetry_test(const char *name, COLOUR player, PIECE movingPiece, SQUARE from, SQUARE to, PIECE cap,
                              INT type, INT dir)
{
    printf("%s\n", name);
    NODE *N = g_engine.S.currNode;
    set_player(N, player, &g_engine);
    N->m.piece = movingPiece;
    N->m.from  = from;
    N->m.to    = to;
    N->m.cap   = cap;
    N->m.type  = type;
    N->m.dir   = dir;
    N->mobEval = 0;

    EngineSnapshot before, after_retract;
    Snapshot(&g_engine, &before);
    PerformMove(&g_engine);
    RetractMove(&g_engine);
    Snapshot(&g_engine, &after_retract);

    char label[128];
    snprintf(label, sizeof(label), "%s round-trip", name);
    total_failures += AssertSnapshotEqual(&before, &after_retract, label);
}

static void run_attack_rebuild_test(const char *name, COLOUR player, PIECE movingPiece, SQUARE from, SQUARE to,
                                    PIECE cap, INT type, INT dir)
{
    printf("%s (attack rebuild)\n", name);
    NODE *N = g_engine.S.currNode;
    set_player(N, player, &g_engine);
    N->m.piece = movingPiece;
    N->m.from  = from;
    N->m.to    = to;
    N->m.cap   = cap;
    N->m.type  = type;
    N->m.dir   = dir;
    N->mobEval = 0;

    PerformMove(&g_engine);

    ATTACK incrementalW[boardSize], incrementalB[boardSize];
    memcpy(incrementalW, g_engine.A.AttackW, sizeof(incrementalW));
    memcpy(incrementalB, g_engine.A.AttackB, sizeof(incrementalB));

    CalcAttackState(&g_engine);

    int diffs = 0;
    for (int i = 0; i < boardSize; i++)
    {
        if (incrementalW[i] != g_engine.A.AttackW[i])
        {
            if (diffs < 8)
                printf("  AttackW[0x%02x]: incremental=0x%08x rebuild=0x%08x\n", i, (unsigned)incrementalW[i],
                       (unsigned)g_engine.A.AttackW[i]);
            diffs++;
        }
        if (incrementalB[i] != g_engine.A.AttackB[i])
        {
            if (diffs < 8)
                printf("  AttackB[0x%02x]: incremental=0x%08x rebuild=0x%08x\n", i, (unsigned)incrementalB[i],
                       (unsigned)g_engine.A.AttackB[i]);
            diffs++;
        }
    }
    if (diffs == 0)
        printf("  [OK] incremental Attack matches rebuild\n");
    else
    {
        printf("  [FAIL] %d Attack-table cells diverge\n", diffs);
        total_failures += diffs;
    }
}

static void setup_queen_kk(SQUARE qSq, PIECE qPiece)
{
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    INDEX qInx = 1;
    if (pieceColour(qPiece) == white)
    {
        place(&g_engine, qSq, qPiece, qInx);
        terminate_lists(&g_engine, 2, 1);
    }
    else
    {
        place(&g_engine, qSq, qPiece, qInx);
        terminate_lists(&g_engine, 1, 2);
    }
    CalcAttackState(&g_engine);
}

static void test_queen_normal_move(void)
{
    setup_queen_kk(d1, wQueen);
    run_symmetry_test("test_queen_normal_move (d1->d4)", white, wQueen, d1, d4, empty, mtype_Normal, 0x10);
    setup_queen_kk(d1, wQueen);
    run_attack_rebuild_test("test_queen_normal_move (d1->d4)", white, wQueen, d1, d4, empty, mtype_Normal, 0x10);
}

static void test_queen_diagonal_move(void)
{
    setup_queen_kk(d1, wQueen);
    run_symmetry_test("test_queen_diagonal_move (d1->h5)", white, wQueen, d1, h5, empty, mtype_Normal, 0x11);
    setup_queen_kk(d1, wQueen);
    run_attack_rebuild_test("test_queen_diagonal_move (d1->h5)", white, wQueen, d1, h5, empty, mtype_Normal, 0x11);
}

static void test_queen_capture(void)
{
    // White queen d1, black queen d8 (after kings removed from d-file path)
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, d1, wQueen, 1);
    place(&g_engine, d8, bQueen, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_symmetry_test("test_queen_capture (d1xd8)", white, wQueen, d1, d8, bQueen, mtype_Normal, 0x10);

    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, d1, wQueen, 1);
    place(&g_engine, d8, bQueen, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_attack_rebuild_test("test_queen_capture (d1xd8)", white, wQueen, d1, d8, bQueen, mtype_Normal, 0x10);
}

// ------------------------------------------------------------------------
// Rook/Bishop tests (PerformRookMove handles both)
// ------------------------------------------------------------------------

static void setup_rook_kk(SQUARE rSq, PIECE rPiece)
{
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, rSq, rPiece, 1);
    if (pieceColour(rPiece) == white)
        terminate_lists(&g_engine, 2, 1);
    else
        terminate_lists(&g_engine, 1, 2);
    CalcAttackState(&g_engine);
}

static void test_rook_normal_move(void)
{
    setup_rook_kk(a1, wRook);
    run_symmetry_test("test_rook_normal_move (a1->a4)", white, wRook, a1, a4, empty, mtype_Normal, 0x10);
    setup_rook_kk(a1, wRook);
    run_attack_rebuild_test("test_rook_normal_move (a1->a4)", white, wRook, a1, a4, empty, mtype_Normal, 0x10);
}

static void test_bishop_normal_move(void)
{
    setup_rook_kk(c1, wBishop);
    run_symmetry_test("test_bishop_normal_move (c1->h6)", white, wBishop, c1, h6, empty, mtype_Normal, 0x11);
    setup_rook_kk(c1, wBishop);
    run_attack_rebuild_test("test_bishop_normal_move (c1->h6)", white, wBishop, c1, h6, empty, mtype_Normal, 0x11);
}

static void test_rook_capture(void)
{
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, a1, wRook, 1);
    place(&g_engine, a5, bRook, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_symmetry_test("test_rook_capture (a1xa5)", white, wRook, a1, a5, bRook, mtype_Normal, 0x10);

    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, a1, wRook, 1);
    place(&g_engine, a5, bRook, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_attack_rebuild_test("test_rook_capture (a1xa5)", white, wRook, a1, a5, bRook, mtype_Normal, 0x10);
}

// ------------------------------------------------------------------------
// Knight tests
// ------------------------------------------------------------------------

static void setup_knight_kk(SQUARE nSq, PIECE nPiece)
{
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, nSq, nPiece, 1);
    if (pieceColour(nPiece) == white)
        terminate_lists(&g_engine, 2, 1);
    else
        terminate_lists(&g_engine, 1, 2);
    CalcAttackState(&g_engine);
}

static void test_knight_normal_move(void)
{
    setup_knight_kk(b1, wKnight);
    run_symmetry_test("test_knight_normal_move (b1->c3)", white, wKnight, b1, c3, empty, mtype_Normal, 0);
    setup_knight_kk(b1, wKnight);
    run_attack_rebuild_test("test_knight_normal_move (b1->c3)", white, wKnight, b1, c3, empty, mtype_Normal, 0);
}

static void test_knight_capture(void)
{
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, b1, wKnight, 1);
    place(&g_engine, c3, bKnight, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_symmetry_test("test_knight_capture (b1xc3)", white, wKnight, b1, c3, bKnight, mtype_Normal, 0);

    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, b1, wKnight, 1);
    place(&g_engine, c3, bKnight, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_attack_rebuild_test("test_knight_capture (b1xc3)", white, wKnight, b1, c3, bKnight, mtype_Normal, 0);
}

// ------------------------------------------------------------------------
// Pawn tests
// ------------------------------------------------------------------------

static void setup_pawn_kk(SQUARE pSq, PIECE pPiece)
{
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, pSq, pPiece, 1);
    if (pieceColour(pPiece) == white)
        terminate_lists(&g_engine, 2, 1);
    else
        terminate_lists(&g_engine, 1, 2);
    CalcAttackState(&g_engine);
}

static void test_pawn_single_move(void)
{
    setup_pawn_kk(e2, wPawn);
    run_symmetry_test("test_pawn_single_move (e2->e3)", white, wPawn, e2, e3, empty, mtype_Normal, 0);
    setup_pawn_kk(e2, wPawn);
    run_attack_rebuild_test("test_pawn_single_move (e2->e3)", white, wPawn, e2, e3, empty, mtype_Normal, 0);
}

static void test_pawn_double_move(void)
{
    setup_pawn_kk(e2, wPawn);
    run_symmetry_test("test_pawn_double_move (e2->e4)", white, wPawn, e2, e4, empty, mtype_Normal, 0);
    setup_pawn_kk(e2, wPawn);
    run_attack_rebuild_test("test_pawn_double_move (e2->e4)", white, wPawn, e2, e4, empty, mtype_Normal, 0);
}

static void test_pawn_capture(void)
{
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, e2, wPawn, 1);
    place(&g_engine, d3, bPawn, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_symmetry_test("test_pawn_capture (e2xd3)", white, wPawn, e2, d3, bPawn, mtype_Normal, 0);

    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, e2, wPawn, 1);
    place(&g_engine, d3, bPawn, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_attack_rebuild_test("test_pawn_capture (e2xd3)", white, wPawn, e2, d3, bPawn, mtype_Normal, 0);
}

// Regression test for the SearchKiller stale-capture bug.
// Setup: a position where the killer's `cap` field is stale (claims a capture but
// Board[killer.to] is actually a different piece). The fix rejects the killer.
// Pre-fix: SearchKiller would proceed and PerformMove would corrupt state.
// Post-fix: rejected silently, numRootMoves stays 0.
static void run_searchkiller_stale_cap_test(void)
{
    printf("test_searchkiller_stale_cap\n");

    // Position: white K e1, black K e8, white R a1, black N a5.
    // White rook can legally capture the knight by moving up the a-file.
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, a1, wRook, 1);
    place(&g_engine, a5, bKnight, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);

    NODE *N = g_engine.S.currNode;
    set_player(N, white, &g_engine);

    // Engage state_Root so ProcessMove dispatches to GenOneRootMove (which appends to RootMoves).
    g_engine.R.state        = state_Root;
    g_engine.S.numRootMoves = 0;

    // Killer: wRook a1xa5 capturing the knight. Sliding piece needs valid `dir`.
    N->killer1.piece = wRook;
    N->killer1.from  = a1;
    N->killer1.to    = a5;
    N->killer1.cap   = bKnight; // matches Board[a5]
    N->killer1.type  = mtype_Normal;
    N->killer1.dir   = 0x10; // up the file
    N->killer1Active = true;
    N->killer2Active = false;
    N->m.dply        = 1;

    SearchKillers(&g_engine);

    INT count_valid = g_engine.S.numRootMoves;
    if (count_valid != 1)
    {
        printf("  [FAIL] valid killer should produce 1 root move, got %d\n", count_valid);
        total_failures++;
        return;
    }

    // Now make cap STALE: claim a wQueen was captured. Board[a5] is bKnight, not wQueen.
    g_engine.S.numRootMoves = 0;
    N->killer1.cap          = wQueen;

    SearchKillers(&g_engine);

    INT count_stale = g_engine.S.numRootMoves;
    if (count_stale != 0)
    {
        printf("  [FAIL] stale-cap killer should be rejected, got %d root move(s)\n", count_stale);
        total_failures++;
        return;
    }

    printf("  [OK] valid killer accepted, stale-cap killer rejected\n");
}

// Regression: SearchFarPawns previously had `rankBase -= 0x10` for the second-rank
// scan for both colors. For BLACK this gave the wrong destination (chess rank 1
// instead of rank 3), producing bogus bPawn moves whose `from` square didn't
// actually contain a black pawn. Verify both colors produce a sensible move set
// for a position with pawns at the relevant ranks.
static void run_searchfarpawns_black_test(void)
{
    printf("test_searchfarpawns_black_rankBase_sign\n");

    // Setup: white K e1, black K e8, black pawn d4. PawnStructB[3] should hold the
    // d4 bit. SearchFarPawns(black) must emit d4d3 (push from rank 4 to rank 3),
    // not d4d1 (which the old code would compute via rankBase=0x00).
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, d4, bPawn, 1);
    terminate_lists(&g_engine, 1, 2);
    CalcAttackState(&g_engine);

    NODE *N = g_engine.S.currNode;
    set_player(N, black, &g_engine);

    g_engine.R.state        = state_Root;
    g_engine.S.numRootMoves = 0;
    N->m.dply               = 1;
    N->bufStart             = g_engine.S.SBuf;
    g_engine.S.bufTop       = N->bufStart;

    SearchFarPawns(&g_engine);

    INT  count    = g_engine.S.numRootMoves;
    BOOL hasD4D3  = false;
    BOOL hasBogus = false;
    for (INT i = 0; i < count; i++)
    {
        MOVE *m = &g_engine.S.RootMoves[i];
        if (m->from == d4 && m->to == d3 && m->piece == bPawn)
            hasD4D3 = true;
        // Any move with from on rank 1 (squares 0x00..0x07) is wrong here — the
        // only bPawn is at d4, so generators should never claim d2/c2/etc.
        if ((m->from & 0xF0) == 0x10 && pieceType(m->piece) == pawn)
            hasBogus = true;
    }
    if (hasD4D3 && !hasBogus)
    {
        printf("  [OK] black pawn d4->d3 generated, no bogus rank-2 from\n");
    }
    else
    {
        printf("  [FAIL] hasD4D3=%d hasBogus=%d count=%d\n", hasD4D3, hasBogus, count);
        for (INT i = 0; i < count; i++)
            printf("    move[%d]: piece=%d from=0x%02x to=0x%02x\n", i, g_engine.S.RootMoves[i].piece,
                   g_engine.S.RootMoves[i].from, g_engine.S.RootMoves[i].to);
        total_failures++;
    }
}

// ----- SelectMove / Turbulent regression tests -----
//
// These exercise the C port of `Turbulent` + `DirectThreat` + `IndirectThreat` in
// src/Searching/Selection.c by setting up positions where SelectMove must call into
// Turbulent (forced via diff <= 0) and asserting the expected return value.
//
// To force the Turbulent path, we set:
//   diff = -NextN->totalEval + N->selMargin - N->alpha
// to a negative value: NextN->totalEval = 100, selMargin = 0, alpha = 0 → diff = -100.
// Turbulent is then called with mval = +100, which TpFromMval maps to enemy(pawn).

static void run_selectmove_turbulent_tests(void)
{
    NODE *N     = g_engine.S.currNode;
    NODE *NextN = N + 1;

    // -- Test 1: direct threat via knight move --
    // Setup: wK e1, wN b1, bK e8, bQ b5. White moves N b1->a3, attacking bQ on b5.
    printf("test_selectmove_direct_threat\n");
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, b1, wKnight, 1);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, b5, bQueen, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    set_player(N, white, &g_engine);

    N->m.piece       = wKnight;
    N->m.from        = b1;
    N->m.to          = a3;
    N->m.cap         = empty;
    N->m.type        = mtype_Normal;
    N->m.dir         = 0;
    N->gen           = gen_None;
    N->selMargin     = 0;
    N->alpha         = 0;
    N->capSelVal     = 0;
    NextN->totalEval = 100; // force diff = -100, calls Turbulent(100)

    BOOL r1 = SelectMove(&g_engine);
    if (r1)
        printf("  [OK] direct threat (Nb1->a3 attacks bQ b5) → SelectMove returns true\n");
    else
    {
        printf("  [FAIL] direct threat should be turbulent, got false\n");
        total_failures++;
    }

    // -- Test 2: no threat (move to irrelevant square) --
    // Setup: wK e1, wN b1, bK e8, bQ h7 (far away). White moves Nb1->a3 — no threat.
    printf("test_selectmove_no_threat\n");
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, b1, wKnight, 1);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, h7, bQueen, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    set_player(N, white, &g_engine);

    N->m.piece       = wKnight;
    N->m.from        = b1;
    N->m.to          = a3;
    N->m.cap         = empty;
    N->m.type        = mtype_Normal;
    N->m.dir         = 0;
    N->gen           = gen_None;
    N->selMargin     = 0;
    N->alpha         = 0;
    N->capSelVal     = 0;
    NextN->totalEval = 100;

    BOOL r2 = SelectMove(&g_engine);
    if (!r2)
        printf("  [OK] no threat (Nb1->a3, bQ on h7 unreachable) → SelectMove returns false\n");
    else
    {
        printf("  [FAIL] no-threat move should be pruned, got true\n");
        total_failures++;
    }

    // -- Test 3: special-move escape (mtype != Normal) → always true --
    // Castling should bypass Turbulent and return true unconditionally.
    printf("test_selectmove_special_move\n");
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, h1, wRook, 1);
    place(&g_engine, e8, bKing, 0);
    terminate_lists(&g_engine, 2, 1);
    CalcAttackState(&g_engine);
    set_player(N, white, &g_engine);

    N->m.piece       = wKing;
    N->m.from        = e1;
    N->m.to          = g1;
    N->m.cap         = empty;
    N->m.type        = mtype_O_O; // special move
    N->gen           = gen_None;
    NextN->totalEval = 100;
    N->selMargin     = 0;
    N->alpha         = 0;

    BOOL r3 = SelectMove(&g_engine);
    if (r3)
        printf("  [OK] special move (castling) bypasses Turbulent → SelectMove returns true\n");
    else
    {
        printf("  [FAIL] special move should always be searched, got false\n");
        total_failures++;
    }
}

// Run UCI_PerftIncremental from startpos at the given depth, suppressing its stdout.
// Returns the leaf count. Uses a dedicated engine to avoid disturbing the symmetry
// test fixtures.
static ENGINE perft_engine;
static int    perft_engine_initialized = 0;

static long long perft_quiet(int depth)
{
    static UCI_State state;
    UCI_SetupStartPosition(&state);

    if (!perft_engine_initialized)
    {
        // Engine_Create requires GLOBAL.Engine[] table; bypass and init the bits we need.
        memset(&perft_engine, 0, sizeof(perft_engine));
        perft_engine.Global  = &g_global;
        perft_engine.R.state = state_Stopped;
        InitBoardState(&perft_engine.B);
        InitSearchState(&perft_engine);
        perft_engine_initialized = 1;
    }

    // Redirect stdout for the duration of the call.
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    int devnull      = open("/dev/null", O_WRONLY);
    dup2(devnull, STDOUT_FILENO);
    close(devnull);

    long long count = UCI_PerftIncremental(&perft_engine, &state, depth);

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    return count;
}

// ============================================================================
// Attack-table consistency check: after each PerformMove anywhere in the
// search tree (depth 1..N), the incrementally-updated Attack tables must
// equal a from-scratch CalcAttackState rebuild on the same board.
//
// Catches "Perform writes wrong bits but Retract happens to undo them"
// asymmetries that perft cannot detect (perft only round-trips Board[]).
// ============================================================================

void GenRootMoves(ENGINE *E);

#define MAX_ATTACK_DEPTH 4

static int  attack_consistency_failures        = 0;
static int  attack_consistency_first_fail_only = 0;
static MOVE attack_consistency_path[MAX_ATTACK_DEPTH];

static void format_path(char *buf, size_t buflen, int depth)
{
    size_t off = 0;
    for (int i = 0; i < depth; i++)
    {
        char ms[16];
        UCI_FormatMove(&attack_consistency_path[i], ms);
        int n = snprintf(buf + off, buflen - off, "%s%s", (i ? " " : ""), ms);
        if (n < 0 || (size_t)n >= buflen - off)
            break;
        off += n;
    }
}

static int compare_and_restore(ENGINE *E, ATTACK *incW, ATTACK *incB, INT incMob, int depth)
{
    // Save incremental state then rebuild from scratch.
    ATTACK rebW[boardSize], rebB[boardSize];
    INT    rebMob;

    CalcAttackState(E);
    memcpy(rebW, E->A.AttackW, sizeof rebW);
    memcpy(rebB, E->A.AttackB, sizeof rebB);
    rebMob = E->S.rootNode->mobEval;

    int diffs = 0;
    // Iterate all valid array indices [0..boardSize). Includes off-board padding cells
    // within the row (file 8..F) — those should also match because CalcAttackState
    // clears the whole array first.
    for (int sq = 0; sq < boardSize; sq++)
    {
        if (incW[sq] != rebW[sq] || incB[sq] != rebB[sq])
        {
            if (diffs == 0)
            {
                char path[256];
                format_path(path, sizeof path, depth);
                printf("  [FAIL] attack mismatch after path: %s\n", path);
            }
            if (diffs < 16)
            {
                int onB = onBoard(sq);
                printf("    sq=0x%02x (%s): incW=%08x rebW=%08x  diffW=%08x  incB=%08x rebB=%08x\n", sq,
                       onB ? "ON " : "OFF", (unsigned)incW[sq], (unsigned)rebW[sq], (unsigned)(incW[sq] ^ rebW[sq]),
                       (unsigned)incB[sq], (unsigned)rebB[sq]);
            }
            diffs++;
        }
    }
    if (diffs > 16)
        printf("    ... (%d more cells differ)\n", diffs - 16);
    // Note: mobEval intentionally not compared. PerformMove writes the post-move mob to
    // nextNode->mobEval, while CalcAttackState writes rootNode->mobEval. These live in
    // different fields and are not directly comparable here.
    (void)incMob;
    (void)rebMob;

    // Restore incremental state regardless — caller may want to recurse with valid state.
    memcpy(E->A.AttackW, incW, sizeof rebW);
    memcpy(E->A.AttackB, incB, sizeof rebB);
    E->S.rootNode->mobEval = incMob;

    return diffs;
}

static void attack_recurse(ENGINE *E, int depth, int max_depth)
{
    NODE *N = E->S.currNode;

    E->R.state        = state_Root;
    E->S.numRootMoves = 0;
    E->S.bufTop       = N->bufStart;
    N->gen            = gen_None;
    clrMove(N->m);
    clrMove(N->rfm);
    N->check      = (N->Attack_[N->PieceLoc[0]] > 0);
    N->quies      = false;
    N->storeSacri = true;
    N->escapeSq   = nullSq;
    N->recapSq    = nullSq;

    GenRootMoves(E);

    INT  numMoves = E->S.numRootMoves;
    MOVE moves[256];
    if (numMoves > 256)
        numMoves = 256;
    for (INT i = 0; i < numMoves; i++)
        moves[i] = E->S.RootMoves[i];

    for (INT i = 0; i < numMoves; i++)
    {
        N->m                           = moves[i];
        attack_consistency_path[depth] = moves[i];

        PerformMove(E);

        // Skip illegal (king left in check)
        if (N->Attack_[N->PieceLoc[0]])
        {
            RetractMove(E);
            continue;
        }

        // Snapshot incremental state, then verify against rebuild.
        ATTACK incW[boardSize], incB[boardSize];
        memcpy(incW, E->A.AttackW, sizeof incW);
        memcpy(incB, E->A.AttackB, sizeof incB);
        INT incMob = E->S.rootNode->mobEval;

        int diffs = compare_and_restore(E, incW, incB, incMob, depth + 1);
        if (diffs)
        {
            attack_consistency_failures += diffs;
            if (attack_consistency_first_fail_only)
            {
                RetractMove(E);
                return;
            }
        }

        if (depth + 1 < max_depth)
        {
            // Set up next node and recurse
            NODE *savedRoot     = E->S.rootNode;
            E->S.currNode       = N + 1;
            E->S.rootNode       = N + 1;
            (N + 1)->bufStart   = E->S.bufTop;
            (N + 1)->maxPly     = 0;
            (N + 1)->ply        = 0;
            (N + 1)->lastPiece  = N->lastPiece_;
            (N + 1)->lastPiece_ = N->lastPiece;

            attack_recurse(E, depth + 1, max_depth);

            E->S.currNode = N;
            E->S.rootNode = savedRoot;

            if (attack_consistency_failures && attack_consistency_first_fail_only)
            {
                RetractMove(E);
                return;
            }
        }

        RetractMove(E);
    }
}

static void run_attack_consistency_recursive_from(UCI_State *initial, const char *label, int max_depth)
{
    UCI_State state = *initial;

    if (!perft_engine_initialized)
    {
        memset(&perft_engine, 0, sizeof(perft_engine));
        perft_engine.Global  = &g_global;
        perft_engine.R.state = state_Stopped;
        InitBoardState(&perft_engine.B);
        InitSearchState(&perft_engine);
        perft_engine_initialized = 1;
    }

    UCI_StateToParam(&state, &perft_engine.P);
    perft_engine.R.state        = state_Root;
    perft_engine.S.numRootMoves = 0;
    InitSearchState(&perft_engine);
    perft_engine.S.rootNode = (perft_engine.P.player == white ? perft_engine.S.whiteNode : perft_engine.S.blackNode);
    perft_engine.S.currNode = perft_engine.S.rootNode;
    CalcBoardState(&perft_engine);
    NODE *N          = perft_engine.S.rootNode;
    N[-1].lastPiece_ = perft_engine.B.LastPiece[perft_engine.B.player];
    N[-1].lastPiece  = perft_engine.B.LastPiece[perft_engine.B.opponent];
    N[0].lastPiece   = N[-1].lastPiece_;
    N[0].lastPiece_  = N[-1].lastPiece;
    CalcAttackState(&perft_engine);
    N[-1].m         = perft_engine.P.lastMove;
    N[-1].gen       = gen_None;
    N[0].check      = (N->Attack_[N->PieceLoc[0]] > 0);
    N[0].quies      = false;
    N[0].maxPly     = 0;
    N[0].bufStart   = perft_engine.S.SBuf;
    N[0].storeSacri = true;
    N[0].escapeSq   = nullSq;
    N[0].recapSq    = nullSq;
    N[0].ply        = 0;
    N[0].depth      = 0;
    N[0].gen        = gen_None;
    clrMove(N[0].m);
    clrMove(N[0].rfm);

    printf("attack_consistency_recursive (depth %d from %s)\n", max_depth, label);
    attack_consistency_failures = 0;
    attack_recurse(&perft_engine, 0, max_depth);
    if (attack_consistency_failures == 0)
        printf("  [OK] all incremental Attack tables match rebuild\n");
    else
    {
        printf("  [FAIL] %d Attack-table cell mismatches\n", attack_consistency_failures);
        total_failures += attack_consistency_failures;
    }
}

static void run_attack_consistency_recursive(int max_depth)
{
    UCI_State state = {0}; // Zeroed: UCI_ClearState keeps state->Global across resets.
    UCI_SetupStartPosition(&state);
    run_attack_consistency_recursive_from(&state, "startpos", max_depth);
}

/**************************************************************************************************/
/*                                                                                                */
/*                              EVAL-STATE CONSISTENCY TEST                                       */
/*                                                                                                */
/* Walks a tree of legal moves with PerformMove + RetractMove; before each PerformMove, snapshots */
/* the eval-relevant board state, and verifies it is fully restored after RetractMove. Catches    */
/* any incremental update that isn't symmetric (state corruption invisible to perft and to the    */
/* Attack-consistency check).                                                                     */
/*                                                                                                */
/**************************************************************************************************/

static int eval_consistency_failures        = 0;
static int eval_consistency_first_fail_only = 1;

static int eval_state_diff(const ENGINE *E, ULONG savedPC, const BYTE *savedPSW, const BYTE *savedPSB,
                           const INT *savedHMT, const INT *savedPLinx)
{
    int diffs = 0;

    if (E->B.pieceCount != savedPC)
    {
        printf("  [FAIL] pieceCount: saved 0x%08x got 0x%08x\n", (unsigned)savedPC, (unsigned)E->B.pieceCount);
        diffs++;
    }
    for (int r = 0; r < 8; r++)
    {
        if (E->B.PawnStructW[r] != savedPSW[r])
        {
            printf("  [FAIL] PawnStructW[%d]: saved 0x%02x got 0x%02x\n", r, savedPSW[r], E->B.PawnStructW[r]);
            diffs++;
        }
        if (E->B.PawnStructB[r] != savedPSB[r])
        {
            printf("  [FAIL] PawnStructB[%d]: saved 0x%02x got 0x%02x\n", r, savedPSB[r], E->B.PawnStructB[r]);
            diffs++;
        }
    }
    for (SQUARE sq = 0; sq < boardSize; sq++)
    {
        if (!onBoard(sq))
            continue;
        if (E->B.HasMovedTo[sq] != savedHMT[sq])
        {
            printf("  [FAIL] HasMovedTo[0x%02x]: saved %d got %d\n", (int)sq, (int)savedHMT[sq],
                   (int)E->B.HasMovedTo[sq]);
            diffs++;
        }
        // PLinx is only meaningful for occupied non-king squares. PerformMove
        // leaves PLinx[src] stale when a piece moves away, and the king's
        // PLinx is never maintained (kings live at PieceLoc[0] by convention).
        if (E->B.Board[sq] != empty && pieceType(E->B.Board[sq]) != king && E->B.PLinx[sq] != savedPLinx[sq])
        {
            printf("  [FAIL] PLinx[0x%02x]: saved %d got %d (piece=%d)\n", (int)sq, (int)savedPLinx[sq],
                   (int)E->B.PLinx[sq], (int)E->B.Board[sq]);
            diffs++;
        }
    }
    return diffs;
}

static void eval_recurse(ENGINE *E, int depth, int max_depth)
{
    NODE *N = E->S.currNode;

    E->R.state        = state_Root;
    E->S.numRootMoves = 0;
    E->S.bufTop       = N->bufStart;
    N->gen            = gen_None;
    clrMove(N->m);
    clrMove(N->rfm);
    N->check      = (N->Attack_[N->PieceLoc[0]] > 0);
    N->quies      = false;
    N->storeSacri = true;
    N->escapeSq   = nullSq;
    N->recapSq    = nullSq;

    GenRootMoves(E);

    INT  numMoves = E->S.numRootMoves;
    MOVE moves[256];
    if (numMoves > 256)
        numMoves = 256;
    for (INT i = 0; i < numMoves; i++)
        moves[i] = E->S.RootMoves[i];

    for (INT i = 0; i < numMoves; i++)
    {
        N->m = moves[i];

        // Snapshot before PerformMove.
        ULONG savedPC = E->B.pieceCount;
        BYTE  savedPSW[8], savedPSB[8];
        INT   savedHMT[boardSize], savedPLinx[boardSize];
        memcpy(savedPSW, E->B.PawnStructW, sizeof savedPSW);
        memcpy(savedPSB, E->B.PawnStructB, sizeof savedPSB);
        memcpy(savedHMT, E->B.HasMovedTo, sizeof savedHMT);
        memcpy(savedPLinx, E->B.PLinx, sizeof savedPLinx);

        PerformMove(E);

        // Skip illegal (king left in check)
        if (N->Attack_[N->PieceLoc[0]])
        {
            RetractMove(E);
            continue;
        }

        // Optionally recurse to add depth coverage.
        if (depth + 1 < max_depth)
        {
            NODE *savedRoot     = E->S.rootNode;
            MOVE *savedBufTop   = E->S.bufTop;
            E->S.currNode       = N + 1;
            E->S.rootNode       = N + 1;
            (N + 1)->bufStart   = E->S.bufTop;
            (N + 1)->maxPly     = 0;
            (N + 1)->ply        = 0;
            (N + 1)->lastPiece  = N->lastPiece_;
            (N + 1)->lastPiece_ = N->lastPiece;

            eval_recurse(E, depth + 1, max_depth);

            E->S.currNode = N;
            E->S.rootNode = savedRoot;
            E->S.bufTop   = savedBufTop;

            if (eval_consistency_failures && eval_consistency_first_fail_only)
            {
                RetractMove(E);
                return;
            }
        }

        RetractMove(E);

        // Verify state restored.
        int diffs = eval_state_diff(E, savedPC, savedPSW, savedPSB, savedHMT, savedPLinx);
        if (diffs)
        {
            printf("    after move %d (depth %d):\n", (int)i, depth);
            // Print the move that triggered the failure.
            char fromFile = 'a' + (moves[i].from & 0x07);
            char fromRank = '1' + ((moves[i].from >> 4) & 0x07);
            char toFile   = 'a' + (moves[i].to & 0x07);
            char toRank   = '1' + ((moves[i].to >> 4) & 0x07);
            printf("    move: %c%c%c%c (piece=%d type=0x%x cap=%d)\n", fromFile, fromRank, toFile, toRank,
                   (int)moves[i].piece, (unsigned)moves[i].type, (int)moves[i].cap);
            eval_consistency_failures += diffs;
            if (eval_consistency_first_fail_only)
                return;
        }
    }
}

static void run_eval_consistency_recursive_from(UCI_State *initial, const char *label, int max_depth)
{
    UCI_State state = *initial;

    if (!perft_engine_initialized)
    {
        memset(&perft_engine, 0, sizeof(perft_engine));
        perft_engine.Global  = &g_global;
        perft_engine.R.state = state_Stopped;
        InitBoardState(&perft_engine.B);
        InitSearchState(&perft_engine);
        perft_engine_initialized = 1;
    }

    UCI_StateToParam(&state, &perft_engine.P);
    perft_engine.R.state        = state_Root;
    perft_engine.S.numRootMoves = 0;
    InitSearchState(&perft_engine);
    perft_engine.S.rootNode = (perft_engine.P.player == white ? perft_engine.S.whiteNode : perft_engine.S.blackNode);
    perft_engine.S.currNode = perft_engine.S.rootNode;
    CalcBoardState(&perft_engine);
    NODE *N          = perft_engine.S.rootNode;
    N[-1].lastPiece_ = perft_engine.B.LastPiece[perft_engine.B.player];
    N[-1].lastPiece  = perft_engine.B.LastPiece[perft_engine.B.opponent];
    N[0].lastPiece   = N[-1].lastPiece_;
    N[0].lastPiece_  = N[-1].lastPiece;
    N[0].maxPly      = 0;
    N[0].bufStart    = perft_engine.S.SBuf;
    N[0].escapeSq    = nullSq;
    N[0].recapSq     = nullSq;
    N[0].ply         = 0;
    N[0].depth       = 0;
    N[0].gen         = gen_None;
    clrMove(N[0].m);
    clrMove(N[0].rfm);
    CalcAttackState(&perft_engine);
    N[-1].m         = perft_engine.P.lastMove;
    N[-1].gen       = gen_None;
    N[0].check      = (N->Attack_[N->PieceLoc[0]] > 0);
    N[0].quies      = false;
    N[0].storeSacri = true;

    printf("eval_consistency_recursive (depth %d from %s)\n", max_depth, label);
    eval_consistency_failures = 0;

    eval_recurse(&perft_engine, 0, max_depth);

    if (eval_consistency_failures == 0)
    {
        printf("  [OK] all eval-state fields restored after Perform+Retract\n");
    }
    else
    {
        printf("  [FAIL] %d eval-state mismatches\n", eval_consistency_failures);
        total_failures += eval_consistency_failures;
    }
}

static void run_eval_consistency_recursive(int max_depth)
{
    UCI_State state = {0};
    UCI_SetupStartPosition(&state);
    run_eval_consistency_recursive_from(&state, "startpos", max_depth);
}

static void run_eval_consistency_after_moves(int max_depth, const char *label, const char *moves)
{
    UCI_State state = {0};
    UCI_SetupStartPosition(&state);
    char buf[256];
    strncpy(buf, moves, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    char *tok           = strtok(buf, " ");
    while (tok)
    {
        MOVE m;
        if (!UCI_ParseMove(&state, tok, &m))
        {
            printf("  [FAIL] parse %s\n", tok);
            return;
        }
        UCI_ApplyMove(&state, &m);
        tok = strtok(NULL, " ");
    }
    run_eval_consistency_recursive_from(&state, label, max_depth);
}

static void run_attack_consistency_after_moves(int max_depth, const char *label, const char *moves)
{
    UCI_State state = {0};
    UCI_SetupStartPosition(&state);
    char buf[256];
    strncpy(buf, moves, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    char *tok           = strtok(buf, " ");
    while (tok)
    {
        MOVE m;
        if (!UCI_ParseMove(&state, tok, &m))
        {
            printf("  [FAIL] could not parse setup move %s\n", tok);
            return;
        }
        UCI_ApplyMove(&state, &m);
        tok = strtok(NULL, " ");
    }
    run_attack_consistency_recursive_from(&state, label, max_depth);
}

static void run_perft_tests(void)
{
    struct
    {
        int       depth;
        long long expected;
    } cases[] = {
        {1, 20}, {2, 400}, {3, 8902}, {4, 197281}, {5, 4865609},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        printf("perft %d (startpos)\n", cases[i].depth);
        long long got = perft_quiet(cases[i].depth);
        if (got == cases[i].expected)
        {
            printf("  [OK] %lld\n", got);
        }
        else
        {
            printf("  [FAIL] expected %lld got %lld\n", cases[i].expected, got);
            total_failures++;
        }
    }
}

// Promotion: white pawn e7 -> e8 promote to queen. No capture.
// For promotion, m.type holds the promoted-piece value (e.g., wQueen = 0x05).
static void test_pawn_promotion(void)
{
    setup_pawn_kk(e7, wPawn);
    // Move e8 must not collide with bKing — relocate bKing to a8 for this test
    g_engine.B.Board[e8]    = empty;
    g_engine.B.PieceLocB[0] = a8;
    g_engine.B.Board[a8]    = bKing;
    g_engine.B.PLinx[a8]    = 0;
    CalcAttackState(&g_engine);
    run_symmetry_test("test_pawn_promotion (e7->e8=Q)", white, wPawn, e7, e8, empty, wQueen, 0);

    setup_pawn_kk(e7, wPawn);
    g_engine.B.Board[e8]    = empty;
    g_engine.B.PieceLocB[0] = a8;
    g_engine.B.Board[a8]    = bKing;
    g_engine.B.PLinx[a8]    = 0;
    CalcAttackState(&g_engine);
    run_attack_rebuild_test("test_pawn_promotion (e7->e8=Q)", white, wPawn, e7, e8, empty, wQueen, 0);
}

// Promotion + capture: white pawn e7 captures black piece at d8, promoting to queen.
static void test_pawn_promotion_capture(void)
{
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, a8, bKing, 0); // bKing out of the way
    place(&g_engine, e7, wPawn, 1);
    place(&g_engine, d8, bRook, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_symmetry_test("test_pawn_promotion_capture (e7xd8=Q)", white, wPawn, e7, d8, bRook, wQueen, 0);

    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, a8, bKing, 0);
    place(&g_engine, e7, wPawn, 1);
    place(&g_engine, d8, bRook, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_attack_rebuild_test("test_pawn_promotion_capture (e7xd8=Q)", white, wPawn, e7, d8, bRook, wQueen, 0);
}

// En passant: white pawn at e5, black pawn at d5; white plays exd6 e.p.
// Move: from=e5, to=d6, type=mtype_EP. Captured pawn lives at d5.
static void test_pawn_en_passant(void)
{
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, e5, wPawn, 1);
    place(&g_engine, d5, bPawn, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_symmetry_test("test_pawn_en_passant (e5xd6 e.p.)", white, wPawn, e5, d6, empty, mtype_EP, 0);

    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    place(&g_engine, e5, wPawn, 1);
    place(&g_engine, d5, bPawn, 1);
    terminate_lists(&g_engine, 2, 2);
    CalcAttackState(&g_engine);
    run_attack_rebuild_test("test_pawn_en_passant (e5xd6 e.p.)", white, wPawn, e5, d6, empty, mtype_EP, 0);
}

// Differential: after Perform, the incrementally-updated attack table must equal
// CalcAttackState rebuilt from scratch on the same board.
static void test_king_attack_rebuild_consistency(void)
{
    printf("test_king_attack_rebuild_consistency\n");
    reset_engine();
    place(&g_engine, e1, wKing, 0);
    place(&g_engine, e8, bKing, 0);
    terminate_lists(&g_engine, 1, 1);
    CalcAttackState(&g_engine);

    NODE *N = g_engine.S.currNode;
    set_player(N, white, &g_engine);
    N->m.piece = wKing;
    N->m.from  = e1;
    N->m.to    = e2;
    N->m.cap   = empty;
    N->m.type  = mtype_Normal;
    N->mobEval = 0;

    PerformMove(&g_engine);

    ATTACK incrementalW[boardSize], incrementalB[boardSize];
    memcpy(incrementalW, g_engine.A.AttackW, sizeof(incrementalW));
    memcpy(incrementalB, g_engine.A.AttackB, sizeof(incrementalB));

    CalcAttackState(&g_engine);

    int diffs = 0;
    for (int i = 0; i < boardSize; i++)
    {
        if (incrementalW[i] != g_engine.A.AttackW[i])
        {
            if (diffs < 8)
                printf("  AttackW[0x%02x]: incremental=0x%08x rebuild=0x%08x\n", i, (unsigned)incrementalW[i],
                       (unsigned)g_engine.A.AttackW[i]);
            diffs++;
        }
        if (incrementalB[i] != g_engine.A.AttackB[i])
        {
            if (diffs < 8)
                printf("  AttackB[0x%02x]: incremental=0x%08x rebuild=0x%08x\n", i, (unsigned)incrementalB[i],
                       (unsigned)g_engine.A.AttackB[i]);
            diffs++;
        }
    }
    if (diffs == 0)
        printf("  [OK] incremental Attack matches rebuild\n");
    else
    {
        printf("  [FAIL] %d Attack-table cells diverge\n", diffs);
        total_failures += diffs;
    }
}

int main(void)
{
    // Mirror Engine_InitSystem (src/Engine.c) so perft has everything it needs.
    extern void InitPieceValModule(GLOBAL * Global);
    extern void InitSearchModule(GLOBAL * Global);
    extern void InitHashCodeModule(GLOBAL * Global);
    memset(&g_global, 0, sizeof(g_global));
    InitBoardModule(&g_global);
    InitAttackModule(&g_global);
    InitMoveGenModule(&g_global);
    InitPerformMoveModule(&g_global);
    InitPieceValModule(&g_global);
    InitEvaluateModule(&g_global, NULL);
    InitSearchModule(&g_global);
    InitHashCodeModule(&g_global);

    test_king_normal_move();
    test_king_capture();
    test_king_kingside_castle();
    test_king_queenside_castle();
    test_king_attack_rebuild_consistency();

    test_queen_normal_move();
    test_queen_diagonal_move();
    test_queen_capture();

    test_rook_normal_move();
    test_bishop_normal_move();
    test_rook_capture();

    test_knight_normal_move();
    test_knight_capture();

    test_pawn_single_move();
    test_pawn_double_move();
    test_pawn_capture();
    test_pawn_promotion();
    test_pawn_promotion_capture();
    test_pawn_en_passant();

    // ----- MoveGen-bug regression tests -----
    run_searchkiller_stale_cap_test();
    run_searchfarpawns_black_test();

    // ----- Turbulent / SelectMove regression tests -----
    run_selectmove_turbulent_tests();

    // ----- Perft regression: end-to-end MoveGen + PerformMove/RetractMove -----
    run_perft_tests();

    // ----- Recursive Attack-table consistency: catches Perform-without-Retract bugs -----
    run_attack_consistency_recursive(4);
    run_attack_consistency_after_moves(3, "after e2e4 e7e5", "e2e4 e7e5");
    run_attack_consistency_after_moves(3, "after e2e4 e7e5 g1f3 b8c6", "e2e4 e7e5 g1f3 b8c6");

    // ----- Recursive eval-state consistency: catches asymmetric incremental updates -----
    run_eval_consistency_recursive(4);
    run_eval_consistency_after_moves(3, "after e2e4 e7e5", "e2e4 e7e5");
    run_eval_consistency_after_moves(3, "after g1f3 b8c6 e2e4", "g1f3 b8c6 e2e4");

    printf("\n");
    if (total_failures == 0)
    {
        printf("ALL SYMMETRY + PERFT TESTS PASSED\n");
        return 0;
    }
    else
    {
        printf("FAILED: %d differences\n", total_failures);
        return 1;
    }
}
