#include "level_static_scene.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "level_draw_graph.h"

/* `draw_zone_graph.s:Draw_Wall` records have this fixed source size. */
enum { LEVEL_STATIC_SCENE_WALL_RECORD_BYTE_COUNT = 30u };

static void level_static_scene_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t level_static_scene_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8u) | source[1]);
}

static int16_t level_static_scene_read_be16s(const uint8_t *source)
{
    return (int16_t)level_static_scene_read_be16(source);
}

static int level_static_scene_count_primitives(const LevelRuntime *runtime,
                                               uint32_t *out_wall_count,
                                               uint32_t *out_flat_count,
                                               char *error, size_t error_size)
{
    uint32_t wall_count = 0u;
    uint32_t flat_count = 0u;
    uint16_t zone_index;

    for (zone_index = 0u; zone_index < runtime->zone_count; ++zone_index) {
        LevelDrawGraphStreams streams;
        uint8_t upper_stream;

        if (!level_runtime_get_zone_draw_graph_streams(runtime, zone_index, &streams,
                                                       error, error_size)) {
            return 0;
        }
        for (upper_stream = 0u;
             upper_stream <= (streams.has_upper_stream != 0u ? 1u : 0u);
             ++upper_stream) {
            uint32_t record_count;
            uint32_t record_index;

            if (!level_draw_graph_record_count(runtime, zone_index, upper_stream,
                                               &record_count, error, error_size)) {
                return 0;
            }
            for (record_index = 0u; record_index < record_count; ++record_index) {
                LevelDrawGraphRecord record;

                if (!level_draw_graph_get_record(runtime, zone_index, upper_stream,
                                                 record_index, &record, error, error_size)) {
                    return 0;
                }
                if (record.type == LEVEL_DRAW_GRAPH_TYPE_WALL) {
                    if (wall_count == UINT32_MAX) {
                        level_static_scene_set_error(error, error_size,
                                                     "source level has too many wall records");
                        return 0;
                    }
                    ++wall_count;
                } else if (record.type == LEVEL_DRAW_GRAPH_TYPE_FLOOR ||
                           record.type == LEVEL_DRAW_GRAPH_TYPE_CEILING ||
                           record.type == LEVEL_DRAW_GRAPH_TYPE_WATER) {
                    if (flat_count == UINT32_MAX) {
                        level_static_scene_set_error(error, error_size,
                                                     "source level has too many flat records");
                        return 0;
                    }
                    ++flat_count;
                }
            }
        }
    }
    *out_wall_count = wall_count;
    *out_flat_count = flat_count;
    return 1;
}

static void level_static_scene_set_vertex(SceneVertex *vertex, int16_t x, int32_t y, int16_t z,
                                          int32_t texture_u, int32_t texture_v)
{
    vertex->position.x = x;
    vertex->position.y = y;
    vertex->position.z = z;
    vertex->texture_u = texture_u;
    vertex->texture_v = texture_v;
    vertex->source_light_level = 0;
}

/*
 * hires.s:pastsides increments the authored scale by SMALLIT: one for solid
 * floors/ceilings and two for water. The source applies the resulting shift
 * to X and Z before its 64x64 logical-tile lookup.
 */
static int level_static_scene_flat_source_scale(SceneGeometryPrimitive primitive,
                                                int16_t texture_scale,
                                                int8_t *out_scale)
{
    int32_t source_scale = (int32_t)texture_scale +
        (primitive == SCENE_GEOMETRY_PRIMITIVE_WATER ? 2 : 1);

    if (!out_scale || source_scale < -31 || source_scale >= 32) {
        return 0;
    }
    *out_scale = (int8_t)source_scale;
    return 1;
}

static int32_t level_static_scene_flat_texture_coordinate(int16_t coordinate, int8_t scale)
{
    if (scale >= 0) {
        return (int32_t)((uint32_t)(int32_t)coordinate << (uint8_t)scale);
    }
    if (coordinate >= 0) {
        return coordinate >> (uint8_t)-scale;
    }
    return -((-(int32_t)coordinate +
              (int32_t)((UINT32_C(1) << (uint8_t)-scale) - 1u)) >>
             (uint8_t)-scale);
}

static void level_static_scene_set_wall_texture_window(LevelStaticWallScene *scene_wall,
                                                       const LevelDrawWall *wall)
{
    scene_wall->texture_window.u_offset = (uint16_t)(wall->texture_u_tile << 4u);
    scene_wall->texture_window.u_period = (uint16_t)wall->texture_width_mask + 1u;
    scene_wall->texture_window.v_period = (uint16_t)wall->texture_height_mask + 1u;
}

