#ifndef AB3D2_RENDER_VIEW_H
#define AB3D2_RENDER_VIEW_H

#include <stdint.h>

/*
 * Native presentation-only camera adjustment.  SceneCamera retains the
 * source game's yaw and small-screen look value; this state supplies the
 * explicitly requested real 3D pitch to every hardware backend.
 */
typedef struct {
    /* Effective source-angle address used by the presentation camera. */
    uint16_t yaw;
    /* modules/player.s PlrT_AimSpeed_l low word and STOPOFFSET. */
    int16_t aim_speed;
    int16_t look_offset;
    float pitch_degrees;
} RenderView;

void render_view_init(RenderView *view);
void render_view_add_mouse_motion(RenderView *view, int32_t delta_y, uint8_t invert_mouse);
/* c/system.c:Sys_ReadMouse horizontal path, applied immediately for host presentation. */
void render_view_add_mouse_yaw(RenderView *view, int32_t delta_x);
void render_view_set_source_yaw(RenderView *view, uint16_t source_yaw);
/* Synchronize the host-cadence view with committed modules/player.s state. */
void render_view_set_source_look(RenderView *view, int32_t source_aim_speed,
                                 int16_t source_look_offset);
/* Reconcile source keyboard/turn state without applying an already-presented mouse delta twice. */
void render_view_reconcile_source_yaw(RenderView *view, uint16_t previous_source_yaw,
                                      uint16_t current_source_yaw,
                                      int16_t consumed_mouse_x);
uint16_t render_view_yaw(const RenderView *view);

#endif
