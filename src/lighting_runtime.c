#include "lighting_runtime.h"

#include <stdio.h>
#include <string.h>

enum {
    LIGHTING_RUNTIME_LOWER_BRIGHTNESS = 0u,
    LIGHTING_RUNTIME_UPPER_BRIGHTNESS = 1u,
    LIGHTING_RUNTIME_ANIMATION_INTERVAL = 5u,
    LIGHTING_RUNTIME_ANIMATION_END = 999
};

/* newanims.s:anim_BrightPulse1_vw through anim_BrightFlicker2_vw. */
static const int16_t lighting_runtime_animation_1[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
    LIGHTING_RUNTIME_ANIMATION_END
};
static const int16_t lighting_runtime_animation_2[] = {
    9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
    1, 2, 3, 4, 5, 6, 7, 8, LIGHTING_RUNTIME_ANIMATION_END
};
static const int16_t lighting_runtime_animation_3[] = {
    17, 18, 19, 20,
    20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    LIGHTING_RUNTIME_ANIMATION_END
};
static const int16_t lighting_runtime_animation_4[] = {
    16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    20, 19, 18, 17, LIGHTING_RUNTIME_ANIMATION_END
};
static const int16_t lighting_runtime_animation_5[] = {
    8, 7, 6, 5, 4, 3, 2, 1,
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9,
    LIGHTING_RUNTIME_ANIMATION_END
};
static const int16_t lighting_runtime_animation_6[] = {
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 1,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 1,
    20, 20, 20, 20, 20, 1, LIGHTING_RUNTIME_ANIMATION_END
};
static const int16_t lighting_runtime_animation_7[] = {
    -10, -9, -6, -10, -6, -5, -5, -7, -5, -10, -9, -8, -7, -5, -5, -5, -5,
    -5, -5, -5, -5, -6, -7, -8, -9, -5, -10, -9, -10, -6, -5, -5, -5, -5,
    -5, -5, -5, LIGHTING_RUNTIME_ANIMATION_END
};

static const int16_t *const lighting_runtime_animation_sequences[] = {
    lighting_runtime_animation_1,
    lighting_runtime_animation_2,
    lighting_runtime_animation_3,
    lighting_runtime_animation_4,
    lighting_runtime_animation_5,
    lighting_runtime_animation_6,
    lighting_runtime_animation_7
};

static void lighting_runtime_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int16_t lighting_runtime_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t lighting_runtime_neg16(int16_t value)
{
    return (int16_t)(UINT16_C(0) - (uint16_t)value);
}

static int16_t lighting_runtime_asr16(int16_t value, unsigned int count)
{
    if (value >= 0) {
        return (int16_t)((uint16_t)value >> count);
    }
    return (int16_t)-(((int32_t)-value + ((INT32_C(1) << count) - 1)) >> count);
}

static int32_t lighting_runtime_asr32(int32_t value, unsigned int count)
{
    if (value >= 0) {
        return value >> count;
    }
    return (int32_t)-(((int64_t)-value + ((INT64_C(1) << count) - 1)) >> count);
}

static int lighting_runtime_get_animation_value(const LightingRuntime *runtime,
                                                uint16_t source_index,
                                                int16_t *out_value,
                                                char *error, size_t error_size)
{
    if (source_index == 0u || source_index > LIGHTING_RUNTIME_ANIMATION_VALUE_COUNT) {
        lighting_runtime_set_error(error, error_size,
                                   "source brightness animation index is outside Anim_BrightTable");
        return 0;
    }
    *out_value = runtime->animation_values[source_index - 1u];
    return 1;
}

