#ifndef AB3D2_PLAYER_RUNTIME_H
#define AB3D2_PLAYER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "game_input.h"
#include "game_inventory.h"
#include "game_math.h"
#include "game_preferences.h"
#include "level_dynamic_state.h"
#include "level_runtime.h"
#include "object_motion.h"

/* Single-player subset of modules/player.s:Plr_Initialise. */
typedef struct {
    /*
     * hires.s:Plr1_Control committed position. X/Z are 16.16 source
     * coordinates: 68000 move.w reads/writes the first (integer) word of the
     * big-endian longword. Y retains its source 8.8 domain.
     */
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
    /* modules/player.s:plr_Fall's single-player plr_FallDamage_w accumulator. */
    int16_t fall_damage;
    /* hires.s:plr1_BobbleY_l, calculated by Plr1_Control. */
    int32_t bobble_y;
    /* hires.s game_main_loop snapshots these before Plr1_Control. */
    int32_t tmp_x;
    int32_t tmp_y;
    int32_t tmp_z;
    int32_t tmp_height;
    uint16_t tmp_yaw;
    int32_t aim_speed;
    uint16_t zone_index;
    uint16_t yaw;
    uint16_t snap_yaw;
    int16_t snap_yaw_speed;
    int16_t look_offset;
    /* hires.s:Plr1_RoomBright_w, derived from CurrentPointBrights_vl. */
    int16_t room_brightness;
    /* newanims.s:LiftRoutine -> modules/player.s:plr_Fall handoff. */
    int16_t floor_speed;
    uint16_t bobble;
    int16_t add_to_bobble;
    /* newplayershoot.s:Plr1_TimeToShoot_w. */
    int16_t time_to_shoot;
    /* newanims.s/newplayershoot.s:Plr1_NoiseVol_w. */
    int16_t noise_volume;
    uint16_t health;
    uint32_t default_enemy_flags;
    uint8_t ducked;
    uint8_t squished;
    uint8_t stood_in_top;
    uint8_t stood_on_lift;
    uint8_t used;
    /* hires.s snapshots the transient input fields before Plr1_Control. */
    uint8_t tmp_used;
    uint8_t tmp_clicked;
    uint8_t tmp_gun_selected;
    uint8_t tmp_fire;
    uint8_t fire;
    uint8_t clicked;
    /* Native controller-mode state corresponding to Plr1_Mouse_b. */
    uint8_t mouse_active;
    /* modules/player.s:PlrT_InvMouse_b, owned by mouse mode selection. */
    uint8_t invert_mouse;
    /* modules/player.s:PlrT_GunSelected_b and its next-weapon edge gate. */
    uint8_t gun_selected;
    uint8_t previous_use_key_state;
    uint8_t previous_centre_view_key_state;
    uint8_t previous_next_weapon_key_state;
    uint8_t decelerate;
} PlayerRuntime;

/*
 * Source X/Z boundary helpers. `move.w Plr1_XOff_l,Dn` reads the high word
 * of the big-endian source longword, while the low word carries sub-unit
 * movement accumulated by modules/player.s:plr_KeyboardControl.
 */
static inline int16_t player_runtime_position_to_world(int32_t source_position)
{
    return (int16_t)((uint32_t)source_position >> 16u);
}

static inline int32_t player_runtime_world_to_position(int16_t world_coordinate)
{
    return (int32_t)((uint32_t)(uint16_t)world_coordinate << 16u);
}

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
                                            const GameInventory *inventory,
                                            char *error, size_t error_size);

/*
 * Single-player spatial sequence from modules/player.s, plr1control.s, and
 * hires.s:Plr1_Control.  It retains the source snap-state order, falling,
 * fixed-point keyboard motion, and static EdgeT/zone collision.  Dynamic
 * Obj_DoCollision remains owned by the later object-runtime slice.
 */
int player_runtime_update_spatial(PlayerRuntime *player, GameInput *input,
                                  const GameControls *controls,
                                  const GamePreferences *preferences,
                                  const GameMath *math,
                                  const LevelRuntime *runtime,
                                  LevelDynamicState *dynamic_state,
                                  char *error, size_t error_size);

/* Same source update with objectmove.s:newx/newz publication retained. */
int player_runtime_update_spatial_with_motion(PlayerRuntime *player, GameInput *input,
                                              const GameControls *controls,
                                              const GamePreferences *preferences,
                                              const GameMath *math,
                                              const LevelRuntime *runtime,
                                              LevelDynamicState *dynamic_state,
                                              ObjectMotionRuntime *motion_runtime,
                                              char *error, size_t error_size);

#endif
