#include "object_projectiles.h"

#include <limits.h>
#include <stdio.h>

#include "object_movement.h"

enum {
    /* defs.i:ObjT/EntT/ShotT fields used by newanims.s:ItsABullet. */
    OBJECT_PROJECTILE_POINT_INDEX = 0u,
    OBJECT_PROJECTILE_VERTICAL_POSITION = 4u,
    OBJECT_PROJECTILE_GRAPHICS_WORD = 6u,
    OBJECT_PROJECTILE_GRAPHICS_LONG = 8u,
    OBJECT_PROJECTILE_ZONE_ID = 12u,
    OBJECT_PROJECTILE_TYPE_ID = 16u,
    OBJECT_PROJECTILE_VELOCITY_X = 18u,
    OBJECT_PROJECTILE_VELOCITY_Z = 22u,
    OBJECT_PROJECTILE_ENTITY_ZONE_ID = 26u,
    OBJECT_PROJECTILE_POWER = 28u,
    OBJECT_PROJECTILE_STATUS = 30u,
    OBJECT_PROJECTILE_SIZE = 31u,
    OBJECT_PROJECTILE_ENTITY_HIT_POINTS = 18u,
    OBJECT_PROJECTILE_ENTITY_DAMAGE_TAKEN = 19u,
    OBJECT_PROJECTILE_ENTITY_ENEMY_FLAGS = 36u,
    OBJECT_PROJECTILE_VELOCITY_Y = 42u,
    OBJECT_PROJECTILE_ACCUMULATED_Y = 44u,
    OBJECT_PROJECTILE_ANIMATION = 52u,
    OBJECT_PROJECTILE_GRAVITY = 54u,
    OBJECT_PROJECTILE_LIFETIME = 58u,
    OBJECT_PROJECTILE_FLAGS = 60u,
    OBJECT_PROJECTILE_IN_UPPER_ZONE = 63u,
    OBJECT_TYPE_PROJECTILE = 2u
};

static void object_projectiles_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_projectiles_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t object_projectiles_read_be16s(const uint8_t *source)
{
    return (int16_t)object_projectiles_read_be16(source);
}

static uint32_t object_projectiles_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static int32_t object_projectiles_read_be32s(const uint8_t *source)
{
    return (int32_t)object_projectiles_read_be32(source);
}

static void object_projectiles_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void object_projectiles_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

/* 68000 ADD/SUB on longwords and words retain their wraparound result. */
static int32_t object_projectiles_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t object_projectiles_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int16_t object_projectiles_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t object_projectiles_sub16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left - (uint16_t)right);
}

static int16_t object_projectiles_neg16(int16_t value)
{
    return (int16_t)(0u - (uint16_t)value);
}

static int32_t object_projectiles_neg32(int32_t value)
{
    return (int32_t)(0u - (uint32_t)value);
}

static int32_t object_projectiles_muls16(int16_t left, int16_t right)
{
    return (int32_t)left * (int32_t)right;
}

static int32_t object_projectiles_asr32(int32_t value, unsigned int count)
{
    if (value >= 0) {
        return value >> count;
    }
    return -((-(int64_t)value + ((INT64_C(1) << count) - 1)) >> count);
}

static int16_t object_projectiles_asr16(int16_t value, unsigned int count)
{
    return (int16_t)object_projectiles_asr32(value, count);
}

/* move.w to the first word of a big-endian Vec2L/velocity longword. */
static int32_t object_projectiles_replace_high_word(int32_t value, int16_t high_word)
{
    return (int32_t)(((uint32_t)(uint16_t)high_word << 16) |
                     ((uint32_t)value & UINT32_C(0x0000ffff)));
}

static int16_t object_projectiles_high_word(int32_t value)
{
    return (int16_t)((uint32_t)value >> 16);
}

static int object_projectiles_divs16(int32_t dividend, int16_t divisor,
                                     int16_t *out_quotient,
                                     char *error, size_t error_size)
{
    int32_t quotient;

    if (!out_quotient || divisor == 0 ||
        (dividend == INT32_MIN && divisor == -1)) {
        object_projectiles_set_error(error, error_size,
                                     "ItsABullet DIVS received invalid source operands");
        return 0;
    }
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        object_projectiles_set_error(error, error_size,
                                     "ItsABullet DIVS quotient exceeds a source word");
        return 0;
    }
    *out_quotient = (int16_t)quotient;
    return 1;
}

/*
 * ItsABullet splits a Vec2L velocity into signed high and unsigned low words,
 * then combines MULS/MULU products with SWAP/CLR.W. The discarded high half
 * of the signed product is intentional source arithmetic.
 */
static int32_t object_projectiles_velocity_delta(int32_t velocity, uint16_t frame_ticks)
{
    int32_t high_product = object_projectiles_muls16(
        object_projectiles_high_word(velocity), (int16_t)frame_ticks);
    uint32_t high_part = (uint32_t)(uint16_t)high_product << 16;
    uint32_t low_part = (uint32_t)(uint16_t)velocity * (uint16_t)frame_ticks;

    return (int32_t)(high_part + low_part);
}

