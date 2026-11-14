#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                          FUNCTION PROTOTYPES                                   */
/*                                                                                                */
/**************************************************************************************************/

void UpdateBestLine(ENGINE *E);
void UpdateDrawState(ENGINE *E);

void PrepareKillers(ENGINE *E);
BOOL KillerRefCollision(ENGINE *E);
void UpdateKillers(ENGINE *E);
