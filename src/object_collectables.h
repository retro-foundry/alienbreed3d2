#ifndef AB3D2_OBJECT_COLLECTABLES_H
#define AB3D2_OBJECT_COLLECTABLES_H

#include <stddef.h>
#include <stdint.h>

#include "game_inventory.h"
#include "game_audio.h"
#include "game_link.h"
#include "level_runtime.h"
#include "message_runtime.h"
#include "object_observation.h"
#include "object_runtime.h"
#include "player_runtime.h"

/* Exact single-player newaliencontrol.s:Plr1_CollectItem state transition. */
int object_collectables_collect_item_single_player(
    const LevelRuntime *level, const GameLink *game_link,
    const GameObjectDefinition *definition, uint8_t *slot,
    const uint8_t *point_bytes, uint16_t point_index,
    const ObjectObservation *observation,
    GameInventory *inventory, const GameInventoryConsumableLimits *limits,
    MessageRuntime *messages, uint8_t messages_enabled,
    uint64_t message_time_milliseconds, GameAudioEvents *audio_events,
    uint8_t *out_collected, char *error, size_t error_size);

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

/* Same ObjectHandler-sized iteration with Plr1_CollectItem's ODefT_SFX_w call. */
int object_collectables_update_slot_single_player_with_audio(
    ObjectRuntime *objects, uint32_t slot_index, const LevelRuntime *level,
    const GameLink *game_link, const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, MessageRuntime *messages,
    uint8_t messages_enabled, uint64_t message_time_milliseconds,
    const ObjectObservation *observation, GameAudioEvents *audio_events,
    uint32_t *out_collected_count,
    char *error, size_t error_size);

#endif
