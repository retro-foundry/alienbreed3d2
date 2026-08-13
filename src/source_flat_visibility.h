#ifndef AB3D2_SOURCE_FLAT_VISIBILITY_H
#define AB3D2_SOURCE_FLAT_VISIBILITY_H

#include <stdint.h>

#include "scene_frame.h"

/* hires.s:Draw_Flats camera-side selection for source horizontal surfaces. */
int source_flat_visible_from_camera(SceneGeometryPrimitive primitive,
                                    int32_t surface_y, int32_t camera_y);

#endif
