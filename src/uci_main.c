#include "uci_bridge.h"
#include "Engine.f"
#include "Search.f"
#include "Time.f"
#include "TransTables.h"
#include "Tablebase.f"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/select.h>
#include <unistd.h>

/**************************************************************************************************/
/*                                                                                                */
/*                                         GLOBAL STATE                                           */
/*                                                                                                */
/**************************************************************************************************/

static GLOBAL    g_Global;
static ENGINE    g_Engine;
static UCI_State g_Position;
static BOOL      g_Running = true;

// Hash table state (reallocated when "setoption name Hash" arrives).
#define HASH_DEFAULT_MB 16
#define HASH_MIN_MB     1
#define HASH_MAX_MB     1024
static INT g_HashMB = HASH_DEFAULT_MB;

// User intent for strength reduction. UCI_AnalyseMode wins over UCI_LimitStrength:
// the GUI's "analysis mode" tells engines to play full strength, even if a strength
// cap was previously set. We resolve to PARAM.reduceStrength at HandleGo time.
static BOOL g_LimitStrength = false;
static BOOL g_AnalyseMode   = false;

// UCI Ponder option: "the GUI is willing to let us think during the opponent's turn."
// Affects (a) the AdjustELO self-rating (drives PARAM.permanentBrain) and (b) whether
// we emit "bestmove <m> ponder <m2>" with the predicted reply.
static BOOL g_PonderEnabled = false;

// In-flight `go ponder` state. Captures the clock parameters so that on `ponderhit`
// we can re-allocate proper deadlines from the diminished post-ponder clock.
static struct
{
    BOOL active;
    LONG startTime;    // Now() at the start of the ponder search.
    LONG initialClock; // ourTime (ms) the GUI sent with `go ponder`.
} g_PonderState = {false, 0, 0};

// UCI: after "go infinite" / "go ponder", bestmove waits for "stop" or "ponderhit" even if the
// search ended on its own (mate found, single legal move, maximum depth). Set by the mid-search
// dispatcher when one of those (or "quit") arrives, or when stdin reaches EOF.
static BOOL g_GoReleased = true;
static void WaitForGoRelease(void);

// Mid-search setoptions get deferred here. Drained by HandleGo after
// Engine_Start returns. Last-value-wins. Hash/Clear Hash because they touch
// the TT buffer the search reads; MultiPV because the search latches it once.
static struct
{
    BOOL hashPending;
    INT  hashMB;
    BOOL clearHashPending;
    BOOL multiPVPending;
    INT  multiPV;
    BOOL syzygyPathPending;
    char syzygyPath[1024];
} g_PendingOption = {false, 0, false, false, 0, false, ""};

#define MULTIPV_MIN     1
#define MULTIPV_MAX     8 // Matches maxMultiPV in Search.h.
#define MULTIPV_DEFAULT 1

// "Personality" combo values (the playing style; must match style_* in Engine.h).
static const char *const g_StyleNames[] = {"Chicken", "Defensive", "Normal", "Aggressive", "Desperado"};

// Allocate (or reallocate) the transposition table buffer at the requested size
// in MB and wire it into PARAM. Safe to call before each search.
static void EnsureHashAllocated(void)
{
    LONG bytes = (LONG)g_HashMB * 1024L * 1024L;
    if (g_Engine.P.TransTables != NULL && g_Engine.P.transSize == bytes)
        return;

    free(g_Engine.P.TransTables);
    g_Engine.P.TransTables = (TRANS *)malloc((size_t)bytes);
    if (g_Engine.P.TransTables == NULL)
    {
        printf("info string failed to allocate %ld MB hash\n", (long)g_HashMB);
        g_Engine.P.transSize = 0;
        return;
    }
    g_Engine.P.transSize = bytes;
}

// Wipe the TT buffer. 0xFF makes data[4] (ply) = -1 signed, which forces every
// probe to fail the depth check regardless of key collision.
static void ClearHash(void)
{
    if (g_Engine.P.TransTables != NULL && g_Engine.P.transSize > 0)
        memset(g_Engine.P.TransTables, 0xFF, (size_t)g_Engine.P.transSize);
    g_Engine.Tr.transUsed = 0;
}

// Apply setoption changes deferred during search.
static void ApplyPendingOptions(void)
{
    if (g_PendingOption.hashPending)
    {
        g_HashMB = g_PendingOption.hashMB;
        EnsureHashAllocated();
        g_PendingOption.hashPending = false;
    }
    if (g_PendingOption.clearHashPending)
    {
        ClearHash();
        g_PendingOption.clearHashPending = false;
    }
    if (g_PendingOption.multiPVPending)
    {
        g_Engine.P.multiPVCount        = g_PendingOption.multiPV;
        g_PendingOption.multiPVPending = false;
    }
    if (g_PendingOption.syzygyPathPending)
    {
        TB_SetPath(g_PendingOption.syzygyPath);
        g_PendingOption.syzygyPathPending = false;
    }
}

