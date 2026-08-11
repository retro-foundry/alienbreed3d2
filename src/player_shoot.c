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
    PLAYER_SHOOT_ENTITY_TIMER1 = 34u,
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

/* 68000 ASL.L with a register count: only the low six count bits participate. */
static int32_t player_shoot_asl32_count(int32_t value, uint16_t count)
{
    unsigned int effective_count = count & 63u;

    if (effective_count >= 32u) {
        return 0;
    }
    return (int32_t)((uint32_t)value << effective_count);
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

static int16_t player_shoot_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t player_shoot_manual_vertical_speed(const PlayerRuntime *player,
                                                  const GameBulletDefinition *bullet)
{
    uint16_t shift_count;

    /* Plr1_Shot:.no_auto_aim / .nothing_to_shoot, including ASR.W's count. */
    shift_count = (uint16_t)(UINT16_C(8) - (uint16_t)bullet->speed);
    return player_shoot_asr16_count((int16_t)player->aim_speed, shift_count);
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
    uint32_t observation_index = 0u;

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
        uint8_t in_line;
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
        /*
         * transform.s:CalcPLR1InLine writes Plr1_ObsInLine_vb densely for
         * non-AUX ObjT records.  Plr1_Shot consumes the same dense sequence
         * with (a1)+, then uses ObjT_PointID for the distance workspace.
         */
        if (observation_index >= OBJECT_OBSERVATION_IN_LINE_COUNT ||
            (uint16_t)point_index >= objects->point_count ||
            (uint16_t)point_index >= OBJECT_OBSERVATION_DISTANCE_COUNT) {
            player_shoot_set_error(error, error_size,
                                   "Plr1_Shot target is outside source observation or point state");
            return 0;
        }
        in_line = observation->in_line[observation_index];
        ++observation_index;
        if (in_line == 0u ||
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

int player_shoot_hitscan_roll_is_hit(const ObjectRuntime *objects,
                                     const PlayerShotTarget *target,
                                     const PlayerRuntime *player,
                                     GameRandom *random,
                                     uint8_t *out_hit,
                                     char *error, size_t error_size)
{
    const uint8_t *target_slot;
    const uint8_t *target_point;
    int16_t point_index;
    int16_t delta_x;
    int16_t delta_z;
    int32_t distance;
    int32_t roll;

    if (!objects || !objects->slot_bytes || !objects->point_bytes || !target || !player ||
        !random || !out_hit || target->found == 0u ||
        target->slot_index >= objects->active_slot_count ||
        objects->active_slot_count > objects->slot_count) {
        player_shoot_set_error(error, error_size,
                               "Plr1_Shot hitscan roll received invalid source state");
        return 0;
    }
    target_slot = objects->slot_bytes +
        (size_t)target->slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
    point_index = player_shoot_read_be16s(target_slot + PLAYER_SHOOT_POINT_INDEX);
    if (point_index < 0 || (uint16_t)point_index >= objects->point_count) {
        player_shoot_set_error(error, error_size,
                               "Plr1_Shot hitscan target has an invalid source point");
        return 0;
    }
    target_point = objects->point_bytes +
        (size_t)(uint16_t)point_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;

    /*
     * `newplayershoot.s:.fire_hitscanned_bullets` uses only the high words
     * of Vec2L and Plr1_XOff_l/Plr1_ZOff_l before its signed MULS/ASR path.
     */
    delta_x = player_shoot_add16(player_shoot_read_be16s(target_point + 0u),
                                 (int16_t)-player_runtime_position_to_world(player->x));
    delta_z = player_shoot_add16(player_shoot_read_be16s(target_point + 4u),
                                 (int16_t)-player_runtime_position_to_world(player->z));
    distance = player_shoot_asr32(
        player_shoot_add32(player_shoot_muls16(delta_x, delta_x),
                           player_shoot_muls16(delta_z, delta_z)),
        6u);
    roll = (int32_t)((uint32_t)(game_random_next(random) & 0x7fffu) << 1);
    *out_hit = roll > distance ? UINT8_MAX : 0u;
    return 1;
}

int player_shoot_update_single_player_with_motion_and_audio(
    ObjectRuntime *objects, LevelDynamicState *dynamic_level,
    const ObjectObservation *observation, PlayerRuntime *player,
    ObjectMotionRuntime *motion_runtime, GameInventory *inventory,
    const GameLink *game_link, const GamePreferences *preferences,
    const GameMath *math, GameRandom *random, uint16_t frame_ticks, uint8_t infinite_ammo,
    GameAudioEvents *audio_events, char *error, size_t error_size)
{
    GameShootDefinition shoot;
    GameBulletDefinition bullet;
    PlayerShotTarget target;
    uint8_t *weapon_slot;
    uint16_t ammunition;
    int16_t vertical_speed;
    int16_t player_sine;
    int16_t player_cosine;

    if (!objects || !dynamic_level || !observation || !player || !inventory || !game_link ||
        !preferences || !math || !random) {
        player_shoot_set_error(error, error_size, "Plr1_Shot received invalid source state");
        return 0;
    }
    if (player->time_to_shoot != 0) {
        player->time_to_shoot =
            (int16_t)((uint16_t)player->time_to_shoot - frame_ticks);
        if (player->time_to_shoot >= 0) {
            return 1;
        }
        player->time_to_shoot = 0;
        return 1;
    }
    if (player->tmp_gun_selected >= GAME_LINK_GUN_COUNT ||
        !game_link_get_shoot_definition(game_link, player->tmp_gun_selected, &shoot,
                                        error, error_size) ||
        shoot.bullet_type >= GAME_INVENTORY_AMMUNITION_COUNT ||
        !game_link_get_bullet_definition(game_link, shoot.bullet_type, &bullet,
                                         error, error_size)) {
        player_shoot_set_error(error, error_size,
                               "Plr1_Shot selected an invalid GLFT weapon or bullet");
        return 0;
    }
    if (player->tmp_fire == 0u) {
        return 1;
    }
    if (!game_math_sine(math, player->yaw, &player_sine, error, error_size) ||
        !game_math_cosine(math, player->yaw, &player_cosine, error, error_size) ||
        !player_shoot_find_target_single_player(objects, observation, player, &bullet,
                                                &target, error, error_size)) {
        return 0;
    }
    ammunition = inventory->ammunition[shoot.bullet_type];
    if (infinite_ammo == 0u && (int16_t)ammunition < (int16_t)shoot.bullet_count) {
        /* newplayershoot.s:Plr1_Shot no-ammunition MakeSomeNoise (slot 12). */
        player->noise_volume = 100;
        game_audio_events_emit_with_source_id_high_byte(
            audio_events, 12, 100,
            player_runtime_position_to_world(player->x),
            player_runtime_position_to_world(player->z),
            UINT8_C(0xfb), GAME_AUDIO_RESTART_SOURCE, 0u, 0u);
        return 1;
    }
    if (objects->player1_slot > UINT32_MAX - 2u ||
        !object_runtime_get_slot_bytes(objects, objects->player1_slot + 2u, &weapon_slot)) {
        player_shoot_set_error(error, error_size,
                               "Plr1_Shot weapon entity is outside owned source state");
        return 0;
    }
    /* newplayershoot.s:.okcanshoot activates Plr1_Use's companion weapon ObjT. */
    player_shoot_write_be16(weapon_slot + PLAYER_SHOOT_ENTITY_TIMER1, 1u);
    player->time_to_shoot = (int16_t)shoot.delay;
    /* Match the first port's player.c desktop branch: preserve the source
     * firing/cooldown sequence, but skip only this source ammunition debit. */
    if (infinite_ammo == 0u) {
        inventory->ammunition[shoot.bullet_type] =
            (uint16_t)(ammunition - shoot.bullet_count);
    }
    /* newplayershoot.s:.okcanshoot emits ShootT_SFX_w at the player point. */
    player->noise_volume = 100;
    game_audio_events_emit_with_source_id_high_byte(
        audio_events, (int16_t)shoot.sound_effect, 300,
        player_runtime_position_to_world(player->x),
        player_runtime_position_to_world(player->z),
        UINT8_C(0xfb), GAME_AUDIO_RESTART_SOURCE, 2u, 0u);

    vertical_speed = target.found != 0u ? target.vertical_speed :
        player_shoot_manual_vertical_speed(player, &bullet);
    if ((player->mouse_active != 0u && preferences->no_auto_aim != 0u) ||
        bullet.gravity != 0u) {
        vertical_speed = player_shoot_manual_vertical_speed(player, &bullet);
    }
    if ((uint16_t)bullet.is_hitscan == 0u) {
        return player_shoot_spawn_projectile_volley(objects, math, player, shoot.bullet_type,
                                                    &bullet, shoot.bullet_count, vertical_speed,
                                                    NULL, error, error_size);
    }
    if (target.found == 0u) {
        /* .nothing_to_shoot forces bulyspd to zero and fires one wall impact. */
        return player_shoot_apply_hitscan_miss_with_motion(
            objects, dynamic_level, player, math, motion_runtime, random, shoot.bullet_type,
            NULL, error, error_size);
    }
    {
        int16_t remaining = (int16_t)shoot.bullet_count;

        /* The source always attempts one bullet, including a zero word count. */
        for (;;) {
            uint8_t hit;

            if (!player_shoot_hitscan_roll_is_hit(objects, &target, player, random, &hit,
                                                  error, error_size)) {
                return 0;
            }
            if (hit != 0u) {
                if (!player_shoot_apply_hitscan_success(
                        objects, &target, shoot.bullet_type, &bullet, player_sine, player_cosine,
                        NULL, error, error_size)) {
                    return 0;
                }
            } else if (!player_shoot_apply_hitscan_target_miss_with_motion(
                           objects, dynamic_level, player, &target, math, motion_runtime,
                           random, shoot.bullet_type, NULL, error, error_size)) {
                return 0;
            }
            remaining = (int16_t)((uint16_t)remaining - 1u);
            if (remaining <= 0) {
                break;
            }
        }
    }
    return 1;
}

int player_shoot_update_single_player_with_motion(
    ObjectRuntime *objects, LevelDynamicState *dynamic_level,
    const ObjectObservation *observation, PlayerRuntime *player,
    ObjectMotionRuntime *motion_runtime, GameInventory *inventory,
    const GameLink *game_link, const GamePreferences *preferences,
    const GameMath *math, GameRandom *random, uint16_t frame_ticks,
    char *error, size_t error_size)
{
    return player_shoot_update_single_player_with_motion_and_audio(
        objects, dynamic_level, observation, player, motion_runtime, inventory, game_link,
        preferences, math, random, frame_ticks, 0u, NULL, error, error_size);
}

int player_shoot_update_single_player(ObjectRuntime *objects,
                                      LevelDynamicState *dynamic_level,
                                      const ObjectObservation *observation,
                                      PlayerRuntime *player,
                                      GameInventory *inventory,
                                      const GameLink *game_link,
                                      const GamePreferences *preferences,
                                      const GameMath *math,
                                      GameRandom *random,
                                      uint16_t frame_ticks,
                                      char *error, size_t error_size)
{
    return player_shoot_update_single_player_with_motion(
        objects, dynamic_level, observation, player, NULL, inventory, game_link,
        preferences, math, random, frame_ticks, error, error_size);
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

static int player_shoot_apply_hitscan_miss_internal(
    ObjectRuntime *objects, LevelDynamicState *dynamic_level,
    const PlayerRuntime *player, const PlayerShotTarget *target, const GameMath *math,
    ObjectMotionRuntime *motion_runtime, GameRandom *random, uint16_t bullet_type,
    uint8_t *out_impact_spawned, char *error, size_t error_size)
{
    ObjectMovementTrace trace = {0};
    int16_t sine;
    int16_t cosine;

    if (!objects || !dynamic_level || !player || !math || !random ||
        !objects->slot_bytes || !objects->point_bytes ||
        objects->active_slot_count > objects->slot_count) {
        player_shoot_set_error(error, error_size,
                               "plr1_HitscanFailed received invalid source state");
        return 0;
    }
    if (out_impact_spawned) {
        *out_impact_spawned = 0u;
    }
    trace.zone_index = player->zone_index;
    trace.old_x = player_runtime_position_to_world(player->x);
    trace.old_z = player_runtime_position_to_world(player->z);
    trace.old_y = player_shoot_add32(player->y, 10 * 128);
    if (target && target->found != 0u) {
        uint8_t *target_point;
        uint8_t *terminal_slot;
        int16_t target_x;
        int16_t target_z;

        if (target->slot_index >= objects->active_slot_count ||
            target->point_index >= objects->point_count ||
            objects->active_slot_count >= objects->slot_count ||
            !object_runtime_get_point_bytes(objects, target->point_index, &target_point) ||
            !object_runtime_get_slot_bytes(objects, objects->active_slot_count,
                                           &terminal_slot)) {
            player_shoot_set_error(error, error_size,
                                   "plr1_HitscanFailed target miss has invalid source state");
            return 0;
        }
        /*
         * Plr1_Shot retains a4 as the selected ObjT while a0 remains on the
         * first-word -1 terminator.  plr1_HitscanFailed traces to the target
         * midpoint and takes newy from that terminator's vertical word.
         */
        target_x = player_shoot_read_be16s(target_point + 0u);
        target_z = player_shoot_read_be16s(target_point + 4u);
        trace.new_x = player_shoot_add16(
            trace.old_x,
            player_shoot_asr16_count(
                player_shoot_add16(target_x, (int16_t)-trace.old_x), 1u));
        trace.new_z = player_shoot_add16(
            trace.old_z,
            player_shoot_asr16_count(
                player_shoot_add16(target_z, (int16_t)-trace.old_z), 1u));
        trace.new_y = (int32_t)player_shoot_read_be16s(
            terminal_slot + PLAYER_SHOOT_VERTICAL_POSITION) * 128;
    } else {
        uint16_t random_value;

        if (!game_math_sine(math, player->yaw, &sine, error, error_size) ||
            !game_math_cosine(math, player->yaw, &cosine, error, error_size)) {
            return 0;
        }
        trace.new_x = player_shoot_add16(trace.old_x, player_shoot_asr16_count(sine, 7u));
        trace.new_z = player_shoot_add16(trace.old_z, player_shoot_asr16_count(cosine, 7u));
        random_value = game_random_next(random);
        trace.new_y = player_shoot_add32(
            trace.old_y, (int32_t)((int32_t)(random_value & 0x0fffu) - 0x0800));
    }
    trace.wall_flags = 0x0400u;
    trace.away_from_wall = -1;
    trace.exit_first = UINT8_MAX;
    trace.step_down = 0x1000000;
    /* newplayershoot.s publishes this ray's newx/newz before MoveObject. */
    object_motion_runtime_set_new_words(motion_runtime, trace.new_x, trace.new_z);

    for (;;) {
        int16_t ray_x;
        int16_t ray_z;
        int32_t ray_y;

        if (!object_movement_trace_zero_extension(dynamic_level, &trace,
                                                  error, error_size)) {
            return 0;
        }
        object_motion_runtime_set_new_words(motion_runtime, trace.new_x, trace.new_z);
        if (trace.hit_wall != 0u) {
            break;
        }
        /* plr1_HitscanFailed:.again advances its unchanged one-word ray. */
        ray_x = (int16_t)((uint16_t)trace.new_x - (uint16_t)trace.old_x);
        ray_z = (int16_t)((uint16_t)trace.new_z - (uint16_t)trace.old_z);
        ray_y = (int32_t)((uint32_t)trace.new_y - (uint32_t)trace.old_y);
        trace.old_x = player_shoot_add16(trace.old_x, ray_x);
        trace.new_x = player_shoot_add16(trace.new_x, ray_x);
        trace.old_z = player_shoot_add16(trace.old_z, ray_z);
        trace.new_z = player_shoot_add16(trace.new_z, ray_z);
        trace.old_y = player_shoot_add32(trace.old_y, ray_y);
        trace.new_y = player_shoot_add32(trace.new_y, ray_y);
        object_motion_runtime_set_new_words(motion_runtime, trace.new_x, trace.new_z);
    }
    for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
         ++shot_index) {
        uint8_t *shot_slot;
        uint8_t *shot_point;
        uint16_t point_index;
        LevelZone zone;

        if (!object_runtime_get_player_shot_slot_bytes(objects, shot_index, &shot_slot)) {
            player_shoot_set_error(error, error_size,
                                   "plr1_HitscanFailed player-shot pool is outside source state");
            return 0;
        }
        if (player_shoot_read_be16s(shot_slot + PLAYER_SHOOT_ZONE_ID) >= 0) {
            continue;
        }
        point_index = player_shoot_read_be16(shot_slot + PLAYER_SHOOT_POINT_INDEX);
        if (!object_runtime_get_point_bytes(objects, point_index, &shot_point)) {
            player_shoot_set_error(error, error_size,
                                   "plr1_HitscanFailed miss slot has an invalid source point");
            return 0;
        }
        if (!level_runtime_get_zone(&dynamic_level->runtime, trace.zone_index, &zone,
                                    error, error_size)) {
            return 0;
        }
        /* move.w updates only Vec2L's source coordinate words. */
        player_shoot_write_be16(shot_point + 0u, (uint16_t)trace.new_x);
        player_shoot_write_be16(shot_point + 4u, (uint16_t)trace.new_z);
        shot_slot[PLAYER_SHOOT_SHOT_STATUS] = 1u;
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_SHOT_GRAVITY, 0u);
        shot_slot[PLAYER_SHOOT_SHOT_SIZE] = (uint8_t)bullet_type;
        shot_slot[PLAYER_SHOOT_SHOT_ANIMATION] = 0u;
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_ZONE_ID, zone.id);
        shot_slot[PLAYER_SHOOT_SHOT_WORRY] = UINT8_MAX;
        player_shoot_write_be32(shot_slot + PLAYER_SHOOT_SHOT_VERTICAL_POSITION,
                                (uint32_t)trace.wall_hit_height);
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_VERTICAL_POSITION,
                                (uint16_t)player_shoot_asr32(trace.wall_hit_height, 7u));
        if (out_impact_spawned) {
            *out_impact_spawned = UINT8_MAX;
        }
        return 1;
    }
    /* The source returns unchanged when all NUM_PLR_SHOT_DATA records are live. */
    return 1;
}

