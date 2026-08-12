#ifndef AB3D2_RENDER_VIEW_H
#define AB3D2_RENDER_VIEW_H

#include <stdint.h>

/*
 * Native host-rate camera state. Its yaw is the current player heading sampled
 * by the next source tick; pitch remains presentation state derived from the
 * source aim fields.
 */
typedef struct {
    /* c/system.c:Sys_ReadMouse angle units, updated at host presentation rate. */
    uint16_t yaw;
    /* modules/player.s PlrT_AimSpeed_l low word and STOPOFFSET. */
    int16_t aim_speed;
    int16_t look_offset;
    float pitch_degrees;
} RenderView;

void render_view_init(RenderView *view);
/* Seed presentation state when a direct-play session starts. */
void render_view_set_source_yaw(RenderView *view, uint16_t source_yaw);
void render_view_set_source_look(RenderView *view, int32_t source_aim_speed,
                                 int16_t source_look_offset);
void render_view_add_mouse_motion(RenderView *view, int32_t delta_y, uint8_t invert_mouse);
/* c/system.c:Sys_ReadMouse horizontal path, applied immediately for host presentation. */
void render_view_add_mouse_yaw(RenderView *view, int32_t delta_x);
uint16_t render_view_yaw(const RenderView *view);

#endif
