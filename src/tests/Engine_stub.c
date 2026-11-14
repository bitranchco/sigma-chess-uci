/**************************************************************************************************/
/*                                                                                                */
/* Module  : ENGINE_STUB.C                                                                        */
/* Purpose : Stub implementations of Engine.c functions for unit tests                            */
/*                                                                                                */
/**************************************************************************************************/

/*
 * Engine.c contains the main engine control loop and GUI interaction code.
 * Unit tests don't need this functionality - they test components in isolation.
 * This stub provides minimal implementations to satisfy linker dependencies.
 */

#include "Engine.h"

// Stub for periodic engine callback - no-op for unit tests
void Engine_Periodic(ENGINE *E)
{
    (void)E;
    // No-op - unit tests don't need periodic callbacks
}
