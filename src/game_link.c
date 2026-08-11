#include "game_link.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* defs.i:28-34 and defs.i:384-418. Keep the source arithmetic visible here. */
enum {
    GLFT_HEADER_SIZE = 64,
    GLFT_LEVEL_COUNT = 16,
    GLFT_OBJECT_COUNT = 30,
    GLFT_SFX_COUNT = 64,
    GLFT_WALL_COUNT = 16,
    GLFT_LEVEL_NAME_SIZE = 40,
    GLFT_PATH_SIZE = 64,
    GLFT_SFX_PATH_SIZE = 60,
    GLFT_BULLET_COUNT = GAME_LINK_BULLET_COUNT,
    GLFT_GUN_COUNT = GAME_LINK_GUN_COUNT,
    GLFT_ALIEN_COUNT = GAME_LINK_ALIEN_COUNT,
    GLFT_BULLET_DEFINITION_SIZE = GAME_LINK_BULLET_DEFINITION_SIZE,
    GLFT_SHOOT_DEFINITION_SIZE = GAME_LINK_SHOOT_DEFINITION_SIZE,
    GLFT_ALIEN_DEFINITION_SIZE = GAME_LINK_ALIEN_DEFINITION_SIZE,
    GLFT_OBJECT_DEFINITION_SIZE = GAME_LINK_OBJECT_DEFINITION_SIZE,
    GLFT_OBJECT_ANIMATION_SIZE = GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT *
                                 GAME_LINK_OBJECT_ANIMATION_FRAME_SIZE,
    GLFT_AMMO_GIVE_SIZE = 44,
    GLFT_GUN_GIVE_SIZE = 24,
    GLFT_ALIEN_ANIMATION_SIZE = GAME_LINK_ALIEN_ANIMATION_SIZE,
    GLFT_NAME_SIZE = 20,
    GLFT_FRAME_DATA_SIZE = GAME_LINK_OBJECT_COUNT * GAME_LINK_OBJECT_FRAME_DATA_COUNT *
                           GAME_LINK_OBJECT_FRAME_DATA_SIZE,
    GLFT_TEXTURE_PATH_SIZE = 192,
    GLFT_ECHO_SIZE = 60
};

typedef struct {
    size_t offset;
    size_t size;
} GameLinkTableRange;

enum {
    GLFT_LEVEL_NAMES_OFFSET = GLFT_HEADER_SIZE,
    GLFT_OBJECT_GRAPHICS_NAMES_OFFSET = GLFT_LEVEL_NAMES_OFFSET + GLFT_LEVEL_COUNT * GLFT_LEVEL_NAME_SIZE,
    GLFT_SFX_FILENAMES_OFFSET = GLFT_OBJECT_GRAPHICS_NAMES_OFFSET + GLFT_OBJECT_COUNT * GLFT_PATH_SIZE,
    GLFT_FLOOR_FILENAME_OFFSET = GLFT_SFX_FILENAMES_OFFSET + GLFT_SFX_COUNT * GLFT_SFX_PATH_SIZE,
    GLFT_TEXTURE_FILENAME_OFFSET = GLFT_FLOOR_FILENAME_OFFSET + GLFT_PATH_SIZE,
    GLFT_GUN_GRAPHICS_FILENAME_OFFSET = GLFT_TEXTURE_FILENAME_OFFSET + GLFT_TEXTURE_PATH_SIZE,
    GLFT_STORY_FILENAME_OFFSET = GLFT_GUN_GRAPHICS_FILENAME_OFFSET + GLFT_PATH_SIZE,
    GLFT_BULLET_DEFINITIONS_OFFSET = GLFT_STORY_FILENAME_OFFSET + GLFT_PATH_SIZE,
    GLFT_BULLET_NAMES_OFFSET = GLFT_BULLET_DEFINITIONS_OFFSET + GLFT_BULLET_COUNT * GLFT_BULLET_DEFINITION_SIZE,
    GLFT_GUN_NAMES_OFFSET = GLFT_BULLET_NAMES_OFFSET + GLFT_BULLET_COUNT * GLFT_NAME_SIZE,
    GLFT_SHOOT_DEFINITIONS_OFFSET = GLFT_GUN_NAMES_OFFSET + GLFT_GUN_COUNT * GLFT_NAME_SIZE,
    GLFT_ALIEN_NAMES_OFFSET = GLFT_SHOOT_DEFINITIONS_OFFSET + GLFT_GUN_COUNT * GLFT_SHOOT_DEFINITION_SIZE,
    GLFT_ALIEN_DEFINITIONS_OFFSET = GLFT_ALIEN_NAMES_OFFSET + GLFT_ALIEN_COUNT * GLFT_NAME_SIZE,
    GLFT_FRAME_DATA_OFFSET = GLFT_ALIEN_DEFINITIONS_OFFSET + GLFT_ALIEN_COUNT * GLFT_ALIEN_DEFINITION_SIZE,
    GLFT_OBJECT_NAMES_OFFSET = GLFT_FRAME_DATA_OFFSET + GLFT_FRAME_DATA_SIZE,
    GLFT_OBJECT_DEFINITIONS_OFFSET = GLFT_OBJECT_NAMES_OFFSET + GLFT_OBJECT_COUNT * GLFT_NAME_SIZE,
    GLFT_OBJECT_DEFINITION_ANIMATIONS_OFFSET = GLFT_OBJECT_DEFINITIONS_OFFSET + GLFT_OBJECT_COUNT * GLFT_OBJECT_DEFINITION_SIZE,
    GLFT_OBJECT_ACTION_ANIMATIONS_OFFSET = GLFT_OBJECT_DEFINITION_ANIMATIONS_OFFSET + GLFT_OBJECT_COUNT * GLFT_OBJECT_ANIMATION_SIZE,
    GLFT_AMMO_GIVE_OFFSET = GLFT_OBJECT_ACTION_ANIMATIONS_OFFSET + GLFT_OBJECT_COUNT * GLFT_OBJECT_ANIMATION_SIZE,
    GLFT_GUN_GIVE_OFFSET = GLFT_AMMO_GIVE_OFFSET + GLFT_OBJECT_COUNT * GLFT_AMMO_GIVE_SIZE,
    GLFT_ALIEN_ANIMATIONS_OFFSET = GLFT_GUN_GIVE_OFFSET + GLFT_OBJECT_COUNT * GLFT_GUN_GIVE_SIZE,
    GLFT_VECTOR_NAMES_OFFSET = GLFT_ALIEN_ANIMATIONS_OFFSET + GLFT_ALIEN_COUNT * GLFT_ALIEN_ANIMATION_SIZE,
    GLFT_WALL_GRAPHICS_NAMES_OFFSET = GLFT_VECTOR_NAMES_OFFSET + GLFT_OBJECT_COUNT * GLFT_PATH_SIZE,
    GLFT_WALL_HEIGHTS_OFFSET = GLFT_WALL_GRAPHICS_NAMES_OFFSET + GLFT_WALL_COUNT * GLFT_PATH_SIZE,
    GLFT_ALIEN_BRIGHTNESS_OFFSET = GLFT_WALL_HEIGHTS_OFFSET + GLFT_WALL_COUNT * 2,
    GLFT_GUN_OBJECTS_OFFSET = GLFT_ALIEN_BRIGHTNESS_OFFSET + GLFT_ALIEN_COUNT * 2,
    GLFT_PLAYER_GRAPHICS_OFFSET = GLFT_GUN_OBJECTS_OFFSET + GLFT_GUN_COUNT * 2,
    GLFT_FLOOR_DATA_OFFSET = GLFT_PLAYER_GRAPHICS_OFFSET + 4,
    GLFT_ALIEN_SHOOT_DEFINITIONS_OFFSET = GLFT_FLOOR_DATA_OFFSET +
                                          GAME_LINK_FLOOR_DATA_COUNT * 4,
    GLFT_AMBIENT_SFX_OFFSET = GLFT_ALIEN_SHOOT_DEFINITIONS_OFFSET + GLFT_ALIEN_COUNT * GLFT_SHOOT_DEFINITION_SIZE,
    GLFT_LEVEL_MUSIC_OFFSET = GLFT_AMBIENT_SFX_OFFSET + 16 * 2,
    GLFT_ECHO_OFFSET = GLFT_LEVEL_MUSIC_OFFSET + GLFT_LEVEL_COUNT * GLFT_PATH_SIZE,
    GLFT_SIZE = GLFT_ECHO_OFFSET + GLFT_ECHO_SIZE
};

