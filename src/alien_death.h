#ifndef AB3D2_ALIEN_DEATH_H
#define AB3D2_ALIEN_DEATH_H

#include <stddef.h>
#include <stdint.h>

#include "alien_animation.h"
#include "alien_setup.h"
#include "level_runtime.h"

/* modules/ai.s:ai_DoDie's ai_GetOut_w and animation handoff. */
typedef struct {
    AlienAnimationState animation;
    uint8_t got_out;
} AlienDeathState;

/*
 * modules/ai.s:ai_DoDie.  The later AI dispatcher owns selecting this mode;
 * this helper only applies that source branch once it has been selected.
 */
int alien_death_update(ObjectRuntime *objects, uint32_t slot_index,
                       ObjectAnimationRuntime *animation_runtime,
                       const GameLink *game_link, const GameMath *math,
                       const LevelRuntime *level, const AlienSetup *setup,
                       uint16_t viewer_yaw, AlienDeathState *out_state,
                       char *error, size_t error_size);

#endif
