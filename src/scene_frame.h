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
    /*
     * Host-rate 16.16 presentation endpoints promoted from the accepted
     * source position words. `position` remains the integer source coordinate
     * used by lighting and object projection. Interpolating these promoted
     * endpoints smooths 50 Hz movement without presenting the rejected low
     * word that Plr1_Control deliberately retains after a collision.
     */
    int32_t source_position_x_16_16;
    int32_t source_position_z_16_16;
    uint16_t yaw;
    int16_t look_offset;
    uint8_t has_source_position_16_16;
    uint8_t reserved[3];
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
    /*
     * Presentation-only authored ambient endpoints. `source_light_level`
     * may additionally contain Flash/torch/projectile contributions; keeping
     * the ambient base separate lets those per-tick effects retain the normal
     * completed-frame interpolation while newanims.s:brightanim is blended
     * across its complete five-tick interval.
     */
    int16_t source_ambient_light_level;
    int16_t source_ambient_light_target_level;
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

/* One material/geometry range inside a GPU-neutral source mesh. */
typedef struct {
    SceneMaterial material;
    SceneGeometry geometry;
} SceneMeshSurface;

typedef enum {
    /* Immutable level geometry: cache as a BLAS candidate across frames. */
    SCENE_ACCELERATION_CLASS_STATIC,
    /* Source-tick mutable geometry or an ObjT/ShotT source object. */
    SCENE_ACCELERATION_CLASS_DYNAMIC
} SceneAccelerationClass;

/*
 * GPU-neutral BLAS candidate. `source_mesh_id` identifies source topology and
 * resources rather than an API resource. Static candidates may be cached;
 * dynamic candidates are rebuilt from the current source frame.
 */
typedef struct {
    uint32_t source_mesh_id;
    SceneAccelerationClass acceleration_class;
    const SceneMeshSurface *surfaces;
    uint32_t surface_count;
} SceneMesh;

/* One TLAS-style placement of a world mesh. */
typedef struct {
    uint32_t source_instance_id;
    SceneMesh mesh;
} SceneGeometryInstance;

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
    /*
     * ZoneT+48 PVST flattened as one viewer-zone bit row per source zone.
     * This immutable level topology is renderer-neutral: the Vulkan path
     * uses it as Q2RTX's cluster/PVS light-list authority, while gameplay
     * continues to read the original signed-terminated records directly.
     */
    const uint8_t *zone_potential_visibility;
    uint16_t zone_potential_visibility_stride;
    /* Presentation phase 0..interval-1 for newanims.s:brightanim. */
    uint8_t ambient_animation_phase_tick;
    uint8_t ambient_animation_interval_ticks;
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
    SCENE_SPRITE_FLAG_PROJECTILE = 1u << 4,
    /* newanims.s:ItsABullet's non-zero ShotT_Status_b stationary pop path. */
    SCENE_SPRITE_FLAG_PROJECTILE_CONTACT = 1u << 5
};

/* GLFT_FrameData_l's eight-byte bitmap-frame record. */
typedef struct {
    uint16_t pointer_table_index;
    uint16_t down_strip;
    uint16_t strip_count;
    uint16_t line_count;
} SceneSpriteFrameMetrics;

/*
 * objdrawhires.s:draw_PolygonModel's dedicated ENT_NEXT_2 projection state.
 * The companion is not placed at an invented world-space distance: the
 * source rotates its authored points in view space, adds this Y/depth state,
 * then projects them around the source viewport centre.  Keeping the state
 * explicit lets OpenGL and a future ray-traced backend share the same source
 * model transform without sharing an API-specific camera matrix.
 */
