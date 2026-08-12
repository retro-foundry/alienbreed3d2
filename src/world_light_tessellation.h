#ifndef AB3D2_WORLD_LIGHT_TESSELLATION_H
#define AB3D2_WORLD_LIGHT_TESSELLATION_H

#include <stddef.h>
#include <stdint.h>

#include "scene_frame.h"

/*
 * Presentation-only world vertex generated immediately before GPU upload.
 * Positions and texture coordinates remain in the source coordinate domains;
 * the backend performs its normal world and texture conversions afterwards.
 */
typedef struct {
    float position_x;
    float position_y;
    float position_z;
    float texture_u;
    float texture_v;
    float source_light_level;
} WorldLightTessellationVertex;

typedef struct {
    WorldLightTessellationVertex *vertices;
    uint32_t vertex_count;
} WorldLightTessellationMesh;

int world_light_tessellation_factor_valid(uint8_t factor);

/*
 * Subdivide one world surface into a triangle list. Walls use the four source
 * corner samples consumed by hiresgourwall.s. Flats use one smooth field built
 * from the boundary samples consumed by hires.s:goursides. This deliberately
 * increases presentation fidelity without changing CurrentPointBrights_vl or
 * any source-tick lighting state.
 */
int world_light_tessellate(const SceneGeometry *geometry, uint8_t factor,
                           WorldLightTessellationMesh *out_mesh,
                           char *error, size_t error_size);

void world_light_tessellation_mesh_release(WorldLightTessellationMesh *mesh);

#endif
