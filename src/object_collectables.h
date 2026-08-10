#ifndef AB3D2_OBJECT_COLLECTABLES_H
#define AB3D2_OBJECT_COLLECTABLES_H

#include <stddef.h>
#include <stdint.h>

#include "game_inventory.h"
#include "game_link.h"
#include "level_runtime.h"
#include "message_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/*
 * Focused single-player branch of newaliencontrol.s:ItsAnObject / Collectable
 * and Plr1_CheckObjectCollide, including the successful and failed collection
 * message calls. The caller supplies the source EClock-equivalent monotonic
 * time in milliseconds for Msg_PushLineDedupLast.
 */
int object_collectables_update_single_player(
    ObjectRuntime *objects, const LevelRuntime *level, const GameLink *game_link,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, MessageRuntime *messages,
    uint8_t messages_enabled, uint64_t message_time_milliseconds,
    uint32_t *out_collected_count,
    char *error, size_t error_size);

/* One source ObjT iteration for ObjectHandler's exact list order. */
int object_collectables_update_slot_single_player(
    ObjectRuntime *objects, uint32_t slot_index, const LevelRuntime *level,
    const GameLink *game_link, const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, MessageRuntime *messages,
    uint8_t messages_enabled, uint64_t message_time_milliseconds,
    uint32_t *out_collected_count,
    char *error, size_t error_size);

#endif
