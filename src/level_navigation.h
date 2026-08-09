#ifndef AB3D2_LEVEL_NAVIGATION_H
#define AB3D2_LEVEL_NAVIGATION_H

#include <stddef.h>
#include <stdint.h>

#include "asset_io.h"

/* objectmove.s:GetNextCPt indexes both twolev maps as 100 by 100 bytes. */
enum {
    LEVEL_NAVIGATION_CONTROL_POINT_LIMIT = 100,
    LEVEL_NAVIGATION_MAP_BYTES = LEVEL_NAVIGATION_CONTROL_POINT_LIMIT *
        LEVEL_NAVIGATION_CONTROL_POINT_LIMIT
};

typedef struct {
    const uint8_t *walk_links;
    size_t walk_links_size;
    const uint8_t *fly_links;
    size_t fly_links_size;
} LevelNavigation;

typedef struct {
    uint8_t next_control_point;
    /* `objectmove.s:GetNextCPt` stores `sne`: zero or the source byte $ff. */
    uint8_t only_see;
} LevelNavigationLink;

int level_navigation_init(const AssetBlob *walk_map, const AssetBlob *fly_map,
                          LevelNavigation *out_navigation,
                          char *error, size_t error_size);
int level_navigation_get_next(const LevelNavigation *navigation,
                              uint8_t current_control_point,
                              uint8_t target_control_point, int use_fly_map,
                              LevelNavigationLink *out_link,
                              char *error, size_t error_size);

#endif
