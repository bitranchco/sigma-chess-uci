#include "uci_bridge.h"
#include "Board.f"
#include "Attack.f"
#include "Move.f"
#include "MoveGen.f"
#include "PerformMove.f"
#include "HashCode.f"
#include "Search.f"
#include "PieceVal.f"
#include "Evaluate.f"
#include "Tablebase.f"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/**************************************************************************************************/
/*                                                                                                */
/*                                   SQUARE/PIECE STRING UTILITIES                                */
/*                                                                                                */
/**************************************************************************************************/

// Convert square enum to UCI notation string (e.g., 0x11 -> "e2")
void UCI_SquareToString(SQUARE sq, char *str)
{
    if (offBoard(sq))
    {
        strcpy(str, "--");
        return;
    }

    int file = sq & 0x07; // Bits 0-2: file (0-7)
    int rank = sq >> 4;   // Bits 4-6: rank (0-7)

    str[0] = 'a' + file;
    str[1] = '1' + rank;
    str[2] = '\0';
}

// Parse UCI square notation to square enum (e.g., "e2" -> 0x11)
BOOL UCI_ParseSquare(const char *str, SQUARE *sq)
{
    if (!str || strlen(str) < 2)
        return false;

    char fileChar = tolower(str[0]);
    char rankChar = str[1];

    if (fileChar < 'a' || fileChar > 'h')
        return false;
    if (rankChar < '1' || rankChar > '8')
        return false;

    int file = fileChar - 'a'; // 0-7
    int rank = rankChar - '1'; // 0-7

    *sq = square(file, rank);
    return true;
}

// Convert piece to character (wPawn -> 'P', bKnight -> 'n', etc.)
char UCI_PieceToChar(PIECE p)
{
    if (p == empty)
        return ' ';

    char c;
    switch (pieceType(p))
    {
    case pawn:
        c = 'p';
        break;
    case knight:
        c = 'n';
        break;
    case bishop:
        c = 'b';
        break;
    case rook:
        c = 'r';
        break;
    case queen:
        c = 'q';
        break;
    case king:
        c = 'k';
        break;
    default:
        c = '?';
        break;
    }

    // Uppercase for white pieces
    if (pieceColour(p) == white)
        c = toupper(c);

    return c;
}

