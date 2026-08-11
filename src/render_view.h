#ifndef AB3D2_RENDER_VIEW_H
#define AB3D2_RENDER_VIEW_H

#include <stdint.h>

/*
 * Native presentation-only camera adjustment.  SceneCamera retains the
 * source game's yaw and small-screen look value; this state supplies the
 * explicitly requested real 3D pitch to every hardware backend.
 */
typedef struct {
    /*
     * Mouse X is shown before the next 50 Hz source tick consumes it. Once
     * consumed, transition_mouse_yaw keeps that already-presented part ahead
     * of source-frame interpolation until the new endpoint catches up.
     */
    uint16_t pending_mouse_yaw;
    int16_t transition_mouse_yaw;
    float pitch_degrees;
} RenderView;

void render_view_init(RenderView *view);
void render_view_add_mouse_motion(RenderView *view, int32_t delta_y, uint8_t invert_mouse);
/* c/system.c:Sys_ReadMouse horizontal path, applied immediately for host presentation. */
void render_view_add_mouse_yaw(RenderView *view, int32_t delta_x);
/* Record the exact Sys_ReadMouse X word consumed by one completed source tick. */
void render_view_commit_mouse_yaw(RenderView *view, int16_t consumed_mouse_x);
/* Host-rate offset applied equally to the interpolated camera and view weapon. */
uint16_t render_view_yaw_offset(const RenderView *view, float interpolation_alpha);
uint16_t render_view_presentation_yaw(const RenderView *view,
                                      uint16_t interpolated_source_yaw,
                                      float interpolation_alpha);

#endif