_Static_assert(GLFT_SIZE == 86268, "GLFT layout must match defs.i");
_Static_assert(GAME_LINK_BULLET_ANIMATION_DATA_SIZE ==
                   GAME_LINK_BULLET_ANIMATION_FRAME_COUNT *
                       GAME_LINK_BULLET_ANIMATION_FRAME_SIZE,
               "BulT animation payload must contain fixed six-byte records");
_Static_assert(GAME_LINK_ALIEN_ANIMATION_SIZE == 2420,
               "Alien animation payload must match defs.i:A_AnimLen");

static const GameLinkTableRange game_link_ranges[GAME_LINK_TABLE_COUNT] = {
    [GAME_LINK_TABLE_LEVEL_NAMES] = {GLFT_LEVEL_NAMES_OFFSET, GLFT_LEVEL_COUNT * GLFT_LEVEL_NAME_SIZE},
    [GAME_LINK_TABLE_OBJECT_GRAPHICS_NAMES] = {GLFT_OBJECT_GRAPHICS_NAMES_OFFSET, GLFT_OBJECT_COUNT * GLFT_PATH_SIZE},
    [GAME_LINK_TABLE_SFX_FILENAMES] = {GLFT_SFX_FILENAMES_OFFSET, GLFT_SFX_COUNT * GLFT_SFX_PATH_SIZE},
    [GAME_LINK_TABLE_FLOOR_FILENAME] = {GLFT_FLOOR_FILENAME_OFFSET, GLFT_PATH_SIZE},
    [GAME_LINK_TABLE_TEXTURE_FILENAME] = {GLFT_TEXTURE_FILENAME_OFFSET, GLFT_TEXTURE_PATH_SIZE},
    [GAME_LINK_TABLE_GUN_GRAPHICS_FILENAME] = {GLFT_GUN_GRAPHICS_FILENAME_OFFSET, GLFT_PATH_SIZE},
    [GAME_LINK_TABLE_STORY_FILENAME] = {GLFT_STORY_FILENAME_OFFSET, GLFT_PATH_SIZE},
    [GAME_LINK_TABLE_BULLET_DEFINITIONS] = {GLFT_BULLET_DEFINITIONS_OFFSET, GLFT_BULLET_COUNT * GLFT_BULLET_DEFINITION_SIZE},
    [GAME_LINK_TABLE_BULLET_NAMES] = {GLFT_BULLET_NAMES_OFFSET, GLFT_BULLET_COUNT * GLFT_NAME_SIZE},
    [GAME_LINK_TABLE_GUN_NAMES] = {GLFT_GUN_NAMES_OFFSET, GLFT_GUN_COUNT * GLFT_NAME_SIZE},
    [GAME_LINK_TABLE_SHOOT_DEFINITIONS] = {GLFT_SHOOT_DEFINITIONS_OFFSET, GLFT_GUN_COUNT * GLFT_SHOOT_DEFINITION_SIZE},
    [GAME_LINK_TABLE_ALIEN_NAMES] = {GLFT_ALIEN_NAMES_OFFSET, GLFT_ALIEN_COUNT * GLFT_NAME_SIZE},
    [GAME_LINK_TABLE_ALIEN_DEFINITIONS] = {GLFT_ALIEN_DEFINITIONS_OFFSET, GLFT_ALIEN_COUNT * GLFT_ALIEN_DEFINITION_SIZE},
    [GAME_LINK_TABLE_FRAME_DATA] = {GLFT_FRAME_DATA_OFFSET, GLFT_FRAME_DATA_SIZE},
    [GAME_LINK_TABLE_OBJECT_NAMES] = {GLFT_OBJECT_NAMES_OFFSET, GLFT_OBJECT_COUNT * GLFT_NAME_SIZE},
    [GAME_LINK_TABLE_OBJECT_DEFINITIONS] = {GLFT_OBJECT_DEFINITIONS_OFFSET, GLFT_OBJECT_COUNT * GLFT_OBJECT_DEFINITION_SIZE},
    [GAME_LINK_TABLE_OBJECT_DEFINITION_ANIMATIONS] = {GLFT_OBJECT_DEFINITION_ANIMATIONS_OFFSET, GLFT_OBJECT_COUNT * GLFT_OBJECT_ANIMATION_SIZE},
    [GAME_LINK_TABLE_OBJECT_ACTION_ANIMATIONS] = {GLFT_OBJECT_ACTION_ANIMATIONS_OFFSET, GLFT_OBJECT_COUNT * GLFT_OBJECT_ANIMATION_SIZE},
    [GAME_LINK_TABLE_AMMO_GIVE] = {GLFT_AMMO_GIVE_OFFSET, GLFT_OBJECT_COUNT * GLFT_AMMO_GIVE_SIZE},
    [GAME_LINK_TABLE_GUN_GIVE] = {GLFT_GUN_GIVE_OFFSET, GLFT_OBJECT_COUNT * GLFT_GUN_GIVE_SIZE},
    [GAME_LINK_TABLE_ALIEN_ANIMATIONS] = {GLFT_ALIEN_ANIMATIONS_OFFSET, GLFT_ALIEN_COUNT * GLFT_ALIEN_ANIMATION_SIZE},
    [GAME_LINK_TABLE_VECTOR_NAMES] = {GLFT_VECTOR_NAMES_OFFSET, GLFT_OBJECT_COUNT * GLFT_PATH_SIZE},
    [GAME_LINK_TABLE_WALL_GRAPHICS_NAMES] = {GLFT_WALL_GRAPHICS_NAMES_OFFSET, GLFT_WALL_COUNT * GLFT_PATH_SIZE},
    [GAME_LINK_TABLE_WALL_HEIGHTS] = {GLFT_WALL_HEIGHTS_OFFSET, GLFT_WALL_COUNT * 2},
    [GAME_LINK_TABLE_ALIEN_BRIGHTNESS] = {GLFT_ALIEN_BRIGHTNESS_OFFSET, GLFT_ALIEN_COUNT * 2},
    [GAME_LINK_TABLE_GUN_OBJECTS] = {GLFT_GUN_OBJECTS_OFFSET, GLFT_GUN_COUNT * 2},
    [GAME_LINK_TABLE_PLAYER_GRAPHICS] = {GLFT_PLAYER_GRAPHICS_OFFSET, 4},
    [GAME_LINK_TABLE_FLOOR_DATA] = {GLFT_FLOOR_DATA_OFFSET,
                                    GAME_LINK_FLOOR_DATA_COUNT * 4},
    [GAME_LINK_TABLE_ALIEN_SHOOT_DEFINITIONS] = {GLFT_ALIEN_SHOOT_DEFINITIONS_OFFSET, GLFT_ALIEN_COUNT * GLFT_SHOOT_DEFINITION_SIZE},
    [GAME_LINK_TABLE_AMBIENT_SFX] = {GLFT_AMBIENT_SFX_OFFSET, 16 * 2},
    [GAME_LINK_TABLE_LEVEL_MUSIC] = {GLFT_LEVEL_MUSIC_OFFSET, GAME_LINK_LEVEL_COUNT * GLFT_PATH_SIZE},
    [GAME_LINK_TABLE_ECHO] = {GLFT_ECHO_OFFSET, GLFT_ECHO_SIZE}
};

