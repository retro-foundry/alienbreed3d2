#include "player_shoot.h"

#include <stdio.h>
#include <string.h>

enum {
    /* newplayershoot.s Plr1_Shot ObjT/EntT fields and $23 shot mask. */
    PLAYER_SHOOT_POINT_INDEX = 0u,
    PLAYER_SHOOT_VERTICAL_POSITION = 4u,
    PLAYER_SHOOT_ZONE_ID = 12u,
    PLAYER_SHOOT_TYPE_ID = 16u,
    PLAYER_SHOOT_SEES_PLAYER = 17u,
    PLAYER_SHOOT_HIT_POINTS = 18u,
    PLAYER_SHOOT_DAMAGE_TAKEN = 19u,
    PLAYER_SHOOT_SHOT_STATUS = 30u,
    PLAYER_SHOOT_SHOT_SIZE = 31u,
    PLAYER_SHOOT_SHOT_ANIMATION = 52u,
    PLAYER_SHOOT_SHOT_GRAVITY = 54u,
    PLAYER_SHOOT_SHOT_LIFETIME = 58u,
    PLAYER_SHOOT_SHOT_FLAGS = 60u,
    PLAYER_SHOOT_TARGET_IMPACT_X = 42u,
    PLAYER_SHOOT_TARGET_IMPACT_Z = 44u,
    PLAYER_SHOOT_VELOCITY_X = 18u,
    PLAYER_SHOOT_VELOCITY_Z = 22u,
    PLAYER_SHOOT_SHOT_POWER = 28u,
    PLAYER_SHOOT_ENTITY_ENEMY_FLAGS = 36u,
    PLAYER_SHOOT_VELOCITY_Y = 42u,
    PLAYER_SHOOT_SHOT_VERTICAL_POSITION = 44u,
    PLAYER_SHOOT_SHOT_WORRY = 62u,
    PLAYER_SHOOT_SHOT_IN_UPPER_ZONE = 63u,
    PLAYER_SHOOT_TYPE_AUX = 3u,
    PLAYER_SHOOT_TYPE_PROJECTILE = 2u,
    PLAYER_SHOOT_TARGET_TYPE_MASK = 0x23u,
    PLAYER_SHOOT_VERTICAL_SCALE_NUMERATOR = 93u,
    PLAYER_SHOOT_HEIGHT_ADJUSTMENT = 18 * 256,
    PLAYER_SHOOT_PROJECTILE_ENEMY_FLAGS = 0x23u,
    PLAYER_SHOOT_PROJECTILE_VERTICAL_SPEED_LIMIT = 20 * 128,
    PLAYER_SHOOT_PROJECTILE_ANGLE_SPACING = 128u,
    PLAYER_SHOOT_PROJECTILE_ANGLE_ADVANCE = 256u,
    PLAYER_SHOOT_PROJECTILE_Y_OFFSET = 30 * 128
};

static void player_shoot_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t player_shoot_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t player_shoot_read_be16s(const uint8_t *source)
{
    return (int16_t)player_shoot_read_be16(source);
}

static void player_shoot_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void player_shoot_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static int32_t player_shoot_asr32(int32_t value, unsigned int shift)
{
    if (value >= 0) {
        return value >> shift;
    }
    return -(((-(int64_t)value) + ((INT64_C(1) << shift) - 1)) >> shift);
}

/* 68000 ASR.W with a register count treats counts over 15 as sign-fill. */
static int16_t player_shoot_asr16_count(int16_t value, uint16_t count)
{
    unsigned int effective_count = count & 63u;

    if (effective_count >= 16u) {
        return value < 0 ? -1 : 0;
    }
    if (value >= 0) {
        return (int16_t)(value >> effective_count);
    }
    return (int16_t)-(((-(int32_t)value) + ((1 << effective_count) - 1)) >>
                     effective_count);
}

static int32_t player_shoot_muls16(int16_t left, int16_t right)
{
    return (int32_t)((int64_t)left * right);
}

static int32_t player_shoot_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

