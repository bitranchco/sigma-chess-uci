#include "SyzygyGlue.h"

#include "fathom/tbprobe.h"

static int initialised = 0;

int SZ_Init(const char *path)
{
    SZ_Free();
    if (!path || !*path || !tb_init(path))
        return 0;
    initialised = 1;
    return (int)TB_LARGEST;
}

void SZ_Free(void)
{
    if (initialised)
        tb_free();
    initialised = 0;
}

void SZ_FileCounts(int *wdl, int *dtz)
{
    unsigned w = 0, d = 0;
    if (initialised)
        tb_file_counts(&w, &d);
    *wdl = (int)w;
    *dtz = (int)d;
}

int SZ_ProbeWDL(const SZ_POS *p, unsigned ep, int whiteToMove)
{
    if (!initialised || TB_LARGEST == 0)
        return SZ_FAIL;
    unsigned r = tb_probe_wdl(p->whiteBB, p->blackBB, p->kingsBB, p->queensBB, p->rooksBB, p->bishopsBB, p->knightsBB,
                              p->pawnsBB, 0, 0, ep, whiteToMove != 0);
    return r == TB_RESULT_FAILED ? SZ_FAIL : (int)r - 2;
}

static int RankToWDL(int rank, int useRule50)
{
    int bound = useRule50 ? 900 : 1; // Fathom's win/draw border (see root_probe_dtz).
    return rank >= bound ? 2 : rank > 0 ? 1 : rank == 0 ? 0 : rank > -bound ? -1 : -2;
}

int SZ_ProbeRoot(const SZ_POS *p, unsigned rule50, unsigned ep, int whiteToMove, int hasRepeated, int useRule50,
                 SZ_ROOTMOVE *out, int maxOut)
{
    static struct TbRootMoves rm; // Large (~100 KB): keep it off the stack.

    if (!initialised || TB_LARGEST == 0)
        return -1;
    if (!tb_probe_root_dtz(p->whiteBB, p->blackBB, p->kingsBB, p->queensBB, p->rooksBB, p->bishopsBB, p->knightsBB,
                           p->pawnsBB, rule50, 0, ep, whiteToMove != 0, hasRepeated != 0, useRule50 != 0, &rm) &&
        !tb_probe_root_wdl(p->whiteBB, p->blackBB, p->kingsBB, p->queensBB, p->rooksBB, p->bishopsBB, p->knightsBB,
                           p->pawnsBB, rule50, 0, ep, whiteToMove != 0, useRule50 != 0, &rm))
        return -1;

    int n = 0;
    for (unsigned i = 0; i < rm.size && n < maxOut; i++, n++)
    {
        TbMove m     = rm.moves[i].move;
        out[n].from  = TB_MOVE_FROM(m);
        out[n].to    = TB_MOVE_TO(m);
        out[n].promo = TB_MOVE_PROMOTES(m);
        out[n].rank  = rm.moves[i].tbRank;
        out[n].wdl   = RankToWDL(rm.moves[i].tbRank, useRule50);
    }
    return n;
}