/* hireswall.s:Draw_Wall consumes these fields from the same source record. */
static int level_static_scene_set_wall_presentation(LevelStaticWallScene *scene_wall,
                                                    const LevelDrawWall *wall,
                                                    uint32_t wall_material_count,
                                                    char *error, size_t error_size)
{
    if (!scene_wall || !wall || wall->texture_id >= wall_material_count) {
        level_static_scene_set_error(error, error_size,
                                     "controlled wall texture id is outside source material table");
        return 0;
    }
    scene_wall->material_id = wall->texture_id;
    scene_wall->point_brightness_selector = wall->point_brightness_selector;
    scene_wall->left_point_brightness = wall->left_point_brightness;
    scene_wall->right_point_brightness = wall->right_point_brightness;
    scene_wall->brightness_offset = wall->brightness_offset;
    scene_wall->other_zone = wall->other_zone;
    level_static_scene_set_wall_texture_window(scene_wall, wall);
    return 1;
}

static void level_static_scene_set_wall_vertices(LevelStaticWallScene *scene_wall,
                                                 const LevelDrawWall *wall,
                                                 const LevelWorldPoint *left_point,
                                                 const LevelWorldPoint *right_point,
                                                 uint16_t texture_u_end,
                                                 uint16_t texture_y_offset,
                                                 uint8_t texture_height_mask)
{
    int32_t texture_v_top = texture_y_offset;
    int32_t texture_v_bottom = texture_v_top + (int32_t)texture_height_mask + 1;
    level_static_scene_set_vertex(&scene_wall->vertices[0], left_point->x, wall->top,
                                  left_point->z, 0, texture_v_top);
    level_static_scene_set_vertex(&scene_wall->vertices[1], right_point->x, wall->top,
                                  right_point->z, texture_u_end, texture_v_top);
    level_static_scene_set_vertex(&scene_wall->vertices[2], right_point->x, wall->bottom,
                                  right_point->z, texture_u_end, texture_v_bottom);
    level_static_scene_set_vertex(&scene_wall->vertices[3], left_point->x, wall->top,
                                  left_point->z, 0, texture_v_top);
    level_static_scene_set_vertex(&scene_wall->vertices[4], right_point->x, wall->bottom,
                                  right_point->z, texture_u_end, texture_v_bottom);
    level_static_scene_set_vertex(&scene_wall->vertices[5], left_point->x, wall->bottom,
                                  left_point->z, 0, texture_v_bottom);
}

typedef struct {
    uint8_t kind;
    uint16_t index;
    uint32_t canonical_wall_source_offset;
    uint32_t lift_graphics_offset;
} LevelStaticWallMechanism;

static int level_static_scene_wall_matches_edge(const LevelRuntime *runtime,
                                                const LevelWorldPoint *left_point,
                                                const LevelWorldPoint *right_point,
                                                int16_t edge_index, int *out_matches,
                                                char *error, size_t error_size)
{
    LevelEdge edge;
    int16_t end_x;
    int16_t end_z;

    if (!runtime || !left_point || !right_point || !out_matches || edge_index < 0) {
        level_static_scene_set_error(error, error_size,
                                     "mechanism wall has an invalid source EdgeT index");
        return 0;
    }
    if (!level_runtime_get_edge(runtime, (uint16_t)edge_index, &edge, error, error_size)) {
        return 0;
    }
    end_x = (int16_t)(uint16_t)((uint16_t)edge.x + (uint16_t)edge.x_length);
    end_z = (int16_t)(uint16_t)((uint16_t)edge.z + (uint16_t)edge.z_length);
    *out_matches = (left_point->x == edge.x && left_point->z == edge.z &&
                    right_point->x == end_x && right_point->z == end_z) ||
                   (right_point->x == edge.x && right_point->z == edge.z &&
                    left_point->x == end_x && left_point->z == end_z);
    return 1;
}

static int level_static_scene_add_wall_mechanism_match(
    LevelStaticWallMechanism *mechanism, uint8_t kind, uint16_t index,
    uint32_t canonical_wall_source_offset, uint32_t lift_graphics_offset,
    char *error, size_t error_size)
{
    if (!mechanism || kind == LEVEL_STATIC_WALL_MECHANISM_NONE) {
        level_static_scene_set_error(error, error_size,
                                     "static scene received an invalid mechanism wall match");
        return 0;
    }
    /*
     * The source permits shared Draw_Wall/EdgeT targets. DoorRoutine walks
     * its list first and LiftRoutine follows, each in table order, so retain
     * the final source writer instead of inventing a conflict rejection.
     */
    (void)error;
    (void)error_size;
    mechanism->kind = kind;
    mechanism->index = index;
    mechanism->canonical_wall_source_offset = canonical_wall_source_offset;
    mechanism->lift_graphics_offset = lift_graphics_offset;
    return 1;
}