static int lighting_runtime_scale_zone_brightness(const LightingRuntime *runtime,
                                                  int16_t encoded_brightness,
                                                  int16_t *out_brightness,
                                                  char *error, size_t error_size)
{
    int16_t source_brightness = encoded_brightness;
    uint16_t animation_index;
    int32_t scaled;

    /* hires.s:donetalking's ZoneT_Brightness_w / ZoneT_UpperBrightness_w path. */
    if (source_brightness >= 0) {
        animation_index = (uint16_t)source_brightness >> 8u;
        if (animation_index != 0u &&
            !lighting_runtime_get_animation_value(runtime, animation_index,
                                                 &source_brightness, error, error_size)) {
            return 0;
        }
    }
    scaled = lighting_runtime_asr32((int32_t)source_brightness * 410, 8u);
    *out_brightness = (int16_t)scaled;
    return 1;
}

static int lighting_runtime_refresh_point_brightness(const LightingRuntime *runtime,
                                                     int16_t encoded_brightness,
                                                     int16_t *out_brightness,
                                                     char *error, size_t error_size)
{
    int16_t source_brightness = (int8_t)encoded_brightness;
    int16_t animation_value;
    int16_t interpolation;
    int16_t result;
    uint16_t animation_byte;
    uint16_t animation_index;
    uint16_t interpolation_scale;
    int32_t scaled;

    /* hires.s:allinzone preserves tst.b/EXT.W, including negative low bytes. */
    if ((int8_t)encoded_brightness >= 0) {
        animation_byte = (uint16_t)encoded_brightness >> 8u;
        if (animation_byte != 0u) {
            animation_index = animation_byte & 0x0fu;
            interpolation_scale = (animation_byte >> 4u) + 1u;
            if (!lighting_runtime_get_animation_value(runtime, animation_index,
                                                     &animation_value, error, error_size)) {
                return 0;
            }
            interpolation = (int16_t)((uint16_t)animation_value -
                                      (uint16_t)source_brightness);
            interpolation = lighting_runtime_asr16(
                (int16_t)((int32_t)interpolation * (int32_t)interpolation_scale), 4u);
            source_brightness = lighting_runtime_add16(source_brightness, interpolation);
        }
    }
    scaled = lighting_runtime_asr32((int32_t)source_brightness * 397, 8u);
    result = (int16_t)scaled;
    if (scaled < 0) {
        result = lighting_runtime_add16(result, -600);
    }
    *out_brightness = lighting_runtime_add16(result, 300);
    return 1;
}

static int lighting_runtime_add_current_point_brightness(LightingRuntime *runtime,
                                                         uint32_t world_point_index,
                                                         uint32_t component_index,
                                                         int16_t brightness_change,
                                                         char *error, size_t error_size)
{
    size_t word_index = (size_t)world_point_index * 4u + component_index;
    size_t word_capacity = (size_t)LIGHTING_RUNTIME_POINT_ZONE_CAPACITY *
        LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT;
    int16_t *point_words = &runtime->current_point_brightness[0u][0u];

    if (component_index >= 4u || word_index >= word_capacity) {
        lighting_runtime_set_error(error, error_size,
                                   "Flash world point is outside CurrentPointBrights_vl");
        return 0;
    }
    point_words[word_index] = lighting_runtime_add16(point_words[word_index], brightness_change);
    return 1;
}

void lighting_runtime_init(LightingRuntime *runtime)
{
    if (runtime) {
        /* BSS is clear; newanims.s pointer tables initially address each sequence head. */
        memset(runtime, 0, sizeof(*runtime));
    }
}

void lighting_runtime_vblank(LightingRuntime *runtime)
{
    if (runtime) {
        runtime->animation_timer = lighting_runtime_add16(runtime->animation_timer, -1);
    }
}

