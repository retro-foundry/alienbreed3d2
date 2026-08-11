#include "render_view.h"

#include <math.h>

enum {
    /* modules/player.s:plr_MouseControl's small-view source branch. */
    RENDER_VIEW_MOUSE_AIM_UNITS_PER_PIXEL = 128,
    RENDER_VIEW_LOOK_LIMIT = 20,
    RENDER_VIEW_AIM_SPEED_LIMIT = 512 * 20,
    /* newplayershoot.s makes this ratio independent of BulT_Speed_l. */
    RENDER_VIEW_PROJECTILE_HORIZONTAL_AIM_SCALE = 8192
};

#define RENDER_VIEW_SOURCE_YAW_MASK UINT16_C(8190)

static void render_view_update_pitch(RenderView *view)
{
    static const float radians_to_degrees = 57.2957795130823208768f;

    if (view) {
        /*
         * newplayershoot.s shifts the horizontal SinCosTable velocity by
         * BulT_Speed_l and PlrT_AimSpeed_l by (8-BulT_Speed_l). After the
         * renderer's 8.8 Y conversion, every projectile therefore has the
         * same signed slope: -AimSpeed/8192. Use that source sight line for
         * the real-3D camera instead of an unrelated degrees-per-pixel path.
         */
        view->pitch_degrees = -atan2f((float)view->aim_speed,
                                      (float)RENDER_VIEW_PROJECTILE_HORIZONTAL_AIM_SCALE) *
            radians_to_degrees;
    }
}

void render_view_init(RenderView *view)
{
    if (view) {
        view->yaw = 0u;
        view->aim_speed = 0;
        view->look_offset = 0;
        view->pitch_degrees = 0.0f;
    }
}

void render_view_add_mouse_yaw(RenderView *view, int32_t delta_x)
{
    if (!view) {
        return;
    }
    /* c/system.c:Sys_ReadMouse increments Vis_AngPos_w by four bytes per count. */
    view->yaw = (uint16_t)((view->yaw + ((uint16_t)delta_x << 2u)) &
                           RENDER_VIEW_SOURCE_YAW_MASK);
}

void render_view_set_source_yaw(RenderView *view, uint16_t source_yaw)
{
    if (view) {
        view->yaw = (uint16_t)(source_yaw & RENDER_VIEW_SOURCE_YAW_MASK);
    }
}

void render_view_set_source_look(RenderView *view, int32_t source_aim_speed,
                                 int16_t source_look_offset)
{
    if (!view) {
        return;
    }
    view->aim_speed = (int16_t)(uint16_t)source_aim_speed;
    view->look_offset = source_look_offset;
    render_view_update_pitch(view);
}

void render_view_reconcile_source_yaw(RenderView *view, uint16_t previous_source_yaw,
                                      uint16_t current_source_yaw,
                                      int16_t consumed_mouse_x)
{
    uint16_t source_delta;
    uint16_t consumed_mouse_delta;

    if (!view) {
        return;
    }
    source_delta = (uint16_t)(current_source_yaw - previous_source_yaw) &
        RENDER_VIEW_SOURCE_YAW_MASK;
    consumed_mouse_delta = (uint16_t)((uint16_t)consumed_mouse_x << 2u) &
        RENDER_VIEW_SOURCE_YAW_MASK;
    /* The first term carries source keyboard turning; the second was applied per host event. */
    view->yaw = (uint16_t)((view->yaw + source_delta - consumed_mouse_delta) &
                           RENDER_VIEW_SOURCE_YAW_MASK);
}

uint16_t render_view_yaw(const RenderView *view)
{
    return view ? view->yaw : 0u;
}

void render_view_add_mouse_motion(RenderView *view, int32_t delta_y, uint8_t invert_mouse)
{
    int16_t source_delta;
    int32_t next_look;

    if (!view) {
        return;
    }
    source_delta = (int16_t)delta_y;
    if (invert_mouse != 0u) {
        source_delta = (int16_t)(0u - (uint16_t)source_delta);
    }
    view->aim_speed = (int16_t)((uint16_t)view->aim_speed +
        ((uint16_t)source_delta * RENDER_VIEW_MOUSE_AIM_UNITS_PER_PIXEL));
    next_look = (int32_t)view->look_offset + source_delta;
    if (next_look <= -RENDER_VIEW_LOOK_LIMIT) {
        view->look_offset = -RENDER_VIEW_LOOK_LIMIT;
        view->aim_speed = -RENDER_VIEW_AIM_SPEED_LIMIT;
    } else if (next_look >= RENDER_VIEW_LOOK_LIMIT) {
        view->look_offset = RENDER_VIEW_LOOK_LIMIT;
        view->aim_speed = RENDER_VIEW_AIM_SPEED_LIMIT;
    } else {
        view->look_offset = (int16_t)next_look;
    }
    render_view_update_pitch(view);
}
