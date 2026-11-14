#include "state_snapshot.h"

#include <stdio.h>
#include <string.h>

void Snapshot(const ENGINE *E, EngineSnapshot *snap)
{
    const NODE *N = E->S.currNode;

    memcpy(snap->Board, E->B.Board, sizeof(snap->Board));
    memcpy(snap->HasMovedTo, E->B.HasMovedTo, sizeof(snap->HasMovedTo));
    memcpy(snap->PLinx, E->B.PLinx, sizeof(snap->PLinx));
    memcpy(snap->PieceLoc, E->B.PieceLoc, sizeof(snap->PieceLoc));
    memcpy(snap->PawnStructW, E->B.PawnStructW, sizeof(snap->PawnStructW));
    memcpy(snap->PawnStructB, E->B.PawnStructB, sizeof(snap->PawnStructB));
    memcpy(snap->LastOffi, E->B.LastOffi, sizeof(snap->LastOffi));
    memcpy(snap->LastPiece, E->B.LastPiece, sizeof(snap->LastPiece));
    memcpy(snap->AttackW, E->A.AttackW, sizeof(snap->AttackW));
    memcpy(snap->AttackB, E->A.AttackB, sizeof(snap->AttackB));

    snap->pieceCount   = E->B.pieceCount;
    snap->capInx       = N->capInx;
    snap->promInx      = N->promInx;
    snap->mobEval      = N->mobEval;
    snap->next_mobEval = (N + 1)->mobEval;
}

static int diff_array(const char *name, const void *a, const void *b, size_t elem_size, int n)
{
    int         differences = 0;
    const char *pa          = a;
    const char *pb          = b;
    for (int i = 0; i < n; i++)
    {
        if (memcmp(pa + i * elem_size, pb + i * elem_size, elem_size) != 0)
        {
            if (differences < 8)
            {
                long long va = 0, vb = 0;
                memcpy(&va, pa + i * elem_size, elem_size);
                memcpy(&vb, pb + i * elem_size, elem_size);
                printf("    %s[%d]: expected=%lld got=%lld\n", name, i, va, vb);
            }
            differences++;
        }
    }
    if (differences > 8)
        printf("    ... (%d more %s differences)\n", differences - 8, name);
    return differences;
}

int AssertSnapshotEqual(const EngineSnapshot *a, const EngineSnapshot *b, const char *label)
{
    int diffs = 0;
    diffs += diff_array("Board", a->Board, b->Board, sizeof(PIECE), boardSize);
    diffs += diff_array("HasMovedTo", a->HasMovedTo, b->HasMovedTo, sizeof(INT), boardSize);
    // PLinx is undefined for empty squares (Board.h:217). Compare only on occupied squares,
    // using the post-state Board as authoritative occupancy.
    int plinx_diffs = 0;
    for (int sq = 0; sq < boardSize; sq++)
    {
        if (b->Board[sq] != empty && b->Board[sq] != edge && a->PLinx[sq] != b->PLinx[sq])
        {
            if (plinx_diffs < 8)
                printf("    PLinx[0x%02x] (occupied by %d): expected=%d got=%d\n", sq, (int)b->Board[sq],
                       (int)a->PLinx[sq], (int)b->PLinx[sq]);
            plinx_diffs++;
        }
    }
    if (plinx_diffs > 8)
        printf("    ... (%d more PLinx differences)\n", plinx_diffs - 8);
    diffs += plinx_diffs;
    diffs += diff_array("PieceLoc", a->PieceLoc, b->PieceLoc, sizeof(SQUARE), 32);
    diffs += diff_array("PawnStructW", a->PawnStructW, b->PawnStructW, sizeof(RANKBITS), 8);
    diffs += diff_array("PawnStructB", a->PawnStructB, b->PawnStructB, sizeof(RANKBITS), 8);
    diffs += diff_array("LastOffi", a->LastOffi, b->LastOffi, sizeof(INDEX), white_black);
    diffs += diff_array("LastPiece", a->LastPiece, b->LastPiece, sizeof(INDEX), white_black);
    diffs += diff_array("AttackW", a->AttackW, b->AttackW, sizeof(ATTACK), boardSize);
    diffs += diff_array("AttackB", a->AttackB, b->AttackB, sizeof(ATTACK), boardSize);

    if (a->pieceCount != b->pieceCount)
    {
        printf("    pieceCount: expected=0x%lx got=0x%lx\n", (unsigned long)a->pieceCount,
               (unsigned long)b->pieceCount);
        diffs++;
    }
    // capInx, promInx are scratch fields written by Perform and read by Retract;
    // not part of persistent state. Excluded from round-trip equality.
    if (a->mobEval != b->mobEval)
    {
        printf("    mobEval: expected=%d got=%d\n", (int)a->mobEval, (int)b->mobEval);
        diffs++;
    }

    if (diffs == 0)
        printf("  [OK] %s\n", label);
    else
        printf("  [FAIL] %s: %d field(s) differ\n", label, diffs);
    return diffs;
}