static void game_link_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t game_link_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static uint32_t game_link_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static int game_link_copy_field(const GameLink *link, GameLinkTable table,
                                uint16_t index, size_t entry_size, uint16_t entry_count,
                                int trim_spaces, int allow_empty,
                                char *out_text, size_t out_text_size,
                                char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    size_t text_size;

    if (index >= entry_count) {
        game_link_set_error(error, error_size, "game-link table index is out of range");
        return 0;
    }
    if (!out_text || out_text_size == 0 ||
        !game_link_table(link, table, &bytes, &size) ||
        size != (size_t)entry_size * entry_count) {
        game_link_set_error(error, error_size, "game-link table is malformed");
        return 0;
    }
    bytes += (size_t)index * entry_size;
    text_size = 0;
    while (text_size < entry_size && bytes[text_size] != 0) {
        ++text_size;
    }
    if (!trim_spaces && text_size == entry_size) {
        game_link_set_error(error, error_size, "game-link resource path is not NUL-terminated");
        return 0;
    }
    if (trim_spaces) {
        while (text_size > 0 && bytes[text_size - 1] == ' ') {
            --text_size;
        }
    }
    if ((!allow_empty && text_size == 0) || text_size >= out_text_size) {
        game_link_set_error(error, error_size, "game-link text does not fit the destination");
        return 0;
    }
    memcpy(out_text, bytes, text_size);
    out_text[text_size] = '\0';
    return 1;
}