int player_shoot_find_target_single_player(const ObjectRuntime *objects,
                                           const ObjectObservation *observation,
                                           const PlayerRuntime *player,
                                           const GameBulletDefinition *bullet,
                                           PlayerShotTarget *out_target,
                                           char *error, size_t error_size)
{
    PlayerShotTarget target;
    uint16_t best_distance = INT16_MAX;

    if (!objects || !objects->slot_bytes || !observation || !player || !bullet || !out_target ||
        objects->active_slot_count > objects->slot_count) {
        player_shoot_set_error(error, error_size,
                               "Plr1_Shot target selection received invalid source state");
        return 0;
    }
    memset(&target, 0, sizeof(target));
    for (uint32_t slot_index = 0u; slot_index < objects->active_slot_count; ++slot_index) {
        const uint8_t *slot = objects->slot_bytes +
            (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
        int16_t point_index = player_shoot_read_be16s(slot + PLAYER_SHOOT_POINT_INDEX);
        uint8_t type_id;
        int32_t vertical_difference;
        int32_t vertical_magnitude;
        int16_t scaled_vertical_magnitude;
        uint16_t distance;

        if (point_index < 0) {
            break;
        }
        type_id = slot[PLAYER_SHOOT_TYPE_ID];
        if (type_id == PLAYER_SHOOT_TYPE_AUX) {
            /* newplayershoot.s does not consume an observation byte for AUX. */
            continue;
        }
        if ((uint16_t)point_index >= objects->point_count ||
            (uint16_t)point_index >= OBJECT_OBSERVATION_DISTANCE_COUNT ||
            (uint16_t)point_index >= OBJECT_OBSERVATION_IN_LINE_COUNT) {
            player_shoot_set_error(error, error_size,
                                   "Plr1_Shot target has an invalid source point");
            return 0;
        }
        if (observation->in_line[(uint16_t)point_index] == 0u ||
            (slot[PLAYER_SHOOT_SEES_PLAYER] & 1u) == 0u ||
            player_shoot_read_be16s(slot + PLAYER_SHOOT_ZONE_ID) < 0 ||
            type_id >= 32u ||
            (PLAYER_SHOOT_TARGET_TYPE_MASK & (UINT32_C(1) << type_id)) == 0u ||
            slot[PLAYER_SHOOT_HIT_POINTS] == 0u) {
            continue;
        }
        distance = observation->distances[(uint16_t)point_index];
        vertical_difference =
            (int32_t)player_shoot_read_be16s(slot + PLAYER_SHOOT_VERTICAL_POSITION) * 128 -
            player->y;
        vertical_magnitude = vertical_difference < 0 ?
            (int32_t)(0u - (uint32_t)vertical_difference) : vertical_difference;
        /* `muls #93,d2` uses the source longword's low signed word. */
        scaled_vertical_magnitude = (int16_t)player_shoot_asr32(
            (int32_t)(int16_t)vertical_magnitude * PLAYER_SHOOT_VERTICAL_SCALE_NUMERATOR, 12u);
        if (scaled_vertical_magnitude > (int16_t)distance || best_distance < distance) {
            continue;
        }
        best_distance = distance;
        target.found = UINT8_MAX;
        target.slot_index = slot_index;
        target.point_index = (uint16_t)point_index;
        target.distance = distance;
        target.vertical_difference = vertical_difference;
    }
    if (target.found != 0u) {
        int32_t vertical_numerator = target.vertical_difference - player->height +
            PLAYER_SHOOT_HEIGHT_ADJUSTMENT;
        int16_t divisor = player_shoot_asr16_count((int16_t)target.distance,
                                                    (uint16_t)bullet->speed);

        if (divisor <= 0) {
            divisor = 1;
        }
        /* 68000 DIVS is signed and truncates toward zero for valid source inputs. */
        target.vertical_speed = (int16_t)(vertical_numerator / divisor);
    }
    *out_target = target;
    return 1;
}

int player_shoot_apply_hitscan_success(ObjectRuntime *objects,
                                       const PlayerShotTarget *target,
                                       uint16_t bullet_type,
                                       const GameBulletDefinition *bullet,
                                       int16_t player_sine, int16_t player_cosine,
                                       uint8_t *out_impact_spawned,
                                       char *error, size_t error_size)
{
    uint8_t *target_slot;
    uint8_t *target_point;

    if (!objects || !target || !bullet || target->found == 0u ||
        target->slot_index >= objects->active_slot_count ||
        target->point_index >= objects->point_count) {
        player_shoot_set_error(error, error_size,
                               "hitscan success received an invalid source target");
        return 0;
    }
    if (!object_runtime_get_slot_bytes(objects, target->slot_index, &target_slot) ||
        !object_runtime_get_point_bytes(objects, target->point_index, &target_point)) {
        player_shoot_set_error(error, error_size,
                               "hitscan success target is outside owned source state");
        return 0;
    }
    if (out_impact_spawned) {
        *out_impact_spawned = 0u;
    }
    for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
         ++shot_index) {
        uint8_t *shot_slot;
        uint8_t *shot_point;
        uint16_t shot_point_index;
        int32_t impact_x;
        int32_t impact_z;

        if (!object_runtime_get_player_shot_slot_bytes(objects, shot_index, &shot_slot)) {
            player_shoot_set_error(error, error_size,
                                   "player-shot pool is outside owned source state");
            return 0;
        }
        if (player_shoot_read_be16s(shot_slot + PLAYER_SHOOT_ZONE_ID) >= 0) {
            continue;
        }
        shot_point_index = player_shoot_read_be16(shot_slot + PLAYER_SHOOT_POINT_INDEX);
        if (!object_runtime_get_point_bytes(objects, shot_point_index, &shot_point)) {
            player_shoot_set_error(error, error_size,
                                   "hitscan impact slot has an invalid source point");
            return 0;
        }
        /* plr1_HitscanSucceded copies both source Vec2L longwords verbatim. */
        memcpy(shot_point, target_point, OBJECT_RUNTIME_POINT_BYTE_COUNT);
        shot_slot[PLAYER_SHOOT_TYPE_ID] = 2u; /* OBJ_TYPE_PROJECTILE */
        shot_slot[PLAYER_SHOOT_SHOT_STATUS] = 1u;
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_SHOT_GRAVITY, 0u);
        shot_slot[PLAYER_SHOOT_SHOT_SIZE] = (uint8_t)bullet_type;
        shot_slot[PLAYER_SHOOT_SHOT_ANIMATION] = 0u;
        player_shoot_write_be32(
            shot_slot + PLAYER_SHOOT_SHOT_VERTICAL_POSITION,
            (uint32_t)((int32_t)player_shoot_read_be16s(
                target_slot + PLAYER_SHOOT_VERTICAL_POSITION) * 128));
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_ZONE_ID,
                                 player_shoot_read_be16(target_slot + PLAYER_SHOOT_ZONE_ID));
        shot_slot[PLAYER_SHOOT_SHOT_WORRY] = UINT8_MAX;
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_VERTICAL_POSITION,
                                 player_shoot_read_be16(
                                     target_slot + PLAYER_SHOOT_VERTICAL_POSITION));
        target_slot[PLAYER_SHOOT_DAMAGE_TAKEN] =
            (uint8_t)(target_slot[PLAYER_SHOOT_DAMAGE_TAKEN] +
                      (uint8_t)bullet->hit_damage);
        impact_x = (int32_t)player_sine * 8;
        impact_z = (int32_t)player_cosine * 8;
        player_shoot_write_be16(target_slot + PLAYER_SHOOT_TARGET_IMPACT_X,
                                 (uint16_t)((uint32_t)impact_x >> 16));
        player_shoot_write_be16(target_slot + PLAYER_SHOOT_TARGET_IMPACT_Z,
                                 (uint16_t)((uint32_t)impact_z >> 16));
        if (out_impact_spawned) {
            *out_impact_spawned = UINT8_MAX;
        }
        return 1;
    }
    /* Source returns before applying damage if all NUM_PLR_SHOT_DATA slots are live. */
    return 1;
}