/**************************************************************************************************/
/*                                                                                                */
/*                                      COMMAND HANDLERS                                          */
/*                                                                                                */
/**************************************************************************************************/

// Handle "uci" command - identify engine and send capabilities
static void HandleUCI(void)
{
    printf("id name Sigma Chess UCI\n");
    printf("id author Ole K. Christensen\n");
    printf("option name Hash type spin default %d min %d max %d\n", HASH_DEFAULT_MB, HASH_MIN_MB, HASH_MAX_MB);
    printf("option name Clear Hash type button\n");
    printf("option name Ponder type check default false\n");
    printf("option name UCI_AnalyseMode type check default false\n");
    printf("option name UCI_LimitStrength type check default false\n");
    printf("option name UCI_Elo type spin default 2000 min 1200 max 2400\n");
    printf("option name MultiPV type spin default %d min %d max %d\n", MULTIPV_DEFAULT, MULTIPV_MIN, MULTIPV_MAX);
    // Engine-specific tuning toggles that map to PARAM.* flags
    printf("option name PVSearch type check default true\n");
    printf("option name Extensions type check default true\n");
    printf("option name Selection type check default true\n");
    printf("option name DeepSelection type check default false\n");
    printf("option name Nondeterministic type check default false\n");
    printf("option name Aspiration type check default true\n");
    printf("option name SyzygyPath type string default <empty>\n");
    printf("option name SyzygyProbeDepth type spin default 1 min 1 max 100\n");
    printf("option name Syzygy50MoveRule type check default true\n");
    printf("option name SyzygyProbeLimit type spin default 7 min 0 max 7\n");
    printf("option name Personality type combo default Normal");
    for (size_t i = 0; i < sizeof(g_StyleNames) / sizeof(g_StyleNames[0]); i++)
        printf(" var %s", g_StyleNames[i]);
    printf("\n");
    printf("uciok\n");
    fflush(stdout);
}