static int game_link_copy_single_path(const GameLink *link, GameLinkTable table,
                                      size_t entry_size, char *out_path, size_t out_path_size,
                                      char *error, size_t error_size)
{
    return game_link_copy_field(link, table, 0, entry_size, 1, 0, 1,
                                out_path, out_path_size, error, error_size);
}

int game_link_init(const AssetBlob *blob, GameLink *out_link,
                   char *error, size_t error_size)
{
    if (!blob || !blob->bytes || !out_link || blob->size < GAME_LINK_SIZE) {
        game_link_set_error(error, error_size, "game-link asset is smaller than the GLFT layout");
        return 0;
    }
    out_link->bytes = blob->bytes;
    out_link->size = blob->size;
    return 1;
}

int game_link_table(const GameLink *link, GameLinkTable table,
                    const uint8_t **out_bytes, size_t *out_size)
{
    GameLinkTableRange range;

    if (!link || !link->bytes || table < 0 || table >= GAME_LINK_TABLE_COUNT ||
        !out_bytes || !out_size) {
        return 0;
    }
    range = game_link_ranges[table];
    if (range.offset > link->size || range.size > link->size - range.offset) {
        return 0;
    }
    *out_bytes = link->bytes + range.offset;
    *out_size = range.size;
    return 1;
}

int game_link_get_object_definition(const GameLink *link, uint16_t object_index,
                                    GameObjectDefinition *out_definition,
                                    char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    const uint8_t *source;
    GameObjectDefinition definition;

    if (!out_definition || object_index >= GAME_LINK_OBJECT_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_OBJECT_DEFINITIONS, &bytes, &size) ||
        size != (size_t)GAME_LINK_OBJECT_COUNT * GAME_LINK_OBJECT_DEFINITION_SIZE) {
        game_link_set_error(error, error_size, "object definition is outside the GLFT table");
        return 0;
    }
    source = bytes + (size_t)object_index * GAME_LINK_OBJECT_DEFINITION_SIZE;
    definition.behaviour = game_link_read_be16(source + 0u);
    definition.graphics_type = game_link_read_be16(source + 2u);
    definition.active_timeout = (int16_t)game_link_read_be16(source + 4u);
    definition.hit_points = game_link_read_be16(source + 6u);
    definition.explosive_force = game_link_read_be16(source + 8u);
    definition.impassible = game_link_read_be16(source + 10u);
    definition.default_animation_length = game_link_read_be16(source + 12u);
    definition.collision_radius = game_link_read_be16(source + 14u);
    definition.collision_height = game_link_read_be16(source + 16u);
    definition.floor_ceiling = game_link_read_be16(source + 18u);
    definition.lock_to_wall = game_link_read_be16(source + 20u);
    definition.active_animation_length = game_link_read_be16(source + 22u);
    definition.sound_effect = (int16_t)game_link_read_be16(source + 24u);
    *out_definition = definition;
    return 1;
}

int game_link_get_object_animation_frame(const GameLink *link,
                                         GameObjectAnimationKind kind,
                                         uint16_t object_index, uint16_t frame_index,
                                         GameObjectAnimationFrame *out_frame,
                                         char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    const uint8_t *source;
    GameLinkTable table;
    GameObjectAnimationFrame frame;

    if (kind == GAME_LINK_OBJECT_ANIMATION_DEFAULT) {
        table = GAME_LINK_TABLE_OBJECT_DEFINITION_ANIMATIONS;
    } else if (kind == GAME_LINK_OBJECT_ANIMATION_ACTION) {
        table = GAME_LINK_TABLE_OBJECT_ACTION_ANIMATIONS;
    } else {
        game_link_set_error(error, error_size, "object animation kind is not defined by the GLFT");
        return 0;
    }
    if (!out_frame || object_index >= GAME_LINK_OBJECT_COUNT ||
        frame_index >= GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT ||
        !game_link_table(link, table, &bytes, &size) ||
        size != (size_t)GAME_LINK_OBJECT_COUNT * GLFT_OBJECT_ANIMATION_SIZE) {
        game_link_set_error(error, error_size, "object animation frame is outside the GLFT table");
        return 0;
    }
    source = bytes + (size_t)object_index * GLFT_OBJECT_ANIMATION_SIZE +
             (size_t)frame_index * GAME_LINK_OBJECT_ANIMATION_FRAME_SIZE;
    frame.byte_0 = source[0u];
    frame.byte_1 = source[1u];
    frame.word_2 = game_link_read_be16(source + 2u);
    frame.signed_byte_4 = (int8_t)source[4u];
    frame.next_timer1 = source[5u];
    *out_frame = frame;
    return 1;
}