static void object_projectiles_mark_impact(uint8_t *slot)
{
    slot[OBJECT_PROJECTILE_ANIMATION] = 0u;
    slot[OBJECT_PROJECTILE_STATUS] = 1u;
}

static void object_projectiles_emit_impact_sound(
    ObjectProjectileSourceRuntime *source_runtime, ObjectRuntime *objects,
    uint32_t slot_index, const uint8_t *slot, const GameBulletDefinition *bullet)
{
    uint8_t *point;
    uint16_t point_index;

    if (!source_runtime || !source_runtime->audio_events || !objects || !slot || !bullet) {
        return;
    }
    point_index = object_projectiles_read_be16(slot + OBJECT_PROJECTILE_POINT_INDEX);
    if (!object_runtime_get_point_bytes(objects, point_index, &point)) {
        return;
    }
    /* newanims.s:ItsABullet writes BulT_ImpactSFX_l directly to Aud_SampleNum_w. */
    game_audio_events_emit(source_runtime->audio_events, (int16_t)bullet->impact_sound_effect,
                           200, object_projectiles_high_word(object_projectiles_read_be32s(point)),
                           object_projectiles_high_word(
                               object_projectiles_read_be32s(point + 4u)),
                           (uint16_t)slot_index, GAME_AUDIO_RESTART_SOURCE, 0u, 0u);
}

static void object_projectiles_apply_animation_descriptor(
    uint8_t *slot, uint32_t graphics_type, const GameBulletAnimationFrame *frame)
{
    /* ItsABullet clears +8 before bitmap, glare, or additive frame setup. */
    object_projectiles_write_be32(slot + OBJECT_PROJECTILE_GRAPHICS_LONG, 0u);
    if ((int32_t)graphics_type < 1) {
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 1u] = frame->byte_0;
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 3u] = frame->byte_1;
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_GRAPHICS_WORD, frame->word_2);
    } else if (graphics_type == 1u) {
        object_projectiles_write_be16(
            slot + OBJECT_PROJECTILE_GRAPHICS_LONG,
            (uint16_t)(int16_t)-(int16_t)(int8_t)frame->byte_0);
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 3u] = frame->byte_1;
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_GRAPHICS_WORD, frame->word_2);
    } else {
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 1u] = frame->byte_0;
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 3u] = frame->byte_1;
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 2u] = 6u;
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_GRAPHICS_WORD, frame->word_2);
    }
}

/* newanims.s:ItsABullet:.nobright's immediate anim_BrightenPoints handoff. */
static int object_projectiles_apply_point_brightness(
    LightingRuntime *lighting_runtime, const LevelRuntime *level,
    const GameBulletAnimationFrame *frame, int16_t x, int16_t z,
    int32_t vertical_position, uint16_t zone_index,
    char *error, size_t error_size)
{
    if (frame->byte_5 == 0u) {
        return 1;
    }
    return lighting_runtime_brighten_points(
        lighting_runtime, level, object_projectiles_neg16((int16_t)frame->byte_5),
        x, z, vertical_position, zone_index, error, error_size);
}

