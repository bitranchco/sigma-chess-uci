#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                          FUNCTION PROTOTYPES                                   */
/*                                                                                                */
/**************************************************************************************************/

void CalcEvaluateState(ENGINE *E);

INT  EvalMove(ENGINE *E);
void Evaluate(ENGINE *E);

void InitEvaluateModule(GLOBAL *Global, PTR kpkData);