typedef struct {
    int32_t y_offset;
    int16_t sine;
    int16_t cosine;
    int16_t depth_bias;
    uint16_t centre_x;
    uint16_t centre_y;
    uint16_t scale_numerator;
    uint16_t scale_denominator;
} SceneViewWeaponProjection;

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
    /*
     * Presentation-only source-frame blend. hires.s:VBlankInterrupt calls
     * dosomething at 50 Hz and newaliencontrol.s:ACTANIMOBJ selects one
     * discrete companion or alien action frame per source update.  A host
     * presentation between two completed snapshots retains that source
     * cadence and blends only matching vector-model vertices.  This applies
     * to the Player 1 view weapon and world vector objects; source bitmap
     * animation remains discrete authored art.
     */
    uint16_t presentation_previous_frame_index;
    float presentation_frame_interpolation_alpha;
    uint8_t presentation_interpolate_vector_frame;
    /*
     * Nonzero only for world vector alien parts driven by hires.s:DOALLANIMS.
     * This includes an authored OBJ_PREV auxiliary part. It is the number of
     * 50 Hz ticks between pose selections, not a renderer timing constant.
     * SceneVectorPoseHistory retains the preceding compiled frame for that
     * complete source interval.
     */
    uint8_t presentation_vector_frame_interval_ticks;
    /*
     * `newplayershoot.s:firefive` owns the source projectile point and
     * velocity, while `hires.s:Plr1_Use` owns the camera-space companion.
     * Preserve the first source movement vector so the presenter can place
     * the visual path at the companion muzzle without changing ShotT state.
     */
    uint8_t presentation_anchor_to_player_weapon;
    int32_t presentation_projectile_velocity_x_16_16;
    int32_t presentation_projectile_velocity_z_16_16;
    int16_t presentation_projectile_velocity_y;
    uint16_t yaw;
    uint16_t source_brightness;
    int16_t source_light_level;
    SceneViewWeaponProjection view_weapon_projection;
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

/*
 * A source object is a dynamic mesh instance even when OpenGL presents it as
 * a billboard. `source_mesh_id` identifies its source resource/BLAS candidate
 * and `sprite.source_record_id` remains the live ObjT or ShotT/TLAS identity.
 */
typedef struct {
    uint32_t source_mesh_id;
    SceneAccelerationClass acceleration_class;
    SceneSprite sprite;
} SceneSpriteInstance;

enum {
    /* c/message.c's longest authored record; commands retain their own copy. */
    SCENE_HUD_TEXT_CAPACITY = 160u
};

typedef enum {
    /* Alien Breed 3D I fonts/ascii_font printable-ASCII atlas. */
    SCENE_HUD_FONT_FIRST_PORT_ASCII,
    /* Alien Breed 3D I fonts/health_digits.png. */
    SCENE_HUD_FONT_FIRST_PORT_HEALTH_DIGITS,
    /* Alien Breed 3D I fonts/ammo_digits.png. */
    SCENE_HUD_FONT_FIRST_PORT_AMMO_DIGITS
} SceneHudFont;

typedef enum {
    /* Top-left position on the command's renderer-neutral reference canvas. */
    SCENE_HUD_LAYOUT_REFERENCE_POSITION,
    /* Centred after the first-port safe top margin plus y reference pixels. */
    SCENE_HUD_LAYOUT_TOP_CENTER,
    /* Alien Breed 3D I display_hud_stats_sdl_overlay health placement. */
    SCENE_HUD_LAYOUT_FIRST_PORT_HEALTH,
    /* Alien Breed 3D I display_hud_stats_sdl_overlay ammunition placement. */
    SCENE_HUD_LAYOUT_FIRST_PORT_AMMUNITION,
    /* Alien Breed 3D I display_text_layout_in_rect grouped story layout. */
    SCENE_HUD_LAYOUT_FIRST_PORT_LEVEL_TEXT
} SceneHudLayout;

typedef struct {
    /* Retained command-owned bytes; consumers must use text_byte_count. */
    char text[SCENE_HUD_TEXT_CAPACITY];
    uint16_t text_byte_count;
    int16_t x;
    int16_t y;
    uint16_t reference_width;
    uint16_t reference_height;
    uint32_t style_id;
    SceneHudFont font;
    SceneHudLayout layout;
} SceneHudText;

typedef enum {
    /* Suppress world drawing and present retained HUD commands over a clear colour. */
    SCENE_PRESENTATION_TEXT_SCREEN
} ScenePresentationMode;

/*
 * Renderer-neutral full-frame presentation state. A text transition still
 * carries source camera/environment commands for the palette, but a backend
 * presents only the retained HUD commands over this clear colour.
 */
