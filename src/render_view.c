#include "render_view.h"

/* Native GPU-camera policy requested for the PC/WebGL presentation path. */
enum {
    RENDER_VIEW_MOUSE_DEGREES_PER_PIXEL_NUMERATOR = 3,
    RENDER_VIEW_MOUSE_DEGREES_PER_PIXEL_DENOMINATOR = 20,
    RENDER_VIEW_MAX_PITCH_DEGREES = 85
};

void render_view_init(RenderView *view)
{
    if (view) {
        view->pitch_degrees = 0.0f;
    }
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