int player_shoot_apply_hitscan_miss_with_motion(
    ObjectRuntime *objects, LevelDynamicState *dynamic_level,
    const PlayerRuntime *player, const GameMath *math,
    ObjectMotionRuntime *motion_runtime, GameRandom *random, uint16_t bullet_type,
    uint8_t *out_impact_spawned, char *error, size_t error_size)
{
    return player_shoot_apply_hitscan_miss_internal(
        objects, dynamic_level, player, NULL, math, motion_runtime, random, bullet_type,
        out_impact_spawned, error, error_size);
}

int player_shoot_apply_hitscan_target_miss_with_motion(
    ObjectRuntime *objects, LevelDynamicState *dynamic_level,
    const PlayerRuntime *player, const PlayerShotTarget *target, const GameMath *math,
    ObjectMotionRuntime *motion_runtime, GameRandom *random, uint16_t bullet_type,
    uint8_t *out_impact_spawned, char *error, size_t error_size)
{
    return player_shoot_apply_hitscan_miss_internal(
        objects, dynamic_level, player, target, math, motion_runtime, random, bullet_type,
        out_impact_spawned, error, error_size);
}

int player_shoot_apply_hitscan_miss(ObjectRuntime *objects,
                                    LevelDynamicState *dynamic_level,
                                    const PlayerRuntime *player, const GameMath *math,
                                    GameRandom *random, uint16_t bullet_type,
                                    uint8_t *out_impact_spawned,
                                    char *error, size_t error_size)
{
    return player_shoot_apply_hitscan_miss_with_motion(
        objects, dynamic_level, player, math, NULL, random, bullet_type, out_impact_spawned,
        error, error_size);
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
        /*
         * newplayershoot.s:firefive sign-extends the SinCosTable values and
         * applies `asl.l BulletSpd,Dn`. BulT_Speed_l is therefore a source
         * fixed-point shift count, not a scalar multiplier.
         */
        velocity_x = player_shoot_asl32_count(sine, (uint16_t)bullet_speed);
        velocity_z = player_shoot_asl32_count(cosine, (uint16_t)bullet_speed);
        launch_y = player_shoot_add32(player->y, PLAYER_SHOOT_PROJECTILE_Y_OFFSET);
        player_shoot_write_be16(shot_slot + PLAYER_SHOOT_SHOT_GRAVITY,
                                 (uint16_t)bullet->gravity);
        shot_slot[PLAYER_SHOOT_SHOT_FLAGS] = (uint8_t)bullet->bounce_horizontal;
        shot_slot[PLAYER_SHOOT_SHOT_FLAGS + 1u] = (uint8_t)bullet->bounce_vertical;
        shot_slot[PLAYER_SHOOT_SHOT_SIZE] = (uint8_t)bullet_type;
        shot_slot[PLAYER_SHOOT_SHOT_POWER] = (uint8_t)bullet->hit_damage;
        /* firefive's move.w writes the high source word and retains each Vec2L tail. */
        player_shoot_write_be16(shot_point + 0u,
                                 (uint16_t)player_runtime_position_to_world(player->x));
        player_shoot_write_be16(shot_point + 4u,
                                 (uint16_t)player_runtime_position_to_world(player->z));
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
