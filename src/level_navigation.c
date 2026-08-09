#include "level_navigation.h"

#include <stdio.h>
#include <string.h>

static void level_navigation_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

int level_navigation_init(const AssetBlob *walk_map, const AssetBlob *fly_map,
                          LevelNavigation *out_navigation,
                          char *error, size_t error_size)
{
    LevelNavigation navigation;

    if (!walk_map || !walk_map->bytes || !fly_map || !fly_map->bytes || !out_navigation ||
        walk_map->size < LEVEL_NAVIGATION_MAP_BYTES ||
        fly_map->size < LEVEL_NAVIGATION_MAP_BYTES) {
        level_navigation_set_error(error, error_size,
                                   "twolev navigation map is smaller than GetNextCPt's 100x100 table");
        return 0;
    }
    memset(&navigation, 0, sizeof(navigation));
    navigation.walk_links = walk_map->bytes;
    navigation.walk_links_size = walk_map->size;
    navigation.fly_links = fly_map->bytes;
    navigation.fly_links_size = fly_map->size;
    *out_navigation = navigation;
    return 1;
}

int level_navigation_get_next(const LevelNavigation *navigation,
                              uint8_t current_control_point,
                              uint8_t target_control_point, int use_fly_map,
                              LevelNavigationLink *out_link,
                              char *error, size_t error_size)
{
    const uint8_t *links;
    size_t links_size;
    size_t link_offset;
    uint8_t encoded_link;
    LevelNavigationLink link;

    if (!navigation || !out_link ||
        current_control_point >= LEVEL_NAVIGATION_CONTROL_POINT_LIMIT ||
        target_control_point >= LEVEL_NAVIGATION_CONTROL_POINT_LIMIT) {
        level_navigation_set_error(error, error_size,
                                   "GetNextCPt control-point index is outside the source 100x100 map");
        return 0;
    }
    links = use_fly_map ? navigation->fly_links : navigation->walk_links;
    links_size = use_fly_map ? navigation->fly_links_size : navigation->walk_links_size;
    if (!links || links_size < LEVEL_NAVIGATION_MAP_BYTES) {
        level_navigation_set_error(error, error_size, "GetNextCPt map is unavailable");
        return 0;
    }

    memset(&link, 0, sizeof(link));
    if (current_control_point == target_control_point) {
        /* objectmove.s:GetNextCPt exits before reading either map. */
        link.next_control_point = current_control_point;
        *out_link = link;
        return 1;
    }

    /* objectmove.s:GetNextCPt: d0 = current * 100 + target. */
    link_offset = (size_t)current_control_point * LEVEL_NAVIGATION_CONTROL_POINT_LIMIT +
        target_control_point;
    encoded_link = links[link_offset];
    link.next_control_point = (uint8_t)(encoded_link & 0x7fu);
    /* objectmove.s uses sne, so true is the source byte value $ff. */
    link.only_see = (encoded_link & 0x80u) != 0u ? UINT8_MAX : 0u;
    *out_link = link;
    return 1;
}
