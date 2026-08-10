#ifndef AB3D2_OBJECT_VISIBILITY_H
#define AB3D2_OBJECT_VISIBILITY_H

#include <stddef.h>
#include <stdint.h>

#include "asset_io.h"
#include "level_runtime.h"

/* objectmove.s:Viewerx/Viewerz/Viewery/ViewerTop shared BSS words. */
typedef struct {
    int16_t viewer_x;
    int16_t viewer_z;
    int16_t viewer_y;
    uint8_t viewer_in_upper_zone;
} ObjectVisibilityRuntime;

/* Source process/BSS initialization. Game_Begin does not reset these words. */
void object_visibility_runtime_init(ObjectVisibilityRuntime *runtime);

/* Records the caller's source writes immediately before a CanItBeSeen call. */
void object_visibility_runtime_set_viewer(ObjectVisibilityRuntime *runtime,
                                          int16_t viewer_x, int16_t viewer_z,
                                          int16_t viewer_y,
                                          uint8_t viewer_in_upper_zone);

/* Source callers that write Viewerx/Viewerz/Viewery but deliberately retain ViewerTop. */
void object_visibility_runtime_set_viewer_position(ObjectVisibilityRuntime *runtime,
                                                   int16_t viewer_x, int16_t viewer_z,
                                                   int16_t viewer_y);

/*
 * Source-word inputs consumed by objectmove.s:CanItBeSeen. This is gameplay
 * line-of-sight state: it does not participate in complete-level rendering.
 */
typedef struct {
    uint16_t viewer_zone_index;
    int16_t viewer_x;
    int16_t viewer_z;
    int16_t viewer_y;
    uint8_t viewer_in_upper_zone;
    uint16_t target_zone_index;
    int16_t target_x;
    int16_t target_z;
    int16_t target_y;
    uint8_t target_in_upper_zone;
} ObjectVisibilityQuery;

/*
 * Direct native translation of objectmove.s:CanItBeSeen's PVST, clip, and
 * joined-zone height tests. `out_can_see` is source-style 0 or 0xff.
 */
int object_visibility_can_see(const LevelRuntime *level, const AssetBlob *clips,
                              const ObjectVisibilityQuery *query,
                              uint8_t *out_can_see,
                              char *error, size_t error_size);

#endif
