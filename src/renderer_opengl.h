#ifndef AB3D2_RENDERER_OPENGL_H
#define AB3D2_RENDERER_OPENGL_H

#include <stddef.h>
#include <stdint.h>

#include "render_view.h"
#include "scene_frame.h"

typedef struct RendererOpenGL RendererOpenGL;

RendererOpenGL *renderer_opengl_create(int window_width, int window_height,
                                       const char *window_title,
                                       int hidden_window,
                                       char *error, size_t error_size);
void renderer_opengl_destroy(RendererOpenGL *renderer);
int renderer_opengl_present(RendererOpenGL *renderer, const SceneFrame *frame,
                            const RenderView *view, char *error, size_t error_size);
/* Hidden-window smoke coverage for Plr1_Use's camera-space companion pass. */
size_t renderer_opengl_last_view_weapon_coverage(const RendererOpenGL *renderer);
/* Hidden GPU-smoke checksum of the fully presented framebuffer's RGB output. */
uint64_t renderer_opengl_last_frame_rgb_checksum(const RendererOpenGL *renderer);

#endif
