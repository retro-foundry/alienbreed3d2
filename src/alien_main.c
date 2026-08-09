#include "alien_main.h"

#include <stdio.h>

enum {
    /* defs.i: ObjT YPos word and EntT_CurrentMode_b. */
    ALIEN_MAIN_SLOT_Y_POSITION = 2u,
    ALIEN_MAIN_SLOT_CURRENT_MODE = 20u
};

static void alien_main_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static void alien_main_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

int alien_main_route(ObjectRuntime *objects, uint32_t slot_index,
                     AlienMainRoute *out_route, char *error, size_t error_size)
{
    uint8_t *slot;
    int8_t current_mode;

    if (!objects || !out_route || slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_main_set_error(error, error_size, "AI_MainRoutine received invalid source state");
        return 0;
    }
    /* modules/ai.s:AI_MainRoutine's `move.w #-20,2(a0)`. */
    alien_main_write_be16(slot + ALIEN_MAIN_SLOT_Y_POSITION, UINT16_C(0xffec));
    current_mode = (int8_t)slot[ALIEN_MAIN_SLOT_CURRENT_MODE];

    /* Each comparison/BLT uses the signed low byte of EntT_CurrentMode_b. */
    if (current_mode < 1) {
        *out_route = ALIEN_MAIN_ROUTE_DEFAULT;
    } else if (current_mode == 1) {
        *out_route = ALIEN_MAIN_ROUTE_RESPONSE;
    } else if (current_mode < 3) {
        *out_route = ALIEN_MAIN_ROUTE_FOLLOWUP;
    } else if (current_mode == 3) {
        *out_route = ALIEN_MAIN_ROUTE_RETREAT;
    } else if (current_mode == 5) {
        *out_route = ALIEN_MAIN_ROUTE_DIE;
    } else {
        *out_route = ALIEN_MAIN_ROUTE_TAKE_DAMAGE;
    }
    return 1;
}

int alien_main_select_behavior(AlienMainRoute route, const AlienSetup *setup,
                               AlienMainBehavior *out_behavior,
                               char *error, size_t error_size)
{
    int16_t behavior_mode;

    if (!setup || !out_behavior) {
        alien_main_set_error(error, error_size,
                             "AI mode selector received invalid source setup state");
        return 0;
    }
    if (route == ALIEN_MAIN_ROUTE_RETREAT) {
        /* modules/ai.s:ai_DoRetreat is an immediate RTS. */
        *out_behavior = ALIEN_MAIN_BEHAVIOR_NONE;
        return 1;
    }
    if (route == ALIEN_MAIN_ROUTE_DIE) {
        *out_behavior = ALIEN_MAIN_BEHAVIOR_DIE;
        return 1;
    }
    if (route == ALIEN_MAIN_ROUTE_TAKE_DAMAGE) {
        *out_behavior = ALIEN_MAIN_BEHAVIOR_TAKE_DAMAGE;
        return 1;
    }
    if (route == ALIEN_MAIN_ROUTE_DEFAULT) {
        behavior_mode = setup->default_mode;
        if (behavior_mode < 1) {
            *out_behavior = ALIEN_MAIN_BEHAVIOR_PROWL_RANDOM;
        } else if (behavior_mode == 1) {
            *out_behavior = ALIEN_MAIN_BEHAVIOR_PROWL_RANDOM_FLYING;
        } else {
            *out_behavior = ALIEN_MAIN_BEHAVIOR_NONE;
        }
        return 1;
    }
    if (route == ALIEN_MAIN_ROUTE_RESPONSE) {
        behavior_mode = setup->response_mode;
        if (behavior_mode < 1) {
            *out_behavior = ALIEN_MAIN_BEHAVIOR_CHARGE;
        } else if (behavior_mode == 1) {
            *out_behavior = ALIEN_MAIN_BEHAVIOR_CHARGE_TO_SIDE;
        } else if (behavior_mode < 3) {
            *out_behavior = ALIEN_MAIN_BEHAVIOR_ATTACK_WITH_GUN;
        } else if (behavior_mode == 3) {
            *out_behavior = ALIEN_MAIN_BEHAVIOR_CHARGE_FLYING;
        } else if (behavior_mode < 5) {
            *out_behavior = ALIEN_MAIN_BEHAVIOR_CHARGE_TO_SIDE_FLYING;
        } else if (behavior_mode == 5) {
            *out_behavior = ALIEN_MAIN_BEHAVIOR_ATTACK_WITH_GUN_FLYING;
        } else {
            *out_behavior = ALIEN_MAIN_BEHAVIOR_NONE;
        }
        return 1;
    }

    if (route != ALIEN_MAIN_ROUTE_FOLLOWUP) {
        alien_main_set_error(error, error_size,
                             "AI mode selector received an unknown main-routine route");
        return 0;
    }
    /* The only remaining main route is ai_DoFollowup. */
    behavior_mode = setup->followup_mode;
    if (behavior_mode < 1) {
        *out_behavior = ALIEN_MAIN_BEHAVIOR_PAUSE_BRIEFLY;
    } else if (behavior_mode == 1) {
        *out_behavior = ALIEN_MAIN_BEHAVIOR_APPROACH;
    } else if (behavior_mode < 3) {
        *out_behavior = ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE;
    } else if (behavior_mode == 3) {
        *out_behavior = ALIEN_MAIN_BEHAVIOR_APPROACH_FLYING;
    } else if (behavior_mode < 5) {
        *out_behavior = ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE_FLYING;
    } else {
        *out_behavior = ALIEN_MAIN_BEHAVIOR_NONE;
    }
    return 1;
}
