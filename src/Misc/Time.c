/**************************************************************************************************/
/*                                                                                                */
/* Module  : TIME.C                                                                               */
/* Purpose : This module controls the time allocation, including the logic for moving quicker for */
/*           forced moves etc. Is only applied in the "mode_Time" playing mode.                   */
/*                                                                                                */
/**************************************************************************************************/

#include "Engine.h"

#include "Time.f"

/**************************************************************************************************/
/*                                                                                                */
/*                                         TIME ALLOCATION/CONTROL                                */
/*                                                                                                */
/**************************************************************************************************/

void AllocateTime(ENGINE *E) // Allocates time for the forthcoming search.
{
    TIME_STATE *T = &E->T;

    // Fixed-time-per-move override (UCI "go movetime"): bypass clock math and
    // give the search exactly the requested budget for every timer slot.
    if (E->P.fixedTimeMs > 0)
    {
        ULONG deadline = (ULONG)Now() + (ULONG)E->P.fixedTimeMs;
        for (INT i = timer_Recap; i <= timer_Sacri; i++)
        {
            T->NormalTime[i]    = deadline;
            T->IterationTime[i] = deadline;
        }
        T->timer       = timer_Normal;
        T->maxTime     = deadline;
        T->ultraTime   = deadline;
        T->nominalTime = deadline;
        return;
    }

    INT  movesLeft = E->P.movesLeft;
    INT  moveNo    = E->P.lastMoveNo / 2; // Moves played in this game.
    LONG msLeft    = E->P.timeLeft;       // Remaining clock (ms).
    LONG msInc     = E->P.timeInc;        // Fischer increment per move (ms), 0 if absent.

    if (movesLeft == allMoves) // Sudden death: estimate a moves-to-play horizon.
        movesLeft = Max(60 - moveNo, 25);

    // Flag buffer: 10% of the clock, at most 1s (classic Sigma kept a flat 2s).
    LONG bank = MaxL(0, msLeft - MinL(1000, msLeft / 10));

    // Classic 35/10 → 15/10 front-loaded share of the bank, plus 3/4 of the
    // increment so the clock settles at a few increments instead of draining.
    LONG mul   = MaxL(15, 35 - moveNo);
    LONG nomMs = mul * (bank / movesLeft) / 10 + 3 * msInc / 4;

    // Stretch-tier ceiling: at most half the bank, and ~16% of the clock plus
    // the increment, so emergencies never eat the clock.
    LONG maxMs = MinL(bank / 2, msLeft / 6 + msInc);
    if (maxMs < 10)
        maxMs = 10;

    // Forced/recap timers originally added a flat +500ms bonus, which blows
    // the entire bank on one move at subsecond TCs. Scale to nomMs instead.
    LONG forcedBonus = MinL(500, nomMs);
    LONG Tm[4];
    Tm[timer_Recap]  = nomMs / 4 + forcedBonus;
    Tm[timer_Forced] = nomMs / 2 + forcedBonus;
    Tm[timer_Normal] = nomMs;
    Tm[timer_Sacri]  = (3 * nomMs) / 2;

    LONG now = Now();
    for (INT i = timer_Recap; i <= timer_Sacri; i++)
    {
        T->NormalTime[i]    = (ULONG)(now + MinL(maxMs, Tm[i]));
        T->IterationTime[i] = (ULONG)(now + MinL(maxMs, (5 * Tm[i]) / 6));
    }

    T->timer       = timer_Normal;
    T->maxTime     = (ULONG)(now + MinL(maxMs, 3 * nomMs)); // score drops 50+ cp
    T->ultraTime   = (ULONG)(now + MinL(maxMs, 6 * nomMs)); // score drops 300+ cp
    T->nominalTime = T->NormalTime[timer_Normal];
} /* AllocateTime */

void AdjustTimeLimit(ENGINE *E) // Adjusts the time limit for the search by
{                               // choosing one of the four timers
    NODE *N = E->S.rootNode;

    if (N->m.dply == 0)
        E->T.timer = (N->m.misc == gen_C ? timer_Recap : timer_Forced);
    else
        E->T.timer = (N->m.misc != gen_I ? timer_Normal : timer_Sacri);

    E->T.nominalTime = E->T.NormalTime[E->T.timer];
} /* AdjustTimeLimit */

/**************************************************************************************************/
/*                                                                                                */
/*                                             TIME CONTROL                                       */
/*                                                                                                */
/**************************************************************************************************/

BOOL TimeOut(ENGINE *E)
{
    LONG timer = Now();
    if (timer < E->T.nominalTime || E->P.playingMode != mode_Time || E->P.backgrounding ||
        (E->R.state == state_Running && E->S.rootNode->ply == 1 && !E->P.reduceStrength))
        return false;

    if (timer < E->T.maxTime)
        return (E->S.mainScore > E->S.prevScore - 50 ||
                (E->S.mainScore > -200 && E->P.reduceStrength && E->P.engineELO < 1500));

    return (E->S.mainScore > E->S.prevScore - 300 || timer >= E->T.ultraTime ||
            (E->P.reduceStrength && E->P.engineELO < 1500));
} /* TimeOut */

BOOL TimeForAnotherIteration(ENGINE *E)
{
    return (E->P.backgrounding || Now() <= (LONG)E->T.IterationTime[E->T.timer]);
} /* TimeForAnotherIteration */
