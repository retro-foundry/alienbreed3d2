#ifndef AB3D2_LEVEL_BOOTSTRAP_H
#define AB3D2_LEVEL_BOOTSTRAP_H

#include <stddef.h>
#include <stdint.h>

#include "asset_io.h"

/* hires.s:Game_Begin reads this many fixed-size messages before TLBT. */
#define AB3D2_LEVEL_MESSAGE_LENGTH 160u
#define AB3D2_LEVEL_MESSAGE_COUNT 10u
#define AB3D2_LEVEL_MESSAGE_BYTES \
    (AB3D2_LEVEL_MESSAGE_LENGTH * AB3D2_LEVEL_MESSAGE_COUNT)
#define AB3D2_TLBT_SIZE 54u

typedef struct {
    /* TLBT stores these as UWORDs, but Plr_Initialise consumes them as signed X/Z words. */
    int16_t player1_start_x;
    int16_t player1_start_z;
    uint16_t player1_start_zone;
    int16_t player2_start_x;
    int16_t player2_start_z;
    uint16_t player2_start_zone;
    uint16_t control_point_count;
    uint16_t point_count;
    uint16_t zone_count;
    uint16_t object_count;
    uint32_t points_offset;
    uint32_t floor_line_offset;
    uint32_t object_data_offset;
    uint32_t player_shot_offset;
    uint32_t alien_shot_offset;
    uint32_t object_points_offset;
    uint32_t player1_object_offset;
    uint32_t player2_object_offset;
} LevelBootstrap;

/* twolev.graph.bin's TLGT header from defs.i. */
#define AB3D2_TLGT_SIZE 20u
typedef struct {
    uint32_t door_data_offset;
    uint32_t lift_data_offset;
    uint32_t switch_data_offset;
    uint32_t zone_graph_adds_offset;
    uint32_t zone_adds_table_offset;
} LevelGraphicsBootstrap;

/* Parses only the TLBT data that Game_Begin establishes before game updates. */
int level_bootstrap_parse(const AssetBlob *level_data, LevelBootstrap *out_level,
                          char *error, size_t error_size);

/* Parses the twolev.graph.bin TLGT header used by hires.s:Game_Begin. */
int level_graphics_bootstrap_parse(const AssetBlob *graphics_data,
                                   LevelGraphicsBootstrap *out_graphics,
                                   char *error, size_t error_size);

#endif