int game_link_get_object_frame_data(const GameLink *link, uint16_t object_index,
                                    uint16_t frame_index, GameObjectFrameData *out_frame,
                                    char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    const uint8_t *source;
    GameObjectFrameData frame;

    if (!out_frame || object_index >= GAME_LINK_OBJECT_COUNT ||
        frame_index >= GAME_LINK_OBJECT_FRAME_DATA_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_FRAME_DATA, &bytes, &size) ||
        size != (size_t)GAME_LINK_OBJECT_COUNT * GAME_LINK_OBJECT_FRAME_DATA_COUNT *
                    GAME_LINK_OBJECT_FRAME_DATA_SIZE) {
        game_link_set_error(error, error_size, "object frame data is outside the GLFT table");
        return 0;
    }
    source = bytes + ((size_t)object_index * GAME_LINK_OBJECT_FRAME_DATA_COUNT + frame_index) *
        GAME_LINK_OBJECT_FRAME_DATA_SIZE;
    frame.pointer_table_index = game_link_read_be16(source + 0u);
    frame.down_strip = game_link_read_be16(source + 2u);
    frame.strip_count = game_link_read_be16(source + 4u);
    frame.line_count = game_link_read_be16(source + 6u);
    *out_frame = frame;
    return 1;
}

int game_link_get_shoot_definition(const GameLink *link, uint16_t gun_index,
                                   GameShootDefinition *out_definition,
                                   char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    const uint8_t *source;
    GameShootDefinition definition;

    if (!out_definition || gun_index >= GAME_LINK_GUN_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_SHOOT_DEFINITIONS, &bytes, &size) ||
        size != (size_t)GAME_LINK_GUN_COUNT * GAME_LINK_SHOOT_DEFINITION_SIZE) {
        game_link_set_error(error, error_size, "shoot definition is outside the GLFT table");
        return 0;
    }
    source = bytes + (size_t)gun_index * GAME_LINK_SHOOT_DEFINITION_SIZE;
    definition.bullet_type = game_link_read_be16(source + 0u);
    definition.delay = game_link_read_be16(source + 2u);
    definition.bullet_count = game_link_read_be16(source + 4u);
    definition.sound_effect = game_link_read_be16(source + 6u);
    *out_definition = definition;
    return 1;
}

int game_link_get_floor_data(const GameLink *link, uint16_t floor_index,
                             GameFloorData *out_data,
                             char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    const uint8_t *source;
    GameFloorData data;

    if (!out_data || floor_index >= GAME_LINK_FLOOR_DATA_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_FLOOR_DATA, &bytes, &size) ||
        size != (size_t)GAME_LINK_FLOOR_DATA_COUNT * 4u) {
        game_link_set_error(error, error_size, "floor data is outside the GLFT table");
        return 0;
    }
    source = bytes + (size_t)floor_index * 4u;
    data.damage = game_link_read_be16(source);
    data.sound_effect = game_link_read_be16(source + 2u);
    *out_data = data;
    return 1;
}

int game_link_get_gun_object_type(const GameLink *link, uint16_t gun_index,
                                  uint16_t *out_object_type,
                                  char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;

    if (!out_object_type || gun_index >= GAME_LINK_GUN_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_GUN_OBJECTS, &bytes, &size) ||
        size != (size_t)GAME_LINK_GUN_COUNT * 2u) {
        game_link_set_error(error, error_size, "gun object type is outside the GLFT table");
        return 0;
    }
    *out_object_type = game_link_read_be16(bytes + (size_t)gun_index * 2u);
    return 1;
}

int game_link_get_alien_definition(const GameLink *link, uint16_t alien_index,
                                   GameAlienDefinition *out_definition,
                                   char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    const uint8_t *source;
    GameAlienDefinition definition;

    if (!out_definition || alien_index >= GAME_LINK_ALIEN_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_ALIEN_DEFINITIONS, &bytes, &size) ||
        size != (size_t)GAME_LINK_ALIEN_COUNT * GAME_LINK_ALIEN_DEFINITION_SIZE) {
        game_link_set_error(error, error_size, "alien definition is outside the GLFT table");
        return 0;
    }
    source = bytes + (size_t)alien_index * GAME_LINK_ALIEN_DEFINITION_SIZE;
    definition.graphics_type = game_link_read_be16(source + 0u);
    definition.default_behaviour = game_link_read_be16(source + 2u);
    definition.reaction_time = game_link_read_be16(source + 4u);
    definition.default_speed = game_link_read_be16(source + 6u);
    definition.response_behaviour = game_link_read_be16(source + 8u);
    definition.response_speed = game_link_read_be16(source + 10u);
    definition.response_timeout = game_link_read_be16(source + 12u);
    definition.damage_to_retreat = game_link_read_be16(source + 14u);
    definition.damage_to_followup = game_link_read_be16(source + 16u);
    definition.followup_behaviour = game_link_read_be16(source + 18u);
    definition.followup_speed = game_link_read_be16(source + 20u);
    definition.followup_timeout = game_link_read_be16(source + 22u);
    definition.retreat_behaviour = game_link_read_be16(source + 24u);
    definition.retreat_speed = game_link_read_be16(source + 26u);
    definition.retreat_timeout = game_link_read_be16(source + 28u);
    definition.bullet_type = game_link_read_be16(source + 30u);
    definition.hit_points = game_link_read_be16(source + 32u);
    definition.height = game_link_read_be16(source + 34u);
    definition.girth = game_link_read_be16(source + 36u);
    definition.splat_type = game_link_read_be16(source + 38u);
    definition.auxiliary_type = game_link_read_be16(source + 40u);
    *out_definition = definition;
    return 1;
}

