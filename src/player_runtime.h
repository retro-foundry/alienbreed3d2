#ifndef AB3D2_PLAYER_RUNTIME_H
#define AB3D2_PLAYER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "game_input.h"
#include "game_math.h"
#include "game_preferences.h"
#include "level_dynamic_state.h"
#include "level_runtime.h"

/* Single-player subset of modules/player.s:Plr_Initialise. */
typedef struct {
    /* hires.s:Plr1_Control committed position. */
    int32_t x;
    int32_t y;
    int32_t z;
    /* modules/player.s input-side state, committed by hires.s:Plr1_Control. */
    int32_t snap_x;
    int32_t snap_y;
    int32_t snap_z;
    int32_t snap_target_y;
    int32_t height;
    int32_t snap_height;
    int32_t snap_target_height;
    int32_t snap_squished_height;
    int32_t snap_x_speed;
    int32_t snap_y_velocity;
    int32_t snap_z_speed;
    /* hires.s game_main_loop snapshots these before Plr1_Control. */
    int32_t tmp_x;
    int32_t tmp_y;
    int32_t tmp_z;
    int32_t tmp_height;
    int32_t aim_speed;
    uint16_t zone_index;
    uint16_t yaw;
    uint16_t snap_yaw;
    int16_t snap_yaw_speed;
    int16_t look_offset;
    uint16_t bobble;
    int16_t add_to_bobble;
    uint16_t health;
    uint32_t default_enemy_flags;
    uint8_t ducked;
    uint8_t squished;
    uint8_t stood_in_top;
    uint8_t used;
    /* hires.s snapshots Used_b to Plr1_TmpSpcTap then clears Used_b each tick. */
    uint8_t tmp_used;
    uint8_t fire;
    uint8_t clicked;
    uint8_t previous_use_key_state;
    uint8_t previous_centre_view_key_state;
    uint8_t decelerate;
} PlayerRuntime;

int player_runtime_init_single_player(const LevelBootstrap *level,
                                      const LevelRuntime *runtime,
                                      PlayerRuntime *out_player,
                                      char *error, size_t error_size);

/*
 * The non-spatial operate/crouch/fire branches of
 * modules/player.s:plr_KeyboardControl. Horizontal angle/motion remains out
 * of this routine until the source collision/update sequence is ported; this
 * function never changes committed x, y, z, or yaw.
 */
int player_runtime_update_discrete_controls(PlayerRuntime *player, GameInput *input,
                                            const GameControls *controls,
                                            const LevelRuntime *runtime,
                                            char *error, size_t error_size);

/*
 * Single-player spatial sequence from modules/player.s, plr1control.s, and
 * hires.s:Plr1_Control.  It retains the source snap-state order, falling,
 * fixed-point keyboard motion, and static EdgeT/zone collision.  Dynamic
 * Obj_DoCollision remains owned by the later object-runtime slice.
 */
int player_runtime_update_spatial(PlayerRuntime *player, const GameInput *input,
                                  const GameControls *controls,
                                  const GamePreferences *preferences,
                                  const GameMath *math,
                                  const LevelRuntime *runtime,
                                  LevelDynamicState *dynamic_state,
                                  char *error, size_t error_size);

#endif
