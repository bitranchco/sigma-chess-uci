#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                          FUNCTION PROTOTYPES                                   */
/*                                                                                                */
/**************************************************************************************************/

void CalcTransState(ENGINE *E);
void ResetTransTab(ENGINE *E);

BOOL ProbeTransTab(ENGINE *E);
BOOL GetTransMove(ENGINE *E);
void StoreTransTab(ENGINE *E);
