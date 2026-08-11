#include "render_view.h"

/* Native GPU-camera policy requested for the PC/WebGL presentation path. */
enum {
    RENDER_VIEW_MOUSE_DEGREES_PER_PIXEL_NUMERATOR = 3,
    RENDER_VIEW_MOUSE_DEGREES_PER_PIXEL_DENOMINATOR = 20,
    RENDER_VIEW_MAX_PITCH_DEGREES = 85
};

#define RENDER_VIEW_SOURCE_YAW_MASK UINT16_C(8190)

void render_view_init(RenderView *view)
{
    if (view) {
        view->pending_mouse_yaw = 0u;
        view->transition_mouse_yaw = 0;
        view->pitch_degrees = 0.0f;
    }
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
    float delta_degrees;

    if (!view) {
        return;
    }
    delta_degrees = (float)delta_y *
        (float)RENDER_VIEW_MOUSE_DEGREES_PER_PIXEL_NUMERATOR /
        (float)RENDER_VIEW_MOUSE_DEGREES_PER_PIXEL_DENOMINATOR;
    /* SDL reports upward relative motion as negative Y. */
    view->pitch_degrees += invert_mouse != 0u ? delta_degrees : -delta_degrees;
    if (view->pitch_degrees > (float)RENDER_VIEW_MAX_PITCH_DEGREES) {
        view->pitch_degrees = (float)RENDER_VIEW_MAX_PITCH_DEGREES;
    } else if (view->pitch_degrees < -(float)RENDER_VIEW_MAX_PITCH_DEGREES) {
        view->pitch_degrees = -(float)RENDER_VIEW_MAX_PITCH_DEGREES;
    }
}
