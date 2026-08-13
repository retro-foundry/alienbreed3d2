#ifndef AB3D2_LIGHTING_RUNTIME_H
#define AB3D2_LIGHTING_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "game_math.h"
#include "level_runtime.h"
#include "player_runtime.h"

enum {
    /* bss/tables_bss.s:CurrentPointBrights_vl is ds.l 2*256*10. */
    LIGHTING_RUNTIME_POINT_ZONE_CAPACITY = 256u,
    /* bss/zone_bss.s:Zone_BrightTable_vl is ds.l 300. */
    LIGHTING_RUNTIME_ZONE_BRIGHTNESS_CAPACITY = 300u,
    /* bss/anim_bss.s:Anim_BrightTable_vw is ds.w 20. */
    LIGHTING_RUNTIME_ANIMATION_VALUE_COUNT = 20u,
    /* newanims.s:anim_BrightessAnimPtrs_vl defines seven sequences. */
    LIGHTING_RUNTIME_ANIMATION_COUNT = 7u,
    /* newanims.s:objmoveanim reloads Anim_Timer_w with five. */
    LIGHTING_RUNTIME_ANIMATION_INTERVAL = 5u
};

/*
 * Native ownership of the source BSS lighting state.  This is gameplay data:
 * modules/ai.s:ai_CheckForDark consumes PlayerRuntime.room_brightness even
 * though the present SDL consumer does not rasterize scene lighting.
 */
typedef struct {
    /* newanims.s:_Anim_LightingEnabled_b defaults to 0xff. */
    uint8_t lighting_enabled;
    int16_t current_point_brightness[LIGHTING_RUNTIME_POINT_ZONE_CAPACITY]
                                   [LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT];
    /* Each source longword holds lower and upper room brightness words. */
    int16_t zone_brightness[LIGHTING_RUNTIME_ZONE_BRIGHTNESS_CAPACITY][2];
    int16_t animation_values[LIGHTING_RUNTIME_ANIMATION_VALUE_COUNT];
    /* Native offsets into newanims.s' seven static brightness sequences. */
    uint16_t animation_cursors[LIGHTING_RUNTIME_ANIMATION_COUNT];
    int16_t animation_timer;
} LightingRuntime;

/* Source BSS/data initialization before the first VBlank game update. */
void lighting_runtime_init(LightingRuntime *runtime);

/* hires.s:VBlankInterrupt's `subq.w #1,Anim_Timer_w`. */
void lighting_runtime_vblank(LightingRuntime *runtime);

/*
 * hires.s' `donetalking` through `whythehell` lighting block for Player 1:
 * refresh every source PVST zone, then derive Plr1_RoomBright_w from the
 * player zone's ten signed border markers.
 */
int lighting_runtime_refresh_single_player(LightingRuntime *runtime,
                                           const LevelRuntime *level,
                                           PlayerRuntime *player,
                                           char *error, size_t error_size);

/*
 * Presentation extension for the complete-level GPU scene. It evaluates the
 * same `hires.s:allinzone` equations for every zone before source effects are
 * applied, rather than leaving zones outside Player 1's source PVST with
 * stale BSS values. It deliberately does not alter PlayerRuntime fields.
 */
int lighting_runtime_refresh_all_zones(LightingRuntime *runtime,
                                       const LevelRuntime *level,
                                       char *error, size_t error_size);

/* newanims.s:objmoveanim's Anim_Timer_w gate and brightanim call. */
void lighting_runtime_advance_animation(LightingRuntime *runtime);

/*
 * Build the next authored ambient-light endpoint and the completed source-tick
 * phase leading to it. This is presentation-only: `baseline` is the copy made
 * immediately after hires.s:allinzone and before Flash/torch/projectile light,
 * while `runtime` remains the authoritative post-object 50 Hz state.
 */
int lighting_runtime_prepare_presentation_target(
    const LightingRuntime *runtime, const LightingRuntime *baseline,
    const LevelRuntime *level, LightingRuntime *out_target,
    uint8_t *out_phase_tick, char *error, size_t error_size);

/*
 * newanims.s:Flash. It applies the source lower bound of -20, alters the
 * current lower pair for the zone's point list, then updates Zone_BrightTable
 * for the source zone and every zone in its PVST list.
 */
int lighting_runtime_flash(LightingRuntime *runtime, const LevelRuntime *level,
                           uint16_t zone_index, int16_t brightness_change,
                           char *error, size_t error_size);

/*
 * newanims.s:anim_BrightenPoints, including its positive-value darken path.
 * Coordinates are source world words; vertical_position is Anim_BrightY_l.
 */
int lighting_runtime_brighten_points(LightingRuntime *runtime, const LevelRuntime *level,
                                     int16_t brightness, int16_t x, int16_t z,
                                     int32_t vertical_position, uint16_t zone_index,
                                     char *error, size_t error_size);

/*
 * newanims.s:Anim_BrightenPointsAngle. The directional gate reads the source
 * SinCosTable at angle_address before applying anim_BrightenPoints' room-height
 * components.
 */
int lighting_runtime_brighten_points_angle(LightingRuntime *runtime,
                                           const LevelRuntime *level,
                                           const GameMath *math,
                                           int16_t brightness, int16_t x, int16_t z,
                                           int32_t vertical_position,
                                           uint16_t zone_index,
                                           uint16_t angle_address,
                                           char *error, size_t error_size);

#endif
