#include "lighting_runtime.h"

#include <stdio.h>
#include <string.h>

enum {
    LIGHTING_RUNTIME_LOWER_BRIGHTNESS = 0u,
    LIGHTING_RUNTIME_UPPER_BRIGHTNESS = 1u,
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

static int32_t lighting_runtime_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int32_t lighting_runtime_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t lighting_runtime_neg32(int32_t value)
{
    return (int32_t)(UINT32_C(0) - (uint32_t)value);
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
        runtime->lighting_enabled = UINT8_MAX;
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

int lighting_runtime_refresh_all_zones(LightingRuntime *runtime,
                                       const LevelRuntime *level,
                                       char *error, size_t error_size)
{
    if (!runtime || !level || level->zone_count > LIGHTING_RUNTIME_POINT_ZONE_CAPACITY ||
        level->zone_count > LIGHTING_RUNTIME_ZONE_BRIGHTNESS_CAPACITY) {
        lighting_runtime_set_error(error, error_size,
                                   "complete-scene lighting refresh received invalid source state");
        return 0;
    }
    /*
     * `hires.s:donetalking` reaches this allinzone loop through Player 1's
     * PVST. The PC scene intentionally has no PVS/portal submission, so run
     * precisely that point and room-brightness conversion for each source
     * zone before flash/torch updates mutate the live tables later this tick.
     */
    for (uint16_t zone_index = 0u; zone_index < level->zone_count; ++zone_index) {
        LevelZone zone;

        if (!level_runtime_get_zone(level, zone_index, &zone, error, error_size) ||
            !lighting_runtime_scale_zone_brightness(
                runtime, (int16_t)zone.brightness,
                &runtime->zone_brightness[zone_index][LIGHTING_RUNTIME_LOWER_BRIGHTNESS],
                error, error_size) ||
            !lighting_runtime_scale_zone_brightness(
                runtime, (int16_t)zone.upper_brightness,
                &runtime->zone_brightness[zone_index][LIGHTING_RUNTIME_UPPER_BRIGHTNESS],
                error, error_size)) {
            return 0;
        }
        for (uint16_t point_index = 0u;
             point_index < LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT; ++point_index) {
            int16_t source_brightness;

            if (!level_runtime_get_point_brightness(level, zone_index, point_index,
                                                    &source_brightness, error, error_size) ||
                !lighting_runtime_refresh_point_brightness(
                    runtime, source_brightness,
                    &runtime->current_point_brightness[zone_index][point_index],
                    error, error_size)) {
                return 0;
            }
        }
    }
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

int lighting_runtime_prepare_presentation_target(
    const LightingRuntime *runtime, const LightingRuntime *baseline,
    const LevelRuntime *level, LightingRuntime *out_target,
    uint8_t *out_phase_tick, char *error, size_t error_size)
{
    if (!runtime || !baseline || !level || !out_target || !out_phase_tick) {
        lighting_runtime_set_error(
            error, error_size, "brightness-animation presentation state is invalid");
        return 0;
    }
    *out_target = *baseline;
    if (runtime->animation_timer == LIGHTING_RUNTIME_ANIMATION_INTERVAL) {
        /*
         * brightanim ran after allinzone in this completed source tick. The
         * baseline still contains the prior authored value, so the newly
         * published Anim_BrightTable_vw is its target and this is the final
         * fifth of the interval.
         */
        memcpy(out_target->animation_values, runtime->animation_values,
               sizeof(out_target->animation_values));
        *out_phase_tick = LIGHTING_RUNTIME_ANIMATION_INTERVAL - 1u;
    } else {
        for (uint16_t animation_index = 0u;
             animation_index < LIGHTING_RUNTIME_ANIMATION_COUNT; ++animation_index) {
            const int16_t *sequence = lighting_runtime_animation_sequences[animation_index];
            uint16_t cursor = runtime->animation_cursors[animation_index];
            int16_t value = sequence[cursor];

            if (value == LIGHTING_RUNTIME_ANIMATION_END) {
                value = sequence[0u];
            }
            out_target->animation_values[animation_index] = value;
        }
        if (runtime->animation_timer >= 1 &&
            runtime->animation_timer < LIGHTING_RUNTIME_ANIMATION_INTERVAL) {
            *out_phase_tick = (uint8_t)(LIGHTING_RUNTIME_ANIMATION_INTERVAL - 1u -
                                        (uint16_t)runtime->animation_timer);
        } else {
            *out_phase_tick = 0u;
        }
    }
    return lighting_runtime_refresh_all_zones(out_target, level, error, error_size);
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

static int16_t lighting_runtime_abs_difference16(int16_t source, int16_t origin)
{
    int16_t difference = (int16_t)((uint16_t)source - (uint16_t)origin);

    /* The source uses BGT, so a zero difference still takes NEG.W (unchanged). */
    return difference > 0 ? difference : lighting_runtime_neg16(difference);
}

static void lighting_runtime_apply_bright_component(LightingRuntime *runtime,
                                                    uint16_t zone_index,
                                                    uint16_t border_index,
                                                    uint16_t component_index,
                                                    int16_t contribution)
{
    int16_t *target = &runtime->current_point_brightness[zone_index]
                                                       [border_index * 4u + component_index];
    int16_t prior = *target;
    int16_t combined;

    if (prior < 0) {
        prior = lighting_runtime_neg16(prior);
    }
    combined = lighting_runtime_add16(prior, contribution);
    /* newanims.s uses BGE then otherwise writes #300: a lower clamp, not an upper one. */
    *target = combined >= 300 ? combined : 300;
}

static void lighting_runtime_apply_bright_room(LightingRuntime *runtime, const LevelZone *zone,
                                               uint16_t zone_index, uint16_t border_index,
                                               int16_t distance, int16_t brightness,
                                               int32_t vertical_position)
{
    int32_t height_delta;
    int16_t contribution;

    /* newanims.s:room_point_loop lower roof component (+2). */
    if (vertical_position <= zone->floor && vertical_position >= zone->roof) {
        height_delta = lighting_runtime_sub32(zone->roof, vertical_position);
        if (height_delta <= 0) {
            contribution = lighting_runtime_add16(
                lighting_runtime_asr16(
                    lighting_runtime_add16(distance,
                        (int16_t)lighting_runtime_asr32(
                            lighting_runtime_sub32(0, height_delta), 7u)),
                    5u),
                brightness);
            if (contribution < 0) {
                lighting_runtime_apply_bright_component(
                    runtime, zone_index, border_index, 1u, contribution);
            }
        }
        /* newanims.s:room_point_loop lower floor component (+0). */
        height_delta = lighting_runtime_sub32(zone->floor, vertical_position);
        if (height_delta >= 0) {
            contribution = lighting_runtime_add16(
                lighting_runtime_asr16(
                    lighting_runtime_add16(distance,
                        (int16_t)lighting_runtime_asr32(height_delta, 7u)), 5u),
                brightness);
            if (contribution < 0) {
                lighting_runtime_apply_bright_component(
                    runtime, zone_index, border_index, 0u, contribution);
            }
        }
    }
    /* newanims.s:room_point_loop upper-floor (+4) and upper-roof (+6). */
    if (vertical_position <= zone->upper_floor && vertical_position >= zone->upper_roof) {
        height_delta = lighting_runtime_sub32(zone->upper_floor, vertical_position);
        if (height_delta >= 0) {
            contribution = lighting_runtime_add16(
                lighting_runtime_asr16(
                    lighting_runtime_add16(distance,
                        (int16_t)lighting_runtime_asr32(height_delta, 7u)), 5u),
                brightness);
            if (contribution < 0) {
                lighting_runtime_apply_bright_component(
                    runtime, zone_index, border_index, 2u, contribution);
            }
        }
        height_delta = lighting_runtime_sub32(zone->upper_roof, vertical_position);
        if (height_delta <= 0) {
            contribution = lighting_runtime_add16(
                lighting_runtime_asr16(
                    lighting_runtime_add16(distance,
                        (int16_t)lighting_runtime_asr32(
                            lighting_runtime_sub32(0, height_delta), 7u)),
                    5u),
                brightness);
            if (contribution < 0) {
                lighting_runtime_apply_bright_component(
                    runtime, zone_index, border_index, 3u, contribution);
            }
        }
    }
}

int lighting_runtime_brighten_points(LightingRuntime *runtime, const LevelRuntime *level,
                                     int16_t brightness, int16_t x, int16_t z,
                                     int32_t vertical_position, uint16_t zone_index,
                                     char *error, size_t error_size)
{
    uint32_t list_index;

    if (!runtime || !level || zone_index >= level->zone_count ||
        level->zone_count > LIGHTING_RUNTIME_POINT_ZONE_CAPACITY) {
        lighting_runtime_set_error(error, error_size,
                                   "anim_BrightenPoints received invalid source lighting state");
        return 0;
    }
    if (runtime->lighting_enabled == 0u) {
        return 1;
    }
    if (brightness > 0) {
        /* newanims.s:darken_points. */
        for (list_index = 0u; ; ++list_index) {
            int16_t point_index;
            LevelWorldPoint point;
            int16_t distance;
            int16_t contribution;

            if (list_index > level->world_point_count) {
                lighting_runtime_set_error(error, error_size,
                                           "anim_BrightenPoints ZoneT point list has no terminator");
                return 0;
            }
            if (!level_runtime_get_zone_point_index(level, zone_index, list_index, &point_index,
                                                    error, error_size)) {
                return 0;
            }
            if (point_index < 0) {
                return 1;
            }
            if ((uint32_t)point_index >= level->world_point_count ||
                !level_runtime_get_world_point(level, (uint16_t)point_index, &point,
                                               error, error_size)) {
                return 0;
            }
            distance = lighting_runtime_add16(
                lighting_runtime_abs_difference16(point.x, x),
                lighting_runtime_abs_difference16(point.z, z));
            contribution = lighting_runtime_add16(lighting_runtime_asr16(distance, 5u),
                                                  brightness);
            if (contribution > 0 &&
                (!lighting_runtime_add_current_point_brightness(
                    runtime, (uint16_t)point_index, 0u, contribution, error, error_size) ||
                 !lighting_runtime_add_current_point_brightness(
                    runtime, (uint16_t)point_index, 1u, contribution, error, error_size))) {
                return 0;
            }
        }
    }

    /* newanims.s:bright_points / room_point_loop: every PVST zone has ten markers. */
    for (list_index = 0u; ; ++list_index) {
        LevelPotentialVisibility visible_zone;
        LevelZone zone;
        uint16_t visible_zone_index;

        if (list_index > level->zone_count) {
            lighting_runtime_set_error(error, error_size,
                                       "anim_BrightenPoints PVST list has no terminator");
            return 0;
        }
        if (!level_runtime_get_zone_potential_visibility(level, zone_index, list_index,
                                                         &visible_zone, error, error_size)) {
            return 0;
        }
        if (visible_zone.zone_index < 0) {
            return 1;
        }
        visible_zone_index = (uint16_t)visible_zone.zone_index;
        if (visible_zone_index >= level->zone_count ||
            !level_runtime_get_zone(level, visible_zone_index, &zone, error, error_size)) {
            return 0;
        }
        for (uint16_t border_index = 0u;
             border_index < LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT; ++border_index) {
            int16_t point_index;
            LevelWorldPoint point;
            int16_t distance;

            if (!level_runtime_get_zone_border_point(level, visible_zone_index, border_index,
                                                      &point_index, error, error_size)) {
                return 0;
            }
            if (point_index < 0) {
                break;
            }
            if ((uint32_t)point_index >= level->world_point_count ||
                !level_runtime_get_world_point(level, (uint16_t)point_index, &point,
                                               error, error_size)) {
                return 0;
            }
            distance = lighting_runtime_add16(
                lighting_runtime_abs_difference16(point.x, x),
                lighting_runtime_abs_difference16(point.z, z));
            lighting_runtime_apply_bright_room(runtime, &zone, visible_zone_index,
                                                border_index, distance, brightness,
                                                vertical_position);
        }
    }
}

static int lighting_runtime_directional_distance(const GameMath *math,
                                                 uint16_t angle_address,
                                                 int16_t signed_x_distance,
                                                 int16_t signed_z_distance,
                                                 int16_t absolute_x_distance,
                                                 int16_t absolute_z_distance,
                                                 int16_t *out_distance,
                                                 int *out_in_front,
                                                 char *error, size_t error_size)
{
    int16_t sine;
    int16_t cosine;
    int32_t forward_distance;
    int32_t base_distance;
    int32_t lateral_distance;
    int16_t directional_distance;

    if (!out_distance || !out_in_front ||
        !game_math_sine(math, angle_address, &sine, error, error_size) ||
        !game_math_cosine(math, angle_address, &cosine, error, error_size)) {
        return 0;
    }

    /* newanims.s:Anim_BrightenPointsAngle's first MULS/ADD.L pair. */
    forward_distance = lighting_runtime_add32(
        (int32_t)cosine * (int32_t)signed_z_distance,
        (int32_t)sine * (int32_t)signed_x_distance);
    if (forward_distance <= 0) {
        *out_in_front = 0;
        return 1;
    }

    base_distance = lighting_runtime_add32(
        lighting_runtime_neg32(forward_distance), 30 * 65536);
    if (base_distance < 0) {
        base_distance = 0;
    }
    /* The second MULS pair produces the signed lateral distance. */
    lateral_distance = lighting_runtime_sub32(
        (int32_t)sine * (int32_t)signed_z_distance,
        (int32_t)cosine * (int32_t)signed_x_distance);
    if (lateral_distance <= 0) {
        lateral_distance = lighting_runtime_neg32(lateral_distance);
    }
    directional_distance = (int16_t)(uint16_t)
        ((uint32_t)lighting_runtime_add32(lateral_distance, base_distance) << 2u >> 16u);
    *out_distance = lighting_runtime_add16(
        lighting_runtime_add16(absolute_z_distance, directional_distance), absolute_x_distance);
    *out_in_front = 1;
    return 1;
}

int lighting_runtime_brighten_points_angle(LightingRuntime *runtime,
                                           const LevelRuntime *level,
                                           const GameMath *math,
                                           int16_t brightness, int16_t x, int16_t z,
                                           int32_t vertical_position,
                                           uint16_t zone_index,
                                           uint16_t angle_address,
                                           char *error, size_t error_size)
{
    uint32_t visible_list_index;

    if (!runtime || !level || !math || zone_index >= level->zone_count ||
        level->zone_count > LIGHTING_RUNTIME_POINT_ZONE_CAPACITY) {
        lighting_runtime_set_error(
            error, error_size, "Anim_BrightenPointsAngle received invalid source lighting state");
        return 0;
    }
    if (runtime->lighting_enabled == 0u) {
        return 1;
    }

    /* newanims.s:bright_points_A follows the source zone's complete PVST list. */
    for (visible_list_index = 0u; ; ++visible_list_index) {
        LevelPotentialVisibility visible_zone;
        LevelZone room;
        uint16_t visible_zone_index;
        uint32_t border_stream_index;
        uint16_t source_d3 = 9u;

        if (visible_list_index > level->zone_count) {
            lighting_runtime_set_error(error, error_size,
                                       "Anim_BrightenPointsAngle PVST list has no terminator");
            return 0;
        }
        if (!level_runtime_get_zone_potential_visibility(level, zone_index, visible_list_index,
                                                         &visible_zone, error, error_size)) {
            return 0;
        }
        if (visible_zone.zone_index < 0) {
            return 1;
        }
        visible_zone_index = (uint16_t)visible_zone.zone_index;
        if (visible_zone_index >= level->zone_count ||
            !level_runtime_get_zone(level, visible_zone_index, &room, error, error_size)) {
            return 0;
        }
        border_stream_index = (uint32_t)visible_zone_index *
            LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT;

        for (;;) {
            uint16_t marker_zone_index;
            uint16_t marker_index;
            int16_t world_point_index;
            LevelWorldPoint point;
            int16_t signed_x_distance;
            int16_t signed_z_distance;
            int16_t absolute_x_distance;
            int16_t absolute_z_distance;
            int16_t distance;
            int in_front;

            /*
             * The source's .behind_point path uses DBRA d7, not d3. Preserve
             * that register-level walk, including its ability to continue into
             * the next contiguous zone-marker block, while keeping it bounded
             * by the loaded source level.
             */
            if (border_stream_index >= (uint32_t)level->zone_count *
                                             LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT) {
                lighting_runtime_set_error(
                    error, error_size,
                    "Anim_BrightenPointsAngle directional marker walk escapes source level");
                return 0;
            }
            marker_zone_index = (uint16_t)(border_stream_index /
                                            LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT);
            marker_index = (uint16_t)(border_stream_index %
                                      LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT);
            if (!level_runtime_get_zone_border_point(level, marker_zone_index, marker_index,
                                                      &world_point_index, error, error_size)) {
                return 0;
            }
            if (world_point_index < 0) {
                break;
            }
            if ((uint32_t)world_point_index >= level->world_point_count ||
                !level_runtime_get_world_point(level, (uint16_t)world_point_index, &point,
                                               error, error_size)) {
                return 0;
            }
            signed_x_distance = lighting_runtime_add16(point.x, lighting_runtime_neg16(x));
            signed_z_distance = lighting_runtime_add16(point.z, lighting_runtime_neg16(z));
            absolute_x_distance = signed_x_distance > 0 ? signed_x_distance :
                lighting_runtime_neg16(signed_x_distance);
            absolute_z_distance = signed_z_distance > 0 ? signed_z_distance :
                lighting_runtime_neg16(signed_z_distance);
            if (!lighting_runtime_directional_distance(
                    math, angle_address, signed_x_distance, signed_z_distance,
                    absolute_x_distance, absolute_z_distance, &distance, &in_front,
                    error, error_size)) {
                return 0;
            }
            ++border_stream_index;
            if (!in_front) {
                uint16_t source_d7 = (uint16_t)signed_z_distance;

                source_d7 = (uint16_t)(source_d7 - 1u);
                if (source_d7 != UINT16_MAX) {
                    continue;
                }
                break;
            }
            lighting_runtime_apply_bright_room(runtime, &room, marker_zone_index, marker_index,
                                                distance, brightness, vertical_position);
            source_d3 = (uint16_t)(source_d3 - 1u);
            if (source_d3 != UINT16_MAX) {
                continue;
            }
            break;
        }
    }
}
