/**************************************************************************************************/
/*                                                                                                */
/* Module  : SEARCHMISC.C                                                                         */
/* Purpose : This module implements various utility routines used during the search, such as      */
/*           "Killer" handling, refutation collision checks, best line updating etc.              */
/*                                                                                                */
/**************************************************************************************************/

#include "SearchMisc.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                       UPDATE BEST LINE                                         */
/*                                                                                                */
/**************************************************************************************************/

// UpdateBestLine - copies the current move and best continuation to the best line
void UpdateBestLine(ENGINE *E)
{
    NODE *N        = E->S.currNode;
    NODE *NextNode = N + 1; // Use NextNode instead of NN to avoid macro collision

    MOVE *L1 = N->BestLine;
    MOVE *L2 = NextNode->BestLine;

    *L1++ = N->m;

    do
    {
        *L1++ = *L2;
    } while (!isNull(*L2++));

} /* UpdateBestLine */

/**************************************************************************************************/
/*                                                                                                */
/*                                       UPDATE DRAW STATE                                        */
/*                                                                                                */
/**************************************************************************************************/

void UpdateDrawState(ENGINE *E)
{
    NODE     *N        = E->S.currNode;
    NODE     *PrevNode = N - 1;        // Previous node (avoid PN macro collision)
    DRAWDATA *dt       = N->drawData;  // Current draw data
    DRAWDATA *prevDt   = dt - 1;       // Previous draw data
    MOVE     *pm       = &PrevNode->m; // Previous move
    GLOBAL   *G        = E->Global;    // Access Global through E->Global

    N->drawType = drawType_None; // N->drawType = drawType_None;

    //--- Compute hash key for current position ---
    // This is done by XOR'ing the hash key change from the most recently performed move with the
    // hash key of the previous position.

    HKEY newkey = prevDt->hashKey; // newkey = (dt-1)->hashKey

    // XOR out piece from source square
    newkey ^= G->H.HashCode[pm->piece][pm->from]; // dt->hashKey ^= HashCode[pm.piece][pm.from];

    // XOR in piece at destination square
    newkey ^= G->H.HashCode[pm->piece][pm->to]; // dt->hashKey ^= HashCode[pm.piece][pm.to];

    // If capture, XOR out captured piece
    if (pm->cap)
        newkey ^= G->H.HashCode[pm->cap][pm->to];

    // Handle special move types
    switch (pm->type)  // switch (pm.type)
    {                  // {
    case mtype_Normal: //    case mtype_Normal : break;
        break;

    case mtype_EP: //    case mtype_EP :
    {
        COLOUR player = pieceColour(pm->piece);                    //       Determine player color
        SQUARE epSq   = pm->to - (player == white ? 0x10 : -0x10); // front(pm.to)
        PIECE  epPawn = pawn + (black - player);                   //       enemy pawn
        newkey ^= G->H.HashCode[epPawn][epSq]; //       dt->hashKey ^= HashCode[friendly(pawn)][front(pm.to)]; break;
        break;
    }

    case mtype_O_O: //    case mtype_O_O :
    {
        // Use the MOVER's castling hash. (Old code computed `opponent = black - pieceColour(...)`
        // and indexed by that, which inverted the hash for both colors — the asm indexes by
        // current-player-rPlayer, where current player is the opponent of the mover, but the
        // table layout `[B at +0, W at +4]` means rPlayer=white→B, rPlayer=black→W, i.e. the
        // mover's color, not the opponent's. The C had the ternary backwards.)
        COLOUR mover     = pieceColour(pm->piece);
        HKEY   castleKey = (mover == black) ? G->H.o_oHashCodeB : G->H.o_oHashCodeW;
        newkey ^= castleKey;
        break;
    }

    case mtype_O_O_O: //    case mtype_O_O_O :
    {
        COLOUR mover     = pieceColour(pm->piece);
        HKEY   castleKey = (mover == black) ? G->H.o_o_oHashCodeB : G->H.o_o_oHashCodeW;
        newkey ^= castleKey;
        break;
    }

    default:                            //    default :     // Prom
        if (pm->type & mtype_Promotion) //       dt->hashKey ^= H->HashCode[pm.type][pm.to];
            newkey ^= G->H.HashCode[pm->type][pm->to];
        break; // }
    }

    //--- Update Draw State ---
    // If the move is irreversible (capture, pawn move or null move), then we update state info
    // for the current node directly, and check if it's a draw by insufficient material.
    // Castling is reversible here, as in irreversible() for the game record: it doesn't reset
    // the 50-move count, and the castling-rights check keeps repetitions across it apart.

    PIECE ptype = pieceType(pm->piece);
    dt->castle  = castleRights(E->B.Board, E->B.HasMovedTo);

    if (pm->cap || ptype == pawn || (pm->type & mtype_Null))
    {
        // Irreversible move
        dt->hashKey  = newkey;
        N->hashKey   = newkey;
        dt->irr      = N->gameDepth;
        dt->repCount = 0;
    }
    else
    {
        // Reversible move
        dt->hashKey = newkey;
        N->hashKey  = newkey;
        dt->irr     = prevDt->irr;

        INT rev = N->gameDepth - dt->irr;

        if (Rule50Count(E, dt, N->gameDepth) >= 100)
        {
            N->drawType = drawType_50;
            return;
        } //    else

        INT n = rev / 2; // Down to the position the irreversible move left.

        if (n > 0)
        {
            DRAWDATA *dt1 = dt - 2;

            do
            {
                if (dt1->hashKey == newkey && dt1->castle == dt->castle)
                {
                    dt->repCount = dt1->repCount + 1;
                    N->drawType  = dt->repCount;
                    return;
                }
                dt1 -= 2;
            } while (--n > 0);
        }

        dt->repCount = 0;
    }

    // Insufficient-material check (KK, KNK, KBK). Asm gates this on irreversible
    // moves; we run it always so FEN-loaded subtrees without captures still detect.
    if (N->drawType == drawType_None)
    {
        ULONG pc = E->B.pieceCount;
        if ((pc & 0xFE0F) == 0 && (pc & 0xFE0F0000) == 0)
        {
            LONG sum = (LONG)pc + ((LONG)pc >> 16);
            if ((sum & 0x0E00) == 0)
                N->drawType = drawType_InsuffMtrl;
        }
    }
} /* UpdateDrawState */

