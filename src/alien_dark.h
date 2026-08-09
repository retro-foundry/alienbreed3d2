#ifndef AB3D2_ALIEN_DARK_H
#define AB3D2_ALIEN_DARK_H

#include <stddef.h>
#include <stdint.h>

#include "game_random.h"
#include "object_runtime.h"

/*
 * modules/ai.s:ai_CheckForDark. `player_zone_id` and `player_room_brightness`
 * are the source Plr1_ZonePtr first word and Plr1_RoomBright_w respectively.
 * The result preserves d0: zero for dark, -1 for not dark.
 */
int alien_dark_check(const ObjectRuntime *objects, uint32_t slot_index,
                     uint16_t player_zone_id, int16_t player_room_brightness,
                     GameRandom *random, int16_t *out_result,
                     char *error, size_t error_size);

#endif
