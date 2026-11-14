#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                      SIGMA CHESS ENGINE API                                    */
/*                                                                                                */
/**************************************************************************************************/

/*----------------------------------- Engine System Initialization -------------------------------*/

void Engine_InitSystem(GLOBAL *Global, PTR kpkData);

/*---------------------------------- Create/Destroy Engine Instance ------------------------------*/

BOOL Engine_Create(GLOBAL *Global, ENGINE *E);
void Engine_Destroy(ENGINE *E);

/*------------------------------------ Start/Stop Engine Instance --------------------------------*/

void Engine_Start(ENGINE *E);
void Engine_Stop(ENGINE *E);
void Engine_Abort(ENGINE *E);
void Engine_Periodic(ENGINE *E);

/*------------------------------- Access Engine Instance Stats/Results ---------------------------*/

#define Engine_BestMove(E) E->S.MainLine[0]

/**************************************************************************************************/
/*                                                                                                */
/*                                    INTERNAL FUNCTION PROTOTYPES                                */
/*                                                                                                */
/**************************************************************************************************/

// These routines should NOT be called by the host application
