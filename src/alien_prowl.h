#ifndef AB3D2_ALIEN_PROWL_H
#define AB3D2_ALIEN_PROWL_H

#include <stddef.h>
#include <stdint.h>

#include "alien_runtime.h"
#include "game_random.h"
#include "level_navigation.h"
#include "level_runtime.h"
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

#endif
