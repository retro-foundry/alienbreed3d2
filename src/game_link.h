#ifndef AB3D2_GAME_LINK_H
#define AB3D2_GAME_LINK_H

#include <stddef.h>
#include <stdint.h>

#include "asset_io.h"
#include "game_inventory.h"

/*
 * Native bounds-checked view of the GLFT record declared at defs.i:384.
 * Numeric/animation records stay as big-endian source bytes until the owning
 * gameplay slice is ported; this module only decodes its fixed catalog layout.
 */
typedef struct {
    const uint8_t *bytes;
    size_t size;
} GameLink;

/* defs.i:ODefT_SizeOf_l and O_FrameStoreSize/O_AnimSize. */
enum {
    GAME_LINK_SHOOT_DEFINITION_SIZE = 8,
    GAME_LINK_BULLET_DEFINITION_SIZE = 300,
    GAME_LINK_BULLET_ANIMATION_DATA_SIZE = 120,
    GAME_LINK_BULLET_ANIMATION_FRAME_SIZE = 6,
    GAME_LINK_BULLET_ANIMATION_FRAME_COUNT = 20,
    GAME_LINK_ALIEN_DEFINITION_SIZE = 42,
    /* defs.i:A_FrameLen/A_OptLen/A_AnimLen. */
    GAME_LINK_ALIEN_ANIMATION_FRAME_SIZE = 11,
    GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT = 20,
    GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT = 11,
    GAME_LINK_ALIEN_ANIMATION_SIZE = GAME_LINK_ALIEN_ANIMATION_FRAME_SIZE *
                                     GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT *
                                     GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT,
    GAME_LINK_OBJECT_DEFINITION_SIZE = 40,
    GAME_LINK_OBJECT_ANIMATION_FRAME_SIZE = 6,
    GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT = 20,
    GAME_LINK_OBJECT_FRAME_DATA_SIZE = 8,
    GAME_LINK_OBJECT_FRAME_DATA_COUNT = 32
};

/*
 * Native read view of defs.i:ODefT. `active_timeout` and `sound_effect` are
 * signed because newaliencontrol.s tests both fields for a negative sentinel.
 * Other values preserve their UWORD source representation until their owning
 * gameplay routine establishes more specific semantics.
 */
typedef struct {
    uint16_t behaviour;
    uint16_t graphics_type;
    int16_t active_timeout;
    uint16_t hit_points;
    uint16_t explosive_force;
    uint16_t impassible;
    uint16_t default_animation_length;
    uint16_t collision_radius;
    uint16_t collision_height;
    uint16_t floor_ceiling;
    uint16_t lock_to_wall;
    uint16_t active_animation_length;
    int16_t sound_effect;
} GameObjectDefinition;

/*
 * One six-byte O_FrameStore record. DEFANIMOBJ and ACTANIMOBJ use these
 * fields differently for bitmap, vector, and glare graphics, so only the
 * signed byte-four operation and next Timer1 value are named here. The first
 * four bytes deliberately retain source-offset names until sprite semantics
 * are established by the ObjectHandler-to-Draw_Objects oracle fixture.
 */
typedef struct {
    uint8_t byte_0;
    uint8_t byte_1;
    uint16_t word_2;
    int8_t signed_byte_4;
    uint8_t next_timer1;
} GameObjectAnimationFrame;

typedef enum {
    GAME_LINK_OBJECT_ANIMATION_DEFAULT,
    GAME_LINK_OBJECT_ANIMATION_ACTION
} GameObjectAnimationKind;

/*
 * One GLFT_FrameData_l entry. objdrawhires.s indexes the table as 32
 * eight-byte records per object. It uses the high word of the first long as a
 * PTR-table index and the low word as its initial down-strip; words +4/+6 are
 * the strip and line counts used to scale the selected bitmap frame.
 */
typedef struct {
    uint16_t pointer_table_index;
    uint16_t down_strip;
    uint16_t strip_count;
    uint16_t line_count;
} GameObjectFrameData;

/* defs.i:ShootT, consumed by newplayershoot.s:Plr1_Shot. */
typedef struct {
    uint16_t bullet_type;
    uint16_t delay;
    uint16_t bullet_count;
    uint16_t sound_effect;
} GameShootDefinition;

/* defs.i:AlienT, consumed by newaliencontrol.s:ItsAnAlien. */
typedef struct {
    uint16_t graphics_type;
    uint16_t default_behaviour;
    uint16_t reaction_time;
    uint16_t default_speed;
    uint16_t response_behaviour;
    uint16_t response_speed;
    uint16_t response_timeout;
    uint16_t damage_to_retreat;
    uint16_t damage_to_followup;
    uint16_t followup_behaviour;
    uint16_t followup_speed;
    uint16_t followup_timeout;
    uint16_t retreat_behaviour;
    uint16_t retreat_speed;
    uint16_t retreat_timeout;
    uint16_t bullet_type;
    uint16_t hit_points;
    uint16_t height;
    uint16_t girth;
    uint16_t splat_type;
    uint16_t auxiliary_type;
} GameAlienDefinition;

/*
 * One eleven-byte A_FrameLen record from GLFT_AlienAnims_l.  hires.s:DOALLANIMS
 * owns the byte-level interpretation (notably offsets 0, 5, 6, and 7), so this
 * catalog reader deliberately preserves every source byte without assigning
 * renderer or AI meaning to the remaining fields.
 */