int object_projectiles_update_impact_slot(ObjectRuntime *objects, uint32_t slot_index,
                                           const LevelDynamicState *dynamic_level,
                                           LightingRuntime *lighting_runtime,
                                           const GameLink *game_link,
                                           char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *point;
    uint16_t bullet_index;
    uint16_t frame_index;
    uint16_t next_frame;
    uint16_t point_index;
    int16_t zone_index;
    GameBulletDefinition bullet;
    GameBulletAnimationFrame frame;

    if (!objects || !dynamic_level || !lighting_runtime || !game_link ||
        !dynamic_level->level_bytes ||
        dynamic_level->runtime.level_bytes != dynamic_level->level_bytes ||
        dynamic_level->runtime.graphics_bytes != dynamic_level->graphics_bytes ||
        slot_index >= objects->active_slot_count ||
        objects->active_slot_count > objects->slot_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        object_projectiles_set_error(error, error_size,
                                     "ItsABullet received an invalid source slot");
        return 0;
    }
    if (slot[OBJECT_PROJECTILE_TYPE_ID] != OBJECT_TYPE_PROJECTILE ||
        object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_ZONE_ID) < 0 ||
        slot[OBJECT_PROJECTILE_STATUS] == 0u) {
        return 1;
    }

    bullet_index = slot[OBJECT_PROJECTILE_SIZE];
    frame_index = slot[OBJECT_PROJECTILE_ANIMATION];
    if (!game_link_get_bullet_definition(game_link, bullet_index, &bullet, error, error_size) ||
        !game_link_get_bullet_animation_frame(game_link, GAME_LINK_BULLET_ANIMATION_POP,
                                              bullet_index, frame_index, &frame,
                                              error, error_size)) {
        return 0;
    }

    object_projectiles_apply_animation_descriptor(slot, bullet.impact_graphics_type, &frame);

    next_frame = (uint16_t)(frame_index + 1u);
    /* cmp.w BulT_PopFrames_l+2,d2 / ble.s notdonepopping. */
    if ((int16_t)next_frame > (int16_t)(uint16_t)bullet.pop_frames) {
        /* macros.i:FREE_ENT, then ItsABullet clears its pop state. */
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_ZONE_ID, UINT16_MAX);
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_ENTITY_ZONE_ID, UINT16_MAX);
        slot[OBJECT_PROJECTILE_STATUS] = 0u;
        slot[OBJECT_PROJECTILE_ANIMATION] = 0u;
    } else {
        slot[OBJECT_PROJECTILE_ANIMATION] = (uint8_t)next_frame;
        /*
         * The source fetches ObjRotated's point after retaining the next pop
         * frame, then derives Anim_BrightY_l from ObjT_YPos_w << 7.
         */
        if (frame.byte_5 != 0u) {
            point_index = object_projectiles_read_be16(slot + OBJECT_PROJECTILE_POINT_INDEX);
            zone_index = object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_ZONE_ID);
            if (zone_index < 0) {
                object_projectiles_set_error(error, error_size,
                                             "ItsABullet impact brightness has an invalid zone");
                return 0;
            }
            if (!object_runtime_get_point_bytes(objects, point_index, &point)) {
                object_projectiles_set_error(error, error_size,
                                             "ItsABullet impact brightness has an invalid source point");
                return 0;
            }
            if (!object_projectiles_apply_point_brightness(
                    lighting_runtime, &dynamic_level->runtime, &frame,
                    object_projectiles_high_word(object_projectiles_read_be32s(point)),
                    object_projectiles_high_word(object_projectiles_read_be32s(point + 4u)),
                    (int32_t)object_projectiles_read_be16s(
                        slot + OBJECT_PROJECTILE_VERTICAL_POSITION) * 128,
                    (uint16_t)zone_index, error, error_size)) {
                return 0;
            }
        }
    }
    return 1;
}