// Handle "setoption name <NAME> [value <VALUE>]"
static void HandleSetOption(char *args)
{
    while (*args && isspace(*args))
        args++;
    if (strncmp(args, "name", 4) != 0)
        return;
    args += 4;
    while (*args && isspace(*args))
        args++;

    // Capture name up to " value " (case-insensitive on the literal "value" token
    // is not strictly required by UCI - GUIs always send lowercase).
    char *nameStart  = args;
    char *valueStart = NULL;
    char *p          = args;
    while (*p)
    {
        if ((p == args || isspace((unsigned char)p[-1])) && strncmp(p, "value", 5) == 0 &&
            (p[5] == '\0' || isspace((unsigned char)p[5])))
        {
            // Trim trailing whitespace from name
            char *nameEnd = p;
            while (nameEnd > nameStart && isspace((unsigned char)nameEnd[-1]))
                nameEnd--;
            *nameEnd   = '\0';
            valueStart = p + 5;
            while (*valueStart && isspace((unsigned char)*valueStart))
                valueStart++;
            // Trim trailing whitespace from value
            char *valEnd = valueStart + strlen(valueStart);
            while (valEnd > valueStart && isspace((unsigned char)valEnd[-1]))
            {
                valEnd--;
                *valEnd = '\0';
            }
            break;
        }
        p++;
    }
    if (valueStart == NULL)
    {
        // No "value" token - trim trailing whitespace from name and treat as empty value.
        char *nameEnd = nameStart + strlen(nameStart);
        while (nameEnd > nameStart && isspace((unsigned char)nameEnd[-1]))
        {
            nameEnd--;
            *nameEnd = '\0';
        }
        valueStart = (char *)"";
    }

    if (strcmp(nameStart, "Hash") == 0)
    {
        INT mb = atoi(valueStart);
        if (mb < HASH_MIN_MB)
            mb = HASH_MIN_MB;
        if (mb > HASH_MAX_MB)
            mb = HASH_MAX_MB;
        // Defer mid-search to avoid UAF on the buffer the search is reading.
        if (g_Engine.R.taskRunning)
        {
            g_PendingOption.hashPending = true;
            g_PendingOption.hashMB      = mb;
        }
        else
        {
            g_HashMB = mb;
            EnsureHashAllocated();
        }
    }
    else if (strcmp(nameStart, "Clear Hash") == 0)
    {
        // Buttons carry no value; defer mid-search to avoid racing the buffer.
        if (g_Engine.R.taskRunning)
            g_PendingOption.clearHashPending = true;
        else
            ClearHash();
    }
    else if (strcmp(nameStart, "Ponder") == 0)
    {
        g_PonderEnabled           = (strcmp(valueStart, "true") == 0);
        g_Engine.P.permanentBrain = g_PonderEnabled; // Drives the AdjustELO bookkeeping.
    }
    else if (strcmp(nameStart, "UCI_AnalyseMode") == 0)
    {
        g_AnalyseMode = (strcmp(valueStart, "true") == 0);
    }
    else if (strcmp(nameStart, "UCI_LimitStrength") == 0)
    {
        g_LimitStrength = (strcmp(valueStart, "true") == 0);
    }
    else if (strcmp(nameStart, "UCI_Elo") == 0)
    {
        INT elo = atoi(valueStart);
        if (elo < 1200)
            elo = 1200;
        if (elo > 2400)
            elo = 2400;
        g_Engine.P.engineELO = elo;
    }
    else if (strcmp(nameStart, "MultiPV") == 0)
    {
        INT n = atoi(valueStart);
        if (n < MULTIPV_MIN)
            n = MULTIPV_MIN;
        if (n > MULTIPV_MAX)
            n = MULTIPV_MAX;
        // Defer mid-search: PrepareSearch latches multiPVCount once.
        if (g_Engine.R.taskRunning)
        {
            g_PendingOption.multiPVPending = true;
            g_PendingOption.multiPV        = n;
        }
        else
        {
            g_Engine.P.multiPVCount = n;
        }
    }
    else if (strcmp(nameStart, "PVSearch") == 0)
    {
        g_Engine.P.pvSearch = (strcmp(valueStart, "true") == 0);
    }
    else if (strcmp(nameStart, "Extensions") == 0)
    {
        g_Engine.P.extensions = (strcmp(valueStart, "true") == 0);
    }
    else if (strcmp(nameStart, "Selection") == 0)
    {
        g_Engine.P.selection = (strcmp(valueStart, "true") == 0);
    }
    else if (strcmp(nameStart, "DeepSelection") == 0)
    {
        g_Engine.P.deepSelection = (strcmp(valueStart, "true") == 0);
    }
    else if (strcmp(nameStart, "Nondeterministic") == 0)
    {
        g_Engine.P.nondeterm = (strcmp(valueStart, "true") == 0);
    }
    else if (strcmp(nameStart, "SyzygyPath") == 0)
    {
        // Expand a leading "~/" (GUIs pass literal paths; scripts often don't expand it).
        char        path[sizeof g_PendingOption.syzygyPath];
        const char *home = getenv("HOME");
        if (valueStart[0] == '~' && valueStart[1] == '/' && home)
            snprintf(path, sizeof path, "%s%s", home, valueStart + 1);
        else
            snprintf(path, sizeof path, "%s", valueStart);
        // Defer mid-search: the search may be probing the tables being replaced.
        if (g_Engine.R.taskRunning)
        {
            g_PendingOption.syzygyPathPending = true;
            snprintf(g_PendingOption.syzygyPath, sizeof g_PendingOption.syzygyPath, "%s", path);
        }
        else
            TB_SetPath(path);
    }
    else if (strcmp(nameStart, "SyzygyProbeDepth") == 0)
    {
        g_Engine.P.tbProbeDepth = (INT)MaxL(1, MinL(100, atoi(valueStart)));
    }
    else if (strcmp(nameStart, "Syzygy50MoveRule") == 0)
    {
        g_Engine.P.tbRule50 = (strcmp(valueStart, "true") == 0);
    }
    else if (strcmp(nameStart, "SyzygyProbeLimit") == 0)
    {
        g_Engine.P.tbProbeLimit = (INT)MaxL(0, MinL(7, atoi(valueStart)));
    }
    else if (strcmp(nameStart, "Aspiration") == 0)
    {
        g_Engine.P.aspiration = (strcmp(valueStart, "true") == 0);
    }
    else if (strcmp(nameStart, "Personality") == 0)
    {
        for (size_t i = 0; i < sizeof(g_StyleNames) / sizeof(g_StyleNames[0]); i++)
        {
            if (strcmp(valueStart, g_StyleNames[i]) == 0)
            {
                g_Engine.P.playingStyle = (INT)(i + 1); // style_Chicken=1
                return;
            }
        }
        printf("info string unknown Personality value: %s\n", valueStart);
    }
    else
    {
        // UCI spec: unknown options should be silently ignored, but log for visibility.
        printf("info string unknown option: %s\n", nameStart);
    }
}

// Handle "isready" command - respond when ready
static void HandleIsReady(void)
{
    printf("readyok\n");
    fflush(stdout);
}

// Handle "ucinewgame" command - reset for new game
static void HandleNewGame(void)
{
    UCI_SetupStartPosition(&g_Position);
}

