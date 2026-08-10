#ifndef AB3D2_RENDERER_OPENGL_H
#define AB3D2_RENDERER_OPENGL_H

#include <stddef.h>

#include "render_view.h"
#include "scene_frame.h"

typedef struct RendererOpenGL RendererOpenGL;

RendererOpenGL *renderer_opengl_create(int window_width, int window_height,
                                       const char *window_title,
                                       char *error, size_t error_size);
void renderer_opengl_destroy(RendererOpenGL *renderer);
int renderer_opengl_present(RendererOpenGL *renderer, const SceneFrame *frame,
                            const RenderView *view, char *error, size_t error_size);

#endif
