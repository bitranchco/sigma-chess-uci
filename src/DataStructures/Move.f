#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                          FUNCTION PROTOTYPES                                   */
/*                                                                                                */
/**************************************************************************************************/

void Move_Perform(PIECE Board[], MOVE *m);
void Move_Retract(PIECE Board[], MOVE *m);

BOOL EqualMove(MOVE *m1, MOVE *m2);