static int level_static_scene_find_wall_mechanism(
    const LevelRuntime *runtime, const LevelMechanisms *mechanisms,
    uint32_t source_record_offset, const LevelWorldPoint *left_point,
    const LevelWorldPoint *right_point, LevelStaticWallMechanism *out_mechanism,
    char *error, size_t error_size)
{
    uint16_t mechanism_index;
    LevelStaticWallMechanism mechanism = {0};

    if (!runtime || !mechanisms || !left_point || !right_point || !out_mechanism) {
        level_static_scene_set_error(error, error_size,
                                     "static scene has no source mechanism wall data");
        return 0;
    }
    for (mechanism_index = 0u; mechanism_index < mechanisms->door_count;
         ++mechanism_index) {
        LevelLiftable door;

        if (!level_mechanisms_get_door(mechanisms, mechanism_index, &door, error, error_size)) {
            return 0;
        }
        for (uint16_t wall_index = 0u; wall_index < door.wall_count; ++wall_index) {
            LevelLiftableWall wall;
            int edge_matches;

            if (!level_mechanisms_get_door_wall(mechanisms, mechanism_index, wall_index, &wall,
                                                error, error_size) ||
                !level_static_scene_wall_matches_edge(runtime, left_point, right_point,
                                                      wall.edge_index, &edge_matches,
                                                      error, error_size)) {
                return 0;
            }
            if (wall.graphics_offset == source_record_offset || edge_matches != 0) {
                if (!level_static_scene_add_wall_mechanism_match(
                        &mechanism, LEVEL_STATIC_WALL_MECHANISM_DOOR,
                        mechanism_index,
                        wall.graphics_offset, 0u, error, error_size)) {
                    return 0;
                }
            }
        }
    }
    for (mechanism_index = 0u; mechanism_index < mechanisms->lift_count;
         ++mechanism_index) {
        LevelLiftable lift;

        if (!level_mechanisms_get_lift(mechanisms, mechanism_index, &lift, error, error_size)) {
            return 0;
        }
        for (uint16_t wall_index = 0u; wall_index < lift.wall_count; ++wall_index) {
            LevelLiftableWall wall;
            int edge_matches;

            if (!level_mechanisms_get_lift_wall(mechanisms, mechanism_index, wall_index, &wall,
                                                error, error_size) ||
                !level_static_scene_wall_matches_edge(runtime, left_point, right_point,
                                                      wall.edge_index, &edge_matches,
                                                      error, error_size)) {
                return 0;
            }
            if (wall.graphics_offset == source_record_offset || edge_matches != 0) {
                if (!level_static_scene_add_wall_mechanism_match(
                        &mechanism, LEVEL_STATIC_WALL_MECHANISM_LIFT,
                        mechanism_index,
                        wall.graphics_offset, lift.graphics_offset, error, error_size)) {
                    return 0;
                }
            }
        }
    }
    *out_mechanism = mechanism;
    return 1;
}

static int level_static_scene_lift_height(const LevelRuntime *runtime,
                                          uint32_t graphics_offset,
                                          int32_t *out_height,
                                          char *error, size_t error_size)
{
    const uint8_t *source;
    uint8_t type;

    if (!runtime || !runtime->graphics_bytes || !out_height ||
        graphics_offset > runtime->graphics_size ||
        6u > runtime->graphics_size - graphics_offset) {
        level_static_scene_set_error(error, error_size,
                                     "lift floor record is outside the mutable graphics data");
        return 0;
    }
    source = runtime->graphics_bytes + graphics_offset;
    type = source[1u];
    if (type != LEVEL_DRAW_GRAPH_TYPE_FLOOR && type != LEVEL_DRAW_GRAPH_TYPE_CEILING) {
        level_static_scene_set_error(error, error_size,
                                     "lift graphics pointer does not reference Draw_Flats");
        return 0;
    }
    *out_height = (int32_t)level_static_scene_read_be16s(source + 2u) * 64;
    return 1;
}

/*
 * A mutable Draw_Flats record belongs to one native dynamic mesh.  The source
 * mutation still happens in LiftRoutine/DoWaterAnims; this only retains its
 * controller identity so the presentation boundary can build a matching
 * dynamic BLAS/TLAS instance instead of treating each polygon as an object.
 */
