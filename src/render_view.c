#include "render_view.h"

#include <math.h>

enum {
    /* modules/player.s:plr_MouseControl's small-view source branch. */
    RENDER_VIEW_MOUSE_AIM_UNITS_PER_PIXEL = 128,
    RENDER_VIEW_LOOK_LIMIT = 20,
    RENDER_VIEW_AIM_SPEED_LIMIT = 512 * RENDER_VIEW_LOOK_LIMIT,
    /*
     * newplayershoot.s launches horizontal velocity as SinCosTable<<BulletSpd
     * and vertical velocity as AimSpeed>>(8-BulletSpd).  SinCosTable's peak
     * is 32767 and renderer_opengl_world_point converts source Y from 8.8,
     * so every projectile's continuous sight-line slope is -AimSpeed/16384.
     */
    RENDER_VIEW_PROJECTILE_HORIZONTAL_AIM_SCALE = 16384
};

#define RENDER_VIEW_SOURCE_YAW_MASK UINT16_C(8190)

static void render_view_update_pitch(RenderView *view)
{
    static const float radians_to_degrees = 57.2957795130823208768f;

    if (view) {
        view->pitch_degrees = -atan2f((float)view->aim_speed,
                                      (float)RENDER_VIEW_PROJECTILE_HORIZONTAL_AIM_SCALE) *
            radians_to_degrees;
    }
}

void render_view_init(RenderView *view)
{
    if (view) {
        view->pending_mouse_yaw = 0u;
        view->transition_mouse_yaw = 0;
        view->aim_speed = 0;
        view->look_offset = 0;
        view->pitch_degrees = 0.0f;
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

void render_view_add_mouse_yaw(RenderView *view, int32_t delta_x)
{
    if (!view) {
        return;
    }
    /* c/system.c:Sys_ReadMouse increments Vis_AngPos_w by four bytes per count. */
    view->pending_mouse_yaw = (uint16_t)(
        (view->pending_mouse_yaw + ((uint16_t)delta_x << 2u)) &
        RENDER_VIEW_SOURCE_YAW_MASK);
}

void render_view_commit_mouse_yaw(RenderView *view, int16_t consumed_mouse_x)
{
    uint16_t consumed_yaw;
    int32_t signed_yaw;

    if (!view) {
        return;
    }
    consumed_yaw = (uint16_t)(((uint16_t)consumed_mouse_x << 2u) &
                              RENDER_VIEW_SOURCE_YAW_MASK);
    view->pending_mouse_yaw = (uint16_t)(
        (view->pending_mouse_yaw - consumed_yaw) & RENDER_VIEW_SOURCE_YAW_MASK);
    signed_yaw = consumed_yaw;
    if (signed_yaw > 4096) {
        signed_yaw -= 8192;
    }
    view->transition_mouse_yaw = (int16_t)signed_yaw;
}

uint16_t render_view_yaw_offset(const RenderView *view, float interpolation_alpha)
{
    double transition;
    int32_t rounded_transition;

    if (!view) {
        return 0u;
    }
    if (interpolation_alpha < 0.0f) {
        interpolation_alpha = 0.0f;
    } else if (interpolation_alpha > 1.0f) {
        interpolation_alpha = 1.0f;
    }
    transition = (double)view->transition_mouse_yaw *
        (1.0 - (double)interpolation_alpha);
    rounded_transition = (int32_t)(transition >= 0.0 ? transition + 0.5 : transition - 0.5);
    return (uint16_t)((view->pending_mouse_yaw + rounded_transition) &
                      RENDER_VIEW_SOURCE_YAW_MASK);
}

uint16_t render_view_presentation_yaw(const RenderView *view,
                                      uint16_t interpolated_source_yaw,
                                      float interpolation_alpha)
{
    return (uint16_t)((interpolated_source_yaw +
                       render_view_yaw_offset(view, interpolation_alpha)) &
                      RENDER_VIEW_SOURCE_YAW_MASK);
}

void render_view_add_mouse_motion(RenderView *view, int32_t delta_y, uint8_t invert_mouse)
{
    int16_t source_delta;
    int32_t next_look;

    if (!view) {
        return;
    }

    /* modules/player.s:plr_MouseControl's Sys_MouseY/Sys_OldMouseY path. */
    source_delta = (int16_t)delta_y;
    if (invert_mouse != 0u) {
        source_delta = (int16_t)(0u - (uint16_t)source_delta);
    }
    view->aim_speed = (int16_t)(uint16_t)(
        (uint16_t)view->aim_speed + ((uint16_t)source_delta << 7u));
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