// Handle "position" command
static void HandlePosition(char *args)
{
    // Parse position command: "position [startpos | fen <fen>] [moves <move1> <move2> ...]"

    // Skip whitespace
    while (*args && isspace(*args))
        args++;

    // Check for "startpos"
    if (strncmp(args, "startpos", 8) == 0)
    {
        UCI_SetupStartPosition(&g_Position);
        args += 8;
    }
    else if (strncmp(args, "fen", 3) == 0)
    {
        args += 3;
        while (*args && isspace(*args))
            args++;

        // Find end of FEN: either start of "moves" token or end of string.
        char *fenStart = args;
        char *p        = args;
        char *fenEnd   = args + strlen(args);
        while (*p)
        {
            while (*p && isspace(*p))
                p++;
            if (strncmp(p, "moves", 5) == 0 && (p[5] == '\0' || isspace(p[5])))
            {
                fenEnd = p;
                break;
            }
            while (*p && !isspace(*p))
                p++;
        }

        // Copy FEN into a null-terminated buffer.
        char   fenBuf[128];
        size_t fenLen = (size_t)(fenEnd - fenStart);
        while (fenLen > 0 && isspace((unsigned char)fenStart[fenLen - 1]))
            fenLen--;
        if (fenLen >= sizeof(fenBuf))
            fenLen = sizeof(fenBuf) - 1;
        memcpy(fenBuf, fenStart, fenLen);
        fenBuf[fenLen] = '\0';

        if (!UCI_SetupFromFEN(&g_Position, fenBuf))
        {
            printf("info string failed to parse FEN: %s\n", fenBuf);
            return;
        }
        args = fenEnd;
    }
    else
    {
        return; // Invalid position command
    }

    // Skip whitespace
    while (*args && isspace(*args))
        args++;

    // Check for "moves" keyword
    if (strncmp(args, "moves", 5) == 0)
    {
        args += 5;

        // Parse and apply each move
        while (*args)
        {
            // Skip whitespace
            while (*args && isspace(*args))
                args++;
            if (!*args)
                break;

            // Extract move string
            char moveStr[16];
            int  i = 0;
            while (*args && !isspace(*args) && i < 15)
            {
                moveStr[i++] = *args++;
            }
            moveStr[i] = '\0';

            if (i > 0)
            {
                // Parse and apply move
                MOVE m;

                if (!UCI_ParseMove(&g_Position, moveStr, &m))
                {
                    printf("info string failed to parse move: %s\n", moveStr);
                    return;
                }

                UCI_ApplyMove(&g_Position, &m);
            }
        }
    }
}

