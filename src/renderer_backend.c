#include "renderer_backend.h"

#include <ctype.h>
#include <stddef.h>

static int renderer_backend_equals_ci(const char *left, const char *right)
{
    if (!left || !right) {
        return 0;
    }
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return 0;
        }
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

const char *renderer_backend_name(RendererBackend backend)
{
    switch (backend) {
    case RENDERER_BACKEND_OPENGL:
        return "opengl";
    case RENDERER_BACKEND_VULKAN_RTX:
        return "rtx";
    default:
        return NULL;
    }
}

int renderer_backend_from_string(const char *text, RendererBackend *out_backend)
{
    if (!text || !out_backend) {
        return 0;
    }
    if (renderer_backend_equals_ci(text, "opengl")) {
        *out_backend = RENDERER_BACKEND_OPENGL;
        return 1;
    }
    if (renderer_backend_equals_ci(text, "rtx")) {
        *out_backend = RENDERER_BACKEND_VULKAN_RTX;
        return 1;
    }
    return 0;
}

const char *renderer_rtx_debug_view_name(RendererRtxDebugView view)
{
    static const char *names[] = {
        "final", "albedo", "normal", "roughness", "metalness",
        "emissive", "direct", "indirect", "specular", "variance",
        "history", "gradients", "lfsignals", "specweight"
    };

    return (unsigned)view < sizeof(names) / sizeof(names[0]) ? names[view] :
        "unknown";
}

int renderer_rtx_debug_view_from_string(const char *text,
                                        RendererRtxDebugView *out_view)
{
    static const char *names[] = {
        "final", "albedo", "normal", "roughness", "metalness",
        "emissive", "direct", "indirect", "specular", "variance",
        "history", "gradients", "lfsignals", "specweight"
    };

    if (!text || !out_view) {
        return 0;
    }
    for (unsigned index = 0u; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (renderer_backend_equals_ci(text, names[index])) {
            *out_view = (RendererRtxDebugView)index;
            return 1;
        }
    }
    return 0;
}
