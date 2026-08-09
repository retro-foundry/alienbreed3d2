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
} SceneMaterial;

typedef struct {
    SceneWorldPoint position;
    /* Ignore these fields when SCENE_GEOMETRY_TEXTURE_COORDS_UNRESOLVED is set. */
    int32_t texture_u;
    int32_t texture_v;
} SceneVertex;

enum {
    /* Position/material provenance is known, but source UV mapping is pending. */
    SCENE_GEOMETRY_TEXTURE_COORDS_UNRESOLVED = 1u << 0
};

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

/* Vertices are owned by the scene producer until the frame ends. */
typedef struct {
    const SceneVertex *vertices;
    uint32_t vertex_count;
    SceneGeometryTopology topology;
    SceneGeometryPrimitive primitive;
    uint32_t material_id;
    uint32_t source_record_id;
    uint32_t flags;
} SceneGeometry;

typedef struct {
    SceneWorldPoint position;
    uint32_t sprite_asset_id;
    uint16_t frame_index;
    uint16_t flags;
} SceneSprite;

typedef struct {
    const char *text;
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
