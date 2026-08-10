#ifndef AB3D2_ALIEN_SETUP_H
#define AB3D2_ALIEN_SETUP_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "level_runtime.h"
#include "object_runtime.h"

/*
 * Per-alien source registers/global inputs established by
 * newaliencontrol.s:ItsAnAlien before its AI_MainRoutine call. This is an
 * immutable handoff; the later mode dispatch owns all mutation and effects.
 */
typedef struct {
    uint16_t alien_type;
    uint16_t zone_id;
    uint16_t object_point_index;
    uint8_t zone_echo;
    int16_t brightness;
    GameShootDefinition shoot_definition;
    int32_t shot_y_offset;
    int16_t shot_offset_multiplier;
    int32_t thing_height;
    int16_t auxiliary_object_type;
    uint8_t vector_object_flag;
    /* newaliencontrol.s:AI_ReactionTime_w from AlienT_ReactionTime_w. */
    int16_t reaction_time;
    int16_t default_mode;
    int16_t response_mode;
    int16_t retreat_mode;
    int16_t followup_mode;
    int16_t prowl_speed;
    int16_t response_speed;
    int16_t retreat_speed;
    int16_t followup_speed;
    int16_t followup_timer;
    int8_t away_from_wall;
    int16_t extended_wall_length;
} AlienSetup;

/* newaliencontrol.s:ItsAnAlien through the immediate AI_MainRoutine call. */
int alien_setup_from_slot(const ObjectRuntime *objects, uint32_t slot_index,
                          const LevelRuntime *level, const GameLink *game_link,
                          AlienSetup *out_setup, char *error, size_t error_size);

#endif