// Handle "go" command - start search
static void HandleGo(char *args)
{
    // Parse go parameters. NOTE: clock/time values are LONG (32-bit), not INT
    // (= int16_t). UCI clock values in ms easily exceed 32k (a 1-min game is
    // 60000ms), so INT here would overflow to negative and the wtime/btime path
    // would never be taken — the engine would silently fall through to a depth
    // search and use ~no clock.
    INT    depth     = 5;  // Default depth (used only if no time info given)
    LONG   movetime  = -1; // ms per move
    LONG64 nodes     = 0;  // node budget (0 = none)
    LONG   wtime     = -1; // white clock (ms)
    LONG   btime     = -1; // black clock (ms)
    LONG   winc      = 0;  // white increment (ms)
    LONG   binc      = 0;  // black increment (ms)
    INT    movestogo = 0;  // moves to next time control (0 = sudden death / "all moves")
    INT    mateMoves = 0;  // "go mate N" target (0 = no mate search requested)
    BOOL   infinite  = false;
    BOOL   hasDepth  = false;
    BOOL   ponder    = false;

    INT  numSearchMoves = 0; // "go searchmoves" list
    MOVE SearchMoves[maxLegalMoves];

    // Skip whitespace
    while (*args && isspace(*args))
        args++;

    // Parse parameters
    while (*args)
    {
        if (strncmp(args, "depth", 5) == 0 && (args[5] == '\0' || isspace(args[5])))
        {
            args += 5;
            while (*args && isspace(*args))
                args++;
            depth    = atoi(args);
            hasDepth = true;
            while (*args && isdigit(*args))
                args++;
        }
        else if (strncmp(args, "movetime", 8) == 0 && (args[8] == '\0' || isspace(args[8])))
        {
            args += 8;
            while (*args && isspace(*args))
                args++;
            movetime = atoi(args);
            while (*args && isdigit(*args))
                args++;
        }
        else if (strncmp(args, "nodes", 5) == 0 && (args[5] == '\0' || isspace(args[5])))
        {
            args += 5;
            while (*args && isspace(*args))
                args++;
            nodes = atoll(args);
            while (*args && isdigit(*args))
                args++;
        }
        else if (strncmp(args, "searchmoves", 11) == 0 && (args[11] == '\0' || isspace(args[11])))
        {
            // Moves follow until the next token that isn't one.
            args += 11;
            for (;;)
            {
                while (*args && isspace(*args))
                    args++;
                INT len = 0;
                while (args[len] && !isspace(args[len]))
                    len++;
                BOOL isMove = (len == 4 || len == 5) && args[0] >= 'a' && args[0] <= 'h' && args[1] >= '1' &&
                              args[1] <= '8' && args[2] >= 'a' && args[2] <= 'h' && args[3] >= '1' && args[3] <= '8';
                if (!isMove)
                    break;
                char moveStr[6];
                memcpy(moveStr, args, len);
                moveStr[len] = '\0';
                if (numSearchMoves < maxLegalMoves && UCI_ParseMove(&g_Position, moveStr, &SearchMoves[numSearchMoves]))
                    numSearchMoves++;
                args += len;
            }
        }
        else if (strncmp(args, "infinite", 8) == 0 && (args[8] == '\0' || isspace(args[8])))
        {
            infinite = true;
            args += 8;
        }
        else if (strncmp(args, "ponder", 6) == 0 && (args[6] == '\0' || isspace(args[6])))
        {
            ponder = true;
            args += 6;
        }
        else if (strncmp(args, "wtime", 5) == 0 && (args[5] == '\0' || isspace(args[5])))
        {
            args += 5;
            while (*args && isspace(*args))
                args++;
            wtime = atoi(args);
            while (*args && (isdigit(*args) || *args == '-'))
                args++;
        }
        else if (strncmp(args, "btime", 5) == 0 && (args[5] == '\0' || isspace(args[5])))
        {
            args += 5;
            while (*args && isspace(*args))
                args++;
            btime = atoi(args);
            while (*args && (isdigit(*args) || *args == '-'))
                args++;
        }
        else if (strncmp(args, "winc", 4) == 0 && (args[4] == '\0' || isspace(args[4])))
        {
            args += 4;
            while (*args && isspace(*args))
                args++;
            winc = atoi(args);
            while (*args && isdigit(*args))
                args++;
        }
        else if (strncmp(args, "binc", 4) == 0 && (args[4] == '\0' || isspace(args[4])))
        {
            args += 4;
            while (*args && isspace(*args))
                args++;
            binc = atoi(args);
            while (*args && isdigit(*args))
                args++;
        }
        else if (strncmp(args, "movestogo", 9) == 0 && (args[9] == '\0' || isspace(args[9])))
        {
            args += 9;
            while (*args && isspace(*args))
                args++;
            movestogo = atoi(args);
            while (*args && isdigit(*args))
                args++;
        }
        else if (strncmp(args, "mate", 4) == 0 && (args[4] == '\0' || isspace(args[4])))
        {
            args += 4;
            while (*args && isspace(*args))
                args++;
            mateMoves = atoi(args);
            while (*args && isdigit(*args))
                args++;
        }
        else
        {
            args++; // Skip unknown character/parameter
        }

        while (*args && isspace(*args))
            args++;
    }

    // Setup engine parameters from current position
    UCI_StateToParam(&g_Position, &g_Engine.P);

    // Make sure the transposition table is allocated (no-op if Hash hasn't changed).
    EnsureHashAllocated();

    // Reset time-control fields each call (UCI_StateToParam doesn't touch them).
    g_Engine.P.fixedTimeMs = 0;
    g_Engine.P.nodeLimit   = 0;
    g_Engine.P.timeLeft    = 0;
    g_Engine.P.timeInc     = 0;
    g_Engine.P.movesLeft   = allMoves;

    g_Engine.P.searchMoveCount = numSearchMoves;
    memcpy(g_Engine.P.searchMoves, SearchMoves, numSearchMoves * sizeof(MOVE));

    // UCI_AnalyseMode forces full strength even if UCI_LimitStrength is on.
    g_Engine.P.reduceStrength = g_LimitStrength && !g_AnalyseMode;

    // Pick our side's clock if wtime/btime were provided.
    LONG ourTime = (g_Position.player == white) ? wtime : btime;
    LONG ourInc  = (g_Position.player == white) ? winc : binc;

    // Determine search mode in priority order:
    //   mate > infinite > movetime > nodes > clock (wtime/btime) > depth
    if (mateMoves > 0)
    {
        // P.depth = N means search up to mateDepth = 2N-1 plies (Search.c sets this).
        g_Engine.P.playingMode = mode_Mate;
        g_Engine.P.depth       = mateMoves;
    }
    else if (infinite)
    {
        g_Engine.P.playingMode = mode_Infinite;
    }
    else if (movetime > 0)
    {
        // Fixed time per move (UCI "go movetime"): hand the budget directly to
        // AllocateTime via the fixedTimeMs override.
        g_Engine.P.playingMode = mode_Time;
        g_Engine.P.fixedTimeMs = movetime;
    }
    else if (nodes > 0)
    {
        g_Engine.P.playingMode = mode_FixDepth;
        g_Engine.P.depth       = hasDepth ? depth : maxSearchDepth;
        g_Engine.P.nodeLimit   = nodes;
    }
    else if (ourTime > 0)
    {
        // Clock-based: feed AllocateTime via the existing PARAM clock fields.
        // timeLeft and timeInc are in milliseconds (matches UCI wire format directly,
        // and AllocateTime now does ms-precision math with the increment factored in).
        g_Engine.P.playingMode = mode_Time;
        g_Engine.P.timeLeft    = ourTime;
        g_Engine.P.timeInc     = ourInc;
        g_Engine.P.movesLeft   = (movestogo > 0) ? movestogo : allMoves;
    }
    else
    {
        g_Engine.P.playingMode = mode_FixDepth;
        g_Engine.P.depth       = hasDepth ? depth : 5;
    }

    // `go ponder`: search the predicted position during the opponent's turn.
    // backgrounding=true makes TimeOut ignore deadlines and AnotherIteration
    // keep deepening — the search runs until `ponderhit` or `stop` arrives via
    // UCI_PollInput. `ponderhit` re-installs proper deadlines from the diminished
    // clock, then the normal time-out logic finishes the search.
    if (ponder)
    {
        g_Engine.P.backgrounding   = true;
        g_PonderState.active       = true;
        g_PonderState.startTime    = Now();
        g_PonderState.initialClock = ourTime;
    }

    // Start search (synchronous - returns when search completes)
    g_GoReleased = !(infinite || ponder);
    Engine_Start(&g_Engine);
    // Apply options deferred during the search now: while waiting for stop, setoption applies
    // at once, and a later deferred value must not override it. bestmove (S.MainLine) is safe.
    ApplyPendingOptions();
    WaitForGoRelease();

    // Either ponderhit/stop already cleared this, or the search ended on its own.
    g_PonderState.active     = false;
    g_Engine.P.backgrounding = false;

    // If a `quit` arrived mid-search, the poller cleared g_Running and aborted
    // the engine. UCI says nothing further should be emitted after quit.
    if (!g_Running)
        return;

    // Get and output best move
    MOVE bestMove;

    if (UCI_GetBestMove(&g_Engine, &bestMove))
    {
        char moveStr[16];
        UCI_FormatMove(&bestMove, moveStr);

        // If Ponder is enabled and we have a predicted opponent reply, include
        // it so the GUI can launch its own ponder search.
        if (g_PonderEnabled && !isNull(g_Engine.S.MainLine[1]))
        {
            char ponderStr[16];
            UCI_FormatMove(&g_Engine.S.MainLine[1], ponderStr);
            printf("bestmove %s ponder %s\n", moveStr, ponderStr);
        }
        else
        {
            printf("bestmove %s\n", moveStr);
        }
    }
    else
    {
        printf("bestmove 0000\n"); // No legal move (stalemate/checkmate)
    }
    fflush(stdout);
}

