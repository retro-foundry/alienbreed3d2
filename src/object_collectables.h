#ifndef AB3D2_OBJECT_COLLECTABLES_H
#define AB3D2_OBJECT_COLLECTABLES_H

#include <stddef.h>
#include <stdint.h>

#include "game_inventory.h"
#include "game_link.h"
#include "level_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/*
 * Focused single-player branch of newaliencontrol.s:ItsAnObject / Collectable
 * and Plr1_CheckObjectCollide. It does not implement animation, activation,
 * locks, messages, sound, PVS traversal, or any other object class.
 */
int object_collectables_update_single_player(
    ObjectRuntime *objects, const LevelRuntime *level, const GameLink *game_link,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, uint32_t *out_collected_count,
    char *error, size_t error_size);

#endif
