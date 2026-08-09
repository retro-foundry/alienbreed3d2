#ifndef AB3D2_LEVEL_RUNTIME_H
#define AB3D2_LEVEL_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "level_bootstrap.h"

/*
 * Non-owning native view of the Game_Begin table resolution. It intentionally
 * excludes the PVST list: the PC renderer draws complete loaded levels and
 * does not use the Amiga PVS/portal traversal as a rendering prerequisite.
 */
typedef struct {
    const uint8_t *level_bytes;
    size_t level_size;
    const uint8_t *graphics_bytes;
    size_t graphics_size;
    uint32_t control_point_coordinates_offset;
    uint16_t control_point_count;
    /* TLBT_NumPoints is the inclusive final Vec2W index used by DBRA. */
    uint32_t world_point_count;
    uint32_t world_points_offset;
    uint32_t point_brightness_offset;
    uint32_t zone_border_points_offset;
    uint32_t zone_graph_adds_offset;
    uint32_t zone_offsets_table_offset;
    uint32_t object_data_offset;
    uint32_t player_shot_offset;
    uint32_t alien_shot_offset;
    uint32_t object_points_offset;
    uint32_t player1_object_offset;
    uint32_t player2_object_offset;
    /* TLBT_NumObjects is the inclusive last object-point index. */
    uint32_t object_point_count;
    /* newanims.s:ObjectHandler's 64-byte ObjT list stops at -1. */
    uint32_t object_record_count;
    /* hires.s:Game_Begin stores TLBT_ObjectDataOffset - TLBT_FloorLineOffset. */
    int32_t edge_data_span;
    uint32_t edge_table_offset;
    /*
     * One past the highest EdgeT referenced by a zone's normal collision
     * sequence. Game_Begin's byte difference is not a usable table length in
     * shipped data, so this is intentionally not claimed as a full-table
     * count.
     */
    uint32_t edge_count;
    int16_t exit_zone_id;
    uint16_t zone_count;
} LevelRuntime;

/* defs.i:ZoneT, represented as native values but retaining source offsets. */
typedef struct {
    uint16_t id;
    int32_t floor;
    int32_t roof;
    int32_t upper_floor;
    int32_t upper_roof;
    int32_t water;
    uint16_t brightness;
    uint16_t upper_brightness;
    uint16_t control_point;
    uint16_t background_sfx_mask;
    int16_t edge_list_relative_offset;
    int16_t points_relative_offset;
    uint8_t draw_backdrop;
    uint8_t echo;
    int16_t teleport_zone;
    int16_t teleport_x;
    int16_t teleport_z;
    uint16_t floor_noise;
    uint16_t upper_floor_noise;
} LevelZone;

/*
 * TLGT_ZoneGraphAddsOffset_l entry used by draw_zone_graph.s. The lower
 * stream is always present; a zero upper-stream offset is the source's
 * sentinel for a zone without an upper draw stream.
 */
typedef struct {
    uint32_t lower_stream_offset;
    uint32_t upper_stream_offset;
    int16_t lower_zone_id;
    int16_t upper_zone_id;
    uint8_t has_upper_stream;
} LevelDrawGraphStreams;

/*
 * defs.i:EdgeT native read view. These are collision/gameplay edges; they do
 * not imply a renderer visibility or portal traversal policy.
 */
typedef struct {
    int16_t x;
    int16_t z;
    int16_t x_length;
    int16_t z_length;
    int16_t join_zone_id;
    int16_t unknown_word;
    int8_t unknown_byte_12;
    int8_t unknown_byte_13;
    uint16_t flags;
} LevelEdge;

/*
 * hires.s:Game_Begin establishes this eight-byte record sequence immediately
 * after TLBT. modules/ai.s consumes x/z at +0/+2 and target height at +4;
 * byte pair +6 is not named by the maintained source.
 */
typedef struct {
    int16_t x;
    int16_t z;
    int16_t height;
    int16_t unknown_word;
} LevelControlPoint;

/* Lvl_PointsPtr_l entries are source Vec2W pairs used by draw and transform. */
typedef struct {
    int16_t x;
    int16_t z;
} LevelWorldPoint;

/*
 * The leading fixed text blocks in twolev.bin. newaliencontrol.s and ai.s
 * pass these exact 160-byte payloads to Msg_PushLine with an explicit length.
 */
typedef struct {
    const uint8_t *bytes;
    size_t byte_count;
} LevelNarrativeMessage;

/*
 * defs.i:ObjT native read view. The source reuses its 64-byte slots at
 * runtime; in the loaded object list, word zero is the object-point index and
 * a negative value is the list terminator (newanims.s:ObjectHandler).
 */
typedef struct {
    uint16_t point_index;
    int16_t zone_id;
    uint8_t type_id;
    uint8_t sees_player;
} LevelObjectSlot;

/* Lvl_ObjectPointsPtr_l entries are pairs of source 32-bit coordinates. */
typedef struct {
    int32_t x;
    int32_t z;
} LevelObjectPoint;

int level_runtime_init(const AssetBlob *level_data, const AssetBlob *graphics_data,
                       const LevelBootstrap *level,
                       const LevelGraphicsBootstrap *graphics_header,
                       LevelRuntime *out_runtime,
                       char *error, size_t error_size);
int level_runtime_get_zone(const LevelRuntime *runtime, uint16_t zone_index,
                           LevelZone *out_zone, char *error, size_t error_size);
int level_runtime_get_zone_draw_graph_streams(const LevelRuntime *runtime, uint16_t zone_index,
                                              LevelDrawGraphStreams *out_streams,
                                              char *error, size_t error_size);
/*
 * objectmove.s' normal collision pass consumes non-negative indexes through
 * the first negative list marker. Extended-edge markers are intentionally not
 * folded into this primary list.
 */
int level_runtime_get_zone_edge_count(const LevelRuntime *runtime, uint16_t zone_index,
                                      uint32_t *out_count,
                                      char *error, size_t error_size);
int level_runtime_get_zone_edge_index(const LevelRuntime *runtime, uint16_t zone_index,
                                      uint32_t list_index, uint32_t *out_edge_index,
                                      char *error, size_t error_size);
int level_runtime_get_edge(const LevelRuntime *runtime, uint32_t edge_index,
                           LevelEdge *out_edge, char *error, size_t error_size);
int level_runtime_get_control_point(const LevelRuntime *runtime, uint16_t control_point_index,
                                    LevelControlPoint *out_control_point,
                                    char *error, size_t error_size);
int level_runtime_get_world_point(const LevelRuntime *runtime, uint32_t point_index,
                                  LevelWorldPoint *out_point,
                                  char *error, size_t error_size);
int level_runtime_get_narrative_message(const LevelRuntime *runtime, uint16_t message_index,
                                        LevelNarrativeMessage *out_message,
                                        char *error, size_t error_size);
int level_runtime_get_object_record(const LevelRuntime *runtime, uint32_t record_index,
                                    LevelObjectSlot *out_object,
                                    char *error, size_t error_size);
int level_runtime_get_object_point(const LevelRuntime *runtime, uint32_t point_index,
                                   LevelObjectPoint *out_point,
                                   char *error, size_t error_size);

#endif
