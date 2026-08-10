#ifndef AB3D2_SCENE_FRAME_H
#define AB3D2_SCENE_FRAME_H

#include <stddef.h>
#include <stdint.h>

/*
 * GPU-neutral producer contract. It deliberately contains no SDL, Amiga
 * bitplane, C2P, palette-raster, or graphics-API state. The command data is
 * owned by the simulation until the consumer finishes the frame.
 */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
} SceneWorldPoint;

typedef struct {
    SceneWorldPoint position;
    uint16_t yaw;
    int16_t look_offset;
} SceneCamera;

typedef enum {
    /* modules/res.s:Res_LoadWallTextures, indexed by Draw_Wall's texture word. */
    SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE,
    /* modules/res.s:Res_LoadLevelData optional wall_N.256wad replacement. */
    SCENE_MATERIAL_SOURCE_LEVEL_WALL_TEXTURE_OVERRIDE,
    /* Res_LoadFloorsAndTextures/floortile, addressed by Draw_Flats byte offset. */
    SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE,
    /* Res_LoadLevelData's optional per-level floortile replacement. */
    SCENE_MATERIAL_SOURCE_LEVEL_FLOOR_TEXTURE_OVERRIDE
} SceneMaterialSource;

typedef struct {
    SceneMaterialSource source;
    uint32_t source_asset_id;
    /* Exact selected source asset; conversion/upload remains backend-owned. */
    const uint8_t *source_bytes;
    size_t source_byte_count;
    /*
     * Exact palette/shade-table asset selected by the source renderer. For a
     * wall this is the 2,048-byte prefix of source_bytes; for a flat it is the
     * shared texture-palette asset loaded by Res_LoadFloorsAndTextures.
     */
    const uint8_t *source_palette_bytes;
    size_t source_palette_byte_count;
    /* data/draw_data.s:draw_Palette_vw (256 big-endian RGB triplets). */
    const uint8_t *source_display_palette_bytes;
    size_t source_display_palette_byte_count;
} SceneMaterial;

typedef struct {
    SceneWorldPoint position;
    /* Exact source texel coordinates, consumed with SceneTextureWindow. */
    int32_t texture_u;
    int32_t texture_v;
} SceneVertex;

typedef enum {
    SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST,
    /* Source polygon boundary order; the GPU backend must triangulate safely. */
    SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY
} SceneGeometryTopology;

typedef enum {
    SCENE_GEOMETRY_PRIMITIVE_WALL,
    SCENE_GEOMETRY_PRIMITIVE_FLOOR,
    SCENE_GEOMETRY_PRIMITIVE_CEILING,
    SCENE_GEOMETRY_PRIMITIVE_WATER
} SceneGeometryPrimitive;

/*
 * The source texture region required by a primitive. Walls use the original
 * packed-strip window; flats use their fixed 64x64 logical tile and leave the
 * fields zero. The coordinates in SceneVertex remain source texel coordinates.
 */
typedef struct {
    uint16_t u_offset;
    uint16_t u_period;
    uint16_t v_period;
} SceneTextureWindow;

/* Vertices are owned by the scene producer until the frame ends. */
typedef struct {
    const SceneVertex *vertices;
    uint32_t vertex_count;
    SceneGeometryTopology topology;
    SceneGeometryPrimitive primitive;
    uint32_t material_id;
    uint32_t source_record_id;
    SceneTextureWindow texture_window;
    uint32_t flags;
} SceneGeometry;

/* objdrawhires.s takes one of these three source paths for an ObjT slot. */
typedef enum {
    SCENE_SPRITE_SOURCE_OBJECT_BITMAP,
    SCENE_SPRITE_SOURCE_VECTOR_MODEL,
    SCENE_SPRITE_SOURCE_GLARE_BITMAP
} SceneSpriteSource;

enum {
    /* objdrawhires.s:draw_Bitmap's byte-10 render controls. */
    SCENE_SPRITE_FLAG_FLIP_HORIZONTAL = 1u << 0,
    SCENE_SPRITE_FLAG_LIGHT_PALETTE = 1u << 1,
    SCENE_SPRITE_FLAG_ADDITIVE = 1u << 2,
    /* ObjT/ShotT byte 63. A backend may use it without treating it as PVS. */
    SCENE_SPRITE_FLAG_UPPER_ZONE = 1u << 3
};

/* GLFT_FrameData_l's eight-byte bitmap-frame record. */
typedef struct {
    uint16_t pointer_table_index;
    uint16_t down_strip;
    uint16_t strip_count;
    uint16_t line_count;
} SceneSpriteFrameMetrics;

/*
 * Raw source assets and draw descriptor for one active ObjT record.  This is
 * intentionally unprojected and unsorted: a GPU backend owns projection,
 * culling, draw order, asset conversion, and upload.  `source_aux_bytes` is
 * the matching bitmap PTR data; vector and glare paths leave unsupported
 * fields zero/null rather than emulating the Amiga rasterizer.
 */
typedef struct {
    SceneWorldPoint position;
    SceneSpriteSource source;
    uint32_t source_asset_id;
    uint32_t source_record_id;
    uint16_t frame_index;
    uint16_t yaw;
    uint16_t source_brightness;
    int16_t source_aux_offset_x;
    int16_t source_aux_offset_y;
    uint8_t source_width;
    uint8_t source_height;
    uint8_t source_effect;
    uint8_t flags;
    SceneSpriteFrameMetrics frame_metrics;
    const uint8_t *source_bytes;
    size_t source_byte_count;
    const uint8_t *source_aux_bytes;
    size_t source_aux_byte_count;
    const uint8_t *source_palette_bytes;
    size_t source_palette_byte_count;
    const uint8_t *source_display_palette_bytes;
    size_t source_display_palette_byte_count;
} SceneSprite;

typedef struct {
    /* Exact source bytes; consumers must not require a trailing NUL. */
    const char *text;
    uint16_t text_byte_count;
    int16_t x;
    int16_t y;
    uint32_t style_id;
} SceneHudText;

typedef enum {
    SCENE_COMMAND_CAMERA,
    SCENE_COMMAND_MATERIAL,
    SCENE_COMMAND_GEOMETRY,
    SCENE_COMMAND_SPRITE,
    SCENE_COMMAND_HUD_TEXT
} SceneCommandType;

typedef struct {
    SceneCommandType type;
    union {
        SceneCamera camera;
        SceneMaterial material;
        SceneGeometry geometry;
        SceneSprite sprite;
        SceneHudText hud_text;
    } data;
} SceneCommand;

typedef struct {
    SceneCommand *commands;
    size_t count;
    size_t capacity;
} SceneFrame;

int scene_frame_init(SceneFrame *frame, size_t command_capacity);
void scene_frame_destroy(SceneFrame *frame);
void scene_frame_begin(SceneFrame *frame);
int scene_frame_reserve(SceneFrame *frame, size_t command_capacity);
int scene_frame_submit(SceneFrame *frame, const SceneCommand *command);

#endif
