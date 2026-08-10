#ifndef AB3D2_OBJECT_HANDLER_H
#define AB3D2_OBJECT_HANDLER_H

#include <stddef.h>
#include <stdint.h>

#include "alien_dispatch.h"
#include "alien_runtime.h"
#include "game_math.h"
#include "game_preferences.h"
#include "game_progression.h"
#include "game_random.h"
#include "game_inventory.h"
#include "game_link.h"
#include "level_dynamic_state.h"
#include "level_navigation.h"
#include "lighting_runtime.h"
#include "mechanism_runtime.h"
#include "message_runtime.h"
#include "object_animation.h"
#include "object_explosion.h"
#include "object_observation.h"
#include "object_runtime.h"
#include "player_runtime.h"

/* Explicit source tick inputs established before newanims.s:ObjectHandler enters ItsAnAlien. */
typedef struct {
    ObjectAnimationRuntime *animation_runtime;
    LightingRuntime *lighting_runtime;
    const LevelNavigation *navigation;
    const AssetBlob *clips;
    GameProgression *progression;
    ObjectExplosionRuntime *explosion_runtime;
    const GameMath *math;
    GameRandom *random;
    const ObjectObservation *observation;
    AlienDispatchWorkspace *dispatch_workspace;
    MessageRuntime *messages;
    const GamePreferences *preferences;
    /* c/message.c Sys_FrameTimeECV_q[0], represented as native monotonic milliseconds. */
    uint64_t message_time_milliseconds;
} ObjectHandlerAlienContext;

/* Single-player newanims.s:ObjectHandler dispatch, including ItsAnAlien in source slot order. */
int object_handler_update_single_player(
    ObjectRuntime *objects, LevelDynamicState *dynamic_level,
    MechanismRuntime *mechanism_runtime, AlienRuntime *alien_runtime,
    const GameLink *game_link, const ObjectHandlerAlienContext *alien_context,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, uint16_t frame_ticks,
    uint32_t *out_collected_count, char *error, size_t error_size);

#endif