/* newanims.s:ItsABullet:.checkloop through .hitnasty. */
static int object_projectiles_check_direct_target_collision(
    ObjectRuntime *objects, uint32_t projectile_slot_index, const GameLink *game_link,
    int16_t old_x, int16_t old_z, int16_t new_x, int16_t new_z, uint8_t moving,
    uint8_t *out_hit,
    char *error, size_t error_size)
{
    uint8_t *projectile_slot;
    int16_t x_difference;
    int16_t z_difference;
    int32_t length_squared;
    int16_t range = 1;
    int32_t range_squared;
    uint32_t enemy_flags;

    if (!out_hit || !object_runtime_get_slot_bytes(objects, projectile_slot_index,
                                                    &projectile_slot)) {
        object_projectiles_set_error(error, error_size,
                                     "ItsABullet projectile slot is outside source state");
        return 0;
    }
    *out_hit = 0u;
    x_difference = object_projectiles_sub16(new_x, old_x);
    z_difference = object_projectiles_sub16(new_z, old_z);
    length_squared = object_projectiles_add32(
        object_projectiles_muls16(x_difference, x_difference),
        object_projectiles_muls16(z_difference, z_difference));
    if (length_squared != 0) {
        uint32_t highest_bit = 31u;

        while ((length_squared & (UINT32_C(1) << highest_bit)) == 0u) {
            --highest_bit;
        }
        range = (int16_t)(UINT16_C(1) << (highest_bit >> 1u));
        /*
         * newanims.s:.foundhigh, .stillnot0, and .stillnot02 each perform
         * one word DIVS refinement before .stillnot03 falls through.  A
         * fourth PC refinement changes the source collision threshold.
         */
        for (uint32_t approximation_pass = 0u; approximation_pass < 3u;
             ++approximation_pass) {
            int32_t error_term = object_projectiles_sub32(
                object_projectiles_muls16(range, range), length_squared);
            int16_t quotient;

            error_term = object_projectiles_asr32(error_term, 1u);
            if (!object_projectiles_divs16(error_term, range, &quotient, error, error_size)) {
                return 0;
            }
            range = object_projectiles_sub16(range, quotient);
            if (range <= 0) {
                range = 1;
            }
        }
    }
    range_squared = object_projectiles_muls16(
        object_projectiles_add16(range, 80), object_projectiles_add16(range, 80));
    enemy_flags = object_projectiles_read_be32(
        projectile_slot + OBJECT_PROJECTILE_ENTITY_ENEMY_FLAGS);

    for (uint32_t candidate_index = 0u; candidate_index < objects->active_slot_count;
         ++candidate_index) {
        uint8_t *candidate_slot;
        uint8_t candidate_type;
        uint16_t candidate_point_index;
        uint8_t *candidate_point;
        int16_t candidate_x;
        int16_t candidate_z;
        int16_t from_old_x;
        int16_t from_old_z;
        int16_t from_new_x;
        int16_t from_new_z;
        int16_t cross_distance;
        int32_t cross;
        int32_t old_distance_squared;
        int32_t new_distance_squared;

        if (!object_runtime_get_slot_bytes(objects, candidate_index, &candidate_slot)) {
            object_projectiles_set_error(error, error_size,
                                         "ItsABullet target slot is outside source state");
            return 0;
        }
        if (object_projectiles_read_be16s(candidate_slot + OBJECT_PROJECTILE_POINT_INDEX) < 0) {
            break;
        }
        if (object_projectiles_read_be16s(candidate_slot + OBJECT_PROJECTILE_ZONE_ID) < 0 ||
            (projectile_slot[OBJECT_PROJECTILE_IN_UPPER_ZONE] != 0u) !=
                (candidate_slot[OBJECT_PROJECTILE_IN_UPPER_ZONE] != 0u)) {
            continue;
        }
        candidate_type = candidate_slot[OBJECT_PROJECTILE_TYPE_ID];
        if ((enemy_flags & (UINT32_C(1) << (candidate_type & 31u))) == 0u) {
            continue;
        }
        if (candidate_type == 1u) {
            GameObjectDefinition definition;

            if (!game_link_get_object_definition(game_link, candidate_slot[54u], &definition,
                                                 error, error_size)) {
                return 0;
            }
            if (definition.behaviour != 2u) {
                continue;
            }
        }
        if (candidate_slot[OBJECT_PROJECTILE_ENTITY_HIT_POINTS] == 0u) {
            continue;
        }
        if (moving != 0u) {
            int16_t height_difference = object_projectiles_sub16(
                object_projectiles_read_be16s(candidate_slot + OBJECT_PROJECTILE_VERTICAL_POSITION),
                object_projectiles_read_be16s(projectile_slot + OBJECT_PROJECTILE_VERTICAL_POSITION));

            if (height_difference < 0) {
                height_difference = object_projectiles_neg16(height_difference);
            }
            if (height_difference > 50) {
                continue;
            }
        }
        candidate_point_index = object_projectiles_read_be16(candidate_slot +
                                                               OBJECT_PROJECTILE_POINT_INDEX);
        if (!object_runtime_get_point_bytes(objects, candidate_point_index, &candidate_point)) {
            object_projectiles_set_error(error, error_size,
                                         "ItsABullet target has an invalid source point");
            return 0;
        }
        candidate_x = object_projectiles_high_word(object_projectiles_read_be32s(candidate_point));
        candidate_z = object_projectiles_high_word(
            object_projectiles_read_be32s(candidate_point + 4u));
        from_new_x = object_projectiles_sub16(candidate_x, new_x);
        from_old_x = object_projectiles_sub16(candidate_x, old_x);
        from_new_z = object_projectiles_sub16(candidate_z, new_z);
        from_old_z = object_projectiles_sub16(candidate_z, old_z);
        cross = object_projectiles_sub32(
            object_projectiles_muls16(from_old_x, z_difference),
            object_projectiles_muls16(from_old_z, x_difference));
        if (cross <= 0) {
            cross = object_projectiles_neg32(cross);
        }
        if (!object_projectiles_divs16(cross, range, &cross_distance, error, error_size)) {
            return 0;
        }
        if (cross_distance > ((int8_t)candidate_type <= 1 ? 80 : 40)) {
            continue;
        }
        old_distance_squared = object_projectiles_add32(
            object_projectiles_muls16(from_old_x, from_old_x),
            object_projectiles_muls16(from_old_z, from_old_z));
        if (old_distance_squared > range_squared) {
            continue;
        }
        new_distance_squared = object_projectiles_add32(
            object_projectiles_muls16(from_new_x, from_new_x),
            object_projectiles_muls16(from_new_z, from_new_z));
        if (new_distance_squared > range_squared) {
            continue;
        }
        candidate_slot[OBJECT_PROJECTILE_ENTITY_DAMAGE_TAKEN] =
            (uint8_t)(candidate_slot[OBJECT_PROJECTILE_ENTITY_DAMAGE_TAKEN] +
                      projectile_slot[OBJECT_PROJECTILE_POWER]);
        object_projectiles_write_be16(candidate_slot + OBJECT_PROJECTILE_VELOCITY_Y,
                                      object_projectiles_read_be16(
                                          projectile_slot + OBJECT_PROJECTILE_VELOCITY_X));
        object_projectiles_write_be16(candidate_slot + OBJECT_PROJECTILE_ACCUMULATED_Y,
                                      object_projectiles_read_be16(
                                          projectile_slot + OBJECT_PROJECTILE_VELOCITY_Z));
        object_projectiles_mark_impact(projectile_slot);
        *out_hit = UINT8_MAX;
        return 1;
    }
    return 1;
}

