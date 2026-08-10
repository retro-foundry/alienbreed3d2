#ifndef AB3D2_ALIEN_DAMAGE_H
#define AB3D2_ALIEN_DAMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "alien_runtime.h"
#include "game_math.h"
#include "game_random.h"
#include "object_animation.h"
#include "object_runtime.h"
#include "player_runtime.h"

/* modules/ai.s:ai_TakeDamage's branch into its own ai_JustDied routine. */
typedef enum {
    ALIEN_DAMAGE_ROUTE_NONFATAL,
    ALIEN_DAMAGE_ROUTE_JUST_DIED
} AlienDamageRoute;

typedef struct {
    AlienDamageRoute route;
    /* ai_TakeDamage asserts ai_GetOut_w for either completed nonfatal path. */
    uint8_t got_out;
} AlienDamageState;

/*
 * modules/ai.s:ai_TakeDamage through the ai_JustDied branch destination.
 * ai_JustDied itself remains separately owned because it includes narrative,
 * progression, spawning, and explosion side effects.
 */
int alien_damage_take(ObjectRuntime *objects, uint32_t slot_index,
                      AlienRuntime *alien_runtime,
                      ObjectAnimationRuntime *animation_runtime,
                      const GameMath *math, GameRandom *random,
                      const PlayerRuntime *player, AlienDamageState *out_state,
                      char *error, size_t error_size);

#endif
