#ifndef AB3D2_RENDER_VIEW_H
#define AB3D2_RENDER_VIEW_H

#include <stdint.h>

/*
 * Native presentation-only camera adjustment.  SceneCamera retains the
 * source game's yaw and small-screen look value; this state supplies the
 * explicitly requested real 3D pitch to every hardware backend.
 */
typedef struct {
    float pitch_degrees;
} RenderView;

void render_view_init(RenderView *view);
void render_view_add_mouse_motion(RenderView *view, int32_t delta_y, uint8_t invert_mouse);

#endif