// Handle "perft" command - count leaf nodes for move generation testing
// Uses PerformMove/RetractMove (same incremental updates as the real search)
static void HandlePerft(char *args)
{
    while (*args && isspace(*args))
        args++;

    INT depth = atoi(args);
    if (depth <= 0)
    {
        fprintf(stderr, "Usage: perft <depth>\n");
        return;
    }

    UCI_Perft(&g_Engine, &g_Position, depth);
}

/**************************************************************************************************/
/*                                                                                                */
/*                                  MID-SEARCH INPUT POLLING                                      */
/*                                                                                                */
/**************************************************************************************************/

// Stdin line-accumulator. Static-lifetime because a single non-blocking read may
// return a partial line and the rest will arrive on a later poll. Capacity matches
// the main-loop fgets buffer.
static char   g_PollBuf[8192];
static size_t g_PollLen = 0;

static BOOL StdinHasData(void)
{
    fd_set         rfds;
    struct timeval tv = {0, 0}; // poll, don't block
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    return select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0;
}

// Handle a single line received during search. Only commands that are legal/meaningful
// mid-search are honored; anything else is logged and dropped.
static void DispatchMidSearch(char *line)
{
    while (*line && isspace((unsigned char)*line))
        line++;
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1]))
        *--end = '\0';
    if (!*line)
        return;

    if (strcmp(line, "stop") == 0 || strcmp(line, "ponderhit") == 0 || strcmp(line, "quit") == 0)
        g_GoReleased = true;

    if (strcmp(line, "stop") == 0)
    {
        // If we're pondering, also clear backgrounding so AnotherIteration
        // honors state_Stopping (otherwise backgrounding=true keeps the loop alive).
        if (g_PonderState.active)
        {
            g_PonderState.active     = false;
            g_Engine.P.backgrounding = false;
        }
        Engine_Stop(&g_Engine);
    }
    else if (strcmp(line, "ponderhit") == 0)
    {
        // Opponent played the move we predicted. Convert the ponder search into
        // a real-time search: subtract elapsed-while-pondering from the clock,
        // re-allocate deadlines, and let TimeOut take over.
        if (g_PonderState.active)
        {
            LONG elapsed  = Now() - g_PonderState.startTime;
            LONG newClock = g_PonderState.initialClock - elapsed;
            if (newClock < 100)
                newClock = 100;
            g_Engine.P.timeLeft      = newClock;
            g_Engine.P.backgrounding = false;
            AllocateTime(&g_Engine);
            g_PonderState.active = false;
        }
    }
    else if (strcmp(line, "quit") == 0)
    {
        // Abort harder than stop: skip remaining iterations entirely.
        if (g_PonderState.active)
        {
            g_PonderState.active     = false;
            g_Engine.P.backgrounding = false;
        }
        Engine_Abort(&g_Engine);
        g_Running = false; // HandleGo checks this and suppresses bestmove output.
    }
    else if (strcmp(line, "isready") == 0)
    {
        // UCI: must respond readyok during search too.
        printf("readyok\n");
        fflush(stdout);
    }
    else if (strncmp(line, "setoption", 9) == 0 && (line[9] == '\0' || isspace((unsigned char)line[9])))
    {
        // HandleSetOption defers Hash/Clear Hash internally; other options apply live.
        HandleSetOption(line + 9);
    }
    else
    {
        printf("info string ignoring command during search: %s\n", line);
    }
}

