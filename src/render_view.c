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
        view->yaw = 0u;
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
