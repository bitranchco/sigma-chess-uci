/**************************************************************************************************/
/*                                                                                                */
/* Module  : MOVE.C                                                                               */
/* Purpose : This module defines the MOVE record and some basic operations.                       */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"

#include "Move.f"

void Move_Perform(PIECE Board[], MOVE *m)
{
    Board[m->from] = empty;
    Board[m->to]   = m->piece;

    switch (m->type)
    {
    case mtype_Normal:
        break;
    case mtype_O_O:
        Board[right(m->to)] = empty;
        Board[left(m->to)]  = pieceColour(m->piece) + rook;
        break;
    case mtype_O_O_O:
        Board[left2(m->to)] = empty;
        Board[right(m->to)] = pieceColour(m->piece) + rook;
        break;
    case mtype_EP:
        Board[m->to + 2 * pieceColour(m->piece) - 0x10] = empty;
        break;
    default:
        Board[m->to] = m->type;
    }
} /* Move_Perform */

void Move_Retract(PIECE Board[], MOVE *m)
{
    switch (m->type) // Retract move on the board.
    {
    case mtype_O_O:
        Board[left(m->to)]  = empty;
        Board[right(m->to)] = pieceColour(m->piece) + rook;
        break;
    case mtype_O_O_O:
        Board[right(m->to)] = empty;
        Board[left2(m->to)] = pieceColour(m->piece) + rook;
        break;
    case mtype_EP:
        Board[m->to + 2 * pieceColour(m->piece) - 0x10] = pawn + black - pieceColour(m->piece);
        break;
    }

    Board[m->from] = m->piece;
    Board[m->to]   = m->cap;
} /* Move_Retract */

BOOL EqualMove(MOVE *m1, MOVE *m2)
{
    return (m1->from == m2->from && m1->to == m2->to && m1->piece == m2->piece && m1->cap == m2->cap &&
            m1->type == m2->type);
} /* EqualMove */
