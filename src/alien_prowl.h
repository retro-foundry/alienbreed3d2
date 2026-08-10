#ifndef AB3D2_ALIEN_PROWL_H
#define AB3D2_ALIEN_PROWL_H

#include <stddef.h>
#include <stdint.h>

#include "alien_animation.h"
#include "alien_damage.h"
#include "alien_dark.h"
#include "alien_decision.h"
#include "alien_death.h"
#include "alien_flight.h"
#include "alien_memory.h"
#include "alien_perception.h"
#include "alien_runtime.h"
#include "alien_spatial.h"
#include "alien_torch.h"
#include "game_random.h"
#include "game_progression.h"
#include "level_dynamic_state.h"
#include "level_navigation.h"
#include "level_runtime.h"
#include "object_collision.h"
#include "object_explosion.h"
#include "object_heading.h"
#include "object_movement.h"
#include "object_runtime.h"
#include "player_runtime.h"

/*
 * Exact eight source words addressable through a2 after modules/ai.s:ai_Widget.
 * `objectmove.s:Obj_DoCollision` consumes word offsets 1/2 for type zero and
 * 5/6 for type one; the base is object points without a team or the selected
 * AI_AlienTeamWorkspace entry with a team.
 */
typedef struct {
    int16_t words[ALIEN_RUNTIME_WORKSPACE_WORD_COUNT];
    uint16_t middle_control_point;
    uint8_t only_see;
} AlienProwlWidgetState;

/*
 * modules/ai.s:ai_Widget. `player_noise_volume` is the source
 * Plr1_NoiseVol_w value after newanims.s has reset it and Plr1_Shot has
 * applied any source firing update. `flying` is the source AI_FlyABit_w.
 */
int alien_prowl_widget(AlienRuntime *alien_runtime, ObjectRuntime *objects,
                       uint32_t slot_index, const LevelRuntime *level,
                       const LevelNavigation *navigation,
                       const PlayerRuntime *player, int16_t player_noise_volume,
                       uint8_t flying, GameRandom *random,
                       AlienProwlWidgetState *out_state,
                       char *error, size_t error_size);

/* Source outputs consumed by modules/ai.s:ai_ProwlFly's immediate caller. */
typedef struct {
    AlienAnimationState animation;
    AlienDamageState damage;
    AlienJustDiedState death;
    AlienProwlWidgetState widget;
    ObjectHeading heading;
    ObjectMovementTrace movement;
    uint8_t damage_taken;
    uint8_t got_out;
    uint8_t hit_object;
} AlienProwlState;

/*
 * modules/ai.s:ai_ProwlRandom/ai_ProwlRandomFlying and their shared
 * ai_ProwlFly body. `flying` is their AI_FlyABit_w value. `frame_ticks` is
 * the source Anim_TempFrames_w captured before gameplay updates.
 */
int alien_prowl_random_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const LevelNavigation *navigation,
    const AssetBlob *clips, const GameLink *game_link, GameProgression *progression,
    ObjectExplosionRuntime *explosion_runtime, const GameMath *math,
    GameRandom *random, const PlayerRuntime *player, const AlienSetup *setup,
    uint8_t flying, uint16_t frame_ticks, AlienProwlState *out_state,
    char *error, size_t error_size);

#endif
