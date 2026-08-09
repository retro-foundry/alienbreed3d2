#ifndef AB3D2_ALIEN_ANIMATION_H
#define AB3D2_ALIEN_ANIMATION_H

#include <stddef.h>
#include <stdint.h>

#include "alien_setup.h"
#include "game_math.h"
#include "object_animation.h"
#include "object_runtime.h"

/* modules/ai.s transient globals written by ai_DoWalkAnim/ai_DoAttackAnim. */
typedef struct {
    uint8_t action;
    uint8_t finished;
    uint16_t facing;
} AlienAnimationState;

/*
 * modules/ai.s:ai_DoWalkAnim and ai_DoAttackAnim.  The caller supplies the
 * immediate newaliencontrol.s:ItsAnAlien setup and consumes the returned
 * transient action, completion, and vector-facing values in its owning mode.
 */
int alien_animation_update_walk_or_attack(
    ObjectRuntime *objects, uint32_t slot_index,
    ObjectAnimationRuntime *animation_runtime, const GameLink *game_link,
    const GameMath *math, const AlienSetup *setup, uint16_t viewer_yaw,
    AlienAnimationState *out_state, char *error, size_t error_size);

#endif
