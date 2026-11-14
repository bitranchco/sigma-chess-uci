#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                          FUNCTION PROTOTYPES                                   */
/*                                                                                                */
/**************************************************************************************************/

void AllocateTime(ENGINE *E);
void AdjustTimeLimit(ENGINE *E);
BOOL TimeOut(ENGINE *E);
BOOL TimeForAnotherIteration(ENGINE *E);
