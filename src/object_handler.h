#ifndef AB3D2_OBJECT_HANDLER_H
#define AB3D2_OBJECT_HANDLER_H

#include <stddef.h>
#include <stdint.h>

#include "game_inventory.h"
#include "game_link.h"
#include "level_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/* Single-player newanims.s:ObjectHandler dispatch for implemented object classes. */
int object_handler_update_single_player(
    ObjectRuntime *objects, const LevelRuntime *level, const GameLink *game_link,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, uint16_t frame_ticks,
    uint32_t *out_collected_count, char *error, size_t error_size);

#endif