typedef struct {
    ScenePresentationMode mode;
    uint8_t clear_red;
    uint8_t clear_green;
    uint8_t clear_blue;
    uint8_t hud_opacity;
} ScenePresentation;

typedef enum {
    SCENE_COMMAND_CAMERA,
    SCENE_COMMAND_LIGHTING,
    SCENE_COMMAND_ENVIRONMENT,
    /* One static or dynamic mesh instance, never a record-level draw. */
    SCENE_COMMAND_GEOMETRY_INSTANCE,
    /* One live ObjT/ShotT instance, including bitmap/vector/glare paths. */
    SCENE_COMMAND_SPRITE_INSTANCE,
    SCENE_COMMAND_HUD_TEXT,
    SCENE_COMMAND_PRESENTATION
} SceneCommandType;

typedef struct {
    SceneCommandType type;
    union {
        SceneCamera camera;
        SceneLighting lighting;
        SceneEnvironment environment;
        SceneGeometryInstance geometry_instance;
        SceneSpriteInstance sprite_instance;
        SceneHudText hud_text;
        ScenePresentation presentation;
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
    /* Retained mesh-surface descriptors for snapshots and interpolation. */
    SceneMeshSurface *owned_mesh_surfaces;
    size_t owned_mesh_surface_count;
    size_t owned_mesh_surface_capacity;
    /*
     * Completed-frame copies of hires.s:donetalking's mutable lighting BSS.
     * Source submission may borrow the runtime tables; snapshots and the
     * presentation frame own these arrays so both 50 Hz endpoints survive.
     */
    int16_t *owned_point_brightness;
    size_t owned_point_brightness_count;
    size_t owned_point_brightness_capacity;
    int16_t (*owned_zone_brightness)[2];
    size_t owned_zone_brightness_count;
    size_t owned_zone_brightness_capacity;
} SceneFrame;

/*
 * Presentation-only history for slow source vector animation.  Entries are
 * keyed by the stable ObjT record identity; source asset pointers remain
 * producer-owned exactly like SceneSprite assets.
 */
typedef struct {
    uint32_t source_record_id;
    uint32_t source_asset_id;
    const uint8_t *source_bytes;
    size_t source_byte_count;
    uint16_t previous_frame_index;
    uint16_t current_frame_index;
    uint8_t interval_ticks;
    uint8_t elapsed_ticks;
    uint8_t seen;
} SceneVectorPoseHistoryEntry;

typedef struct {
    SceneVectorPoseHistoryEntry *entries;
    size_t count;
    size_t capacity;
} SceneVectorPoseHistory;

int scene_frame_init(SceneFrame *frame, size_t command_capacity);
void scene_frame_destroy(SceneFrame *frame);
void scene_frame_begin(SceneFrame *frame);
int scene_frame_reserve(SceneFrame *frame, size_t command_capacity);
int scene_frame_reserve_mesh_surfaces(SceneFrame *frame, size_t surface_capacity);
int scene_frame_submit(SceneFrame *frame, const SceneCommand *command);
/* Copy exact, not-necessarily-NUL-terminated text into a retained HUD command. */
int scene_hud_text_set(SceneHudText *destination, const void *text, size_t text_byte_count);

/*
 * Reserve producer-owned contiguous surfaces for one SceneMesh. The caller
 * fills every range before submitting its geometry-instance command.
 */
SceneMeshSurface *scene_frame_allocate_mesh_surfaces(SceneFrame *frame,
                                                     uint32_t surface_count);

/*
 * Copy a source frame into an independently retained presentation snapshot.
 * Asset pointers remain source-owned; mutable geometry vertices and raw
 * lighting tables are copied so a later source VBlank cannot alter the
 * retained endpoint.
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

void scene_vector_pose_history_destroy(SceneVectorPoseHistory *history);
void scene_vector_pose_history_reset(SceneVectorPoseHistory *history);
/* Call once after each completed 50 Hz source frame. */
int scene_vector_pose_history_update(SceneVectorPoseHistory *history,
                                     const SceneFrame *source_frame);
/* Call after scene_frame_interpolate, using the same VBlank remainder alpha. */
void scene_vector_pose_history_apply(const SceneVectorPoseHistory *history,
                                     SceneFrame *presentation_frame,
                                     float source_alpha);

#endif
