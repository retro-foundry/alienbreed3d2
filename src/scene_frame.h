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
    /*
     * Smooth presentation value derived from the active source brightness
     * tables.  It is deliberately not an Amiga palette-row index: the GPU
     * converts this continuous source value to lighting in its forward pass.
     */
    int16_t source_light_level;
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
    /* draw-zone stream identity retained for source-light diagnostics. */
    uint16_t source_zone_index;
    uint8_t source_upper_zone;
    uint8_t reserved;
    uint32_t flags;
} SceneGeometry;

/*
 * `hires.s:donetalking` owns these tables.  Geometry receives its sampled
 * values per vertex, while retaining the raw tables here lets a future GPU
 * backend reproduce more of the source interpolation without touching game
 * simulation state.
 */
typedef struct {
    const int16_t *current_point_brightness;
    uint16_t point_zone_capacity;
    uint16_t point_brightness_count;
    const int16_t (*zone_brightness)[2];
    uint16_t zone_count;
} SceneLighting;

/* `newanims.s:Draw_SkyBackdrop` and `DoWaterAnims` source presentation state. */
typedef struct {
    uint8_t sky_enabled;
    uint8_t water_frame;
    uint16_t reserved;
    uint32_t water_scroll;
    const uint8_t *backdrop_bytes;
    size_t backdrop_byte_count;
    const uint8_t *water_bytes;
    size_t water_byte_count;
    const uint8_t *source_display_palette_bytes;
    size_t source_display_palette_byte_count;
} SceneEnvironment;

/* objdrawhires.s takes one of these three source paths for an ObjT slot. */
typedef enum {
    SCENE_SPRITE_SOURCE_OBJECT_BITMAP,
    SCENE_SPRITE_SOURCE_VECTOR_MODEL,
    SCENE_SPRITE_SOURCE_GLARE_BITMAP
} SceneSpriteSource;

typedef enum {
    SCENE_SPRITE_PRESENTATION_WORLD_OBJECT,
    /* hires.s:Plr1_Use's live ENT_NEXT_2 companion object. */
    SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON
} SceneSpritePresentation;

/*
 * Fixed ObjT records use ODefT_FloorCeiling_w to place their source origin on
 * the selected sector surface.  Publishing this independently of the packed
 * bitmap lets a 3D backend put the full image above/below that surface instead
 * of inheriting the software renderer's screen-space crop.
 */
typedef enum {
    SCENE_SPRITE_SURFACE_FREE,
    SCENE_SPRITE_SURFACE_FLOOR,
    SCENE_SPRITE_SURFACE_CEILING
} SceneSpriteSurfaceAttachment;

enum {
    /* objdrawhires.s:draw_Bitmap's byte-10 render controls. */
    SCENE_SPRITE_FLAG_FLIP_HORIZONTAL = 1u << 0,
    SCENE_SPRITE_FLAG_LIGHT_PALETTE = 1u << 1,
    SCENE_SPRITE_FLAG_ADDITIVE = 1u << 2,
    /* ObjT/ShotT byte 63. A backend may use it without treating it as PVS. */
    SCENE_SPRITE_FLAG_UPPER_ZONE = 1u << 3,
    /*
     * defs.i:OBJ_TYPE_PROJECTILE. The source draws its projectile frames
     * after the room's software columns; a depth-buffer presenter retains
     * this identity to prevent a contact effect from disappearing into the
     * surface that spawned it.
     */
    SCENE_SPRITE_FLAG_PROJECTILE = 1u << 4
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
 * culling, draw order, asset conversion, and upload. `source_aux_bytes` is
 * the matching bitmap PTR data. Ordinary/glare bitmap frames use its packed
 * 5-bit WAD-column modes; `draw_bitmap_lighted` instead uses a direct 8-bit
 * WAD column, its two live angle-brightness rings, and data/draw_data.s's
 * `guff` curve to construct a selected 256-byte light palette. Vector sprites
 * carry `Draw_TextureMapsPtr` bytes, `Draw_TexturePalettePtr`'s light rows,
 * and the display palette for their original face-map colours; glare sprites
 * carry the shared texture palette.
 */
typedef struct {
    SceneWorldPoint position;
    SceneSpriteSource source;
    SceneSpritePresentation presentation;
    SceneSpriteSurfaceAttachment surface_attachment;
    uint32_t source_asset_id;
    uint32_t source_record_id;
    uint16_t frame_index;
    uint16_t yaw;
    uint16_t source_brightness;
    int16_t source_light_level;
    /* draw_ResetAngleBrights' lower/upper 16-direction source rings for a
     * draw_bitmap_lighted object. Unused sprite modes leave these as zero. */
    int8_t source_bitmap_angle_brightness[16u * 2u];
    /*
     * objdrawhires.s:draw_CalcBrightRings builds this 16-by-16 directional
     * point/polygon light field for every vector model.  The values retain
     * the source byte representation; the GPU consumes them as live lighting
     * state rather than selecting a pre-lit palette texture.
     */
    int8_t source_point_and_polygon_brightness[16u * 16u];
    uint16_t source_zone_index;
    /*
     * The active lower/upper source-sector span in 8.8 down-positive Y
     * units. Bitmap paths preserve Draw_Objects' packed-frame clip; complete
     * 3D paths use the same live bounds to keep world object geometry from
     * extending through opaque floor and ceiling geometry.
     */
    int32_t source_clip_top_y;
    int32_t source_clip_bottom_y;
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
    const uint8_t *source_light_palette_bytes;
    size_t source_light_palette_byte_count;
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
    SCENE_COMMAND_LIGHTING,
    SCENE_COMMAND_ENVIRONMENT,
    SCENE_COMMAND_MATERIAL,
    SCENE_COMMAND_GEOMETRY,
    SCENE_COMMAND_SPRITE,
    SCENE_COMMAND_HUD_TEXT
} SceneCommandType;

typedef struct {
    SceneCommandType type;
    union {
        SceneCamera camera;
        SceneLighting lighting;
        SceneEnvironment environment;
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
    /*
     * Snapshot/presentation frames own copies of mutable geometry vertices.
     * Ordinary source-submitted frames continue borrowing level-owned vertex
     * arrays for the duration of one source frame.
     */
    SceneVertex *owned_vertices;
    size_t owned_vertex_count;
    size_t owned_vertex_capacity;
} SceneFrame;

int scene_frame_init(SceneFrame *frame, size_t command_capacity);
void scene_frame_destroy(SceneFrame *frame);
void scene_frame_begin(SceneFrame *frame);
int scene_frame_reserve(SceneFrame *frame, size_t command_capacity);
int scene_frame_submit(SceneFrame *frame, const SceneCommand *command);

/*
 * Copy a source frame into an independently retained presentation snapshot.
 * Asset/table pointers remain source-owned; mutable geometry vertices are
 * copied so a later source VBlank cannot alter the retained endpoint.
 */
int scene_frame_clone(SceneFrame *destination, const SceneFrame *source);

/*
 * Blend two completed source-frame snapshots for one host presentation frame.
 * The `current` frame supplies all non-continuous source state. Camera,
 * matching geometry, and matching source object records interpolate only
 * values that represent a continuous source state. A spawned, removed, or
 * structurally changed command is deliberately left at its current endpoint.
 */
int scene_frame_interpolate(SceneFrame *destination, const SceneFrame *previous,
                            const SceneFrame *current, float alpha);

#endif