int game_link_get_alien_brightness(const GameLink *link, uint16_t alien_index,
                                   int16_t *out_brightness,
                                   char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;

    if (!out_brightness || alien_index >= GAME_LINK_ALIEN_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_ALIEN_BRIGHTNESS, &bytes, &size) ||
        size != (size_t)GAME_LINK_ALIEN_COUNT * 2u) {
        game_link_set_error(error, error_size, "alien brightness is outside the GLFT table");
        return 0;
    }
    *out_brightness = (int16_t)game_link_read_be16(bytes + (size_t)alien_index * 2u);
    return 1;
}

int game_link_get_alien_shoot_definition(const GameLink *link, uint16_t alien_index,
                                         GameShootDefinition *out_definition,
                                         char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    const uint8_t *source;
    GameShootDefinition definition;

    if (!out_definition || alien_index >= GAME_LINK_ALIEN_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_ALIEN_SHOOT_DEFINITIONS, &bytes, &size) ||
        size != (size_t)GAME_LINK_ALIEN_COUNT * GAME_LINK_SHOOT_DEFINITION_SIZE) {
        game_link_set_error(error, error_size, "alien shoot definition is outside the GLFT table");
        return 0;
    }
    source = bytes + (size_t)alien_index * GAME_LINK_SHOOT_DEFINITION_SIZE;
    definition.bullet_type = game_link_read_be16(source + 0u);
    definition.delay = game_link_read_be16(source + 2u);
    definition.bullet_count = game_link_read_be16(source + 4u);
    definition.sound_effect = game_link_read_be16(source + 6u);
    *out_definition = definition;
    return 1;
}

int game_link_get_alien_animation_frame(const GameLink *link, uint16_t alien_index,
                                        uint16_t animation_option, uint16_t frame_index,
                                        GameAlienAnimationFrame *out_frame,
                                        char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    size_t frame_offset;

    if (!out_frame || alien_index >= GAME_LINK_ALIEN_COUNT ||
        animation_option >= GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT ||
        frame_index >= GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_ALIEN_ANIMATIONS, &bytes, &size) ||
        size != (size_t)GAME_LINK_ALIEN_COUNT * GAME_LINK_ALIEN_ANIMATION_SIZE) {
        game_link_set_error(error, error_size, "alien animation frame is outside the GLFT table");
        return 0;
    }
    /* defs.i:A_AnimLen = A_OptLen * 11 and A_OptLen = A_FrameLen * 20. */
    frame_offset = ((size_t)alien_index * GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT +
                    animation_option) * GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT + frame_index;
    memcpy(out_frame->bytes,
           bytes + frame_offset * GAME_LINK_ALIEN_ANIMATION_FRAME_SIZE,
           GAME_LINK_ALIEN_ANIMATION_FRAME_SIZE);
    return 1;
}

int game_link_get_bullet_definition(const GameLink *link, uint16_t bullet_index,
                                    GameBulletDefinition *out_definition,
                                    char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    const uint8_t *source;
    GameBulletDefinition definition;

    if (!out_definition || bullet_index >= GAME_LINK_BULLET_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_BULLET_DEFINITIONS, &bytes, &size) ||
        size != (size_t)GAME_LINK_BULLET_COUNT * GAME_LINK_BULLET_DEFINITION_SIZE) {
        game_link_set_error(error, error_size, "bullet definition is outside the GLFT table");
        return 0;
    }
    source = bytes + (size_t)bullet_index * GAME_LINK_BULLET_DEFINITION_SIZE;
    definition.is_hitscan = game_link_read_be32(source + 0u);
    definition.gravity = game_link_read_be32(source + 4u);
    definition.lifetime = game_link_read_be32(source + 8u);
    definition.ammunition_in_clip = game_link_read_be32(source + 12u);
    definition.bounce_horizontal = game_link_read_be32(source + 16u);
    definition.bounce_vertical = game_link_read_be32(source + 20u);
    definition.hit_damage = game_link_read_be32(source + 24u);
    definition.explosive_force = game_link_read_be32(source + 28u);
    definition.speed = game_link_read_be32(source + 32u);
    definition.animation_frames = game_link_read_be32(source + 36u);
    definition.pop_frames = game_link_read_be32(source + 40u);
    definition.bounce_sound_effect = game_link_read_be32(source + 44u);
    definition.impact_sound_effect = game_link_read_be32(source + 48u);
    definition.graphics_type = game_link_read_be32(source + 52u);
    definition.impact_graphics_type = game_link_read_be32(source + 56u);
    definition.animation_data = source + 60u;
    definition.pop_data = source + 180u;
    *out_definition = definition;
    return 1;
}