int player_shoot_spawn_projectile_volley(ObjectRuntime *objects, const GameMath *math,
                                         const PlayerRuntime *player,
                                         uint16_t bullet_type,
                                         const GameBulletDefinition *bullet,
                                         uint16_t bullet_count,
                                         int16_t vertical_speed,
                                         uint32_t *out_spawned_count,
                                         char *error, size_t error_size)
{
    int16_t shot_angle;
    int16_t bullet_speed;
    int16_t clamped_vertical_speed;
    uint16_t remaining_count;
    uint32_t spawned_count = 0u;

    if (!objects || !math || !player || !bullet ||
        objects->active_slot_count > objects->slot_count ||
        (uint16_t)bullet->is_hitscan != 0u ||
        player->zone_index > INT16_MAX) {
        player_shoot_set_error(error, error_size,
                               "firefive received invalid non-hitscan source state");
        return 0;
    }
    if (out_spawned_count) {
        *out_spawned_count = 0u;
    }
    /*
     * plr1_FireProjectile: -(ShootT_BulCount_w - 1) * 128 + tempangpos,
     * followed by AMOD_A before firefive reads the sine table.
    */
    shot_angle = (int16_t)((uint16_t)player->yaw -
                           (uint16_t)((uint16_t)(bullet_count - 1u) *
                                      PLAYER_SHOOT_PROJECTILE_ANGLE_SPACING));
    shot_angle = (int16_t)game_math_wrap_angle_address((uint16_t)shot_angle);
    bullet_speed = (int16_t)(uint16_t)bullet->speed;
    clamped_vertical_speed = vertical_speed;
    if (clamped_vertical_speed > PLAYER_SHOOT_PROJECTILE_VERTICAL_SPEED_LIMIT) {
        clamped_vertical_speed = PLAYER_SHOOT_PROJECTILE_VERTICAL_SPEED_LIMIT;
    }
    if (clamped_vertical_speed < -PLAYER_SHOOT_PROJECTILE_VERTICAL_SPEED_LIMIT) {
        clamped_vertical_speed = -PLAYER_SHOOT_PROJECTILE_VERTICAL_SPEED_LIMIT;
    }
    /* firefive always attempts its first projectile, including a source count of zero. */
    remaining_count = bullet_count;
    for (;;) {
        uint8_t *shot_slot = NULL;
        uint8_t *shot_point;
        int16_t sine;
        int16_t cosine;
        int32_t velocity_x;
        int32_t velocity_z;
        int32_t launch_y;
        uint8_t found_free_slot = 0u;

        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            if (!object_runtime_get_player_shot_slot_bytes(objects, shot_index, &shot_slot)) {
                player_shoot_set_error(error, error_size,
                                       "firefive player-shot pool is outside owned source state");
                return 0;
            }
            if (player_shoot_read_be16s(shot_slot + PLAYER_SHOOT_ZONE_ID) < 0) {
                found_free_slot = UINT8_MAX;
                break;
            }
        }
        /* firefive returns immediately if its source player-shot pool is full. */
        if (found_free_slot == 0u) {
            break;
        }
        if (!object_runtime_get_point_bytes(
                objects, player_shoot_read_be16(shot_slot + PLAYER_SHOOT_POINT_INDEX),
                &shot_point)) {
            player_shoot_set_error(error, error_size,
                                   "firefive player-shot slot has an invalid source point");
            return 0;
        }
        if (!game_math_sine(math, (uint16_t)shot_angle, &sine, error, error_size) ||
            !game_math_cosine(math, (uint16_t)shot_angle, &cosine, error, error_size)) {
            return 0;
        }
        velocity_x = player_shoot_add32(
            player_shoot_muls16(sine, bullet_speed), player_shoot_muls16(sine, bullet_speed));
        velocity_z = player_shoot_add32(
            player_shoot_muls16(cosine, bullet_speed), player_shoot_muls16(cosine, bullet_speed));
        launch_y = player_shoot_add32(player->y, PLAYER_SHOOT_PROJECTILE_Y_OFFSET);
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_SHOT_GRAVITY,
                                 (uint16_t)bullet->gravity);
        shot_slot[PLAYER_SHOOT_SHOT_FLAGS] = (uint8_t)bullet->bounce_horizontal;
        shot_slot[PLAYER_SHOOT_SHOT_FLAGS + 1u] = (uint8_t)bullet->bounce_vertical;
        shot_slot[PLAYER_SHOOT_SHOT_SIZE] = (uint8_t)bullet_type;
        shot_slot[PLAYER_SHOOT_SHOT_POWER] = (uint8_t)bullet->hit_damage;
        /* firefive's move.w writes the high source word and retains each Vec2L tail. */
        player_shoot_write_be16(shot_point + 0u, (uint16_t)player->x);
        player_shoot_write_be16(shot_point + 4u, (uint16_t)player->z);
        player_shoot_write_be32(shot_slot + PLAYER_SHOOT_VELOCITY_X, (uint32_t)velocity_x);
        player_shoot_write_be32(shot_slot + PLAYER_SHOOT_VELOCITY_Z, (uint32_t)velocity_z);
        shot_slot[PLAYER_SHOOT_TYPE_ID] = PLAYER_SHOOT_TYPE_PROJECTILE;
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_VELOCITY_Y,
                                 (uint16_t)clamped_vertical_speed);
        shot_slot[PLAYER_SHOOT_SHOT_IN_UPPER_ZONE] = player->stood_in_top;
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_SHOT_LIFETIME, 0u);
        player_shoot_write_be32(shot_slot + PLAYER_SHOOT_ENTITY_ENEMY_FLAGS,
                                 PLAYER_SHOOT_PROJECTILE_ENEMY_FLAGS);
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_ZONE_ID, player->zone_index);
        player_shoot_write_be32(shot_slot + PLAYER_SHOOT_SHOT_VERTICAL_POSITION,
                                 (uint32_t)launch_y);
        shot_slot[PLAYER_SHOOT_SHOT_WORRY] = UINT8_MAX;
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_VERTICAL_POSITION,
                                 (uint16_t)player_shoot_asr32(launch_y, 7u));
        ++spawned_count;
        --remaining_count;
        if ((int16_t)remaining_count <= 0) {
            break;
        }
        shot_angle = (int16_t)game_math_wrap_angle_address(
            (uint16_t)((uint16_t)shot_angle + PLAYER_SHOOT_PROJECTILE_ANGLE_ADVANCE));
    }
    if (out_spawned_count) {
        *out_spawned_count = spawned_count;
    }
    return 1;
}