int lighting_runtime_refresh_single_player(LightingRuntime *runtime,
                                           const LevelRuntime *level,
                                           PlayerRuntime *player,
                                           char *error, size_t error_size)
{
    uint32_t visible_entry_index;
    int16_t brightness_sum = 0;
    uint16_t marker_count = 0u;

    if (!runtime || !level || !player || player->zone_index >= level->zone_count ||
        level->zone_count > LIGHTING_RUNTIME_POINT_ZONE_CAPACITY) {
        lighting_runtime_set_error(error, error_size,
                                   "source lighting runtime received an unsupported player or zone state");
        return 0;
    }

    /* hires.s:donetalking loops the player's ZoneT_PotVisibleZoneList_vw. */
    for (visible_entry_index = 0u; ; ++visible_entry_index) {
        LevelPotentialVisibility visible_zone;
        LevelZone zone;

        if (visible_entry_index > level->zone_count) {
            lighting_runtime_set_error(error, error_size,
                                       "source lighting PVST list has no negative terminator");
            return 0;
        }
        if (!level_runtime_get_zone_potential_visibility(
                level, player->zone_index, visible_entry_index, &visible_zone,
                error, error_size)) {
            return 0;
        }
        if (visible_zone.zone_index < 0) {
            break;
        }
        if ((uint16_t)visible_zone.zone_index >= level->zone_count ||
            (uint16_t)visible_zone.zone_index >= LIGHTING_RUNTIME_ZONE_BRIGHTNESS_CAPACITY ||
            !level_runtime_get_zone(level, (uint16_t)visible_zone.zone_index, &zone,
                                    error, error_size) ||
            !lighting_runtime_scale_zone_brightness(
                runtime, (int16_t)zone.brightness,
                &runtime->zone_brightness[(uint16_t)visible_zone.zone_index]
                                         [LIGHTING_RUNTIME_LOWER_BRIGHTNESS],
                error, error_size) ||
            !lighting_runtime_scale_zone_brightness(
                runtime, (int16_t)zone.upper_brightness,
                &runtime->zone_brightness[(uint16_t)visible_zone.zone_index]
                                         [LIGHTING_RUNTIME_UPPER_BRIGHTNESS],
                error, error_size)) {
            return 0;
        }
        for (uint16_t point_index = 0u;
             point_index < LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT; ++point_index) {
            int16_t source_brightness;

            if (!level_runtime_get_point_brightness(
                    level, (uint16_t)visible_zone.zone_index, point_index,
                    &source_brightness, error, error_size) ||
                !lighting_runtime_refresh_point_brightness(
                    runtime, source_brightness,
                    &runtime->current_point_brightness[(uint16_t)visible_zone.zone_index]
                                                     [point_index],
                    error, error_size)) {
                return 0;
            }
        }
    }

    /* hires.s:whythehell reads marker presence but averages the first ten current words. */
    for (uint16_t marker_index = 0u;
         marker_index < LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT; ++marker_index) {
        int16_t marker;
        int16_t brightness;

        if (!level_runtime_get_zone_border_point(level, player->zone_index, marker_index,
                                                  &marker, error, error_size)) {
            return 0;
        }
        if (marker < 0) {
            break;
        }
        brightness = runtime->current_point_brightness[player->zone_index][marker_index];
        if (brightness < 0) {
            brightness = lighting_runtime_neg16(brightness);
        }
        brightness_sum = lighting_runtime_add16(brightness_sum, brightness);
        ++marker_count;
    }
    if (marker_count == 0u) {
        lighting_runtime_set_error(error, error_size,
                                   "source player zone has no room-brightness border markers");
        return 0;
    }
    player->room_brightness = lighting_runtime_add16(
        (int16_t)((int32_t)brightness_sum / (int32_t)marker_count), -300);
    return 1;
}

void lighting_runtime_advance_animation(LightingRuntime *runtime)
{
    if (!runtime || runtime->animation_timer > 0) {
        return;
    }
    /* newanims.s:brightanim advances each list, restarting only after its 999 word. */
    for (uint16_t animation_index = 0u;
         animation_index < LIGHTING_RUNTIME_ANIMATION_COUNT; ++animation_index) {
        const int16_t *sequence = lighting_runtime_animation_sequences[animation_index];
        uint16_t cursor = runtime->animation_cursors[animation_index];
        int16_t value = sequence[cursor];

        if (value == LIGHTING_RUNTIME_ANIMATION_END) {
            cursor = 0u;
            value = sequence[cursor];
        }
        runtime->animation_cursors[animation_index] = (uint16_t)(cursor + 1u);
        runtime->animation_values[animation_index] = value;
    }
    runtime->animation_timer = LIGHTING_RUNTIME_ANIMATION_INTERVAL;
}