void UCI_PollInput(ENGINE *E)
{
    (void)E;

    while (StdinHasData())
    {
        ssize_t n = read(STDIN_FILENO, g_PollBuf + g_PollLen, sizeof(g_PollBuf) - 1 - g_PollLen);
        if (n <= 0)
        {
            g_GoReleased = true; // EOF or error: don't wait for a stop that can't come.
            return;              // The main loop will notice via fgets failing.
        }

        g_PollLen += (size_t)n;
        g_PollBuf[g_PollLen] = '\0';

        // Process each complete line in the buffer.
        char *start = g_PollBuf;
        char *nl;
        while ((nl = strchr(start, '\n')) != NULL)
        {
            *nl = '\0';
            DispatchMidSearch(start);
            start = nl + 1;
        }

        // Shift any leftover partial line to the front of the buffer.
        size_t remaining = g_PollLen - (size_t)(start - g_PollBuf);
        if (remaining > 0 && start != g_PollBuf)
            memmove(g_PollBuf, start, remaining);
        g_PollLen = remaining;

        // If we filled the buffer without finding a newline, drop the line.
        if (g_PollLen >= sizeof(g_PollBuf) - 1)
        {
            printf("info string poll buffer overflow, dropping\n");
            g_PollLen = 0;
        }
    }
}

// Block until "stop", "ponderhit" or "quit" arrives (see g_GoReleased).
static void WaitForGoRelease(void)
{
    while (!g_GoReleased && g_Running)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, NULL) > 0)
            UCI_PollInput(&g_Engine);
    }
}

/**************************************************************************************************/
/*                                                                                                */
/*                                    STOP / QUIT HANDLERS                                        */
/*                                                                                                */
/**************************************************************************************************/

// Handle "stop" command - stop current search
static void HandleStop(void)
{
    if (g_Engine.R.taskRunning)
    {
        Engine_Stop(&g_Engine);
    }
}

