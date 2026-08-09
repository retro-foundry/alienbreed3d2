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
    GLFT_BULLET_COUNT = 20,
    GLFT_GUN_COUNT = 10,
    GLFT_ALIEN_COUNT = 20,
    GLFT_BULLET_DEFINITION_SIZE = 300,
    GLFT_SHOOT_DEFINITION_SIZE = 8,
    GLFT_ALIEN_DEFINITION_SIZE = 42,
    GLFT_OBJECT_DEFINITION_SIZE = 40,
    GLFT_OBJECT_ANIMATION_SIZE = 120,
    GLFT_AMMO_GIVE_SIZE = 44,
    GLFT_GUN_GIVE_SIZE = 24,
    GLFT_ALIEN_ANIMATION_SIZE = 2420,
    GLFT_NAME_SIZE = 20,
    GLFT_FRAME_DATA_SIZE = 7680,
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
    GLFT_ALIEN_SHOOT_DEFINITIONS_OFFSET = GLFT_FLOOR_DATA_OFFSET + 16 * 4,
    GLFT_AMBIENT_SFX_OFFSET = GLFT_ALIEN_SHOOT_DEFINITIONS_OFFSET + GLFT_ALIEN_COUNT * GLFT_SHOOT_DEFINITION_SIZE,
    GLFT_LEVEL_MUSIC_OFFSET = GLFT_AMBIENT_SFX_OFFSET + 16 * 2,
    GLFT_ECHO_OFFSET = GLFT_LEVEL_MUSIC_OFFSET + GLFT_LEVEL_COUNT * GLFT_PATH_SIZE,
    GLFT_SIZE = GLFT_ECHO_OFFSET + GLFT_ECHO_SIZE
};

_Static_assert(GLFT_SIZE == 86268, "GLFT layout must match defs.i");

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
    [GAME_LINK_TABLE_FLOOR_DATA] = {GLFT_FLOOR_DATA_OFFSET, 16 * 4},
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

static int game_link_copy_field(const GameLink *link, GameLinkTable table,
                                uint16_t index, size_t entry_size, uint16_t entry_count,
                                int trim_spaces, char *out_text, size_t out_text_size,
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
    if (text_size == 0 || text_size >= out_text_size) {
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
    return game_link_copy_field(link, table, 0, entry_size, 1, 0,
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

int game_link_copy_level_name(const GameLink *link, uint16_t level_index,
                              char *out_text, size_t out_text_size,
                              char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_LEVEL_NAMES, level_index,
                                GLFT_LEVEL_NAME_SIZE, GAME_LINK_LEVEL_COUNT, 1,
                                out_text, out_text_size, error, error_size);
}

int game_link_copy_level_music_path(const GameLink *link, uint16_t level_index,
                                    char *out_path, size_t out_path_size,
                                    char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_LEVEL_MUSIC, level_index,
                                GLFT_PATH_SIZE, GAME_LINK_LEVEL_COUNT, 0,
                                out_path, out_path_size, error, error_size);
}

int game_link_copy_object_graphics_path(const GameLink *link, uint16_t object_index,
                                        char *out_path, size_t out_path_size,
                                        char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_OBJECT_GRAPHICS_NAMES, object_index,
                                GLFT_PATH_SIZE, GAME_LINK_OBJECT_COUNT, 0,
                                out_path, out_path_size, error, error_size);
}

int game_link_copy_sfx_path(const GameLink *link, uint16_t sfx_index,
                            char *out_path, size_t out_path_size,
                            char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_SFX_FILENAMES, sfx_index,
                                GLFT_SFX_PATH_SIZE, GAME_LINK_SFX_COUNT, 0,
                                out_path, out_path_size, error, error_size);
}

int game_link_copy_vector_path(const GameLink *link, uint16_t object_index,
                               char *out_path, size_t out_path_size,
                               char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_VECTOR_NAMES, object_index,
                                GLFT_PATH_SIZE, GAME_LINK_OBJECT_COUNT, 0,
                                out_path, out_path_size, error, error_size);
}

int game_link_copy_wall_graphics_path(const GameLink *link, uint16_t wall_index,
                                      char *out_path, size_t out_path_size,
                                      char *error, size_t error_size)
{
    return game_link_copy_field(link, GAME_LINK_TABLE_WALL_GRAPHICS_NAMES, wall_index,
                                GLFT_PATH_SIZE, GAME_LINK_WALL_COUNT, 0,
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

static int game_link_volume_is_staged(const char *volume, size_t volume_size)
{
    static const char *const staged_volumes[] = {"ab3", "tkg1", "tkg2"};
    size_t index;
    size_t candidate;

    for (index = 0; index < sizeof(staged_volumes) / sizeof(staged_volumes[0]); ++index) {
        size_t length = strlen(staged_volumes[index]);
        if (length != volume_size) {
            continue;
        }
        for (candidate = 0; candidate < length; ++candidate) {
            if (tolower((unsigned char)volume[candidate]) != staged_volumes[index][candidate]) {
                break;
            }
        }
        if (candidate == length) {
            return 1;
        }
    }
    return 0;
}

int game_link_resolve_staged_path(const char *volume_path,
                                  char *out_relative_path, size_t out_path_size,
                                  char *error, size_t error_size)
{
    const char *separator;
    const char *source;
    size_t source_size;
    size_t result_size = 0;
    size_t segment_start = 0;

    if (!volume_path || !out_relative_path || out_path_size == 0 ||
        !(separator = strchr(volume_path, ':')) || separator == volume_path || !separator[1] ||
        strchr(separator + 1, ':') || !game_link_volume_is_staged(volume_path,
                                                                   (size_t)(separator - volume_path))) {
        game_link_set_error(error, error_size, "game-link resource uses an unstaged or malformed volume");
        return 0;
    }

    source = separator + 1;
    source_size = strlen(source);
    if (source[0] == '/' || source[0] == '\\' || source_size >= out_path_size) {
        game_link_set_error(error, error_size, "game-link resource path is absolute or too long");
        return 0;
    }
    while (result_size < source_size) {
        unsigned char character = (unsigned char)source[result_size];
        if (character == '\\') {
            character = '/';
        }
        if (!(isalnum(character) || character == '.' || character == '_' || character == '-' ||
              character == '/')) {
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
