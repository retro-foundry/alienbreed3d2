#ifndef AB3D2_LEVEL_STATIC_SCENE_H
#define AB3D2_LEVEL_STATIC_SCENE_H

#include <stddef.h>
#include <stdint.h>

#include "level_mechanisms.h"
#include "level_runtime.h"
#include "scene_frame.h"

enum {
    LEVEL_STATIC_WALL_MECHANISM_NONE = 0u,
    LEVEL_STATIC_WALL_MECHANISM_DOOR = 1u,
    LEVEL_STATIC_WALL_MECHANISM_LIFT = 2u
};

enum {
    LEVEL_STATIC_DYNAMIC_SURFACE_NONE = 0u,
    LEVEL_STATIC_DYNAMIC_SURFACE_DOOR = 1u,
    LEVEL_STATIC_DYNAMIC_SURFACE_LIFT = 2u,
    LEVEL_STATIC_DYNAMIC_SURFACE_WATER = 3u
};

/* One source Draw_Wall quad expanded to a GPU-neutral triangle list. */
typedef struct {
    SceneVertex vertices[6];
    uint32_t material_id;
    uint32_t source_record_offset;
    SceneTextureWindow texture_window;
    /*
     * GPU presentation cache for a DoorRoutine/LiftRoutine wall.  The source
     * mutates its V offset to compensate for a software strip renderer; a
     * native moving solid retains its authored texture mapping instead.
     */
    uint16_t solid_texture_u_end;
    uint16_t solid_texture_y_offset;
    uint8_t solid_texture_height_mask;
    uint8_t is_mechanism_surface;
    /* The canonical wall record and lift flat selected by newanims.s. */
    uint8_t mechanism_kind;
    uint8_t reserved0;
    /* DoorRoutine/LiftRoutine table index; groups all rigid source surfaces. */
    uint16_t mechanism_index;
    uint32_t mechanism_wall_source_offset;
    uint32_t lift_graphics_offset;
    /* Native rigid-lift side bounds, all in scene Y units. */
    int32_t solid_initial_top;
    int32_t solid_initial_bottom;
    uint16_t source_zone_index;
    uint8_t source_upper_zone;
    uint8_t point_brightness_selector;
    uint8_t left_point_brightness;
    uint8_t right_point_brightness;
    int8_t brightness_offset;
    uint8_t other_zone;
} LevelStaticWallScene;

/*
 * One source Draw_Flats boundary. Its vertices deliberately remain a polygon
 * boundary because the source record does not establish that a triangle fan is
 * safe for every floor, ceiling, or water polygon.
 */
typedef struct {
    SceneVertex *vertices;
    /* hires.s:goursides takes this from each Draw_Flats point word's high nibble. */
    uint8_t *point_brightness_selectors;
    uint32_t vertex_count;
    uint32_t material_id;
    uint32_t source_record_offset;
    uint32_t source_record_byte_count;
    SceneGeometryPrimitive primitive;
    int16_t texture_scale;
    int16_t brightness_offset;
    uint16_t source_zone_index;
    uint8_t source_upper_zone;
    /* LiftRoutine/DoWaterAnims owner, if this flat is source-tick mutable. */
    uint8_t dynamic_surface_kind;
    uint16_t dynamic_surface_index;
} LevelStaticFlatScene;

typedef struct {
    LevelStaticWallScene *walls;
    uint32_t wall_count;
    LevelStaticFlatScene *flats;
    uint32_t flat_count;
} LevelStaticScene;

/*
 * Builds all static wall and flat geometry from every lower and upper
 * draw-graph stream. `floor_texture_size` selects the currently active global
 * or level-override floortile byte range. It does not use PVS, portals, or
 * source screen clipping.
 */
int level_static_scene_build(const LevelRuntime *runtime, const LevelMechanisms *mechanisms,
                             uint32_t wall_material_count,
                             size_t floor_texture_size,
                             LevelStaticScene *out_scene,
                             char *error, size_t error_size);
/*
 * Updates the already allocated complete-level geometry from the current
 * mutable draw graph. DoorRoutine, LiftRoutine, and DoWaterAnims change these
 * source records in place; no PVS, portal traversal, or renderer state is
 * involved. A record-count/topology change is rejected because that requires a
 * new source scene allocation rather than a native fallback.
 */
int level_static_scene_apply_runtime(LevelStaticScene *scene, const LevelRuntime *runtime,
                                     uint32_t wall_material_count, size_t floor_texture_size,
                                     char *error, size_t error_size);
void level_static_scene_destroy(LevelStaticScene *scene);

#endif