/**************************************************************************************************/
/*                                                                                                */
/*                                         KILLER MOVES                                           */
/*                                                                                                */
/**************************************************************************************************/

// Up to 2 killer moves are maintained at each node.

/*-------------------------------------- Preparing Killers ---------------------------------------*/
// "Prepares" the killers by setting the "killerActive" flags and making sure the most popular
// killer is searched first (i.e. "killer1").

void PrepareKillers(ENGINE *E)
{
    NODE *N = E->S.currNode;

    if (N->check || N->quies)
    {
        N->killer1Active = false;
        N->killer2Active = false;
        return;
    }

    N->killer1Active = (N->killer1Count > 0);
    N->killer2Active = (N->killer2Count > 0);
} /* PrepareKillers */

/*------------------------- Checking Killer/Refutation Move Collision ----------------------------*/
// Each move must be compared against the killers and the refutation move, so we don't search a
// move more than once.

BOOL KillerRefCollision(ENGINE *E)
{
    NODE *N = E->S.currNode;
    MOVE *m = &N->m;

    // EqualMove(m, k) requires from, to, piece, cap, AND type to match. The asm reads
    // cap+type together as a 4-byte word; we check both fields explicitly here for clarity.

    // Check killer1 collision
    if (N->killer1Active && m->from == N->killer1.from && m->to == N->killer1.to && m->piece == N->killer1.piece &&
        m->cap == N->killer1.cap && m->type == N->killer1.type && N->gen != gen_F1)
    {
        N->killer1Active = false;
        if (N->gen > gen_F1)
            return true;
    }
    // Check killer2 collision
    else if (N->killer2Active && m->from == N->killer2.from && m->to == N->killer2.to && m->piece == N->killer2.piece &&
             m->cap == N->killer2.cap && m->type == N->killer2.type && N->gen != gen_F2)
    {
        N->killer2Active = false;
        if (N->gen > gen_F2)
            return true;
    }

    // Check refutation move collision
    return (m->from == N->rfm.from && m->to == N->rfm.to && m->piece == N->rfm.piece && m->cap == N->rfm.cap &&
            m->type == N->rfm.type);
} /* KillerRefCollision */

/*-------------------------------------- Update Killers ------------------------------------------*/
// Updates the killer table upon exit from a node: If a killer worked again, its popularity count is
// increased by 1. Otherwise replace "killer2" by the best move if it's a non capture or a sacrifice.

void UpdateKillers(ENGINE *E)
{
    NODE *N          = E->S.currNode;
    NODE *ParentNode = N - 1; // Previous node (parent) - avoid PN macro collision

    if (N->check || N->quies || N->bestGen < gen_F1) // if (quies || check || bestGen < killer1Gen)
        return;                                      //    return;

    switch (N->bestGen)    // switch (bestGen)
    {                      // {
    case gen_F1:           //    case killer1Gen :
        N->killer1Count++; //       killer1Count++; break;
        break;

    case gen_F2:           //    case killer2Gen :
        N->killer2Count++; //       killer2Count++; break;
        break;

    case gen_G: //    case castlingGen :
        return; //       return;

    default: //    default: // non-captures and sacrifices
        // Check if best move recaptures on same square as parent's move
        if (N->BestLine[0].to == ParentNode->m.to) //       if (BestLine[0].to == PN->m.to) return;
            return;

        // If killer1 is more popular than killer2, shift killer1 to killer2
        if (N->killer1Count > N->killer2Count) //       if (killer1Count > killer2Count)
        {                                      //       {
            N->killer2      = N->killer1;      //          killer2 = killer1;
            N->killer2Count = N->killer1Count; //          killer2Count = killer1Count;
        } //       }

        // Store new killer move in killer1
        N->killer1      = N->BestLine[0]; //       killer1 = BestLine[0];
        N->killer1Count = 1;              //       killer1Count = 1;
        break;                            // }
    }
} /* UpdateKillers */
