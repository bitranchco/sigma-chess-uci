#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                        FUNCTION PROTOTYPES                                     */
/*                                                                                                */
/**************************************************************************************************/

HKEY CalcHashKey(GLOBAL *Global, PIECE Board[]);
HKEY HashKeyChange(GLOBAL *Global, MOVE *m);

void InitHashCodeModule(GLOBAL *Global);