static int level_static_scene_find_flat_dynamic_surface(
    const LevelMechanisms *mechanisms, uint32_t source_record_offset,
    uint8_t *out_kind, uint16_t *out_index, char *error, size_t error_size)
{
    if (!mechanisms || !out_kind || !out_index) {
        level_static_scene_set_error(error, error_size,
                                     "flat dynamic-surface lookup received invalid state");
        return 0;
    }
    *out_kind = LEVEL_STATIC_DYNAMIC_SURFACE_NONE;
    *out_index = 0u;
    for (uint16_t lift_index = 0u; lift_index < mechanisms->lift_count; ++lift_index) {
        LevelLiftable lift;

        if (!level_mechanisms_get_lift(mechanisms, lift_index, &lift, error, error_size)) {
            return 0;
        }
        if (lift.graphics_offset == source_record_offset) {
            *out_kind = LEVEL_STATIC_DYNAMIC_SURFACE_LIFT;
            *out_index = lift_index;
        }
    }
    for (uint16_t animation_index = 0u;
         animation_index < mechanisms->water_animation_count; ++animation_index) {
        LevelWaterAnimation animation;

        if (!level_mechanisms_get_water_animation(mechanisms, animation_index, &animation,
                                                  error, error_size)) {
            return 0;
        }
        for (uint16_t target_index = 0u; target_index < animation.target_count; ++target_index) {
            LevelWaterAnimationTarget target;

            if (!level_mechanisms_get_water_animation_target(
                    mechanisms, animation_index, target_index, &target, error, error_size)) {
                return 0;
            }
            if (target.graphics_offset == source_record_offset) {
                *out_kind = LEVEL_STATIC_DYNAMIC_SURFACE_WATER;
                *out_index = animation_index;
            }
        }
    }
    return 1;
}

static int level_static_scene_read_mechanism_wall(const LevelRuntime *runtime,
                                                  uint32_t source_record_offset,
                                                  LevelDrawWall *out_wall,
                                                  char *error, size_t error_size)
{
    LevelDrawGraphRecord record;

    memset(&record, 0, sizeof(record));
    record.type = LEVEL_DRAW_GRAPH_TYPE_WALL;
    record.source_offset = source_record_offset;
    record.byte_count = LEVEL_STATIC_SCENE_WALL_RECORD_BYTE_COUNT;
    return level_draw_graph_read_wall(runtime, &record, out_wall, error, error_size);
}

static int level_static_scene_flat_primitive(uint8_t draw_graph_type,
                                             SceneGeometryPrimitive *out_primitive)
{
    if (!out_primitive) {
        return 0;
    }
    switch (draw_graph_type) {
    case LEVEL_DRAW_GRAPH_TYPE_FLOOR:
        *out_primitive = SCENE_GEOMETRY_PRIMITIVE_FLOOR;
        return 1;
    case LEVEL_DRAW_GRAPH_TYPE_CEILING:
        *out_primitive = SCENE_GEOMETRY_PRIMITIVE_CEILING;
        return 1;
    case LEVEL_DRAW_GRAPH_TYPE_WATER:
        *out_primitive = SCENE_GEOMETRY_PRIMITIVE_WATER;
        return 1;
    default:
        return 0;
    }
}

static int level_static_scene_draw_graph_type_for_primitive(
    SceneGeometryPrimitive primitive, uint8_t *out_draw_graph_type)
{
    if (!out_draw_graph_type) {
        return 0;
    }
    switch (primitive) {
    case SCENE_GEOMETRY_PRIMITIVE_FLOOR:
        *out_draw_graph_type = LEVEL_DRAW_GRAPH_TYPE_FLOOR;
        return 1;
    case SCENE_GEOMETRY_PRIMITIVE_CEILING:
        *out_draw_graph_type = LEVEL_DRAW_GRAPH_TYPE_CEILING;
        return 1;
    case SCENE_GEOMETRY_PRIMITIVE_WATER:
        *out_draw_graph_type = LEVEL_DRAW_GRAPH_TYPE_WATER;
        return 1;
    default:
        return 0;
    }
}