// Handle "quit" command - exit program
static void HandleQuit(void)
{
    if (g_Engine.R.taskRunning)
    {
        Engine_Abort(&g_Engine);
    }
    g_Running = false;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                       COMMAND PARSER                                           */
/*                                                                                                */
/**************************************************************************************************/

static void ProcessCommand(char *line)
{
    // Trim leading whitespace
    while (*line && isspace(*line))
        line++;

    // Trim trailing whitespace
    char *end = line + strlen(line) - 1;
    while (end > line && isspace(*end))
    {
        *end = '\0';
        end--;
    }

    // Empty line
    if (!*line)
        return;

    // Dispatch command
    if (strcmp(line, "uci") == 0)
    {
        HandleUCI();
    }
    else if (strcmp(line, "isready") == 0)
    {
        HandleIsReady();
    }
    else if (strcmp(line, "ucinewgame") == 0)
    {
        HandleNewGame();
    }
    else if (strncmp(line, "setoption", 9) == 0 && (line[9] == '\0' || isspace(line[9])))
    {
        HandleSetOption(line + 9);
    }
    else if (strncmp(line, "position", 8) == 0)
    {
        HandlePosition(line + 8);
    }
    else if (strncmp(line, "go", 2) == 0 && (line[2] == '\0' || isspace(line[2])))
    {
        HandleGo(line + 2);
    }
    else if (strncmp(line, "perft", 5) == 0 && (line[5] == '\0' || isspace(line[5])))
    {
        HandlePerft(line + 5);
    }
    else if (strcmp(line, "tbwdl") == 0) // Hidden debug command: WDL of the current position.
    {
        SZ_POS pos;
        SQUARE ep  = nullSq;
        INT    wdl = SZ_FAIL;
        if (g_Position.moveCount > 0)
            ep = TB_EpSquare(&g_Position.moveHistory[g_Position.moveCount - 1]);
        if (TB_BuildPos(g_Position.Board, &pos))
            wdl = SZ_ProbeWDL(&pos, ep == nullSq ? 0 : rank(ep) * 8 + file(ep), g_Position.player == white);
        if (wdl == SZ_FAIL)
            printf("info string tbwdl fail\n");
        else
            printf("info string tbwdl %d\n", wdl);
        fflush(stdout);
    }
    else if (strcmp(line, "attackcheck") == 0)
    {
        UCI_VerifyAttacks(&g_Engine, &g_Position);
    }
    else if (strcmp(line, "eval") == 0)
    {
        UCI_DumpEval(&g_Engine, &g_Position);
    }
    else if (strcmp(line, "dumpboard") == 0)
    {
        printf("UCI_State.Board (file a..h per rank, rank 8 down):\n");
        for (INT r = 7; r >= 0; r--)
        {
            printf("  %d: ", r + 1);
            for (INT f = 0; f < 8; f++)
            {
                SQUARE sq = (r << 4) | f;
                printf("%2d ", (int)g_Position.Board[sq]);
            }
            printf("\n");
        }
        // Sync engine then dump E->B.Board too (must matche unless CalcBoardState differs)
        UCI_StateToParam(&g_Position, &g_Engine.P);
        for (int i = 0; i < boardSize; i++)
            g_Engine.B.Board[i] = g_Engine.P.Board[i];
        printf("Engine.B.Board (after UCI_StateToParam + memcpy):\n");
        for (INT r = 7; r >= 0; r--)
        {
            printf("  %d: ", r + 1);
            for (INT f = 0; f < 8; f++)
            {
                SQUARE sq = (r << 4) | f;
                printf("%2d ", (int)g_Engine.B.Board[sq]);
            }
            printf("\n");
        }
        fflush(stdout);
    }
    else if (strcmp(line, "dumpdraw") == 0)
    {
        printf("plyCount=%d player=%d\n", (int)g_Position.plyCount, (int)g_Position.player);
        for (INT i = 0; i <= g_Position.plyCount && i < MAX_MOVE_HISTORY; i++)
        {
            printf("  drawData[%d] hashKey=0x%08x irr=%d repCount=%d\n", (int)i,
                   (unsigned)g_Position.drawData[i].hashKey, (int)g_Position.drawData[i].irr,
                   (int)g_Position.drawData[i].repCount);
        }
        fflush(stdout);
    }
    else if (strcmp(line, "stop") == 0)
    {
        HandleStop();
    }
    else if (strcmp(line, "quit") == 0)
    {
        HandleQuit();
    }
    // Ignore unknown commands (UCI spec says to ignore them).
}

/**************************************************************************************************/
/*                                                                                                */
/*                                          MAIN LOOP                                             */
/*                                                                                                */
/**************************************************************************************************/

int main(int argc, char *argv[])
{
    // Disable FILE*-level buffering on stdin so the main loop's fgets() and the
    // mid-search read() in UCI_PollInput don't fight over buffered bytes.
    setvbuf(stdin, NULL, _IONBF, 0);

    // Line-buffer stdout so `info string` messages emitted via printf reach the
    // GUI promptly even when stdout is piped (default would be fully buffered).
    setvbuf(stdout, NULL, _IOLBF, 0);

    // Initialize engine system
    Engine_InitSystem(&g_Global, NULL);

    // Create engine instance
    if (!Engine_Create(&g_Global, &g_Engine))
    {
        fprintf(stderr, "Failed to create engine\n");
        return 1;
    }

    // Bind GLOBAL so position setup / move application produce real Zobrist
    // keys (needed for repetition detection and TT keying).
    UCI_BindGlobal(&g_Position, &g_Global);

    // Setup initial position
    UCI_SetupStartPosition(&g_Position);

    // Allocate the default-sized hash table up front.
    EnsureHashAllocated();

    // Main command loop
    char line[8192];
    while (g_Running && fgets(line, sizeof(line), stdin))
    {
        ProcessCommand(line);
    }

    // Cleanup
    Engine_Destroy(&g_Engine);

    return 0;
}
