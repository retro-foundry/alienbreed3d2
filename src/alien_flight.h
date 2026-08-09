#ifndef AB3D2_ALIEN_FLIGHT_H
#define AB3D2_ALIEN_FLIGHT_H

#include <stddef.h>
#include <stdint.h>

#include "level_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/* modules/ai.s:ai_CheckFloorCeiling for a caller-owned current zone. */
int alien_flight_check_floor_ceiling(ObjectRuntime *objects, uint32_t slot_index,
                                     const LevelRuntime *level, uint16_t zone_index,
                                     int32_t thing_height,
                                     char *error, size_t error_size);

/* modules/ai.s:ai_FlyToHeightCommon plus ai_CheckFloorCeiling. */
int alien_flight_move_toward_height(ObjectRuntime *objects, uint32_t slot_index,
                                    const LevelRuntime *level, uint16_t zone_index,
                                    int16_t target_height, int32_t thing_height,
                                    char *error, size_t error_size);

/* modules/ai.s:ai_FlyToPlayerHeight. */
int alien_flight_move_toward_player_height(ObjectRuntime *objects, uint32_t slot_index,
                                           const LevelRuntime *level, uint16_t zone_index,
                                           const PlayerRuntime *player,
                                           int32_t thing_height,
                                           char *error, size_t error_size);

/* modules/ai.s:ai_FlyToCPTHeight. */
int alien_flight_move_toward_control_point_height(
    ObjectRuntime *objects, uint32_t slot_index, const LevelRuntime *level,
    uint16_t zone_index, uint16_t control_point_index, int32_t thing_height,
    char *error, size_t error_size);

#endif