int game_link_get_bullet_animation_frame(const GameLink *link,
                                         GameBulletAnimationKind kind,
                                         uint16_t bullet_index, uint16_t frame_index,
                                         GameBulletAnimationFrame *out_frame,
                                         char *error, size_t error_size)
{
    const uint8_t *bytes;
    size_t size;
    const uint8_t *source;
    GameBulletAnimationFrame frame;
    size_t animation_offset;

    if (kind == GAME_LINK_BULLET_ANIMATION_FLIGHT) {
        animation_offset = 60u;
    } else if (kind == GAME_LINK_BULLET_ANIMATION_POP) {
        animation_offset = 180u;
    } else {
        game_link_set_error(error, error_size, "bullet animation kind is not defined by the GLFT");
        return 0;
    }
    if (!out_frame || bullet_index >= GAME_LINK_BULLET_COUNT ||
        frame_index >= GAME_LINK_BULLET_ANIMATION_FRAME_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_BULLET_DEFINITIONS, &bytes, &size) ||
        size != (size_t)GAME_LINK_BULLET_COUNT * GAME_LINK_BULLET_DEFINITION_SIZE) {
        game_link_set_error(error, error_size, "bullet animation frame is outside the GLFT table");
        return 0;
    }
    source = bytes + (size_t)bullet_index * GAME_LINK_BULLET_DEFINITION_SIZE + animation_offset +
        (size_t)frame_index * GAME_LINK_BULLET_ANIMATION_FRAME_SIZE;
    frame.byte_0 = source[0u];
    frame.byte_1 = source[1u];
    frame.word_2 = game_link_read_be16(source + 2u);
    frame.byte_4 = source[4u];
    frame.byte_5 = source[5u];
    *out_frame = frame;
    return 1;
}

int game_link_get_object_inventory_grant(const GameLink *link, uint16_t object_index,
                                         GameInventory *out_grant,
                                         char *error, size_t error_size)
{
    const uint8_t *ammunition_bytes;
    const uint8_t *item_bytes;
    size_t ammunition_size;
    size_t item_size;
    GameInventory grant;

    if (!out_grant || object_index >= GAME_LINK_OBJECT_COUNT ||
        !game_link_table(link, GAME_LINK_TABLE_AMMO_GIVE, &ammunition_bytes, &ammunition_size) ||
        !game_link_table(link, GAME_LINK_TABLE_GUN_GIVE, &item_bytes, &item_size) ||
        ammunition_size != (size_t)GAME_LINK_OBJECT_COUNT * GLFT_AMMO_GIVE_SIZE ||
        item_size != (size_t)GAME_LINK_OBJECT_COUNT * GLFT_GUN_GIVE_SIZE) {
        game_link_set_error(error, error_size, "object inventory grant is outside the GLFT table");
        return 0;
    }
    ammunition_bytes += (size_t)object_index * GLFT_AMMO_GIVE_SIZE;
    item_bytes += (size_t)object_index * GLFT_GUN_GIVE_SIZE;
    grant.health = game_link_read_be16(ammunition_bytes + 0u);
    grant.jetpack_fuel = game_link_read_be16(ammunition_bytes + 2u);
    for (uint16_t index = 0u; index < GAME_INVENTORY_AMMUNITION_COUNT; ++index) {
        grant.ammunition[index] = game_link_read_be16(ammunition_bytes + 4u + (size_t)index * 2u);
    }
    grant.shield = game_link_read_be16(item_bytes + 0u);
    grant.jetpack = game_link_read_be16(item_bytes + 2u);
    for (uint16_t index = 0u; index < GAME_INVENTORY_WEAPON_COUNT; ++index) {
        grant.weapons[index] = game_link_read_be16(item_bytes + 4u + (size_t)index * 2u);
    }
    *out_grant = grant;
    return 1;
}

int game_link_copy_level_name(const GameLink *link, uint16_t level_index,
                              char *out_text, size_t out_text_size,
                              char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_LEVEL_NAMES, level_index,
                                GLFT_LEVEL_NAME_SIZE, GAME_LINK_LEVEL_COUNT, 1, 0,
                                out_text, out_text_size, error, error_size);
}

int game_link_copy_object_name(const GameLink *link, uint16_t object_index,
                               char *out_text, size_t out_text_size,
                               char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_OBJECT_NAMES, object_index,
                                GLFT_NAME_SIZE, GAME_LINK_OBJECT_COUNT, 1, 1,
                                out_text, out_text_size, error, error_size);
}

int game_link_copy_level_music_path(const GameLink *link, uint16_t level_index,
                                    char *out_path, size_t out_path_size,
                                    char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_LEVEL_MUSIC, level_index,
                                GLFT_PATH_SIZE, GAME_LINK_LEVEL_COUNT, 0, 1,
                                out_path, out_path_size, error, error_size);
}

int game_link_copy_object_graphics_path(const GameLink *link, uint16_t object_index,
                                        char *out_path, size_t out_path_size,
                                        char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_OBJECT_GRAPHICS_NAMES, object_index,
                                GLFT_PATH_SIZE, GAME_LINK_OBJECT_COUNT, 0, 1,
                                out_path, out_path_size, error, error_size);
}

int game_link_copy_sfx_path(const GameLink *link, uint16_t sfx_index,
                            char *out_path, size_t out_path_size,
                            char *error, size_t error_size)
{
    /*
     * Res_LoadSoundFx uses RES_NUM_SFX=59 and advances a0 by 64 bytes, even
     * though defs.i expresses the same 3,840-byte range as NUM_SFX * 60.
     * Preserve the executable loader's record stride for actual sound paths.
     */
    if (sfx_index >= GAME_LINK_SFX_LOAD_COUNT) {
        game_link_set_error(error, error_size, "game-link sound index is outside Res_LoadSoundFx");
        return 0;
    }
    return game_link_copy_field(link, GAME_LINK_TABLE_SFX_FILENAMES, sfx_index,
                                GLFT_PATH_SIZE, 60, 0, 1,
                                out_path, out_path_size, error, error_size);
}

int game_link_copy_vector_path(const GameLink *link, uint16_t object_index,
                               char *out_path, size_t out_path_size,
                               char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_VECTOR_NAMES, object_index,
                                GLFT_PATH_SIZE, GAME_LINK_OBJECT_COUNT, 0, 1,
                                out_path, out_path_size, error, error_size);
}