int level_static_scene_build(const LevelRuntime *runtime, const LevelMechanisms *mechanisms,
                             uint32_t wall_material_count,
                             size_t floor_texture_size,
                             LevelStaticScene *out_scene,
                             char *error, size_t error_size)
{
    LevelStaticScene scene;
    uint32_t wall_count;
    uint32_t flat_count;
    uint32_t wall_index;
    uint32_t flat_index;
    uint16_t zone_index;

    if (!runtime || !mechanisms || !out_scene || wall_material_count == 0u ||
        floor_texture_size == 0u) {
        level_static_scene_set_error(error, error_size,
                                     "static scene received invalid source data or materials");
        return 0;
    }
    memset(&scene, 0, sizeof(scene));
    if (!level_static_scene_count_primitives(runtime, &wall_count, &flat_count,
                                             error, error_size)) {
        return 0;
    }
    if (wall_count > SIZE_MAX / sizeof(*scene.walls)) {
        level_static_scene_set_error(error, error_size, "static scene wall allocation is too large");
        return 0;
    }
    if (wall_count != 0u) {
        scene.walls = calloc(wall_count, sizeof(*scene.walls));
        if (!scene.walls) {
            level_static_scene_set_error(error, error_size, "static scene wall allocation failed");
            return 0;
        }
    }
    if (flat_count > SIZE_MAX / sizeof(*scene.flats)) {
        level_static_scene_set_error(error, error_size, "static scene flat allocation is too large");
        goto fail;
    }
    if (flat_count != 0u) {
        scene.flats = calloc(flat_count, sizeof(*scene.flats));
        if (!scene.flats) {
            level_static_scene_set_error(error, error_size, "static scene flat allocation failed");
            goto fail;
        }
    }
    /* Retain the allocation extent so the failure path releases every vertex array. */
    scene.flat_count = flat_count;
    wall_index = 0u;
    flat_index = 0u;
    for (zone_index = 0u; zone_index < runtime->zone_count; ++zone_index) {
        LevelDrawGraphStreams streams;
        uint8_t upper_stream;

        if (!level_runtime_get_zone_draw_graph_streams(runtime, zone_index, &streams,
                                                       error, error_size)) {
            goto fail;
        }
        for (upper_stream = 0u;
             upper_stream <= (streams.has_upper_stream != 0u ? 1u : 0u);
             ++upper_stream) {
            uint32_t record_count;
            uint32_t record_index;

            if (!level_draw_graph_record_count(runtime, zone_index, upper_stream,
                                               &record_count, error, error_size)) {
                goto fail;
            }
            for (record_index = 0u; record_index < record_count; ++record_index) {
                LevelDrawGraphRecord record;
                LevelDrawWall wall;
                LevelDrawFlat flat;
                LevelWorldPoint left_point;
                LevelWorldPoint right_point;
                LevelStaticWallScene *scene_wall;
                LevelStaticWallMechanism wall_mechanism;

                if (!level_draw_graph_get_record(runtime, zone_index, upper_stream,
                                                 record_index, &record, error, error_size)) {
                    goto fail;
                }
                if (record.type == LEVEL_DRAW_GRAPH_TYPE_WALL) {
                    if (!level_draw_graph_read_wall(runtime, &record, &wall, error, error_size) ||
                        wall.texture_id >= wall_material_count ||
                        !level_runtime_get_world_point(runtime, wall.left_point_index, &left_point,
                                                       error, error_size) ||
                        !level_runtime_get_world_point(runtime, wall.right_point_index, &right_point,
                                                       error, error_size) || wall_index >= wall_count) {
                        if (wall.texture_id >= wall_material_count) {
                            level_static_scene_set_error(error, error_size,
                                                         "wall texture id is outside source material table");
                        }
                        goto fail;
                    }
                    scene_wall = &scene.walls[wall_index++];
                    scene_wall->material_id = wall.texture_id;
                    scene_wall->source_record_offset = record.source_offset;
                    scene_wall->source_zone_index = zone_index;
                    scene_wall->source_upper_zone = upper_stream;
                    scene_wall->point_brightness_selector = wall.point_brightness_selector;
                    scene_wall->left_point_brightness = wall.left_point_brightness;
                    scene_wall->right_point_brightness = wall.right_point_brightness;
                    scene_wall->brightness_offset = wall.brightness_offset;
                    scene_wall->other_zone = wall.other_zone;
                    if (!level_static_scene_find_wall_mechanism(
                            runtime, mechanisms, record.source_offset, &left_point, &right_point,
                            &wall_mechanism, error, error_size)) {
                        goto fail;
                    }
                    scene_wall->mechanism_kind = wall_mechanism.kind;
                    scene_wall->mechanism_index = wall_mechanism.index;
                    scene_wall->is_mechanism_surface =
                        wall_mechanism.kind != LEVEL_STATIC_WALL_MECHANISM_NONE ? 1u : 0u;
                    scene_wall->mechanism_wall_source_offset =
                        wall_mechanism.canonical_wall_source_offset;
                    scene_wall->lift_graphics_offset = wall_mechanism.lift_graphics_offset;
                    if (scene_wall->mechanism_kind == LEVEL_STATIC_WALL_MECHANISM_LIFT) {
                        int32_t lift_initial_height;

                        if (!level_static_scene_lift_height(runtime,
                                                            scene_wall->lift_graphics_offset,
                                                            &lift_initial_height,
                                                            error, error_size)) {
                            goto fail;
                        }
                    }
                    scene_wall->solid_initial_top = wall.top;
                    scene_wall->solid_initial_bottom = wall.bottom;
                    level_static_scene_set_wall_texture_window(scene_wall, &wall);
                    level_static_scene_set_wall_vertices(scene_wall, &wall, &left_point,
                                                         &right_point, wall.texture_u_end,
                                                         wall.texture_y_offset,
                                                         wall.texture_height_mask);
                    continue;
                }
                if (record.type == LEVEL_DRAW_GRAPH_TYPE_FLOOR ||
                    record.type == LEVEL_DRAW_GRAPH_TYPE_CEILING ||
                    record.type == LEVEL_DRAW_GRAPH_TYPE_WATER) {
                    LevelStaticFlatScene *scene_flat;
                    uint16_t point_index;
                    int8_t source_scale;

                    if (!level_draw_graph_read_flat(runtime, &record, &flat, error, error_size)) {
                        goto fail;
                    }
                    if (flat.point_count < 3u || flat.texture_offset >= floor_texture_size ||
                        flat_index >= flat_count) {
                        if (flat_index >= flat_count) {
                            level_static_scene_set_error(error, error_size,
                                                         "static scene flat count changed during build");
                        } else if (flat.texture_offset >= floor_texture_size) {
                            level_static_scene_set_error(error, error_size,
                                                         "flat texture offset is outside active source asset");
                        } else {
                            level_static_scene_set_error(error, error_size,
                                                         "flat record has fewer than three points");
                        }
                        goto fail;
                    }
                    scene_flat = &scene.flats[flat_index++];
                    if (!level_static_scene_flat_primitive(record.type, &scene_flat->primitive)) {
                        level_static_scene_set_error(error, error_size,
                                                     "flat geometry has an invalid source primitive type");
                        goto fail;
                    }
                    scene_flat->vertices = calloc(flat.point_count, sizeof(*scene_flat->vertices));
                    scene_flat->point_brightness_selectors =
                        calloc(flat.point_count, sizeof(*scene_flat->point_brightness_selectors));
                    if (!scene_flat->vertices || !scene_flat->point_brightness_selectors) {
                        level_static_scene_set_error(error, error_size,
                                                     "flat geometry allocation failed");
                        goto fail;
                    }
                    scene_flat->vertex_count = flat.point_count;
                    scene_flat->material_id = flat.texture_offset;
                    scene_flat->source_record_offset = record.source_offset;
                    scene_flat->source_record_byte_count = record.byte_count;
                    scene_flat->texture_scale = flat.texture_scale;
                    scene_flat->brightness_offset = flat.brightness_offset;
                    scene_flat->source_zone_index = zone_index;
                    scene_flat->source_upper_zone = upper_stream;
                    if (!level_static_scene_find_flat_dynamic_surface(
                            mechanisms, record.source_offset,
                            &scene_flat->dynamic_surface_kind,
                            &scene_flat->dynamic_surface_index, error, error_size)) {
                        goto fail;
                    }
                    if (!level_static_scene_flat_source_scale(scene_flat->primitive,
                                                               flat.texture_scale,
                                                               &source_scale)) {
                        level_static_scene_set_error(error, error_size,
                                                     "flat texture scale is outside source shift range");
                        goto fail;
                    }
                    for (point_index = 0u; point_index < flat.point_count; ++point_index) {
                        uint16_t raw_point_word;
                        uint16_t world_point_index;
                        LevelWorldPoint world_point;

                        if (!level_draw_graph_get_flat_point(runtime, &flat, point_index,
                                                             &raw_point_word, &world_point_index,
                                                             error, error_size) ||
                            !level_runtime_get_world_point(runtime, world_point_index,
                                                           &world_point, error, error_size)) {
                            goto fail;
                        }
                        if ((raw_point_word >> 12u) >= LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT) {
                            level_static_scene_set_error(
                                error, error_size,
                                "flat point brightness selector is outside CurrentPointBrights_vl");
                            goto fail;
                        }
                        scene_flat->point_brightness_selectors[point_index] =
                            (uint8_t)(raw_point_word >> 12u);
                        level_static_scene_set_vertex(&scene_flat->vertices[point_index],
                                                      world_point.x, (int32_t)flat.height * 64,
                                                      world_point.z,
                                                      level_static_scene_flat_texture_coordinate(
                                                          world_point.x, source_scale),
                                                      level_static_scene_flat_texture_coordinate(
                                                          world_point.z, source_scale));
                    }
                }
            }
        }
    }
    if (wall_index != wall_count || flat_index != flat_count) {
        level_static_scene_set_error(error, error_size,
                                     "static scene primitive count changed during build");
        goto fail;
    }
    scene.wall_count = wall_count;
    *out_scene = scene;
    return 1;

fail:
    level_static_scene_destroy(&scene);
    return 0;
}

