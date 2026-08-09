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

typedef struct {
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

/* Triangle-list vertices owned by the scene producer until the frame ends. */
typedef struct {
    const SceneVertex *vertices;
    uint32_t vertex_count;
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