int game_link_copy_wall_graphics_path(const GameLink *link, uint16_t wall_index,
                                      char *out_path, size_t out_path_size,
                                      char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_WALL_GRAPHICS_NAMES, wall_index,
                                GLFT_PATH_SIZE, GAME_LINK_WALL_COUNT, 0, 1,
                                out_path, out_path_size, error, error_size);
}

int game_link_copy_floor_path(const GameLink *link, char *out_path, size_t out_path_size,
                              char *error, size_t error_size)
{
    return game_link_copy_single_path(link, GAME_LINK_TABLE_FLOOR_FILENAME, GLFT_PATH_SIZE,
                                      out_path, out_path_size, error, error_size);
}

int game_link_copy_texture_path(const GameLink *link, char *out_path, size_t out_path_size,
                                char *error, size_t error_size)
{
    return game_link_copy_single_path(link, GAME_LINK_TABLE_TEXTURE_FILENAME,
                                      GLFT_TEXTURE_PATH_SIZE, out_path, out_path_size,
                                      error, error_size);
}

int game_link_copy_gun_graphics_path(const GameLink *link,
                                     char *out_path, size_t out_path_size,
                                     char *error, size_t error_size)
{
    return game_link_copy_single_path(link, GAME_LINK_TABLE_GUN_GRAPHICS_FILENAME,
                                      GLFT_PATH_SIZE, out_path, out_path_size,
                                      error, error_size);
}

int game_link_copy_story_path(const GameLink *link, char *out_path, size_t out_path_size,
                              char *error, size_t error_size)
{
    return game_link_copy_single_path(link, GAME_LINK_TABLE_STORY_FILENAME, GLFT_PATH_SIZE,
                                      out_path, out_path_size, error, error_size);
}

static const char *game_link_staged_volume_prefix(const char *volume, size_t volume_size)
{
    static const struct {
        const char *volume;
        const char *relative_prefix;
    } staged_volumes[] = {
        {"ab3", ""},
        {"tkg1", ""},
        {"tkg2", ""},
        /* test.lnk:sfx:samples/... matches media/ab3dsfx/samples/... exactly. */
        {"sfx", "ab3dsfx/"}
    };
    size_t index;
    size_t candidate;

    for (index = 0; index < sizeof(staged_volumes) / sizeof(staged_volumes[0]); ++index) {
        size_t length = strlen(staged_volumes[index].volume);
        if (length != volume_size) {
            continue;
        }
        for (candidate = 0; candidate < length; ++candidate) {
            if (tolower((unsigned char)volume[candidate]) != staged_volumes[index].volume[candidate]) {
                break;
            }
        }
        if (candidate == length) {
            return staged_volumes[index].relative_prefix;
        }
    }
    return NULL;
}

int game_link_resolve_staged_path(const char *volume_path,
                                  char *out_relative_path, size_t out_path_size,
                                  char *error, size_t error_size)
{
    const char *separator;
    const char *source;
    const char *prefix;
    char source_copy[256];
    size_t source_size;
    size_t prefix_size;
    size_t result_size;
    size_t source_index;
    size_t segment_start;

    if (!volume_path || !out_relative_path || out_path_size == 0 ||
        !(separator = strchr(volume_path, ':')) || separator == volume_path || !separator[1] ||
        strchr(separator + 1, ':') || !(prefix = game_link_staged_volume_prefix(
            volume_path, (size_t)(separator - volume_path)))) {
        game_link_set_error(error, error_size, "game-link resource uses an unstaged or malformed volume");
        return 0;
    }

    source = separator + 1;
    source_size = strlen(source);
    prefix_size = strlen(prefix);
    if (source_size >= sizeof(source_copy) || source[0] == '/' || source[0] == '\\' ||
        prefix_size > out_path_size || source_size >= out_path_size - prefix_size) {
        game_link_set_error(error, error_size, "game-link resource path is absolute or too long");
        return 0;
    }
    memcpy(source_copy, source, source_size + 1);
    memcpy(out_relative_path, prefix, prefix_size);
    result_size = prefix_size;
    segment_start = result_size;
    for (source_index = 0; source_index < source_size; ++source_index) {
        unsigned char character = (unsigned char)source_copy[source_index];
        if (character == '\\') {
            character = '/';
        }
        /* test.lnk contains the legal source filename sfx:samples/fire!.fib. */
        if (!(isalnum(character) || character == '.' || character == '_' || character == '-' ||
              character == '!' || character == '/')) {
            game_link_set_error(error, error_size, "game-link resource path has unsupported characters");
            return 0;
        }
        out_relative_path[result_size] = (char)tolower(character);
        if (character == '/') {
            size_t segment_size = result_size - segment_start;
            if (segment_size == 0 || (segment_size == 1 && out_relative_path[segment_start] == '.') ||
                (segment_size == 2 && out_relative_path[segment_start] == '.' &&
                 out_relative_path[segment_start + 1] == '.')) {
                game_link_set_error(error, error_size, "game-link resource path escapes its volume");
                return 0;
            }
            segment_start = result_size + 1;
        }
        ++result_size;
    }
    if (result_size == segment_start ||
        (result_size - segment_start == 1 && out_relative_path[segment_start] == '.') ||
        (result_size - segment_start == 2 && out_relative_path[segment_start] == '.' &&
         out_relative_path[segment_start + 1] == '.')) {
        game_link_set_error(error, error_size, "game-link resource path has an invalid final segment");
        return 0;
    }
    out_relative_path[result_size] = '\0';
    return 1;
}
