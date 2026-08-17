#include "renderer_rtx.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    char error[160] = {0};
    RendererRtx *renderer = renderer_rtx_create(
        1280, 720, "RTX scaffold test", 0, 1, 1u,
        error, sizeof(error));

    if (renderer != NULL) {
        fprintf(stderr, "RTX scaffold unexpectedly created a renderer\n");
        renderer_rtx_destroy(renderer);
        return 1;
    }
    if (strstr(error, "AB3D2_ENABLE_DXR=OFF") == NULL ||
        strstr(error, "AB3D2_ENABLE_DXR=ON") == NULL) {
        fprintf(stderr, "RTX scaffold failure was not explicit: %s\n", error);
        return 1;
    }
    return 0;
}
