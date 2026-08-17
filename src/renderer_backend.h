#ifndef AB3D2_RENDERER_BACKEND_H
#define AB3D2_RENDERER_BACKEND_H

/*
 * Graphics API selection shared by desktop configuration and the API-neutral
 * renderer boundary.  Scene producers remain independent of this choice.
 */
typedef enum {
    RENDERER_BACKEND_OPENGL = 0,
    /* Reserved clean-room backend boundary; the implementation is absent. */
    RENDERER_BACKEND_RTX = 1
} RendererBackend;

const char *renderer_backend_name(RendererBackend backend);
int renderer_backend_from_string(const char *text, RendererBackend *out_backend);

#endif
