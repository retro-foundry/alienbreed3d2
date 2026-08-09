#ifndef AB3D2_LIGHTING_RUNTIME_H
#define AB3D2_LIGHTING_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

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
    LIGHTING_RUNTIME_ANIMATION_COUNT = 7u
};

/*
 * Native ownership of the source BSS lighting state.  This is gameplay data:
 * modules/ai.s:ai_CheckForDark consumes PlayerRuntime.room_brightness even
 * though the present SDL consumer does not rasterize scene lighting.
 */
typedef struct {
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

/* newanims.s:objmoveanim's Anim_Timer_w gate and brightanim call. */
void lighting_runtime_advance_animation(LightingRuntime *runtime);

#endif
