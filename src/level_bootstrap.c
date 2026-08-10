#include "level_bootstrap.h"

#include <stdio.h>
#include <string.h>

static uint16_t read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t read_be16s(const uint8_t *source)
{
    return (int16_t)read_be16(source);
}

static uint32_t read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static int level_bootstrap_offset_is_valid(uint32_t offset, size_t file_size)
{
    return (size_t)offset < file_size;
}

static void level_bootstrap_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

int level_bootstrap_parse(const AssetBlob *level_data, LevelBootstrap *out_level,
                          char *error, size_t error_size)
{
    const uint8_t *header;
    LevelBootstrap level;
    const uint32_t offsets[] = {
        22u, 26u, 30u, 34u, 38u, 42u, 46u, 50u
    };
    size_t index;

    if (!level_data || !level_data->bytes || !out_level) {
        level_bootstrap_set_error(error, error_size, "level parser received null data");
        return 0;
    }
    if (level_data->size < AB3D2_LEVEL_MESSAGE_BYTES + AB3D2_TLBT_SIZE) {
        level_bootstrap_set_error(error, error_size,
                                  "twolev.bin is shorter than messages plus TLBT header");
        return 0;
    }

    header = level_data->bytes + AB3D2_LEVEL_MESSAGE_BYTES;
    memset(&level, 0, sizeof(level));
    /* modules/player.s:Plr_Initialise moves these coordinate words into X/Z state. */
    level.player1_start_x = read_be16s(header + 0u);
    level.player1_start_z = read_be16s(header + 2u);
    level.player1_start_zone = read_be16(header + 4u);
    level.player2_start_x = read_be16s(header + 6u);
    level.player2_start_z = read_be16s(header + 8u);
    level.player2_start_zone = read_be16(header + 10u);
    level.control_point_count = read_be16(header + 12u);
    level.point_count = read_be16(header + 14u);
    /* hires.s:Game_Begin adds one because TLBT stores count minus one. */
    level.zone_count = (uint16_t)(read_be16(header + 16u) + 1u);
    level.object_count = read_be16(header + 20u);
    level.points_offset = read_be32(header + 22u);
    level.floor_line_offset = read_be32(header + 26u);
    level.object_data_offset = read_be32(header + 30u);
    level.player_shot_offset = read_be32(header + 34u);
    level.alien_shot_offset = read_be32(header + 38u);
    level.object_points_offset = read_be32(header + 42u);
    level.player1_object_offset = read_be32(header + 46u);
    level.player2_object_offset = read_be32(header + 50u);

    for (index = 0; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        if (!level_bootstrap_offset_is_valid(read_be32(header + offsets[index]),
                                             level_data->size)) {
            level_bootstrap_set_error(error, error_size,
                                      "twolev.bin contains a TLBT offset outside the file");
            return 0;
        }
    }
    if (level.zone_count == 0 || level.point_count == 0) {
        level_bootstrap_set_error(error, error_size,
                                  "twolev.bin contains an empty zone or point table");
        return 0;
    }

    *out_level = level;
    return 1;
}

int level_graphics_bootstrap_parse(const AssetBlob *graphics_data,
                                   LevelGraphicsBootstrap *out_graphics,
                                   char *error, size_t error_size)
{
    LevelGraphicsBootstrap graphics;
    const uint32_t offsets[] = {0u, 4u, 8u, 12u};
    size_t index;

    if (!graphics_data || !graphics_data->bytes || !out_graphics) {
        level_bootstrap_set_error(error, error_size,
                                  "level graphics parser received null data");
        return 0;
    }
    if (graphics_data->size < AB3D2_TLGT_SIZE) {
        level_bootstrap_set_error(error, error_size,
                                  "twolev.graph.bin is shorter than the TLGT header");
        return 0;
    }

    graphics.door_data_offset = read_be32(graphics_data->bytes + 0u);
    graphics.lift_data_offset = read_be32(graphics_data->bytes + 4u);
    graphics.switch_data_offset = read_be32(graphics_data->bytes + 8u);
    graphics.zone_graph_adds_offset = read_be32(graphics_data->bytes + 12u);
    /* Game_Begin uses byte 16 as the start of the ZoneT offset table. */
    graphics.zone_adds_table_offset = 16u;

    for (index = 0; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        if (!level_bootstrap_offset_is_valid(read_be32(graphics_data->bytes + offsets[index]),
                                             graphics_data->size)) {
            level_bootstrap_set_error(error, error_size,
                                      "twolev.graph.bin contains a TLGT offset outside the file");
            return 0;
        }
    }

    *out_graphics = graphics;
    return 1;
}
