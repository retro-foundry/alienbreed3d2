#ifndef AB3D2_PLAYER_RUNTIME_H
#define AB3D2_PLAYER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "game_input.h"
#include "level_runtime.h"

/* Single-player subset of modules/player.s:Plr_Initialise. */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t height;
    int32_t snap_height;
    int32_t snap_target_height;
    int32_t snap_squished_height;
    uint16_t zone_index;
    uint16_t yaw;
    uint32_t default_enemy_flags;
    uint8_t ducked;
    uint8_t squished;
    uint8_t stood_in_top;
    uint8_t used;
    uint8_t fire;
    uint8_t clicked;
    uint8_t previous_use_key_state;
} PlayerRuntime;

int player_runtime_init_single_player(const LevelBootstrap *level,
                                      const LevelRuntime *runtime,
                                      PlayerRuntime *out_player,
                                      char *error, size_t error_size);

/*
 * The non-spatial operate/crouch/fire branches of
 * modules/player.s:plr_KeyboardControl. Horizontal angle/motion remains out
 * of this routine until the authoritative bigsine table and collision/update
 * order are available; this function never changes x, y, z, or yaw.
 */
int player_runtime_update_discrete_controls(PlayerRuntime *player, GameInput *input,
                                            const GameControls *controls,
                                            const LevelRuntime *runtime,
                                            char *error, size_t error_size);

#endif
