#ifndef AB3D2_ALIEN_SPATIAL_H
#define AB3D2_ALIEN_SPATIAL_H

#include <stddef.h>
#include <stdint.h>

#include "level_runtime.h"
#include "object_runtime.h"

/*
 * modules/ai.s:ai_GetRoomStats. `new_x` and `new_z` are the owning movement
 * path's source words. This routine updates only the high words of the
 * source Vec2L point before applying ai_GetRoomStatsStill.
 */
int alien_spatial_store_room_stats(ObjectRuntime *objects, uint32_t slot_index,
                                   const LevelRuntime *level, uint16_t zone_index,
                                   int16_t new_x, int16_t new_z, int32_t thing_height,
                                   char *error, size_t error_size);

/* modules/ai.s:ai_GetRoomStatsStill, for callers that did not move x/z. */
int alien_spatial_store_room_stats_still(ObjectRuntime *objects, uint32_t slot_index,
                                         const LevelRuntime *level, uint16_t zone_index,
                                         int32_t thing_height,
                                         char *error, size_t error_size);

/* modules/ai.s:ai_GetRoomCPT updates EntT_CurrentControlPoint_w. */
int alien_spatial_store_current_control_point(ObjectRuntime *objects, uint32_t slot_index,
                                              const LevelRuntime *level,
                                              uint16_t zone_index,
                                              char *error, size_t error_size);

#endif
