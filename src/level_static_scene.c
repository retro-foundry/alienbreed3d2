#include "level_static_scene.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "level_draw_graph.h"

static void level_static_scene_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int level_static_scene_count_walls(const LevelRuntime *runtime, uint32_t *out_count,
                                          char *error, size_t error_size)
{
    uint32_t total_count = 0u;
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
                    if (total_count == UINT32_MAX) {
                        level_static_scene_set_error(error, error_size,
                                                     "source level has too many wall records");
                        return 0;
                    }
                    ++total_count;
                }
            }
        }
    }
    *out_count = total_count;
    return 1;
}

static void level_static_scene_set_vertex(SceneVertex *vertex, int16_t x, int32_t y, int16_t z)
{
    vertex->position.x = x;
    vertex->position.y = y;
    vertex->position.z = z;
    /* UVs remain explicitly unresolved in the submitted geometry flags. */
    vertex->texture_u = 0;
    vertex->texture_v = 0;
}

int level_static_scene_build(const LevelRuntime *runtime, uint32_t material_count,
                             LevelStaticScene *out_scene,
                             char *error, size_t error_size)
{
    LevelStaticScene scene;
    uint32_t wall_count;
    uint32_t wall_index;
    uint16_t zone_index;

    if (!runtime || !out_scene || material_count == 0u) {
        level_static_scene_set_error(error, error_size,
                                     "static scene received invalid source data or materials");
        return 0;
    }
    memset(&scene, 0, sizeof(scene));
    if (!level_static_scene_count_walls(runtime, &wall_count, error, error_size)) {
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
    wall_index = 0u;
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
                LevelWorldPoint left_point;
                LevelWorldPoint right_point;
                LevelStaticWallScene *scene_wall;

                if (!level_draw_graph_get_record(runtime, zone_index, upper_stream,
                                                 record_index, &record, error, error_size)) {
                    goto fail;
                }
                if (record.type != LEVEL_DRAW_GRAPH_TYPE_WALL) {
                    continue;
                }
                if (!level_draw_graph_read_wall(runtime, &record, &wall, error, error_size) ||
                    wall.texture_id >= material_count ||
                    !level_runtime_get_world_point(runtime, wall.left_point_index, &left_point,
                                                   error, error_size) ||
                    !level_runtime_get_world_point(runtime, wall.right_point_index, &right_point,
                                                   error, error_size) || wall_index >= wall_count) {
                    if (wall.texture_id >= material_count) {
                        level_static_scene_set_error(error, error_size,
                                                     "wall texture id is outside source material table");
                    }
                    goto fail;
                }
                scene_wall = &scene.walls[wall_index++];
                scene_wall->material_id = wall.texture_id;
                scene_wall->source_record_offset = record.source_offset;
                level_static_scene_set_vertex(&scene_wall->vertices[0], left_point.x, wall.top,
                                              left_point.z);
                level_static_scene_set_vertex(&scene_wall->vertices[1], right_point.x, wall.top,
                                              right_point.z);
                level_static_scene_set_vertex(&scene_wall->vertices[2], right_point.x, wall.bottom,
                                              right_point.z);
                level_static_scene_set_vertex(&scene_wall->vertices[3], left_point.x, wall.top,
                                              left_point.z);
                level_static_scene_set_vertex(&scene_wall->vertices[4], right_point.x, wall.bottom,
                                              right_point.z);
                level_static_scene_set_vertex(&scene_wall->vertices[5], left_point.x, wall.bottom,
                                              left_point.z);
            }
        }
    }
    if (wall_index != wall_count) {
        level_static_scene_set_error(error, error_size, "static scene wall count changed during build");
        goto fail;
    }
    scene.wall_count = wall_count;
    *out_scene = scene;
    return 1;

fail:
    free(scene.walls);
    return 0;
}

void level_static_scene_destroy(LevelStaticScene *scene)
{
    if (!scene) {
        return;
    }
    free(scene->walls);
    scene->walls = NULL;
    scene->wall_count = 0u;
}
