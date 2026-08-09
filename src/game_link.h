#ifndef AB3D2_GAME_LINK_H
#define AB3D2_GAME_LINK_H

#include <stddef.h>
#include <stdint.h>

#include "asset_io.h"

/*
 * Native bounds-checked view of the GLFT record declared at defs.i:384.
 * Numeric/animation records stay as big-endian source bytes until the owning
 * gameplay slice is ported; this module only decodes its fixed catalog layout.
 */
typedef struct {
    const uint8_t *bytes;
    size_t size;
} GameLink;

enum {
    GAME_LINK_LEVEL_COUNT = 16,
    GAME_LINK_OBJECT_COUNT = 30,
    GAME_LINK_SFX_COUNT = 64,
    /* modules/res.s:RES_NUM_SFX; the original loader consumes slots 0-58. */
    GAME_LINK_SFX_LOAD_COUNT = 59,
    GAME_LINK_WALL_COUNT = 16,
    GAME_LINK_SIZE = 86268
};

typedef enum {
    GAME_LINK_TABLE_LEVEL_NAMES,
    GAME_LINK_TABLE_OBJECT_GRAPHICS_NAMES,
    GAME_LINK_TABLE_SFX_FILENAMES,
    GAME_LINK_TABLE_FLOOR_FILENAME,
    GAME_LINK_TABLE_TEXTURE_FILENAME,
    GAME_LINK_TABLE_GUN_GRAPHICS_FILENAME,
    GAME_LINK_TABLE_STORY_FILENAME,
    GAME_LINK_TABLE_BULLET_DEFINITIONS,
    GAME_LINK_TABLE_BULLET_NAMES,
    GAME_LINK_TABLE_GUN_NAMES,
    GAME_LINK_TABLE_SHOOT_DEFINITIONS,
    GAME_LINK_TABLE_ALIEN_NAMES,
    GAME_LINK_TABLE_ALIEN_DEFINITIONS,
    GAME_LINK_TABLE_FRAME_DATA,
    GAME_LINK_TABLE_OBJECT_NAMES,
    GAME_LINK_TABLE_OBJECT_DEFINITIONS,
    GAME_LINK_TABLE_OBJECT_DEFINITION_ANIMATIONS,
    GAME_LINK_TABLE_OBJECT_ACTION_ANIMATIONS,
    GAME_LINK_TABLE_AMMO_GIVE,
    GAME_LINK_TABLE_GUN_GIVE,
    GAME_LINK_TABLE_ALIEN_ANIMATIONS,
    GAME_LINK_TABLE_VECTOR_NAMES,
    GAME_LINK_TABLE_WALL_GRAPHICS_NAMES,
    GAME_LINK_TABLE_WALL_HEIGHTS,
    GAME_LINK_TABLE_ALIEN_BRIGHTNESS,
    GAME_LINK_TABLE_GUN_OBJECTS,
    GAME_LINK_TABLE_PLAYER_GRAPHICS,
    GAME_LINK_TABLE_FLOOR_DATA,
    GAME_LINK_TABLE_ALIEN_SHOOT_DEFINITIONS,
    GAME_LINK_TABLE_AMBIENT_SFX,
    GAME_LINK_TABLE_LEVEL_MUSIC,
    GAME_LINK_TABLE_ECHO,
    GAME_LINK_TABLE_COUNT
} GameLinkTable;

int game_link_init(const AssetBlob *blob, GameLink *out_link,
                   char *error, size_t error_size);
int game_link_table(const GameLink *link, GameLinkTable table,
                    const uint8_t **out_bytes, size_t *out_size);

/* Fixed 40-byte labels have no NUL terminator in the shipped game link. */
int game_link_copy_level_name(const GameLink *link, uint16_t level_index,
                              char *out_text, size_t out_text_size,
                              char *error, size_t error_size);

/* Resource entries are source C strings padded to a fixed GLFT table size. */
int game_link_copy_level_music_path(const GameLink *link, uint16_t level_index,
                                    char *out_path, size_t out_path_size,
                                    char *error, size_t error_size);
int game_link_copy_object_graphics_path(const GameLink *link, uint16_t object_index,
                                        char *out_path, size_t out_path_size,
                                        char *error, size_t error_size);
int game_link_copy_sfx_path(const GameLink *link, uint16_t sfx_index,
                            char *out_path, size_t out_path_size,
                            char *error, size_t error_size);
int game_link_copy_vector_path(const GameLink *link, uint16_t object_index,
                               char *out_path, size_t out_path_size,
                               char *error, size_t error_size);
int game_link_copy_wall_graphics_path(const GameLink *link, uint16_t wall_index,
                                      char *out_path, size_t out_path_size,
                                      char *error, size_t error_size);
int game_link_copy_floor_path(const GameLink *link, char *out_path, size_t out_path_size,
                              char *error, size_t error_size);
int game_link_copy_texture_path(const GameLink *link, char *out_path, size_t out_path_size,
                                char *error, size_t error_size);
int game_link_copy_gun_graphics_path(const GameLink *link,
                                     char *out_path, size_t out_path_size,
                                     char *error, size_t error_size);
int game_link_copy_story_path(const GameLink *link, char *out_path, size_t out_path_size,
                              char *error, size_t error_size);

/*
 * Maps source volumes staged by tools/stage_media.py. The `sfx:` volume maps
 * directly to the shipped media/ab3dsfx/ tree; other volumes fail explicitly.
 */
int game_link_resolve_staged_path(const char *volume_path,
                                  char *out_relative_path, size_t out_path_size,
                                  char *error, size_t error_size);

#endif