int lighting_runtime_flash(LightingRuntime *runtime, const LevelRuntime *level,
                           uint16_t zone_index, int16_t brightness_change,
                           char *error, size_t error_size)
{
    uint32_t list_index;

    if (!runtime || !level || zone_index >= level->zone_count ||
        zone_index >= LIGHTING_RUNTIME_ZONE_BRIGHTNESS_CAPACITY ||
        level->zone_count > LIGHTING_RUNTIME_POINT_ZONE_CAPACITY) {
        lighting_runtime_set_error(error, error_size, "Flash received invalid source lighting state");
        return 0;
    }
    /* newanims.s:Flash clamps only values at or below -20. */
    if (brightness_change <= -20) {
        brightness_change = -20;
    }
    for (list_index = 0u; ; ++list_index) {
        int16_t point_index;

        if (list_index > level->world_point_count) {
            lighting_runtime_set_error(error, error_size,
                                       "Flash ZoneT point list has no negative terminator");
            return 0;
        }
        if (!level_runtime_get_zone_point_index(level, zone_index, list_index, &point_index,
                                                error, error_size)) {
            return 0;
        }
        if (point_index < 0) {
            break;
        }
        if ((uint32_t)point_index >= level->world_point_count ||
            !lighting_runtime_add_current_point_brightness(
                runtime, (uint16_t)point_index, 0u, brightness_change, error, error_size) ||
            !lighting_runtime_add_current_point_brightness(
                runtime, (uint16_t)point_index, 1u, brightness_change, error, error_size)) {
            return 0;
        }
    }
    runtime->zone_brightness[zone_index][LIGHTING_RUNTIME_LOWER_BRIGHTNESS] =
        lighting_runtime_add16(runtime->zone_brightness[zone_index]
                                                       [LIGHTING_RUNTIME_LOWER_BRIGHTNESS],
                               brightness_change);
    runtime->zone_brightness[zone_index][LIGHTING_RUNTIME_UPPER_BRIGHTNESS] =
        lighting_runtime_add16(runtime->zone_brightness[zone_index]
                                                       [LIGHTING_RUNTIME_UPPER_BRIGHTNESS],
                               brightness_change);

    /* newanims.s:doemall follows the source zone's complete PVST list. */
    for (list_index = 0u; ; ++list_index) {
        LevelPotentialVisibility visible_zone;
        uint16_t visible_zone_index;

        if (list_index > level->zone_count) {
            lighting_runtime_set_error(error, error_size,
                                       "Flash PVST list has no negative terminator");
            return 0;
        }
        if (!level_runtime_get_zone_potential_visibility(level, zone_index, list_index,
                                                         &visible_zone, error, error_size)) {
            return 0;
        }
        if (visible_zone.zone_index < 0) {
            break;
        }
        visible_zone_index = (uint16_t)visible_zone.zone_index;
        if (visible_zone_index >= level->zone_count ||
            visible_zone_index >= LIGHTING_RUNTIME_ZONE_BRIGHTNESS_CAPACITY) {
            lighting_runtime_set_error(error, error_size,
                                       "Flash PVST entry is outside Zone_BrightTable_vl");
            return 0;
        }
        runtime->zone_brightness[visible_zone_index][LIGHTING_RUNTIME_LOWER_BRIGHTNESS] =
            lighting_runtime_add16(runtime->zone_brightness[visible_zone_index]
                                                           [LIGHTING_RUNTIME_LOWER_BRIGHTNESS],
                                   brightness_change);
        runtime->zone_brightness[visible_zone_index][LIGHTING_RUNTIME_UPPER_BRIGHTNESS] =
            lighting_runtime_add16(runtime->zone_brightness[visible_zone_index]
                                                           [LIGHTING_RUNTIME_UPPER_BRIGHTNESS],
                                   brightness_change);
    }
    return 1;
}
