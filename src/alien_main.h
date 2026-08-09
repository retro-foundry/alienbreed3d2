#ifndef AB3D2_ALIEN_MAIN_H
#define AB3D2_ALIEN_MAIN_H

#include <stddef.h>
#include <stdint.h>

#include "alien_setup.h"
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

/* modules/ai.s:ai_DoDefault, ai_DoResponse, and ai_DoFollowup destinations. */
typedef enum {
    ALIEN_MAIN_BEHAVIOR_NONE,
    ALIEN_MAIN_BEHAVIOR_PROWL_RANDOM,
    ALIEN_MAIN_BEHAVIOR_PROWL_RANDOM_FLYING,
    ALIEN_MAIN_BEHAVIOR_CHARGE,
    ALIEN_MAIN_BEHAVIOR_CHARGE_TO_SIDE,
    ALIEN_MAIN_BEHAVIOR_ATTACK_WITH_GUN,
    ALIEN_MAIN_BEHAVIOR_CHARGE_FLYING,
    ALIEN_MAIN_BEHAVIOR_CHARGE_TO_SIDE_FLYING,
    ALIEN_MAIN_BEHAVIOR_ATTACK_WITH_GUN_FLYING,
    ALIEN_MAIN_BEHAVIOR_PAUSE_BRIEFLY,
    ALIEN_MAIN_BEHAVIOR_APPROACH,
    ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE,
    ALIEN_MAIN_BEHAVIOR_APPROACH_FLYING,
    ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE_FLYING,
    ALIEN_MAIN_BEHAVIOR_DIE,
    ALIEN_MAIN_BEHAVIOR_TAKE_DAMAGE
} AlienMainBehavior;

/*
 * Direct AI_MainRoutine preamble and signed CurrentMode branch selection.  It
 * does not execute a route: those source modes remain separate translations.
 */
int alien_main_route(ObjectRuntime *objects, uint32_t slot_index,
                     AlienMainRoute *out_route, char *error, size_t error_size);

/*
 * Routes an AI_MainRoutine branch through the AlienT mode-word comparisons.
 * The selected behavior remains uncalled until its complete body is ported.
 */
int alien_main_select_behavior(AlienMainRoute route, const AlienSetup *setup,
                               AlienMainBehavior *out_behavior,
                               char *error, size_t error_size);

#endif
