#ifndef AB3D2_ALIEN_DAMAGE_H
#define AB3D2_ALIEN_DAMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "alien_animation.h"
#include "alien_runtime.h"
#include "alien_setup.h"
#include "game_math.h"
#include "game_random.h"
#include "lighting_runtime.h"
#include "object_animation.h"
#include "object_heading.h"
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

/* modules/ai.s:ai_DoTakeDamage's complete nonfatal damage-animation branch. */
typedef struct {
    AlienAnimationState animation;
    /* objectmove.s:newx/newz after ai_DoTakeDamage's HeadTowardsAng call. */
    ObjectHeading heading;
    uint8_t got_out;
} AlienDamageReactionState;

/*
 * modules/ai.s:ai_DoTakeDamage. `torch_new_x` and `torch_new_z` are the
 * caller-owned source globals consumed by its ai_DoTorch call; this reaction
 * mode does not create a substitute position for them.
 */
int alien_damage_update_reaction(
    ObjectRuntime *objects, uint32_t slot_index,
    ObjectAnimationRuntime *animation_runtime, const GameLink *game_link,
    const GameMath *math, const LevelRuntime *level, LightingRuntime *lighting,
    const PlayerRuntime *player, const AlienSetup *setup,
    int16_t torch_new_x, int16_t torch_new_z,
    AlienDamageReactionState *out_state, char *error, size_t error_size);

#endif