typedef struct {
    uint8_t bytes[GAME_LINK_ALIEN_ANIMATION_FRAME_SIZE];
} GameAlienAnimationFrame;

/*
 * defs.i:BulT. The original declares each leading parameter as ULONG, so
 * their raw unsigned representation is retained here; source instructions
 * choose signed, word, or byte views when running a projectile update.
 */
typedef struct {
    uint32_t is_hitscan;
    uint32_t gravity;
    uint32_t lifetime;
    uint32_t ammunition_in_clip;
    uint32_t bounce_horizontal;
    uint32_t bounce_vertical;
    uint32_t hit_damage;
    uint32_t explosive_force;
    uint32_t speed;
    uint32_t animation_frames;
    uint32_t pop_frames;
    uint32_t bounce_sound_effect;
    uint32_t impact_sound_effect;
    uint32_t graphics_type;
    uint32_t impact_graphics_type;
    const uint8_t *animation_data;
    const uint8_t *pop_data;
} GameBulletDefinition;

/*
 * One six-byte record in BulT_AnimData_vb or BulT_PopData_vb. ItsABullet
 * branches on the graphic type before assigning these fields, so retain raw
 * source offsets until projectile updates have an oracle fixture.
 */
typedef struct {
    uint8_t byte_0;
    uint8_t byte_1;
    uint16_t word_2;
    uint8_t byte_4;
    uint8_t byte_5;
} GameBulletAnimationFrame;

typedef enum {
    GAME_LINK_BULLET_ANIMATION_FLIGHT,
    GAME_LINK_BULLET_ANIMATION_POP
} GameBulletAnimationKind;

enum {
    GAME_LINK_LEVEL_COUNT = 16,
    GAME_LINK_OBJECT_COUNT = 30,
    GAME_LINK_BULLET_COUNT = 20,
    GAME_LINK_GUN_COUNT = 10,
    GAME_LINK_ALIEN_COUNT = 20,
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

/*
 * Endian-safe GLFT object records used by newaliencontrol.s:ItsAnObject,
 * DEFANIMOBJ, and ACTANIMOBJ. These readers have no runtime side effects.
 */
int game_link_get_object_definition(const GameLink *link, uint16_t object_index,
                                    GameObjectDefinition *out_definition,
                                    char *error, size_t error_size);
int game_link_get_object_animation_frame(const GameLink *link,
                                         GameObjectAnimationKind kind,
                                         uint16_t object_index, uint16_t frame_index,
                                         GameObjectAnimationFrame *out_frame,
                                         char *error, size_t error_size);
int game_link_get_object_frame_data(const GameLink *link, uint16_t object_index,
                                    uint16_t frame_index, GameObjectFrameData *out_frame,
                                    char *error, size_t error_size);
int game_link_get_shoot_definition(const GameLink *link, uint16_t gun_index,
                                   GameShootDefinition *out_definition,
                                   char *error, size_t error_size);
/* defs.i:GLFT_GunObjects_l, consumed by hires.s:Plr1_Use. */
int game_link_get_gun_object_type(const GameLink *link, uint16_t gun_index,
                                  uint16_t *out_object_type,
                                  char *error, size_t error_size);
int game_link_get_alien_definition(const GameLink *link, uint16_t alien_index,
                                   GameAlienDefinition *out_definition,
                                   char *error, size_t error_size);
/* newaliencontrol.s:ItsAnAlien negates this source word before ai_DoTorch. */
int game_link_get_alien_brightness(const GameLink *link, uint16_t alien_index,
                                   int16_t *out_brightness,
                                   char *error, size_t error_size);
/* newaliencontrol.s:ItsAnAlien's source ShootT for alien-fired bullets. */
int game_link_get_alien_shoot_definition(const GameLink *link, uint16_t alien_index,
                                         GameShootDefinition *out_definition,
                                         char *error, size_t error_size);
int game_link_get_alien_animation_frame(const GameLink *link, uint16_t alien_index,
                                        uint16_t animation_option, uint16_t frame_index,
                                        GameAlienAnimationFrame *out_frame,
                                        char *error, size_t error_size);
int game_link_get_bullet_definition(const GameLink *link, uint16_t bullet_index,
                                    GameBulletDefinition *out_definition,
                                    char *error, size_t error_size);
int game_link_get_bullet_animation_frame(const GameLink *link,
                                         GameBulletAnimationKind kind,
                                         uint16_t bullet_index, uint16_t frame_index,
                                         GameBulletAnimationFrame *out_frame,
                                         char *error, size_t error_size);
/*
 * GLFT_AmmoGive_l / GLFT_GunGive_l form an InvCT/InvIT-shaped grant for each
 * object class. This accessor only decodes source bytes; ObjectHandler owns
 * the decision to apply a grant.
 */
int game_link_get_object_inventory_grant(const GameLink *link, uint16_t object_index,
                                         GameInventory *out_grant,
                                         char *error, size_t error_size);

/* Fixed 40-byte labels have no NUL terminator in the shipped game link. */
int game_link_copy_level_name(const GameLink *link, uint16_t level_index,
                              char *out_text, size_t out_text_size,
                              char *error, size_t error_size);
int game_link_copy_object_name(const GameLink *link, uint16_t object_index,
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
