#ifndef AB3D2_RENDERER_BACKEND_H
#define AB3D2_RENDERER_BACKEND_H

/*
 * Graphics API selection shared by desktop configuration and the API-neutral
 * renderer boundary.  Scene producers remain independent of this choice.
 */
typedef enum {
    RENDERER_BACKEND_OPENGL = 0,
    RENDERER_BACKEND_VULKAN_RTX = 1
} RendererBackend;

/* Native RTX diagnostic output.  This remains a presentation-only choice. */
typedef enum {
    RENDERER_RTX_DEBUG_FINAL = 0,
    RENDERER_RTX_DEBUG_ALBEDO,
    RENDERER_RTX_DEBUG_NORMAL,
    RENDERER_RTX_DEBUG_ROUGHNESS,
    RENDERER_RTX_DEBUG_METALNESS,
    RENDERER_RTX_DEBUG_EMISSIVE,
    RENDERER_RTX_DEBUG_DIRECT,
    RENDERER_RTX_DEBUG_INDIRECT,
    RENDERER_RTX_DEBUG_SPECULAR,
    RENDERER_RTX_DEBUG_VARIANCE
} RendererRtxDebugView;

const char *renderer_backend_name(RendererBackend backend);
int renderer_backend_from_string(const char *text, RendererBackend *out_backend);
const char *renderer_rtx_debug_view_name(RendererRtxDebugView view);
int renderer_rtx_debug_view_from_string(const char *text,
                                        RendererRtxDebugView *out_view);

#endif
