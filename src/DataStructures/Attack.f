#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                          FUNCTION PROTOTYPES                                   */
/*                                                                                                */
/**************************************************************************************************/

void InitAttackModule(GLOBAL *Global);

void CalcAttackState(ENGINE *E);
INT  UpdPieceAttack(ENGINE *E, SQUARE sq); // Returns the mobility change
INT  UpdBlockAttack(ENGINE *E, SQUARE sq); // Returns the mobility change (still has assembly)