int level_static_scene_apply_runtime(LevelStaticScene *scene, const LevelRuntime *runtime,
                                     uint32_t wall_material_count, size_t floor_texture_size,
                                     char *error, size_t error_size)
{
    uint32_t wall_index;
    uint32_t flat_index;

    if (!scene || !runtime || wall_material_count == 0u || floor_texture_size == 0u ||
        (scene->wall_count != 0u && !scene->walls) ||
        (scene->flat_count != 0u && !scene->flats)) {
        level_static_scene_set_error(error, error_size,
                                     "dynamic scene update received invalid source state");
        return 0;
    }
    for (wall_index = 0u; wall_index < scene->wall_count; ++wall_index) {
        LevelStaticWallScene *scene_wall = &scene->walls[wall_index];
        LevelDrawGraphRecord record;
        LevelDrawWall wall;
        LevelWorldPoint left_point;
        LevelWorldPoint right_point;
        int wall_read;

        memset(&record, 0, sizeof(record));
        record.type = LEVEL_DRAW_GRAPH_TYPE_WALL;
        record.source_offset = scene_wall->source_record_offset;
        record.byte_count = LEVEL_STATIC_SCENE_WALL_RECORD_BYTE_COUNT;
        wall_read = level_draw_graph_read_wall(runtime, &record, &wall, error, error_size);
        if (!wall_read ||
            wall.texture_id >= wall_material_count ||
            !level_runtime_get_world_point(runtime, wall.left_point_index, &left_point,
                                           error, error_size) ||
            !level_runtime_get_world_point(runtime, wall.right_point_index, &right_point,
                                           error, error_size)) {
            if (wall_read != 0 && wall.texture_id >= wall_material_count) {
                level_static_scene_set_error(error, error_size,
                                             "dynamic wall texture id is outside source material table");
            }
            return 0;
        }
        if (scene_wall->mechanism_kind == LEVEL_STATIC_WALL_MECHANISM_LIFT) {
            LevelDrawWall solid_wall = wall;
            LevelDrawWall texture_wall;
            int32_t lift_height;
            int64_t solid_bottom;

            if (!level_static_scene_lift_height(runtime, scene_wall->lift_graphics_offset,
                                                &lift_height, error, error_size)) {
                return 0;
            }
            solid_bottom = (int64_t)lift_height +
                ((int64_t)scene_wall->solid_initial_bottom - scene_wall->solid_initial_top);
            if (solid_bottom < INT32_MIN || solid_bottom > INT32_MAX) {
                level_static_scene_set_error(error, error_size,
                                             "lift solid side position is outside scene range");
                return 0;
            }
            /*
             * newanims.s:LiftRoutine writes the controlled Draw_Wall's
             * +12 texture origin while updating the lift.  The rigid native
             * side still receives that source V scroll; only its closed
             * boundary is a presentation choice.
             */
            if (!level_static_scene_read_mechanism_wall(
                    runtime, scene_wall->mechanism_wall_source_offset, &texture_wall,
                    error, error_size)) {
                return 0;
            }
            if (!level_static_scene_set_wall_presentation(
                    scene_wall, &texture_wall, wall_material_count, error, error_size)) {
                return 0;
            }
            /*
             * newanims.s:LiftRoutine changes a Draw_Flats plane and selected
             * Draw_Wall tops independently for the column renderer. In the
             * complete-level GPU view, translate every wall on its source
             * EdgeT as one rigid side and snap that side to the live flat.
             * This removes counterpart-wall gaps without touching gameplay.
             */
            solid_wall.top = lift_height;
            solid_wall.bottom = (int32_t)solid_bottom;
            level_static_scene_set_wall_vertices(
                scene_wall, &solid_wall, &left_point, &right_point,
                texture_wall.texture_u_end, texture_wall.texture_y_offset,
                texture_wall.texture_height_mask);
        } else if (scene_wall->mechanism_kind == LEVEL_STATIC_WALL_MECHANISM_DOOR) {
            LevelDrawWall solid_wall = wall;

            if (!level_static_scene_read_mechanism_wall(
                    runtime, scene_wall->mechanism_wall_source_offset, &solid_wall,
                    error, error_size)) {
                return 0;
            }
            /* Preserve the duplicate graph record's X/Z endpoints. */
            solid_wall.left_point_index = wall.left_point_index;
            solid_wall.right_point_index = wall.right_point_index;
            /* DoorRoutine likewise owns the live Draw_Wall texture origin. */
            if (!level_static_scene_set_wall_presentation(
                    scene_wall, &solid_wall, wall_material_count, error, error_size)) {
                return 0;
            }
            level_static_scene_set_wall_vertices(
                scene_wall, &solid_wall, &left_point, &right_point,
                solid_wall.texture_u_end, solid_wall.texture_y_offset,
                solid_wall.texture_height_mask);
        } else if (scene_wall->mechanism_kind == LEVEL_STATIC_WALL_MECHANISM_NONE) {
            if (!level_static_scene_set_wall_presentation(
                    scene_wall, &wall, wall_material_count, error, error_size)) {
                return 0;
            }
            level_static_scene_set_wall_vertices(scene_wall, &wall, &left_point, &right_point,
                                                 wall.texture_u_end, wall.texture_y_offset,
                                                 wall.texture_height_mask);
        } else {
            level_static_scene_set_error(error, error_size,
                                         "static wall has an invalid mechanism type");
            return 0;
        }
    }
    for (flat_index = 0u; flat_index < scene->flat_count; ++flat_index) {
        LevelStaticFlatScene *scene_flat = &scene->flats[flat_index];
        LevelDrawGraphRecord record;
        LevelDrawFlat flat;
        uint8_t draw_graph_type;
        int8_t source_scale;
        int flat_read;

        if (!scene_flat->vertices || !scene_flat->point_brightness_selectors ||
            !level_static_scene_draw_graph_type_for_primitive(scene_flat->primitive,
                                                               &draw_graph_type)) {
            level_static_scene_set_error(error, error_size,
                                         "dynamic scene contains an unsupported flat primitive");
            return 0;
        }
        memset(&record, 0, sizeof(record));
        record.type = draw_graph_type;
        record.source_offset = scene_flat->source_record_offset;
        record.byte_count = scene_flat->source_record_byte_count;
        flat_read = level_draw_graph_read_flat(runtime, &record, &flat, error, error_size);
        if (!flat_read ||
            flat.point_count != scene_flat->vertex_count ||
            flat.texture_offset >= floor_texture_size) {
            if (flat_read != 0 && flat.point_count != scene_flat->vertex_count) {
                level_static_scene_set_error(error, error_size,
                                             "dynamic flat point count changed from the source scene allocation");
            } else if (flat_read != 0 && flat.texture_offset >= floor_texture_size) {
                level_static_scene_set_error(error, error_size,
                                             "dynamic flat texture offset is outside active source asset");
            }
            return 0;
        }
        scene_flat->material_id = flat.texture_offset;
        scene_flat->texture_scale = flat.texture_scale;
        scene_flat->brightness_offset = flat.brightness_offset;
        if (!level_static_scene_flat_source_scale(scene_flat->primitive, flat.texture_scale,
                                                   &source_scale)) {
            level_static_scene_set_error(error, error_size,
                                         "dynamic flat texture scale is outside source shift range");
            return 0;
        }
        for (uint16_t point_index = 0u; point_index < flat.point_count; ++point_index) {
            uint16_t raw_point_word;
            uint16_t world_point_index;
            LevelWorldPoint world_point;

            if (!level_draw_graph_get_flat_point(runtime, &flat, point_index, &raw_point_word,
                                                 &world_point_index, error, error_size) ||
                !level_runtime_get_world_point(runtime, world_point_index, &world_point,
                                               error, error_size)) {
                return 0;
            }
            if ((raw_point_word >> 12u) >= LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT) {
                level_static_scene_set_error(
                    error, error_size,
                    "dynamic flat point brightness selector is outside CurrentPointBrights_vl");
                return 0;
            }
            scene_flat->point_brightness_selectors[point_index] =
                (uint8_t)(raw_point_word >> 12u);
            level_static_scene_set_vertex(&scene_flat->vertices[point_index], world_point.x,
                                          (int32_t)flat.height * 64, world_point.z,
                                          level_static_scene_flat_texture_coordinate(
                                              world_point.x, source_scale),
                                          level_static_scene_flat_texture_coordinate(
                                              world_point.z, source_scale));
        }
    }
    return 1;
}

void level_static_scene_destroy(LevelStaticScene *scene)
{
    if (!scene) {
        return;
    }
    free(scene->walls);
    scene->walls = NULL;
    scene->wall_count = 0u;
    for (uint32_t flat_index = 0u; flat_index < scene->flat_count; ++flat_index) {
        free(scene->flats[flat_index].vertices);
        free(scene->flats[flat_index].point_brightness_selectors);
    }
    free(scene->flats);
    scene->flats = NULL;
    scene->flat_count = 0u;
}