static int object_projectiles_compute_blast(
    ObjectProjectileSourceRuntime *source_runtime, ObjectRuntime *objects,
    uint32_t slot_index, LevelDynamicState *dynamic_level, const GameLink *game_link,
    const uint8_t *slot, const GameBulletDefinition *bullet, uint8_t set_viewer_top,
    char *error, size_t error_size)
{
    if (bullet->explosive_force == 0u || !source_runtime || !source_runtime->blast_runtime ||
        !source_runtime->motion_runtime || !source_runtime->visibility_runtime ||
        !source_runtime->clips || !source_runtime->random) {
        return 1;
    }
    if (set_viewer_top != 0u) {
        object_visibility_runtime_set_viewer(
            source_runtime->visibility_runtime, source_runtime->motion_runtime->new_x,
            source_runtime->motion_runtime->new_z,
            object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_VERTICAL_POSITION),
            slot[OBJECT_PROJECTILE_IN_UPPER_ZONE]);
    } else {
        /* ItsABullet's direct-target caller omits the ViewerTop write. */
        object_visibility_runtime_set_viewer_position(
            source_runtime->visibility_runtime, source_runtime->motion_runtime->new_x,
            source_runtime->motion_runtime->new_z,
            object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_VERTICAL_POSITION));
    }
    return object_blast_compute(
        source_runtime->blast_runtime, objects, slot_index, dynamic_level,
        source_runtime->clips, game_link, source_runtime->random,
        source_runtime->motion_runtime, source_runtime->visibility_runtime,
        (int16_t)bullet->explosive_force, error, error_size);
}

