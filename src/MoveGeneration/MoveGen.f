#pragma once

#include "Engine.h"

/**************************************************************************************************/
/*                                                                                                */
/*                                          FUNCTION PROTOTYPES                                   */
/*                                                                                                */
/**************************************************************************************************/

void GenRootMoves(ENGINE *E);

void SearchEnPriseCaptures(ENGINE *E);
void SearchPromotions(ENGINE *E);
void SearchRecaptures(ENGINE *E);
void SearchSafeCaptures(ENGINE *E);
void SearchEscapes(ENGINE *E);
void SearchKillers(ENGINE *E);
void SearchCastling(ENGINE *E);
void SearchNonCaptures(ENGINE *E);
void SearchSacrifices(ENGINE *E);
void SearchSafeChecks(ENGINE *E);
void SearchFarPawns(ENGINE *E);
void SearchCheckEvasion(ENGINE *E);

void InitMoveGenModule(GLOBAL *Global);