// Parse piece character to piece enum ('P' -> wPawn, 'n' -> bKnight, etc.)
PIECE UCI_CharToPiece(char c)
{
    COLOUR colour = isupper(c) ? white : black;
    c             = tolower(c);

    PIECE p = empty;
    switch (c)
    {
    case 'p':
        p = colour | pawn;
        break;
    case 'n':
        p = colour | knight;
        break;
    case 'b':
        p = colour | bishop;
        break;
    case 'r':
        p = colour | rook;
        break;
    case 'q':
        p = colour | queen;
        break;
    case 'k':
        p = colour | king;
        break;
    }
    return p;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                       MOVE FORMATTING/PARSING                                  */
/*                                                                                                */
/**************************************************************************************************/

// Format a MOVE struct to UCI notation (e.g., "e2e4", "e7e8q", "e1g1")
void UCI_FormatMove(const MOVE *m, char *str)
{
    char fromStr[3], toStr[3];

    UCI_SquareToString(m->from, fromStr);
    UCI_SquareToString(m->to, toStr);

    strcpy(str, fromStr);
    strcat(str, toStr);

    // Add promotion piece if applicable
    if (isPromotion(*m))
    {
        PIECE promoPiece = m->type & mtype_Promotion;
        char  promoChar  = tolower(UCI_PieceToChar(promoPiece));
        int   len        = strlen(str);
        str[len]         = promoChar;
        str[len + 1]     = '\0';
    }
}

// Parse UCI move notation to a MOVE struct (pure string parsing, no validation)
BOOL UCI_ParseMove(UCI_State *state, const char *str, MOVE *m)
{
    if (!str || strlen(str) < 4)
        return false;

    // Parse from and to squares
    SQUARE from, to;
    char   fromStr[3] = {str[0], str[1], '\0'};
    char   toStr[3]   = {str[2], str[3], '\0'};

    if (!UCI_ParseSquare(fromStr, &from))
        return false;
    if (!UCI_ParseSquare(toStr, &to))
        return false;

    // Build basic MOVE structure
    m->from  = from;
    m->to    = to;
    m->piece = state->Board[from];
    m->cap   = state->Board[to];
    m->type  = mtype_Normal;
    m->dir   = 0;
    m->dply  = 1;
    m->misc  = 0;

    // Castling notation — only when the moving piece is actually a king;
    // otherwise a rook traversing e1→g1 gets misclassified as O-O.
    if (pieceType(m->piece) == king)
    {
        if (strcmp(str, "e1g1") == 0 || strcmp(str, "e8g8") == 0)
        {
            m->type = mtype_O_O; // Kingside castling
        }
        else if (strcmp(str, "e1c1") == 0 || strcmp(str, "e8c8") == 0)
        {
            m->type = mtype_O_O_O; // Queenside castling
        }
    }
    else
    {
        // Detect en passant (pawn capturing diagonally to empty square)
        PIECE piece = state->Board[from];
        if (pieceType(piece) == pawn && m->cap == empty)
        {
            INT delta = to - from;
            // Diagonal pawn moves: 0x0F, 0x11, -0x0F, -0x11
            if (delta == 0x0F || delta == 0x11 || delta == -0x0F || delta == -0x11)
            {
                m->type = mtype_EP;
                // Set cap to the captured pawn
                INT pawnDir = (pieceColour(piece) == white) ? -0x10 : 0x10;
                m->cap      = state->Board[to + pawnDir];
            }
        }
    }

    // Handle promotion
    if (strlen(str) >= 5)
    {
        char   promoChar = tolower(str[4]);
        COLOUR colour    = state->player;

        switch (promoChar)
        {
        case 'q':
            m->type = colour | queen;
            break;
        case 'r':
            m->type = colour | rook;
            break;
        case 'b':
            m->type = colour | bishop;
            break;
        case 'n':
            m->type = colour | knight;
            break;
        default:
            return false; // Invalid promotion piece
        }
    }

    return true;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                    POSITION SETUP/MANAGEMENT                                   */
/*                                                                                                */
/**************************************************************************************************/

// Initialize UCI_State to starting position
void UCI_SetupStartPosition(UCI_State *state)
{
    UCI_ClearState(state);

    // Setup initial board position
    NewBoard(state->Board);

    // HasMovedTo: 0 = untouched (castling allowed), non-zero = moved (no castling).
    // (PerformMove increments HasMovedTo[dst] on each move; MoveGen checks it as
    // a boolean: any non-zero blocks castling.)
    for (int i = 0; i < boardSize; i++)
        state->HasMovedTo[i] = 0;

    state->player       = white;
    state->moveCount    = 0;
    state->plyCount     = 0;
    state->fenHalfmoves = 0;

    // Seed drawData[0] with the starting position's hash so search-side
    // repetition / TT keys roll correctly.
    if (state->Global != NULL)
        state->drawData[0].hashKey = CalcHashKey(state->Global, state->Board);
    state->drawData[0].castle = castleRights(state->Board, state->HasMovedTo);
}

// Setup UCI_State from FEN string
BOOL UCI_SetupFromFEN(UCI_State *state, const char *fen)
{
    UCI_ClearState(state);

    const char *p    = fen;
    int         rank = 7; // Start at rank 8 (0x70)
    int         file = 0;

    // Parse piece placement (ranks 8 down to 1)
    while (*p && *p != ' ')
    {
        if (*p == '/')
        {
            rank--;
            file = 0;
            p++;
            continue;
        }

        if (isdigit(*p))
        {
            // Empty squares
            int emptyCount = *p - '0';
            file += emptyCount;
            p++;
            continue;
        }

        // Parse piece
        PIECE piece = UCI_CharToPiece(*p);
        if (piece == empty)
            return false; // Invalid piece character

        SQUARE sq = square(file, rank);
        if (offBoard(sq))
            return false; // Out of bounds

        state->Board[sq] = piece;
        file++;
        p++;
    }

    if (*p != ' ')
        return false;
    p++; // Skip space

    // Parse active color
    if (*p == 'w')
        state->player = white;
    else if (*p == 'b')
        state->player = black;
    else
        return false;
    p++;

    if (*p != ' ')
        return false;
    p++; // Skip space

    // Parse castling rights. Convention: HasMovedTo[sq] == 0 means "untouched"
    // (PerformMove increments on each move, MoveGen tests as boolean for
    // castling). Default everything to 0 = untouched, then mark missing rights
    // by setting the relevant rook square non-zero. If neither side of a king's
    // castling rights is present, mark the king itself moved (blocks both sides).
    for (int i = 0; i < boardSize; i++)
        state->HasMovedTo[i] = 0;

    if (*p == '-')
    {
        // No castling rights for either side: mark both kings as moved.
        state->HasMovedTo[e1] = 1;
        state->HasMovedTo[e8] = 1;
        p++;
    }
    else
    {
        BOOL wK = false, wQ = false, bK = false, bQ = false;
        while (*p && *p != ' ')
        {
            switch (*p)
            {
            case 'K':
                wK = true;
                break;
            case 'Q':
                wQ = true;
                break;
            case 'k':
                bK = true;
                break;
            case 'q':
                bQ = true;
                break;
            }
            p++;
        }
        // For each missing castling right, mark the relevant piece as moved.
        if (!wK && !wQ)
            state->HasMovedTo[e1] = 1; // White king moved
        else
        {
            if (!wK)
                state->HasMovedTo[h1] = 1; // h-rook moved
            if (!wQ)
                state->HasMovedTo[a1] = 1; // a-rook moved
        }
        if (!bK && !bQ)
            state->HasMovedTo[e8] = 1; // Black king moved
        else
        {
            if (!bK)
                state->HasMovedTo[h8] = 1;
            if (!bQ)
                state->HasMovedTo[a8] = 1;
        }
    }

    // En passant target square: recorded as the previous move (a double pawn push).
    SQUARE epSq = nullSq;
    if (*p == ' ')
    {
        p++;
        if (p[0] >= 'a' && p[0] <= 'h' && (p[1] == '3' || p[1] == '6'))
            epSq = square(p[0] - 'a', p[1] - '1');
        while (*p && *p != ' ')
            p++;
    }

    state->moveCount    = 0;
    state->plyCount     = 0;
    state->fenHalfmoves = 0;

    // Halfmove clock (optional; missing or nonnumeric means 0, capped at 100). Fullmove ignored.
    while (*p == ' ')
        p++;
    if (*p >= '0' && *p <= '9')
    {
        long clock          = strtol(p, NULL, 10); // long: MinL would truncate to 32 bits first.
        state->fenHalfmoves = (INT)(clock > 100 ? 100 : clock);
    }

    if (epSq != nullSq)
    {
        COLOUR mover = black - state->player;
        INT    dir   = (mover == white ? 0x10 : -0x10);
        MOVE  *m     = &state->moveHistory[0];
        memset(m, 0, sizeof(*m));
        m->piece         = mover + pawn;
        m->from          = epSq - dir;
        m->to            = epSq + dir;
        m->cap           = empty;
        m->type          = mtype_Normal;
        state->moveCount = 1;
    }

    if (state->Global != NULL)
        state->drawData[0].hashKey = CalcHashKey(state->Global, state->Board);
    state->drawData[0].castle = castleRights(state->Board, state->HasMovedTo);

    return true;
}

// Convert UCI_State to FEN string
void UCI_StateToFEN(const UCI_State *state, char *fen)
{
    char *p = fen;

    // Piece placement (rank 8 to rank 1)
    for (int rank = 7; rank >= 0; rank--)
    {
        int emptyCount = 0;

        for (int file = 0; file < 8; file++)
        {
            SQUARE sq    = square(file, rank);
            PIECE  piece = state->Board[sq];

            if (piece == empty)
            {
                emptyCount++;
            }
            else
            {
                // Output any accumulated empty squares
                if (emptyCount > 0)
                {
                    *p++       = '0' + emptyCount;
                    emptyCount = 0;
                }

                // Output piece
                *p++ = UCI_PieceToChar(piece);
            }
        }

        // Output any trailing empty squares on this rank
        if (emptyCount > 0)
        {
            *p++ = '0' + emptyCount;
        }

        // Add rank separator (except after rank 1)
        if (rank > 0)
            *p++ = '/';
    }

    // Active color
    *p++ = ' ';
    *p++ = (state->player == white) ? 'w' : 'b';

    // Castling rights (based on HasMovedTo tracking)
    *p++                   = ' ';
    BOOL hasCastlingRights = false;

    // HasMovedTo == 0 means "untouched" (castling allowed); non-zero means "moved".
    if (state->HasMovedTo[e1] == 0 && state->HasMovedTo[h1] == 0)
    {
        *p++              = 'K';
        hasCastlingRights = true;
    }
    if (state->HasMovedTo[e1] == 0 && state->HasMovedTo[a1] == 0)
    {
        *p++              = 'Q';
        hasCastlingRights = true;
    }
    if (state->HasMovedTo[e8] == 0 && state->HasMovedTo[h8] == 0)
    {
        *p++              = 'k';
        hasCastlingRights = true;
    }
    if (state->HasMovedTo[e8] == 0 && state->HasMovedTo[a8] == 0)
    {
        *p++              = 'q';
        hasCastlingRights = true;
    }

    if (!hasCastlingRights)
        *p++ = '-';

    // En passant target square (we don't track this, so always "-")
    *p++ = ' ';
    *p++ = '-';

    // Halfmove clock and fullmove number
    sprintf(p, " 0 1");
}

// Bind a GLOBAL for hash-key computation. Idempotent.
void UCI_BindGlobal(UCI_State *state, GLOBAL *G)
{
    state->Global = G;
}

// Clear/reset the UCI_State
void UCI_ClearState(UCI_State *state)
{
    GLOBAL *savedGlobal = state->Global; // preserve binding across reset

    // Clear board. HasMovedTo: 0 = untouched (castling allowed); the engine's
    // MoveGen treats any non-zero as "moved".
    for (int i = 0; i < boardSize; i++)
    {
        state->Board[i]      = empty;
        state->HasMovedTo[i] = 0;
    }

    state->player       = white;
    state->moveCount    = 0;
    state->plyCount     = 0;
    state->fenHalfmoves = 0;
    state->Global       = savedGlobal;

    // Clear move history
    for (int i = 0; i < MAX_MOVE_HISTORY; i++)
    {
        clrMove(state->moveHistory[i]);
        state->drawData[i].hashKey  = 0;
        state->drawData[i].irr      = 0;
        state->drawData[i].repCount = 0;
        state->drawData[i].castle   = 0;
    }
}

// Apply a move to the UCI_State (updates Board, moveHistory, drawData, etc.)
void UCI_ApplyMove(UCI_State *state, const MOVE *m)
{
    if (state->moveCount >= MAX_MOVE_HISTORY - 1)
        return; // History buffer full

    // Store move in history
    state->moveHistory[state->moveCount] = *m;

    // Update board
    PIECE  piece = m->piece;
    SQUARE from  = m->from;
    SQUARE to    = m->to;

    state->Board[from] = empty;

    // Handle special move types
    if (m->type & mtype_O_O)
    { // Kingside castling
        state->Board[to] = piece;
        // Move rook
        SQUARE rookFrom           = from + 3;
        SQUARE rookTo             = from + 1;
        state->Board[rookTo]      = state->Board[rookFrom];
        state->Board[rookFrom]    = empty;
        state->HasMovedTo[rookTo] = state->plyCount;
    }
    else if (m->type & mtype_O_O_O)
    { // Queenside castling
        state->Board[to] = piece;
        // Move rook
        SQUARE rookFrom           = from - 4;
        SQUARE rookTo             = from - 1;
        state->Board[rookTo]      = state->Board[rookFrom];
        state->Board[rookFrom]    = empty;
        state->HasMovedTo[rookTo] = state->plyCount;
    }
    else if (m->type & mtype_EP)
    { // En passant
        state->Board[to] = piece;
        // Remove captured pawn
        INT pawnDir                = (pieceColour(piece) == white) ? -0x10 : 0x10;
        state->Board[to + pawnDir] = empty;
    }
    else if (isPromotion(*m))
    {
        PIECE promoPiece = m->type & mtype_Promotion;
        state->Board[to] = promoPiece;
    }
    else
    { // Normal move or capture
        state->Board[to] = piece;
    }

    // Update HasMovedTo for moved piece
    state->HasMovedTo[to] = state->plyCount;

    // Advance counters first; drawData[plyCount] now refers to the slot for the
    // position AFTER this move (matches the engine convention where
    // DrawData[lastMoveNo] holds the current root position's key).
    state->moveCount++;
    state->plyCount++;

    DRAWDATA *dt     = &state->drawData[state->plyCount];
    DRAWDATA *prevDt = &state->drawData[state->plyCount - 1];

    if (state->Global != NULL)
        dt->hashKey = prevDt->hashKey ^ HashKeyChange(state->Global, (MOVE *)m);
    else
        dt->hashKey = 0;

    // irr = ply index of position right after the most recent irreversible move
    // (capture, pawn move, or special). Used by the 50-move rule and as the
    // floor for repetition scans.
    dt->irr    = irreversible(*m) ? state->plyCount : prevDt->irr;
    dt->castle = castleRights(state->Board, state->HasMovedTo);

    // Repetition count: scan back by 2 (same side to move) until the irr floor.
    // Only meaningful for reversible moves and only when hashKey is real.
    dt->repCount = 0;
    if (state->Global != NULL && !irreversible(*m))
    {
        for (INT i = state->plyCount - 2; i >= dt->irr; i -= 2)
        {
            if (state->drawData[i].hashKey == dt->hashKey && state->drawData[i].castle == dt->castle)
            {
                dt->repCount = state->drawData[i].repCount + 1;
                break;
            }
        }
    }

    // Switch player
    state->player = (state->player == white) ? black : white;
}

// Setup engine PARAM structure from UCI_State
void UCI_StateToParam(const UCI_State *state, PARAM *param)
{
    // Copy board position
    for (int i = 0; i < boardSize; i++)
    {
        param->Board[i]      = state->Board[i];
        param->HasMovedTo[i] = state->HasMovedTo[i];
    }

    param->player       = state->player;
    param->lastMoveNo   = state->plyCount;
    param->fenHalfmoves = state->fenHalfmoves;

    if (state->moveCount > 0)
        param->lastMove = state->moveHistory[state->moveCount - 1];
    else
        clrMove(param->lastMove);

    param->DrawData = (DRAWDATA *)state->drawData;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                     ENGINE CONTROL HELPERS                                     */
/*                                                                                                */
/**************************************************************************************************/

// Extract best move from engine after search completes
BOOL UCI_GetBestMove(ENGINE *E, MOVE *bestMove)
{
    if (E->S.numRootMoves == 0)
        return false;

    *bestMove = Engine_BestMove(E);

    // Check if move is valid
    return !isNull(*bestMove);
}

/**************************************************************************************************/
/*                                                                                                */
/*                                            PERFT                                               */
/*                                                                                                */
/**************************************************************************************************/
/*                                                                                                */
/*                                     DEBUG / VERIFICATION                                       */
/*                                                                                                */
/**************************************************************************************************/

void UCI_DumpEval(ENGINE *E, UCI_State *state)
{
    UCI_StateToParam(state, &E->P);
    InitSearchState(E);
    E->S.rootNode = (E->P.player == white ? E->S.whiteNode : E->S.blackNode);
    E->S.currNode = E->S.rootNode;
    CalcBoardState(E);
    CalcAttackState(E);
    CalcPieceValState(E);
    CalcEvaluateState(E);

    NODE *N = E->S.rootNode;

    // Components are stored from white's POV (per Search.h:94-95). totalEval is
    // already flipped to side-to-move POV by CalcEvaluateState.
    printf("eval total %d pvSum %d mob %d pawnStruct %d endGame %d player %d\n", (int)N->totalEval, (int)N->pvSumEval,
           (int)N->mobEval, (int)N->pawnStructEval, (int)N->endGameEval, (int)N->player);
    fflush(stdout);
}

INT UCI_VerifyAttacks(ENGINE *E, UCI_State *state)
{
    // Set up position
    UCI_StateToParam(state, &E->P);
    InitSearchState(E);
    E->S.rootNode = (E->P.player == white ? E->S.whiteNode : E->S.blackNode);
    E->S.currNode = E->S.rootNode;
    CalcBoardState(E);
    CalcAttackState(E);

    // Save the CalcAttackState result
    ATTACK  refW_buf[boardSize1 + boardSize2];
    ATTACK  refB_buf[boardSize1 + boardSize2];
    ATTACK *refW = refW_buf + boardSize1;
    ATTACK *refB = refB_buf + boardSize1;
    for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
    {
        refW[sq] = E->A.AttackW[sq];
        refB[sq] = E->A.AttackB[sq];
    }
    BYTE refPawnW[8], refPawnB[8];
    for (INT r = 0; r < 8; r++)
    {
        refPawnW[r] = E->B.PawnStructW[r];
        refPawnB[r] = E->B.PawnStructB[r];
    }

    // Now do a Perform + Retract for each legal move and check the state is restored
    NODE *N          = E->S.rootNode;
    N[-1].m.from     = 0;
    N[-1].m.to       = 0; // Clear prev move
    N[-1].gen        = gen_None;
    N[0].check       = (N->Attack_[N->PieceLoc[0]] > 0);
    N[0].quies       = false;
    N[0].maxPly      = 0;
    N[0].bufStart    = E->S.SBuf;
    N[0].storeSacri  = true;
    N[0].escapeSq    = nullSq;
    N[0].recapSq     = nullSq;
    N[0].ply         = 0;
    N[0].depth       = 0;
    N[0].gen         = gen_None;
    N[-1].lastPiece_ = E->B.LastPiece[E->B.player];
    N[-1].lastPiece  = E->B.LastPiece[E->B.opponent];
    N[0].lastPiece   = N[-1].lastPiece_;
    N[0].lastPiece_  = N[-1].lastPiece;
    clrMove(N[0].m);
    clrMove(N[0].rfm);

    E->R.state        = state_Root;
    E->S.numRootMoves = 0;
    GenRootMoves(E);

    INT errors = 0;
    for (INT i = 0; i < E->S.numRootMoves; i++)
    {
        N->m = E->S.RootMoves[i];

        ATTACK f1_pre = E->A.AttackW[0x05];
        PerformMove(E);
        ATTACK f1_post = E->A.AttackW[0x05];
        if (f1_pre != f1_post)
        {
            char ms[16];
            UCI_FormatMove(&N->m, ms);
            fprintf(stderr, "F1_CHANGE during %s: %08x -> %08x (diff=%08x)\n", ms, f1_pre, f1_post, f1_pre ^ f1_post);
        }

        // Check: does CalcAttackState match incremental?
        ATTACK  checkW_buf[boardSize1 + boardSize2];
        ATTACK  checkB_buf[boardSize1 + boardSize2];
        ATTACK *checkW = checkW_buf + boardSize1;
        ATTACK *checkB = checkB_buf + boardSize1;
        for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
        {
            checkW[sq] = E->A.AttackW[sq];
            checkB[sq] = E->A.AttackB[sq];
        }
        BYTE checkPawnW[8], checkPawnB[8];
        for (INT r = 0; r < 8; r++)
        {
            checkPawnW[r] = E->B.PawnStructW[r];
            checkPawnB[r] = E->B.PawnStructB[r];
        }
        INT savedMob = E->S.rootNode->mobEval;

        CalcAttackState(E);

        char moveStr[16];
        UCI_FormatMove(&N->m, moveStr);

        for (SQUARE sq = 0; sq <= 0x77; sq++)
        {
            if (!onBoard(sq))
                continue;
            ATTACK dW = checkW[sq] ^ E->A.AttackW[sq];
            ATTACK dB = checkB[sq] ^ E->A.AttackB[sq];
            if (dW || dB)
            {
                fprintf(stderr, "MISMATCH after %s: sq=%c%c incW=%08x calcW=%08x incB=%08x calcB=%08x\n", moveStr,
                        'a' + file(sq), '1' + rank(sq), checkW[sq], E->A.AttackW[sq], checkB[sq], E->A.AttackB[sq]);
                errors++;
            }
        }

        // Restore incremental state and retract
        for (SQUARE sq = -boardSize1; sq < boardSize2; sq++)
        {
            E->A.AttackW[sq] = checkW[sq];
            E->A.AttackB[sq] = checkB[sq];
        }
        for (INT r = 0; r < 8; r++)
        {
            E->B.PawnStructW[r] = checkPawnW[r];
            E->B.PawnStructB[r] = checkPawnB[r];
        }
        E->S.rootNode->mobEval = savedMob;

        RetractMove(E);
    }

    if (errors == 0)
        fprintf(stderr, "All %d moves verified OK\n", E->S.numRootMoves);
    else
        fprintf(stderr, "%d mismatches found across %d moves\n", errors, E->S.numRootMoves);

    return errors;
}

/**************************************************************************************************/
/*                                                                                                */
/*                                       UCI INFO OUTPUT                                          */
/*                                                                                                */
/**************************************************************************************************/

void UCI_SendInfo(ENGINE *E)
{
    SEARCH_STATE *S = &E->S;

    // Compute elapsed time in milliseconds
    LONG elapsed_ms = Now() - S->startTime;
    if (elapsed_ms < 1)
        elapsed_ms = 1;

    // moveCount = per-interior-node count (nodeCount only ticks at the root).
    LONG64 nodes = S->moveCount;
    LONG64 nps   = (nodes * 1000) / elapsed_ms;

    INT hashfull = 0;
    if (E->Tr.transSize > 0)
        hashfull = (INT)((1000LL * E->Tr.transUsed) / E->Tr.transSize);

    // Format score. Bound suffix only on cp (UCI spec). A tablebase root reports its exact
    // tablebase score unless the search found a mate; with MultiPV every kept line shares the
    // best tablebase result, so each line gets the same score.
    char scoreBuf[64];
    if (S->bestScore >= mateWinVal)
    {
        INT movesToMate = (maxVal - S->bestScore + 1) / 2;
        snprintf(scoreBuf, sizeof(scoreBuf), "score mate %d", movesToMate);
    }
    else if (S->bestScore <= mateLoseVal)
    {
        INT movesToMate = -(maxVal + S->bestScore + 1) / 2;
        snprintf(scoreBuf, sizeof(scoreBuf), "score mate %d", movesToMate);
    }
    else if (S->tbRootActive)
    {
        snprintf(scoreBuf, sizeof(scoreBuf), "score cp %d", S->tbRootScore);
    }
    else
    {
        const char *bound = (S->scoreType == scoreType_LowerBound)   ? " lowerbound"
                            : (S->scoreType == scoreType_UpperBound) ? " upperbound"
                                                                     : "";
        snprintf(scoreBuf, sizeof(scoreBuf), "score cp %d%s", S->bestScore, bound);
    }

    // Format PV (principal variation)
    char pvBuf[2048] = "";
    int  pvLen       = 0;
    for (INT i = 0; i < maxSearchDepth && !isNull(S->MainLine[i]); i++)
    {
        char moveStr[16];
        UCI_FormatMove(&S->MainLine[i], moveStr);
        int n = snprintf(pvBuf + pvLen, sizeof(pvBuf) - pvLen, "%s%s", (i > 0 ? " " : ""), moveStr);
        if (n < 0 || pvLen + n >= (int)sizeof(pvBuf))
            break;
        pvLen += n;
    }

    // tbhits only when tablebases are loaded, so output without them is unchanged.
    char tbBuf[32] = "";
    if (TB_Cardinality(E) > 0)
        snprintf(tbBuf, sizeof(tbBuf), " tbhits %lld", (long long)S->tbHits);

    // Omit the multipv token in single-PV mode (some GUIs dislike a stray "multipv 1").
    if (S->multiPVCount > 1)
    {
        printf("info depth %d seldepth %d multipv %d %s nodes %lld nps %lld time %ld hashfull %d%s pv %s\n",
               S->mainDepth, S->selDepth, S->multiPVCurrent, scoreBuf, nodes, nps, elapsed_ms, hashfull, tbBuf, pvBuf);
    }
    else
    {
        printf("info depth %d seldepth %d %s nodes %lld nps %lld time %ld hashfull %d%s pv %s\n", S->mainDepth,
               S->selDepth, scoreBuf, nodes, nps, elapsed_ms, hashfull, tbBuf, pvBuf);
    }
    fflush(stdout);
}

// Throttled to ~1 emission/sec, skipped during the first second of search.
void UCI_SendCurrMove(ENGINE *E)
{
    SEARCH_STATE *S = &E->S;
    if (S->currMove < 0 || S->currMove >= S->numRootMoves)
        return;

    if (Now() - S->startTime < 1000) // Skip currmove during first second of search.
        return;

    static ULONG lastEmit = 0;
    ULONG        now      = (ULONG)Now();
    if (now - lastEmit < 1000) // Throttle currmove emissions to ~1/sec.
        return;
    lastEmit = now;

    char moveStr[16];
    UCI_FormatMove(&S->RootMoves[S->currMove], moveStr);
    printf("info depth %d currmove %s currmovenumber %d\n", S->mainDepth, moveStr, S->currMove + 1);
    fflush(stdout);
}

/**************************************************************************************************/

// Set up the engine state from E->P so GenRootMoves can run.
static void PerftSetup(ENGINE *E)
{
    E->R.state        = state_Root;
    E->S.numRootMoves = 0;

    InitSearchState(E);

    E->S.rootNode = (E->P.player == white ? E->S.whiteNode : E->S.blackNode);
    E->S.currNode = E->S.rootNode;

    CalcBoardState(E);

    NODE *N          = E->S.rootNode;
    N[-1].lastPiece_ = E->B.LastPiece[E->B.player];
    N[-1].lastPiece  = E->B.LastPiece[E->B.opponent];
    N[0].lastPiece   = N[-1].lastPiece_;
    N[0].lastPiece_  = N[-1].lastPiece;

    CalcAttackState(E);

    // Initialize previous node (needed for en passant detection)
    N[-1].m   = E->P.lastMove;
    N[-1].gen = gen_None;

    // Initialize root node fields needed by GenRootMoves/AnalyzeThreats
    N[0].check      = (N->Attack_[N->PieceLoc[0]] > 0);
    N[0].quies      = false;
    N[0].maxPly     = 0;
    N[0].bufStart   = E->S.SBuf;
    N[0].storeSacri = true;
    N[0].escapeSq   = nullSq;
    N[0].recapSq    = nullSq;
    N[0].ply        = 0;
    N[0].depth      = 0;
    N[0].gen        = gen_None;
    clrMove(N[0].m);
    clrMove(N[0].rfm);

    GenRootMoves(E);
}

// Incremental perft: uses PerformMove/RetractMove like the real search,
// instead of rebuilding state from scratch at each level. This exercises
// the same code path as the search engine and will reveal incremental
// update bugs.
static LONG64 PerftRecurseIncremental(ENGINE *E, INT depth)
{
    NODE *N = E->S.currNode;

    // Generate all legal moves at this node
    E->R.state        = state_Root;
    E->S.numRootMoves = 0;
    E->S.bufTop       = N->bufStart;

    N->gen = gen_None;
    clrMove(N->m);
    clrMove(N->rfm);

    N->check      = (N->Attack_[N->PieceLoc[0]] > 0);
    N->quies      = false;
    N->storeSacri = true;
    N->escapeSq   = nullSq;
    N->recapSq    = nullSq;

    GenRootMoves(E);

    INT numMoves = E->S.numRootMoves;

    if (depth == 1)
        return numMoves;

    // Save root moves — GenRootMoves will overwrite on recursion
    MOVE moves[maxLegalMoves];
    for (INT i = 0; i < numMoves; i++)
        moves[i] = E->S.RootMoves[i];

    LONG64 total = 0;

    for (INT i = 0; i < numMoves; i++)
    {
        // Set up the move on the current node (PerformMove reads from N->m)
        N->m = moves[i];

        // PerformMove updates E->B incrementally (board, attacks, PieceLoc, etc.)
        PerformMove(E);

        // Check legality: if our king is attacked after the move, skip it.
        // (GenRootMoves should only give legal moves, but just in case)
        if (N->Attack_[N->PieceLoc[0]])
        {
            RetractMove(E);
            continue;
        }

        // Set up next node for recursion
        NODE *savedRootNode = E->S.rootNode;
        E->S.currNode       = NN;
        E->S.rootNode       = NN;
        NN->bufStart        = E->S.bufTop;
        NN->maxPly          = 0;
        NN->ply             = 0;
        NN->lastPiece       = N->lastPiece_; // Next player's pieces = current opponent's
        NN->lastPiece_      = N->lastPiece;  // Next opponent's pieces = current player's

        total += PerftRecurseIncremental(E, depth - 1);

        // Restore current node
        E->S.currNode = N;
        E->S.rootNode = savedRootNode;
        RetractMove(E);
    }

    return total;
}

// Public perft interface. Prints per-move breakdown (divide) and total.
LONG64 UCI_Perft(ENGINE *E, UCI_State *state, INT depth)
{
    return UCI_PerftIncremental(E, state, depth);
}

// Incremental perft public interface — uses PerformMove/RetractMove like the real search.
LONG64 UCI_PerftIncremental(ENGINE *E, UCI_State *state, INT depth)
{
    if (depth <= 0)
        return 1;

    // Copy UCI state into engine params and do full setup for the root
    UCI_StateToParam(state, &E->P);
    PerftSetup(E);

    INT numMoves = E->S.numRootMoves;

    if (depth == 1)
    {
        for (INT i = 0; i < numMoves; i++)
        {
            char moveStr[16];
            UCI_FormatMove(&E->S.RootMoves[i], moveStr);
            printf("%s: 1\n", moveStr);
        }
        printf("\nNodes searched: %d\n\n", numMoves);
        fflush(stdout);
        return numMoves;
    }

    // Save root moves
    MOVE moves[maxLegalMoves];
    for (INT i = 0; i < numMoves; i++)
        moves[i] = E->S.RootMoves[i];

    NODE *N = E->S.currNode;

    LONG64 total = 0;

    for (INT i = 0; i < numMoves; i++)
    {
        N->m = moves[i];
        PerformMove(E);

        // Skip illegal (shouldn't happen since GenRootMoves filters, but safety)
        if (N->Attack_[N->PieceLoc[0]])
        {
            RetractMove(E);
            continue;
        }

        NODE *savedRootNode = E->S.rootNode;
        E->S.currNode       = NN;
        E->S.rootNode       = NN;
        NN->bufStart        = E->S.bufTop;
        NN->maxPly          = 0;
        NN->ply             = 0;
        NN->lastPiece       = N->lastPiece_;
        NN->lastPiece_      = N->lastPiece;

        LONG64 count = PerftRecurseIncremental(E, depth - 1);

        char moveStr[16];
        UCI_FormatMove(&moves[i], moveStr);
        printf("%s: %lld\n", moveStr, count);

        total += count;

        E->S.rootNode = savedRootNode;
        E->S.currNode = N;
        RetractMove(E);
    }

    printf("\nNodes searched: %lld\n\n", total);
    fflush(stdout);
    return total;
}