int object_projectiles_update_flight_animation_slot_with_source_state(
    ObjectRuntime *objects, uint32_t slot_index, LevelDynamicState *dynamic_level,
    LightingRuntime *lighting_runtime, ObjectProjectileSourceRuntime *source_runtime,
    const GameLink *game_link, uint16_t frame_ticks, char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *point;
    uint16_t bullet_index;
    uint16_t frame_index;
    uint16_t next_frame;
    GameBulletDefinition bullet;
    GameBulletAnimationFrame frame;
    LevelZone zone;
    uint16_t point_index;
    int16_t zone_index;
    int32_t old_x;
    int32_t old_z;
    int32_t new_x;
    int32_t new_z;
    int32_t old_y;
    int32_t new_y;
    uint8_t moving = 0u;
    uint8_t timed_out = 0u;
    uint8_t direct_target_hit = 0u;
    ObjectMotionRuntime *motion_runtime =
        source_runtime ? source_runtime->motion_runtime : NULL;
    ObjectMovementTrace trace = {0};

    if (!objects || !dynamic_level || !lighting_runtime || !game_link ||
        !dynamic_level->level_bytes ||
        dynamic_level->runtime.level_bytes != dynamic_level->level_bytes ||
        dynamic_level->runtime.graphics_bytes != dynamic_level->graphics_bytes ||
        slot_index >= objects->active_slot_count ||
        objects->active_slot_count > objects->slot_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        object_projectiles_set_error(error, error_size,
                                     "ItsABullet flight received an invalid source slot");
        return 0;
    }
    /* ItsABullet copies ObjT_ZoneID to EntT_ZoneID before its negative return. */
    object_projectiles_write_be16(slot + OBJECT_PROJECTILE_ENTITY_ZONE_ID,
                                  object_projectiles_read_be16(
                                      slot + OBJECT_PROJECTILE_ZONE_ID));
    if (slot[OBJECT_PROJECTILE_TYPE_ID] != OBJECT_TYPE_PROJECTILE ||
        (zone_index = object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_ZONE_ID)) < 0 ||
        slot[OBJECT_PROJECTILE_STATUS] != 0u) {
        return 1;
    }
    if ((uint16_t)zone_index >= dynamic_level->runtime.zone_count) {
        object_projectiles_set_error(error, error_size,
                                     "ItsABullet projectile zone is outside source level state");
        return 0;
    }
    bullet_index = slot[OBJECT_PROJECTILE_SIZE];
    frame_index = slot[OBJECT_PROJECTILE_ANIMATION];
    if (!game_link_get_bullet_definition(game_link, bullet_index, &bullet, error, error_size) ||
        !game_link_get_bullet_animation_frame(game_link, GAME_LINK_BULLET_ANIMATION_FLIGHT,
                                              bullet_index, frame_index, &frame,
                                              error, error_size)) {
        return 0;
    }
    if (source_runtime && source_runtime->blast_runtime) {
        /* ItsABullet:notpopping updates BLOODYGREATBOMB before every impact branch. */
        object_blast_runtime_note_bullet(source_runtime->blast_runtime, (uint8_t)bullet_index);
    }
    /* ItsABullet compares lifetime through word operands after signed long sentinels. */
    if (object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_LIFETIME) >= 0 &&
        (int32_t)bullet.lifetime >= 0) {
        if ((int16_t)bullet.lifetime >=
            object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_LIFETIME)) {
            object_projectiles_write_be16(
                slot + OBJECT_PROJECTILE_LIFETIME,
                (uint16_t)object_projectiles_add16(
                    object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_LIFETIME),
                    (int16_t)frame_ticks));
        } else {
            /* The source defers the pop-state write until after flight collision. */
            timed_out = UINT8_MAX;
        }
    }
    object_projectiles_apply_animation_descriptor(slot, bullet.graphics_type, &frame);
    next_frame = (uint16_t)(frame_index + 1u);
    /* cmp.w BulT_AnimFrames_l+2,d2 / ble.s notdoneanim. */
    slot[OBJECT_PROJECTILE_ANIMATION] =
        (int16_t)next_frame > (int16_t)(uint16_t)bullet.animation_frames ?
        0u : (uint8_t)next_frame;

    if (!level_runtime_get_zone(&dynamic_level->runtime, (uint16_t)zone_index, &zone,
                                error, error_size)) {
        return 0;
    }
    /* ZoneT+8 selects the upper floor/roof pair for an upper-layer projectile. */
    /* newanims.s:ItsABullet branches to .nohitroof when this distance is below 10*128. */
    if (object_projectiles_sub32(slot[OBJECT_PROJECTILE_IN_UPPER_ZONE] != 0u ?
                                     zone.upper_roof : zone.roof,
                                 object_projectiles_read_be32s(
                                     slot + OBJECT_PROJECTILE_ACCUMULATED_Y)) >= 10 * 128) {
        if ((slot[OBJECT_PROJECTILE_FLAGS + 1u] & 1u) != 0u) {
            object_projectiles_write_be16(
                slot + OBJECT_PROJECTILE_VELOCITY_Y,
                (uint16_t)object_projectiles_neg16(object_projectiles_read_be16s(
                    slot + OBJECT_PROJECTILE_VELOCITY_Y)));
            object_projectiles_write_be32(
                slot + OBJECT_PROJECTILE_ACCUMULATED_Y,
                (uint32_t)object_projectiles_add32(
                    slot[OBJECT_PROJECTILE_IN_UPPER_ZONE] != 0u ? zone.upper_roof : zone.roof,
                    10 * 128));
            if (bullet.gravity != 0u) {
                object_projectiles_write_be32(
                    slot + OBJECT_PROJECTILE_VELOCITY_X,
                    (uint32_t)object_projectiles_asr32(object_projectiles_read_be32s(
                        slot + OBJECT_PROJECTILE_VELOCITY_X), 1u));
                object_projectiles_write_be32(
                    slot + OBJECT_PROJECTILE_VELOCITY_Z,
                    (uint32_t)object_projectiles_asr32(object_projectiles_read_be32s(
                        slot + OBJECT_PROJECTILE_VELOCITY_Z), 1u));
            }
        } else {
            object_projectiles_mark_impact(slot);
            object_projectiles_emit_impact_sound(source_runtime, objects, slot_index, slot,
                                                 &bullet);
            if (!object_projectiles_compute_blast(
                    source_runtime, objects, slot_index, dynamic_level, game_link, slot,
                    &bullet, UINT8_MAX, error, error_size)) {
                return 0;
            }
        }
    }
    if (object_projectiles_sub32(slot[OBJECT_PROJECTILE_IN_UPPER_ZONE] != 0u ?
                                     zone.upper_floor : zone.floor,
                                 object_projectiles_read_be32s(
                                     slot + OBJECT_PROJECTILE_ACCUMULATED_Y)) <= 10 * 128) {
        if (bullet.bounce_vertical != 0u &&
            object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_VELOCITY_Y) >= 0) {
            object_projectiles_write_be16(
                slot + OBJECT_PROJECTILE_VELOCITY_Y,
                (uint16_t)object_projectiles_neg16(object_projectiles_asr16(
                    object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_VELOCITY_Y), 1u)));
            object_projectiles_write_be32(
                slot + OBJECT_PROJECTILE_ACCUMULATED_Y,
                (uint32_t)object_projectiles_sub32(
                    slot[OBJECT_PROJECTILE_IN_UPPER_ZONE] != 0u ? zone.upper_floor : zone.floor,
                    10 * 128));
            if (bullet.gravity != 0u) {
                object_projectiles_write_be32(
                    slot + OBJECT_PROJECTILE_VELOCITY_X,
                    (uint32_t)object_projectiles_asr32(object_projectiles_read_be32s(
                        slot + OBJECT_PROJECTILE_VELOCITY_X), 1u));
                object_projectiles_write_be32(
                    slot + OBJECT_PROJECTILE_VELOCITY_Z,
                    (uint32_t)object_projectiles_asr32(object_projectiles_read_be32s(
                        slot + OBJECT_PROJECTILE_VELOCITY_Z), 1u));
            }
        } else {
            object_projectiles_mark_impact(slot);
            object_projectiles_emit_impact_sound(source_runtime, objects, slot_index, slot,
                                                 &bullet);
            if (!object_projectiles_compute_blast(
                    source_runtime, objects, slot_index, dynamic_level, game_link, slot,
                    &bullet, UINT8_MAX, error, error_size)) {
                return 0;
            }
        }
    }
    point_index = object_projectiles_read_be16(slot + OBJECT_PROJECTILE_POINT_INDEX);
    if (!object_runtime_get_point_bytes(objects, point_index, &point)) {
        object_projectiles_set_error(error, error_size,
                                     "ItsABullet projectile has an invalid source point");
        return 0;
    }
    old_x = object_projectiles_read_be32s(point);
    old_z = object_projectiles_read_be32s(point + 4u);
    new_x = object_projectiles_add32(
        old_x, object_projectiles_velocity_delta(
                   object_projectiles_read_be32s(slot + OBJECT_PROJECTILE_VELOCITY_X),
                   frame_ticks));
    new_z = object_projectiles_add32(
        old_z, object_projectiles_velocity_delta(
                   object_projectiles_read_be32s(slot + OBJECT_PROJECTILE_VELOCITY_Z),
                   frame_ticks));
    old_y = object_projectiles_read_be32s(slot + OBJECT_PROJECTILE_ACCUMULATED_Y);
    {
        int32_t vertical_delta = object_projectiles_muls16(
            object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_VELOCITY_Y),
            (int16_t)frame_ticks);

        if (bullet.gravity != 0u) {
            int32_t gravity_delta = object_projectiles_muls16((int16_t)bullet.gravity,
                                                               (int16_t)frame_ticks);
            int32_t next_velocity = object_projectiles_add32(
                object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_VELOCITY_Y),
                gravity_delta);

            vertical_delta = object_projectiles_add32(vertical_delta, gravity_delta);
            if (next_velocity >= 10 * 256) {
                next_velocity = 10 * 256;
            }
            object_projectiles_write_be16(slot + OBJECT_PROJECTILE_VELOCITY_Y,
                                          (uint16_t)next_velocity);
        }
        old_y = object_projectiles_read_be32s(slot + OBJECT_PROJECTILE_ACCUMULATED_Y);
        new_y = object_projectiles_add32(old_y, vertical_delta);
        object_projectiles_write_be32(slot + OBJECT_PROJECTILE_ACCUMULATED_Y,
                                      (uint32_t)new_y);
    }
    new_y = object_projectiles_sub32(new_y, 5 * 128);
    object_projectiles_write_be16(slot + OBJECT_PROJECTILE_VERTICAL_POSITION,
                                  (uint16_t)object_projectiles_asr32(
                                      object_projectiles_read_be32s(
                                          slot + OBJECT_PROJECTILE_ACCUMULATED_Y), 7u));
    trace.zone_index = (uint16_t)zone_index;
    trace.old_x = object_projectiles_high_word(old_x);
    trace.old_z = object_projectiles_high_word(old_z);
    trace.new_x = object_projectiles_high_word(new_x);
    trace.new_z = object_projectiles_high_word(new_z);
    trace.old_y = old_y;
    trace.new_y = new_y;
    trace.thing_height = 10 * 128;
    trace.step_down = 0x1000000;
    trace.wall_flags = 0x0400u;
    trace.away_from_wall = -1;
    trace.stood_in_top = slot[OBJECT_PROJECTILE_IN_UPPER_ZONE];
    trace.wall_bounce = bullet.bounce_horizontal != 0u ? UINT8_MAX : 0u;
    trace.exit_first = bullet.bounce_horizontal == 0u ? UINT8_MAX : 0u;
    /* The roof/floor branches above intentionally used the prior shared words. */
    object_motion_runtime_set_new_words(motion_runtime, trace.new_x, trace.new_z);
    if (trace.old_x != trace.new_x || trace.old_z != trace.new_z) {
        moving = UINT8_MAX;
        if (!object_movement_trace_zero_extension(dynamic_level, &trace, error, error_size)) {
            return 0;
        }
        new_x = object_projectiles_replace_high_word(new_x, trace.new_x);
        new_z = object_projectiles_replace_high_word(new_z, trace.new_z);
        new_y = trace.new_y;
        /* ItsABullet only brightens after the `lalal` MoveObject path. */
        if (!object_projectiles_apply_point_brightness(
                lighting_runtime, &dynamic_level->runtime, &frame,
                trace.new_x, trace.new_z, new_y, trace.zone_index,
                error, error_size)) {
            return 0;
        }
    }
    /* MoveObject (or the stationary flight path) leaves current newx/newz live. */
    object_motion_runtime_set_new_words(motion_runtime,
                                        object_projectiles_high_word(new_x),
                                        object_projectiles_high_word(new_z));
    slot[OBJECT_PROJECTILE_IN_UPPER_ZONE] = trace.stood_in_top;
    if (trace.wall_bounce != 0u && trace.hit_wall != 0u) {
        int16_t reflected_normal;
        int16_t reflected_component;
        int16_t velocity_x = object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_VELOCITY_X);
        int16_t velocity_z = object_projectiles_read_be16s(slot + OBJECT_PROJECTILE_VELOCITY_Z);
        int32_t numerator = object_projectiles_sub32(
            object_projectiles_muls16(velocity_z, trace.wall_x_size),
            object_projectiles_muls16(velocity_x, trace.wall_z_size));

        if (!object_projectiles_divs16(numerator, trace.wall_length, &reflected_normal,
                                       error, error_size)) {
            return 0;
        }
        if (!object_projectiles_divs16(
                object_projectiles_muls16(reflected_normal,
                                          object_projectiles_add16(trace.wall_z_size,
                                                                     trace.wall_z_size)),
                trace.wall_length, &reflected_component, error, error_size)) {
            return 0;
        }
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_VELOCITY_X,
                                      (uint16_t)object_projectiles_add16(velocity_x,
                                                                         reflected_component));
        if (!object_projectiles_divs16(
                object_projectiles_muls16(reflected_normal,
                                          object_projectiles_add16(trace.wall_x_size,
                                                                     trace.wall_x_size)),
                trace.wall_length, &reflected_component, error, error_size)) {
            return 0;
        }
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_VELOCITY_Z,
                                      (uint16_t)object_projectiles_sub16(velocity_z,
                                                                         reflected_component));
        if (bullet.gravity != 0u) {
            object_projectiles_write_be32(
                slot + OBJECT_PROJECTILE_VELOCITY_X,
                (uint32_t)object_projectiles_asr32(object_projectiles_read_be32s(
                    slot + OBJECT_PROJECTILE_VELOCITY_X), 1u));
            object_projectiles_write_be32(
                slot + OBJECT_PROJECTILE_VELOCITY_Z,
                (uint32_t)object_projectiles_asr32(object_projectiles_read_be32s(
                    slot + OBJECT_PROJECTILE_VELOCITY_Z), 1u));
        }
    } else if (trace.wall_bounce == 0u && trace.hit_wall != 0u) {
        object_projectiles_write_be32(slot + OBJECT_PROJECTILE_ACCUMULATED_Y,
                                      (uint32_t)trace.wall_hit_height);
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_VERTICAL_POSITION,
                                      (uint16_t)object_projectiles_asr32(trace.wall_hit_height,
                                                                         7u));
        object_projectiles_mark_impact(slot);
        object_projectiles_emit_impact_sound(source_runtime, objects, slot_index, slot, &bullet);
        if (!object_projectiles_compute_blast(
                source_runtime, objects, slot_index, dynamic_level, game_link, slot,
                &bullet, UINT8_MAX, error, error_size)) {
            return 0;
        }
    }
    if (timed_out != 0u) {
        object_projectiles_mark_impact(slot);
        object_projectiles_emit_impact_sound(source_runtime, objects, slot_index, slot, &bullet);
        if (!object_projectiles_compute_blast(
                source_runtime, objects, slot_index, dynamic_level, game_link, slot,
                &bullet, UINT8_MAX, error, error_size)) {
            return 0;
        }
    }
    if (!level_runtime_get_zone(&dynamic_level->runtime, trace.zone_index, &zone,
                                error, error_size)) {
        return 0;
    }
    object_projectiles_write_be16(slot + OBJECT_PROJECTILE_ZONE_ID, zone.id);
    object_projectiles_write_be16(slot + OBJECT_PROJECTILE_ENTITY_ZONE_ID, zone.id);
    object_projectiles_write_be32(point, (uint32_t)new_x);
    object_projectiles_write_be32(point + 4u, (uint32_t)new_z);
    if (object_projectiles_read_be32(slot + OBJECT_PROJECTILE_ENTITY_ENEMY_FLAGS) != 0u &&
        !object_projectiles_check_direct_target_collision(
            objects, slot_index, game_link, object_projectiles_high_word(old_x),
            object_projectiles_high_word(old_z), object_projectiles_high_word(new_x),
            object_projectiles_high_word(new_z), moving, &direct_target_hit,
            error, error_size)) {
        return 0;
    }
    if (direct_target_hit != 0u) {
        object_projectiles_emit_impact_sound(source_runtime, objects, slot_index, slot, &bullet);
        if (!object_projectiles_compute_blast(source_runtime, objects, slot_index, dynamic_level,
                                              game_link, slot, &bullet, 0u,
                                              error, error_size)) {
            return 0;
        }
    }
    return 1;
}

int object_projectiles_update_flight_animation_slot(ObjectRuntime *objects, uint32_t slot_index,
                                                     LevelDynamicState *dynamic_level,
                                                     LightingRuntime *lighting_runtime,
                                                     const GameLink *game_link,
                                                     uint16_t frame_ticks,
                                                     char *error, size_t error_size)
{
    return object_projectiles_update_flight_animation_slot_with_source_state(
        objects, slot_index, dynamic_level, lighting_runtime, NULL, game_link, frame_ticks,
        error, error_size);
}
