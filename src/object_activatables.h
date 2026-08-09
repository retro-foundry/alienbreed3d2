#ifndef AB3D2_OBJECT_ACTIVATABLES_H
#define AB3D2_OBJECT_ACTIVATABLES_H

#include <stddef.h>
#include <stdint.h>

#include "game_inventory.h"
#include "game_link.h"
#include "level_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/* Bounded single-player newaliencontrol.s:Activatable/ObjectHandler slice. */
int object_activatables_update_single_player(
    ObjectRuntime *objects, const LevelRuntime *level, const GameLink *game_link,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, uint16_t frame_ticks,
    char *error, size_t error_size);

/* One source ObjT iteration for ObjectHandler's exact list order. */
int object_activatables_update_slot_single_player(
    ObjectRuntime *objects, uint32_t slot_index, const LevelRuntime *level,
    const GameLink *game_link, const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, uint16_t frame_ticks,
    char *error, size_t error_size);

#endif
