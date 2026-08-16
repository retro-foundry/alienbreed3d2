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
    RENDERER_RTX_DEBUG_VARIANCE,
    /* Temporal reprojection diagnostics: accumulated history length
     * (red = specular, green = diffuse, white = full 64+ frames) and the
     * reconstructed lighting-change gradients that cut history
     * (red = indirect LF, green = direct HF, blue = specular). */
    RENDERER_RTX_DEBUG_HISTORY,
    RENDERER_RTX_DEBUG_GRADIENTS,
    /* The two signals the LF gradient compares: red = current raw bounce
     * luma, green = accumulated history luma. */
    RENDERER_RTX_DEBUG_LF_SIGNALS,
    /* Temporal specular internals: red = reprojection tap weight sum,
     * green = antilag, blue = carried history length / 64. */
    RENDERER_RTX_DEBUG_SPEC_WEIGHT
} RendererRtxDebugView;

const char *renderer_backend_name(RendererBackend backend);
int renderer_backend_from_string(const char *text, RendererBackend *out_backend);
const char *renderer_rtx_debug_view_name(RendererRtxDebugView view);
int renderer_rtx_debug_view_from_string(const char *text,
                                        RendererRtxDebugView *out_view);

#endif
