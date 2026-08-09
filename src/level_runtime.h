#ifndef AB3D2_LEVEL_RUNTIME_H
#define AB3D2_LEVEL_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "level_bootstrap.h"

/*
 * Non-owning native view of the Game_Begin table resolution. It intentionally
 * excludes the PVST list: the PC renderer draws complete loaded levels and
 * does not use the Amiga PVS/portal traversal as a rendering prerequisite.
 */
typedef struct {
    const uint8_t *level_bytes;
    size_t level_size;
    const uint8_t *graphics_bytes;
    size_t graphics_size;
    uint32_t control_point_coordinates_offset;
    uint32_t point_brightness_offset;
    uint32_t zone_border_points_offset;
    uint32_t zone_graph_adds_offset;
    uint32_t zone_offsets_table_offset;
    uint32_t object_data_offset;
    uint32_t player_shot_offset;
    uint32_t alien_shot_offset;
    uint32_t object_points_offset;
    uint32_t player1_object_offset;
    uint32_t player2_object_offset;
    /* TLBT_NumObjects is the inclusive last object-point index. */
    uint32_t object_point_count;
    /* newanims.s:ObjectHandler's 64-byte ObjT list stops at -1. */
    uint32_t object_record_count;
    /* hires.s:Game_Begin stores TLBT_ObjectDataOffset - TLBT_FloorLineOffset. */
    int32_t edge_data_span;
    int16_t exit_zone_id;
    uint16_t zone_count;
} LevelRuntime;

/* defs.i:ZoneT, represented as native values but retaining source offsets. */
typedef struct {
    uint16_t id;
    int32_t floor;
    int32_t roof;
    int32_t upper_floor;
    int32_t upper_roof;
    int32_t water;
    uint16_t brightness;
    uint16_t upper_brightness;
    uint16_t control_point;
    uint16_t background_sfx_mask;
    int16_t edge_list_relative_offset;
    int16_t points_relative_offset;
    uint8_t draw_backdrop;
    uint8_t echo;
    int16_t teleport_zone;
    int16_t teleport_x;
    int16_t teleport_z;
    uint16_t floor_noise;
    uint16_t upper_floor_noise;
} LevelZone;

/*
 * defs.i:ObjT native read view. The source reuses its 64-byte slots at
 * runtime; in the loaded object list, word zero is the object-point index and
 * a negative value is the list terminator (newanims.s:ObjectHandler).
 */
typedef struct {
    uint16_t point_index;
    int16_t zone_id;
    uint8_t type_id;
    uint8_t sees_player;
} LevelObjectSlot;

/* Lvl_ObjectPointsPtr_l entries are pairs of source 32-bit coordinates. */
typedef struct {
    int32_t x;
    int32_t z;
} LevelObjectPoint;

int level_runtime_init(const AssetBlob *level_data, const AssetBlob *graphics_data,
                       const LevelBootstrap *level,
                       const LevelGraphicsBootstrap *graphics_header,
                       LevelRuntime *out_runtime,
                       char *error, size_t error_size);
int level_runtime_get_zone(const LevelRuntime *runtime, uint16_t zone_index,
                           LevelZone *out_zone, char *error, size_t error_size);
int level_runtime_get_object_record(const LevelRuntime *runtime, uint32_t record_index,
                                    LevelObjectSlot *out_object,
                                    char *error, size_t error_size);
int level_runtime_get_object_point(const LevelRuntime *runtime, uint32_t point_index,
                                   LevelObjectPoint *out_point,
                                   char *error, size_t error_size);

#endif
