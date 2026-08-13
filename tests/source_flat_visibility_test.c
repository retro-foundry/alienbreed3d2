#include "source_flat_visibility.h"

#include <stdio.h>

int main(void)
{
    if (!source_flat_visible_from_camera(SCENE_GEOMETRY_PRIMITIVE_FLOOR, 64, 0) ||
        source_flat_visible_from_camera(SCENE_GEOMETRY_PRIMITIVE_FLOOR, -64, 0) ||
        source_flat_visible_from_camera(SCENE_GEOMETRY_PRIMITIVE_FLOOR, 0, 0)) {
        fprintf(stderr, "source floor camera-side visibility is inconsistent\n");
        return 1;
    }
    if (!source_flat_visible_from_camera(SCENE_GEOMETRY_PRIMITIVE_CEILING, -64, 0) ||
        source_flat_visible_from_camera(SCENE_GEOMETRY_PRIMITIVE_CEILING, 64, 0) ||
        source_flat_visible_from_camera(SCENE_GEOMETRY_PRIMITIVE_CEILING, 0, 0)) {
        fprintf(stderr, "source ceiling camera-side visibility is inconsistent\n");
        return 1;
    }
    if (!source_flat_visible_from_camera(SCENE_GEOMETRY_PRIMITIVE_WATER, -64, 0) ||
        !source_flat_visible_from_camera(SCENE_GEOMETRY_PRIMITIVE_WATER, 64, 0) ||
        source_flat_visible_from_camera(SCENE_GEOMETRY_PRIMITIVE_WALL, 64, 0)) {
        fprintf(stderr, "source flat visibility primitive handling is inconsistent\n");
        return 1;
    }
    return 0;
}
