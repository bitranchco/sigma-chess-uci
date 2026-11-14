#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                          FUNCTION PROTOTYPES                                   */
/*                                                                                                */
/**************************************************************************************************/

void MainSearch(register ENGINE *E);

void SearchNode(ENGINE *E);
void SearchMove(ENGINE *E);

void InitSearchState(ENGINE *E);
void InitSearchModule(GLOBAL *Global);
