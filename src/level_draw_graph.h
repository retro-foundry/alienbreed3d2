#ifndef AB3D2_LEVEL_DRAW_GRAPH_H
#define AB3D2_LEVEL_DRAW_GRAPH_H

#include <stddef.h>
#include <stdint.h>

#include "level_runtime.h"

/* Low byte dispatch values from modules/draw/draw_zone_graph.s. */
enum {
    LEVEL_DRAW_GRAPH_TYPE_WALL = 0,
    LEVEL_DRAW_GRAPH_TYPE_FLOOR = 1,
    LEVEL_DRAW_GRAPH_TYPE_CEILING = 2,
    LEVEL_DRAW_GRAPH_TYPE_SET_CLIP = 3,
    LEVEL_DRAW_GRAPH_TYPE_OBJECTS = 4,
    LEVEL_DRAW_GRAPH_TYPE_WATER = 7,
    LEVEL_DRAW_GRAPH_TYPE_BACKDROP = 12
};

/* One source record, including its two-byte raw dispatch tag. */
typedef struct {
    uint16_t raw_tag;
    uint8_t type;
    uint32_t source_offset;
    uint32_t byte_count;
} LevelDrawGraphRecord;

/* hireswall.s:Draw_Wall's complete 30-byte graph record. */
typedef struct {
    uint16_t left_point_index;
    uint16_t right_point_index;
    uint16_t texture_id;
    int32_t top;
    int32_t bottom;
    uint8_t left_point_brightness;
    uint8_t right_point_brightness;
    uint8_t texture_height_mask;
    uint8_t texture_height_shift;
    uint8_t texture_width_mask;
    uint8_t point_brightness_selector;
    int8_t brightness_offset;
    int8_t other_zone;
} LevelDrawWall;

/* Draw_Flats record with its source point words kept intact. */
typedef struct {
    int16_t height;
    uint16_t point_count;
    uint32_t points_offset;
} LevelDrawFlat;

/*
 * All calls follow draw_zone_graph.s directly and do not consult PVS or
 * portal state. `upper_stream` is zero for the lower stream and nonzero for
 * the optional upper stream.
 */
int level_draw_graph_record_count(const LevelRuntime *runtime, uint16_t zone_index,
                                  int upper_stream, uint32_t *out_count,
                                  char *error, size_t error_size);
int level_draw_graph_get_record(const LevelRuntime *runtime, uint16_t zone_index,
                                int upper_stream, uint32_t record_index,
                                LevelDrawGraphRecord *out_record,
                                char *error, size_t error_size);
int level_draw_graph_read_wall(const LevelRuntime *runtime,
                               const LevelDrawGraphRecord *record,
                               LevelDrawWall *out_wall,
                               char *error, size_t error_size);
int level_draw_graph_read_flat(const LevelRuntime *runtime,
                               const LevelDrawGraphRecord *record,
                               LevelDrawFlat *out_flat,
                               char *error, size_t error_size);
int level_draw_graph_get_flat_point(const LevelRuntime *runtime,
                                    const LevelDrawFlat *flat, uint16_t point_index,
                                    uint16_t *out_raw_point_word,
                                    uint16_t *out_world_point_index,
                                    char *error, size_t error_size);

#endif
