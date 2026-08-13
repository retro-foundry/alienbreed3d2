#include "source_flat_visibility.h"

int source_flat_visible_from_camera(SceneGeometryPrimitive primitive,
                                    int32_t surface_y, int32_t camera_y)
{
    /*
     * hires.s:Draw_Flats compares floorY with flooryoff before rasterizing:
     * type 1 floors are visible only below the camera, type 2 ceilings only
     * above it.  Water enters with both type bits set and remains two-sided.
     */
    switch (primitive) {
    case SCENE_GEOMETRY_PRIMITIVE_FLOOR:
        return surface_y > camera_y;
    case SCENE_GEOMETRY_PRIMITIVE_CEILING:
        return surface_y < camera_y;
    case SCENE_GEOMETRY_PRIMITIVE_WATER:
        return 1;
    default:
        return 0;
    }
}
