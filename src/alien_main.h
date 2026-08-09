#ifndef AB3D2_ALIEN_MAIN_H
#define AB3D2_ALIEN_MAIN_H

#include <stddef.h>
#include <stdint.h>

#include "object_runtime.h"

/* modules/ai.s:AI_MainRoutine's six direct branch destinations. */
typedef enum {
    ALIEN_MAIN_ROUTE_DEFAULT,
    ALIEN_MAIN_ROUTE_RESPONSE,
    ALIEN_MAIN_ROUTE_FOLLOWUP,
    ALIEN_MAIN_ROUTE_RETREAT,
    ALIEN_MAIN_ROUTE_DIE,
    ALIEN_MAIN_ROUTE_TAKE_DAMAGE
} AlienMainRoute;

/*
 * Direct AI_MainRoutine preamble and signed CurrentMode branch selection.  It
 * does not execute a route: those source modes remain separate translations.
 */
int alien_main_route(ObjectRuntime *objects, uint32_t slot_index,
                     AlienMainRoute *out_route, char *error, size_t error_size);

#endif
