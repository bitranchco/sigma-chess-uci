#pragma once

/**************************************************************************************************/
/*                                                                                                */
/*                                       CONSTANTS & MACROS                                       */
/*                                                                                                */
/**************************************************************************************************/

enum TIMER
{
    timer_Recap,
    timer_Forced,
    timer_Normal,
    timer_Sacri
};

/**************************************************************************************************/
/*                                                                                                */
/*                                          TYPE DEFINITIONS                                      */
/*                                                                                                */
/**************************************************************************************************/

typedef struct
{
    INT timer;            // Timer selector. Is set according to the type of
                          // the best move found so far during the search:
                          // forced recaptures, other forced moves, normal
                          // moves and sacrifices.
    ULONG NormalTime[4],  // Time deadlines (process-ms, see Now()) for the 4 move types.
        nominalTime,      // Equals "NormalTime[timer]".
        IterationTime[4], // Limits for another iteration for each move type.
        maxTime,          // Emergency brake if the best move is losing.
        ultraTime;        // "Ultra" emergency brake. Stops the search no
                          // matter what.
} TIME_STATE;
