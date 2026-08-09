#ifndef AB3D2_RENDERER_STUB_H
#define AB3D2_RENDERER_STUB_H

#include "scene_frame.h"

typedef struct RendererStub RendererStub;

RendererStub *renderer_stub_create(void);
void renderer_stub_destroy(RendererStub *renderer);
int renderer_stub_is_running(const RendererStub *renderer);
void renderer_stub_request_quit(RendererStub *renderer);
void renderer_stub_set_status(RendererStub *renderer, const char *status);
void renderer_stub_present(RendererStub *renderer, const SceneFrame *frame);

#endif
