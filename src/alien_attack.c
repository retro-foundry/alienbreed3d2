#include "alien_attack.h"

#include <limits.h>
#include <stdio.h>

#include "alien_memory.h"
#include "alien_decision.h"
#include "alien_math.h"
#include "alien_perception.h"
#include "alien_torch.h"
#include "object_heading.h"
#include "object_movement.h"

#include <string.h>

enum {
    /* defs.i: ObjT/EntT/ShotT fields used by ai_AttackCommon/FireAtPlayer1. */
    ALIEN_ATTACK_SLOT_POINT_INDEX = 0u,
    ALIEN_ATTACK_SLOT_Y_POSITION = 4u,
    ALIEN_ATTACK_SLOT_ZONE_ID = 12u,
    ALIEN_ATTACK_SLOT_TYPE_ID = 16u,
    ALIEN_ATTACK_SLOT_SEES_PLAYER = 17u,
    ALIEN_ATTACK_SLOT_DAMAGE_TAKEN = 19u,
    ALIEN_ATTACK_SLOT_CURRENT_MODE = 20u,
    ALIEN_ATTACK_SLOT_ENTITY_ZONE_ID = 26u,
    ALIEN_ATTACK_SLOT_CURRENT_ANGLE = 30u,
    ALIEN_ATTACK_SLOT_TIMER1 = 34u,
    ALIEN_ATTACK_SLOT_TIMER2 = 40u,
    ALIEN_ATTACK_SLOT_IMPACT_X = 42u,
    ALIEN_ATTACK_SLOT_IMPACT_Z = 44u,
    ALIEN_ATTACK_SLOT_WHICH_ANIMATION = 55u,
    ALIEN_ATTACK_SLOT_ENTITY_TYPE = 54u,
    ALIEN_ATTACK_SHOT_VELOCITY_X = 18u,
    ALIEN_ATTACK_SHOT_VELOCITY_Z = 22u,
    ALIEN_ATTACK_SHOT_POWER = 28u,
    ALIEN_ATTACK_SHOT_SIZE = 31u,
    ALIEN_ATTACK_SHOT_ENEMY_FLAGS = 36u,
    ALIEN_ATTACK_SHOT_VELOCITY_Y = 42u,
    ALIEN_ATTACK_SHOT_ACCUMULATED_Y = 44u,
    ALIEN_ATTACK_SHOT_LIFETIME = 58u,
    ALIEN_ATTACK_SHOT_WORRY = 62u,
    ALIEN_ATTACK_SHOT_IN_UPPER_ZONE = 63u,
    ALIEN_ATTACK_OBJECT_TYPE_PROJECTILE = 2u,
    ALIEN_ATTACK_DAMAGE_MODE = 4u,
    ALIEN_ATTACK_FOLLOWUP_MODE = 2u,
    ALIEN_ATTACK_RESPONSE_MODE = 1u,
    ALIEN_ATTACK_PLAYER_AIM_HEIGHT = 20
};

static void alien_attack_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_attack_bset_longword_low_word(uint32_t bit_number)
{
    /* 68000 BSET Dn,Dm uses the low five bits of Dn for a longword target. */
    uint32_t result = UINT32_C(1) << (bit_number & 31u);

    return (uint16_t)result;
}

static uint16_t alien_attack_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t alien_attack_read_be16s(const uint8_t *source)
{
    return (int16_t)alien_attack_read_be16(source);
}

static void alien_attack_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void alien_attack_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static int16_t alien_attack_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int16_t alien_attack_sub16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left - (uint16_t)right);
}

static int32_t alien_attack_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t alien_attack_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int32_t alien_attack_asr32(int32_t value, unsigned int count)
{
    if (value >= 0) {
        return value >> count;
    }
    return -((-(int64_t)value + ((INT64_C(1) << count) - 1)) >> count);
}

/* 68000 ASR.W with a register count uses the count modulo 64. */
static int16_t alien_attack_asr16_count(int16_t value, uint16_t count)
{
    unsigned int effective_count = count & 63u;

    if (effective_count >= 16u) {
        return value < 0 ? -1 : 0;
    }
    return (int16_t)alien_attack_asr32(value, effective_count);
}

static int16_t alien_attack_high_word(int32_t value)
{
    return (int16_t)((uint32_t)value >> 16u);
}

static int32_t alien_attack_muls16(int16_t left, int16_t right)
{
    return (int32_t)left * (int32_t)right;
}

static int alien_attack_divs16(int32_t dividend, int16_t divisor,
                               int16_t *out_quotient,
                               char *error, size_t error_size)
{
    int32_t quotient;

    if (!out_quotient || divisor == 0) {
        alien_attack_set_error(error, error_size,
                               "alien attack DIVS received invalid source operands");
        return 0;
    }
    /*
     * 68000 DIVS.W leaves Dn unchanged on quotient overflow; modules/ai.s
     * ignores the V flag and subsequently consumes Dn's low word. Preserve
     * that destination-register result for ai_AttackWithHitScan and FireAtPlayer1.
     */
    if (dividend == INT32_MIN && divisor == -1) {
        *out_quotient = (int16_t)(uint16_t)dividend;
        return 1;
    }
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        *out_quotient = (int16_t)(uint16_t)dividend;
        return 1;
    }
    *out_quotient = (int16_t)quotient;
    return 1;
}

static void alien_attack_add_facing(uint8_t *slot, uint16_t facing)
{
    alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_CURRENT_ANGLE,
                            (uint16_t)(alien_attack_read_be16(
                                slot + ALIEN_ATTACK_SLOT_CURRENT_ANGLE) + facing));
}

int alien_attack_setup_from_slot(const ObjectRuntime *objects, uint32_t slot_index,
                                const GameLink *game_link,
                                AlienAttackSetup *out_setup,
                                char *error, size_t error_size)
{
    uint8_t *slot;
    GameAlienDefinition alien;
    GameBulletDefinition bullet;
    AlienAttackSetup setup;

    if (!objects || !game_link || !out_setup || slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes((ObjectRuntime *)objects, slot_index, &slot)) {
        alien_attack_set_error(error, error_size, "ai_AttackCommon received invalid source state");
        return 0;
    }

    /*
     * ai_AttackCommon reads AlienT_BulType_w, then uses that same word to
     * address BulT before retaining only its low byte in SHOTTYPE.
     */
    if (!game_link_get_alien_definition(game_link, slot[ALIEN_ATTACK_SLOT_ENTITY_TYPE], &alien,
                                        error, error_size) ||
        !game_link_get_bullet_definition(game_link, alien.bullet_type, &bullet,
                                         error, error_size)) {
        return 0;
    }

    setup.shot_type = (uint8_t)alien.bullet_type;
    setup.shot_power = (uint8_t)bullet.hit_damage;
    setup.shot_speed = alien_attack_bset_longword_low_word(bullet.speed);
    /* `sub.w #1,d0` retains the low source word of BulT_Speed_l. */
    setup.shot_shift = (uint16_t)(bullet.speed - 1u);
    setup.is_hitscan = bullet.is_hitscan != 0u ? UINT8_MAX : 0u;
    *out_setup = setup;
    return 1;
}

int alien_attack_fire_at_player_one(ObjectRuntime *objects, uint32_t alien_slot_index,
                                    const PlayerRuntime *player,
                                    const AlienSetup *alien_setup,
                                    const AlienAttackSetup *attack_setup,
                                    const ObjectObservation *observation,
                                    GameAudioEvents *audio_events,
                                    uint8_t *out_spawned,
                                    char *error, size_t error_size)
{
    uint8_t *alien_slot;
    uint8_t *alien_point;
    uint8_t *player_slot;
    uint8_t *shot_slot = NULL;
    uint8_t *shot_point;
    ObjectApproach approach = {0};
    int16_t lead;
    int16_t player_height;
    int16_t vertical_divisor;
    int32_t accumulated_y;

    if (out_spawned) {
        *out_spawned = 0u;
    }
    if (!objects || !player || !alien_setup || !attack_setup ||
        alien_slot_index >= objects->active_slot_count ||
        objects->active_slot_count > objects->slot_count ||
        !object_runtime_get_slot_bytes(objects, alien_slot_index, &alien_slot) ||
        !object_runtime_get_point_bytes(
            objects, alien_attack_read_be16(alien_slot + ALIEN_ATTACK_SLOT_POINT_INDEX),
            &alien_point) ||
        !object_runtime_get_player1_slot_bytes(objects, &player_slot)) {
        alien_attack_set_error(error, error_size, "FireAtPlayer1 received invalid source state");
        return 0;
    }
    if (attack_setup->is_hitscan != 0u) {
        alien_attack_set_error(error, error_size,
                               "FireAtPlayer1 received a hitscan ai_AttackCommon state");
        return 0;
    }
    if ((int16_t)attack_setup->shot_speed == 0) {
        alien_attack_set_error(error, error_size,
                               "FireAtPlayer1 SHOTSPEED is zero before its source DIVS");
        return 0;
    }

    /* AI_AlienShotDataPtr_l is scanned in source pool order for a negative zone. */
    for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
         ++shot_index) {
        if (!object_runtime_get_alien_shot_slot_bytes(objects, shot_index, &shot_slot)) {
            alien_attack_set_error(error, error_size,
                                   "FireAtPlayer1 alien-shot pool is outside source state");
            return 0;
        }
        if (alien_attack_read_be16s(shot_slot + ALIEN_ATTACK_SLOT_ZONE_ID) < 0) {
            break;
        }
        shot_slot = NULL;
    }
    /* .cantshoot performs no source state writes. */
    if (!shot_slot) {
        return 1;
    }
    if (!object_runtime_get_point_bytes(
            objects, alien_attack_read_be16(shot_slot + ALIEN_ATTACK_SLOT_POINT_INDEX),
            &shot_point)) {
        alien_attack_set_error(error, error_size,
                               "FireAtPlayer1 shot slot has an invalid source point");
        return 0;
    }

    shot_slot[ALIEN_ATTACK_SLOT_TYPE_ID] = ALIEN_ATTACK_OBJECT_TYPE_PROJECTILE;
    alien_attack_write_be16(shot_slot + ALIEN_ATTACK_SHOT_LIFETIME, 0u);
    shot_slot[ALIEN_ATTACK_SHOT_SIZE] = attack_setup->shot_type;
    /* The source uses MOVE.B despite ShotT_Power_w's word declaration. */
    shot_slot[ALIEN_ATTACK_SHOT_POWER] = attack_setup->shot_power;
    /*
     * FireAtPlayer1 does not select a sample. It reuses Aud_SampleNum_w from
     * DOALLANIMS byte five, raises the volume, and restarts this alien's ID.
     */
    if (audio_events) {
        uint16_t alien_point_index = alien_attack_read_be16(
            alien_slot + ALIEN_ATTACK_SLOT_POINT_INDEX);

        if (!observation || alien_point_index >= OBJECT_OBSERVATION_DISTANCE_COUNT) {
            alien_attack_set_error(error, error_size,
                                   "FireAtPlayer1 audio point is outside ObjRotated state");
            return 0;
        }
        game_audio_events_emit_current_sample_relative(
            audio_events, 100, observation->rotated_x[alien_point_index],
            observation->rotated_z[alien_point_index], alien_point_index,
            GAME_AUDIO_RESTART_SOURCE, 1u, alien_setup->zone_echo);
    }

    approach.old_x = alien_attack_read_be16s(alien_point);
    approach.old_z = alien_attack_read_be16s(alien_point + 4u);
    approach.new_x = player_runtime_position_to_world(player->x);
    approach.new_z = player_runtime_position_to_world(player->z);
    if (!object_heading_calculate_distance(&approach, error, error_size) ||
        !alien_attack_divs16(
            alien_attack_muls16(approach.x_difference, approach.distance),
            (int16_t)attack_setup->shot_speed, &lead, error, error_size)) {
        return 0;
    }
    approach.new_x = alien_attack_add16(
        approach.new_x, (int16_t)alien_attack_asr32(lead, 4u));
    if (!alien_attack_divs16(
            alien_attack_muls16(approach.z_difference, approach.distance),
            (int16_t)attack_setup->shot_speed, &lead, error, error_size)) {
        return 0;
    }
    approach.new_z = alien_attack_add16(
        approach.new_z, (int16_t)alien_attack_asr32(lead, 4u));
    {
        int16_t future_x = approach.new_x;
        int16_t future_z = approach.new_z;

        approach.range = 0;
        approach.speed = (int16_t)attack_setup->shot_speed;
        if (!object_heading_towards(&approach, error, error_size)) {
            return 0;
        }
        if (alien_setup->shot_offset_multiplier != 0) {
            int16_t x_offset = (int16_t)alien_attack_asr32(
                alien_attack_muls16(alien_setup->shot_offset_multiplier,
                                    alien_attack_sub16(approach.new_x, approach.old_x)),
                8u);
            int16_t z_offset = (int16_t)alien_attack_asr32(
                alien_attack_muls16(alien_setup->shot_offset_multiplier,
                                    alien_attack_sub16(approach.new_z, approach.old_z)),
                8u);

            approach.old_x = alien_attack_add16(approach.old_x, z_offset);
            approach.old_z = alien_attack_sub16(approach.old_z, x_offset);
            approach.new_x = future_x;
            approach.new_z = future_z;
            if (!object_heading_towards(&approach, error, error_size)) {
                return 0;
            }
        }
    }

    /* MOVE.W into a Vec2L preserves each pool point's low source word. */
    alien_attack_write_be16(shot_point + 0u, (uint16_t)approach.new_x);
    alien_attack_write_be16(shot_slot + ALIEN_ATTACK_SHOT_VELOCITY_X,
                            (uint16_t)alien_attack_sub16(approach.new_x, approach.old_x));
    alien_attack_write_be16(shot_point + 4u, (uint16_t)approach.new_z);
    alien_attack_write_be16(shot_slot + ALIEN_ATTACK_SHOT_VELOCITY_Z,
                            (uint16_t)alien_attack_sub16(approach.new_z, approach.old_z));
    alien_attack_write_be32(shot_slot + ALIEN_ATTACK_SHOT_ENEMY_FLAGS, UINT32_C(0x32));
    alien_attack_write_be16(shot_slot + ALIEN_ATTACK_SLOT_ZONE_ID,
                            alien_attack_read_be16(alien_slot + ALIEN_ATTACK_SLOT_ZONE_ID));
    alien_attack_write_be16(shot_slot + ALIEN_ATTACK_SLOT_Y_POSITION,
                            alien_attack_read_be16(alien_slot + ALIEN_ATTACK_SLOT_Y_POSITION));
    accumulated_y = (int32_t)((uint32_t)(int32_t)alien_attack_read_be16s(
        alien_slot + ALIEN_ATTACK_SLOT_Y_POSITION) << 7u);
    accumulated_y = alien_attack_add32(accumulated_y, alien_setup->shot_y_offset);
    alien_attack_write_be32(shot_slot + ALIEN_ATTACK_SHOT_ACCUMULATED_Y,
                            (uint32_t)accumulated_y);
    shot_slot[ALIEN_ATTACK_SHOT_IN_UPPER_ZONE] =
        alien_slot[ALIEN_ATTACK_SHOT_IN_UPPER_ZONE];

    player_height = alien_attack_sub16(
        alien_attack_read_be16s(player_slot + ALIEN_ATTACK_SLOT_Y_POSITION),
        ALIEN_ATTACK_PLAYER_AIM_HEIGHT);
    accumulated_y = alien_attack_sub32(
        (int32_t)((uint32_t)(int32_t)player_height << 7u), accumulated_y);
    accumulated_y = alien_attack_add32(accumulated_y, accumulated_y);
    vertical_divisor = alien_attack_asr16_count(approach.distance, attack_setup->shot_shift);
    if (vertical_divisor <= 0) {
        vertical_divisor = 1;
    }
    if (!alien_attack_divs16(accumulated_y, vertical_divisor, &lead, error, error_size)) {
        return 0;
    }
    alien_attack_write_be16(shot_slot + ALIEN_ATTACK_SHOT_VELOCITY_Y, (uint16_t)lead);
    shot_slot[ALIEN_ATTACK_SHOT_WORRY] = UINT8_MAX;
    if (out_spawned) {
        *out_spawned = UINT8_MAX;
    }
    return 1;
}

int alien_attack_shoot_player_one(ObjectRuntime *objects, uint32_t alien_slot_index,
                                  LevelDynamicState *dynamic_level,
                                  const PlayerRuntime *player, GameRandom *random,
                                  AlienHitscanMissState *out_state,
                                  char *error, size_t error_size)
{
    uint8_t *alien_slot;
    uint8_t *alien_point;
    ObjectMovementTrace trace = {0};
    AlienHitscanMissState state;
    int16_t spread;
    int16_t player_height;
    int16_t player_x;
    int16_t player_z;
    int16_t x_difference;
    int16_t z_difference;

    if (!objects || !dynamic_level || !player || !random || !out_state ||
        alien_slot_index >= objects->active_slot_count ||
        objects->active_slot_count > objects->slot_count ||
        !object_runtime_get_slot_bytes(objects, alien_slot_index, &alien_slot) ||
        !object_runtime_get_point_bytes(
            objects, alien_attack_read_be16(alien_slot + ALIEN_ATTACK_SLOT_POINT_INDEX),
            &alien_point)) {
        alien_attack_set_error(error, error_size, "SHOOTPLAYER1 received invalid source state");
        return 0;
    }
    memset(&state, 0, sizeof(state));
    trace.zone_index = alien_attack_read_be16(alien_slot + ALIEN_ATTACK_SLOT_ZONE_ID);
    if (trace.zone_index >= dynamic_level->runtime.zone_count) {
        alien_attack_set_error(error, error_size,
                               "SHOOTPLAYER1 alien zone is outside the source level");
        return 0;
    }

    trace.old_x = alien_attack_read_be16s(alien_point);
    trace.old_z = alien_attack_read_be16s(alien_point + 4u);
    player_x = player_runtime_position_to_world(player->tmp_x);
    player_z = player_runtime_position_to_world(player->tmp_z);
    x_difference = alien_attack_sub16(player_x, trace.old_x);
    z_difference = alien_attack_sub16(player_z, trace.old_z);
    spread = alien_attack_asr16_count((int16_t)game_random_next(random), 4u);
    trace.new_z = alien_attack_add16(
        player_z, alien_attack_high_word(alien_attack_muls16(spread, x_difference)));
    trace.new_x = alien_attack_sub16(
        player_x, alien_attack_high_word(alien_attack_muls16(spread, z_difference)));
    player_height = (int16_t)alien_attack_asr32(
        alien_attack_add32(player->tmp_y, 15 * 128), 7u);
    player_height = alien_attack_add16(
        player_height, alien_attack_high_word(alien_attack_muls16(spread, player_height)));
    trace.new_y = (int32_t)((uint32_t)(int32_t)player_height << 7u);
    trace.old_y = (int32_t)((uint32_t)(int32_t)alien_attack_read_be16s(
        alien_slot + ALIEN_ATTACK_SLOT_Y_POSITION) << 7u);
    trace.stood_in_top = alien_slot[ALIEN_ATTACK_SHOT_IN_UPPER_ZONE];
    trace.exit_first = UINT8_MAX;
    trace.extension_length = 0;
    trace.away_from_wall = -1;
    trace.wall_flags = 0x0400u;
    trace.step_up = 0;
    trace.step_down = 0x1000000;
    trace.thing_height = 0;

    for (;;) {
        int16_t ray_x;
        int16_t ray_z;
        int32_t ray_y;

        if (!object_movement_trace_zero_extension(dynamic_level, &trace,
                                                  error, error_size)) {
            return 0;
        }
        if (trace.hit_wall != 0u) {
            break;
        }
        ray_x = alien_attack_sub16(trace.new_x, trace.old_x);
        ray_z = alien_attack_sub16(trace.new_z, trace.old_z);
        ray_y = alien_attack_sub32(trace.new_y, trace.old_y);
        trace.old_x = alien_attack_add16(trace.old_x, ray_x);
        trace.new_x = alien_attack_add16(trace.new_x, ray_x);
        trace.old_z = alien_attack_add16(trace.old_z, ray_z);
        trace.new_z = alien_attack_add16(trace.new_z, ray_z);
        trace.old_y = alien_attack_add32(trace.old_y, ray_y);
        trace.new_y = alien_attack_add32(trace.new_y, ray_y);
    }
    state.movement = trace;

    for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
         ++shot_index) {
        uint8_t *shot_slot;
        uint8_t *shot_point;
        LevelZone zone;

        if (!object_runtime_get_player_shot_slot_bytes(objects, shot_index, &shot_slot)) {
            alien_attack_set_error(error, error_size,
                                   "SHOOTPLAYER1 player-shot pool is outside source state");
            return 0;
        }
        if (alien_attack_read_be16s(shot_slot + ALIEN_ATTACK_SLOT_ZONE_ID) >= 0) {
            continue;
        }
        if (!object_runtime_get_point_bytes(
                objects, alien_attack_read_be16(shot_slot + ALIEN_ATTACK_SLOT_POINT_INDEX),
                &shot_point) ||
            !level_runtime_get_zone(&dynamic_level->runtime, trace.zone_index, &zone,
                                    error, error_size)) {
            alien_attack_set_error(error, error_size,
                                   "SHOOTPLAYER1 impact slot is outside source state");
            return 0;
        }
        alien_attack_write_be16(shot_point + 0u, (uint16_t)trace.new_x);
        alien_attack_write_be16(shot_point + 4u, (uint16_t)trace.new_z);
        shot_slot[30u] = 1u;
        alien_attack_write_be16(shot_slot + 54u, 0u);
        shot_slot[31u] = 0u;
        shot_slot[52u] = 0u;
        alien_attack_write_be16(shot_slot + ALIEN_ATTACK_SLOT_ZONE_ID, zone.id);
        shot_slot[ALIEN_ATTACK_SHOT_WORRY] = UINT8_MAX;
        alien_attack_write_be32(shot_slot + ALIEN_ATTACK_SHOT_ACCUMULATED_Y,
                                (uint32_t)trace.wall_hit_height);
        alien_attack_write_be16(shot_slot + ALIEN_ATTACK_SLOT_Y_POSITION,
                                (uint16_t)alien_attack_asr32(trace.wall_hit_height, 7u));
        state.impact_spawned = UINT8_MAX;
        *out_state = state;
        return 1;
    }
    *out_state = state;
    return 1;
}

static int alien_attack_apply_hitscan_player_impact(
    ObjectRuntime *objects, const uint8_t *alien_point,
    const PlayerRuntime *player, const AlienAttackSetup *setup,
    AlienHitscanAttackState *state, char *error, size_t error_size)
{
    uint8_t *player_slot;
    int16_t x_difference;
    int16_t z_difference;
    int32_t squared_distance;
    int16_t square_root;
    int16_t impact_divisor;

    if (!objects || !alien_point || !player || !setup || !state ||
        !object_runtime_get_player1_slot_bytes(objects, &player_slot)) {
        alien_attack_set_error(error, error_size,
                               "ai_AttackWithHitScan has no Player 1 source entity");
        return 0;
    }
    /* `add.b SHOTPOWER,EntT_DamageTaken_b(Plr1_ObjectPtr_l)`. */
    player_slot[ALIEN_ATTACK_SLOT_DAMAGE_TAKEN] =
        (uint8_t)(player_slot[ALIEN_ATTACK_SLOT_DAMAGE_TAKEN] + setup->shot_power);

    x_difference = alien_attack_sub16(
        alien_attack_read_be16s(alien_point), player_runtime_position_to_world(player->tmp_x));
    z_difference = alien_attack_sub16(
        alien_attack_read_be16s(alien_point + 4u), player_runtime_position_to_world(player->tmp_z));
    squared_distance = alien_attack_add32(
        alien_attack_muls16(x_difference, x_difference),
        alien_attack_muls16(z_difference, z_difference));
    if (!alien_math_calc_sqrt(squared_distance, &square_root, error, error_size)) {
        return 0;
    }
    /* ai_CalcSqrt returns d2; DIVS below reads the low word after ADD.L d2,d2. */
    impact_divisor = (int16_t)alien_attack_add32(square_root, square_root);
    if (!alien_attack_divs16(alien_attack_muls16((int16_t)setup->shot_power, x_difference),
                             impact_divisor, &state->impact_x, error, error_size) ||
        !alien_attack_divs16(alien_attack_muls16((int16_t)setup->shot_power, z_difference),
                             impact_divisor, &state->impact_z, error, error_size)) {
        return 0;
    }
    alien_attack_write_be16(player_slot + ALIEN_ATTACK_SLOT_IMPACT_X,
                            (uint16_t)alien_attack_sub16(
                                alien_attack_read_be16s(
                                    player_slot + ALIEN_ATTACK_SLOT_IMPACT_X),
                                state->impact_x));
    alien_attack_write_be16(player_slot + ALIEN_ATTACK_SLOT_IMPACT_Z,
                            (uint16_t)alien_attack_sub16(
                                alien_attack_read_be16s(
                                    player_slot + ALIEN_ATTACK_SLOT_IMPACT_Z),
                                state->impact_z));
    state->player_hit = UINT8_MAX;
    return 1;
}

int alien_attack_with_hitscan_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    LevelDynamicState *dynamic_level, const AssetBlob *clips, const GameLink *game_link,
    GameProgression *progression, ObjectExplosionRuntime *explosion_runtime,
    const GameMath *math, GameRandom *random, const PlayerRuntime *player,
    const AlienSetup *alien_setup, const ObjectObservation *observation,
    AlienHitscanAttackState *out_state, char *error, size_t error_size)
{
    const LevelRuntime *level;
    uint8_t *slot;
    uint8_t *point;
    uint16_t point_index;
    uint16_t zone_index;
    int16_t point_x;
    int16_t point_z;
    AlienHitscanAttackState state;

    if (!objects || !alien_runtime || !animation_runtime || !lighting || !dynamic_level ||
        !clips || !game_link || !progression || !explosion_runtime || !math || !random ||
        !player || !alien_setup || !observation || !out_state || slot_index == 0u ||
        slot_index >= objects->active_slot_count || slot_index >= ALIEN_RUNTIME_ENTITY_COUNT ||
        player->zone_index >= dynamic_level->runtime.zone_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_attack_set_error(error, error_size,
                               "ai_AttackWithHitScan received invalid source state");
        return 0;
    }
    if (!object_animation_runtime_reserve(animation_runtime, objects->active_slot_count,
                                          error, error_size)) {
        return 0;
    }
    level = &dynamic_level->runtime;
    point_index = alien_attack_read_be16(slot + ALIEN_ATTACK_SLOT_POINT_INDEX);
    zone_index = alien_attack_read_be16(slot + ALIEN_ATTACK_SLOT_ZONE_ID);
    if (point_index >= objects->point_count ||
        point_index >= OBJECT_OBSERVATION_DISTANCE_COUNT || zone_index >= level->zone_count ||
        !object_runtime_get_point_bytes(objects, point_index, &point)) {
        alien_attack_set_error(error, error_size,
                               "ai_AttackWithHitScan has an invalid source point or zone");
        return 0;
    }
    memset(&state, 0, sizeof(state));
    if (!alien_attack_setup_from_slot(objects, slot_index, game_link, &state.setup,
                                      error, error_size)) {
        return 0;
    }
    if (state.setup.is_hitscan == 0u) {
        alien_attack_set_error(error, error_size,
                               "ai_AttackWithHitScan selected a projectile source bullet");
        return 0;
    }

    if (slot[ALIEN_ATTACK_SLOT_DAMAGE_TAKEN] != 0u) {
        state.damage_taken = UINT8_MAX;
        slot[ALIEN_ATTACK_SLOT_CURRENT_MODE] = ALIEN_ATTACK_DAMAGE_MODE;
        if (!alien_damage_take(objects, slot_index, alien_runtime, animation_runtime, math,
                               random, player, &state.damage, error, error_size)) {
            return 0;
        }
        if (state.damage.route == ALIEN_DAMAGE_ROUTE_JUST_DIED) {
            if (!alien_death_just_died(
                    objects, slot_index, alien_runtime, level, game_link, progression,
                    animation_runtime, explosion_runtime, math, random, &state.death,
                    error, error_size)) {
                return 0;
            }
            state.got_out = state.death.got_out;
        } else {
            state.got_out = state.damage.got_out;
        }
        if (state.got_out != 0u) {
            *out_state = state;
            return 1;
        }
    }

    if (!alien_animation_update_walk_or_attack(
            objects, slot_index, animation_runtime, game_link, math, alien_setup, player->yaw,
            &state.animation, error, error_size)) {
        return 0;
    }
    state.heading.old_x = alien_attack_read_be16s(point);
    state.heading.old_z = alien_attack_read_be16s(point + 4u);
    state.heading.new_x = player_runtime_position_to_world(player->x);
    state.heading.new_z = player_runtime_position_to_world(player->z);
    state.heading.range = -20;
    state.heading.speed = 20;
    state.heading.angle = alien_runtime->heading_angle;
    if (!object_heading_towards_angle(math, &state.heading, error, error_size)) {
        return 0;
    }
    alien_runtime->heading_angle = state.heading.angle;
    alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_CURRENT_ANGLE, state.heading.angle);
    if (!alien_memory_store_player_position(alien_runtime, objects, slot_index, level, player,
                                            error, error_size)) {
        return 0;
    }

    point_x = alien_attack_read_be16s(point);
    point_z = alien_attack_read_be16s(point + 4u);
    if (!alien_perception_look_for_player_one(alien_runtime, objects, slot_index,
                                              level, clips, player, zone_index, point_x, point_z,
                                              error, error_size)) {
        return 0;
    }
    slot[ALIEN_ATTACK_SLOT_CURRENT_MODE] = 0u;
    if (slot[ALIEN_ATTACK_SLOT_SEES_PLAYER] == 0u) {
        slot[ALIEN_ATTACK_SLOT_WHICH_ANIMATION] = 0u;
        alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER2, 0u);
        alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER1,
                                (uint16_t)alien_setup->followup_timer);
        /* ai_AttackWithHitScan writes Timer2 twice on this source branch. */
        alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER2, 0u);
        alien_attack_add_facing(slot, state.animation.facing);
        *out_state = state;
        return 1;
    }
    {
        uint8_t in_front;

        if (!alien_decision_check_in_front(objects, slot_index, player, math, &in_front,
                                           error, error_size)) {
            return 0;
        }
        if (in_front == 0u) {
            slot[ALIEN_ATTACK_SLOT_WHICH_ANIMATION] = 0u;
            alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER2, 0u);
            alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER1,
                                    (uint16_t)alien_setup->followup_timer);
            /* ai_AttackWithHitScan writes Timer2 twice on this source branch. */
            alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER2, 0u);
            alien_attack_add_facing(slot, state.animation.facing);
            *out_state = state;
            return 1;
        }
    }
    slot[ALIEN_ATTACK_SLOT_CURRENT_MODE] = ALIEN_ATTACK_RESPONSE_MODE;
    slot[ALIEN_ATTACK_SLOT_WHICH_ANIMATION] = 1u;
    alien_attack_add_facing(slot, state.animation.facing);

    if (state.animation.action != 0u) {
        int32_t squared_view_distance = alien_attack_add32(
            alien_attack_muls16(observation->rotated_x[point_index],
                                observation->rotated_x[point_index]),
            alien_attack_muls16(observation->rotated_z[point_index],
                                observation->rotated_z[point_index]));

        state.chance_roll = (int32_t)((game_random_next(random) & UINT16_C(0x7fff)) << 2u);
        state.chance_distance = alien_attack_asr32(squared_view_distance, 6u);
        if (state.chance_roll > state.chance_distance) {
            if (!alien_attack_apply_hitscan_player_impact(
                    objects, point, player, &state.setup, &state, error, error_size)) {
                return 0;
            }
        } else if (!alien_attack_shoot_player_one(objects, slot_index, dynamic_level, player,
                                                  random, &state.miss, error, error_size)) {
            return 0;
        } else {
            state.player_missed = UINT8_MAX;
        }
    }

    /* The source restores the alien point after any hitscan trace. */
    object_motion_runtime_set_new_words(&alien_runtime->motion, point_x, point_z);
    if (!alien_torch_apply(lighting, level, math, objects, slot_index, alien_setup,
                           point_x, point_z, error, error_size)) {
        return 0;
    }
    if (state.animation.finished != 0u) {
        slot[ALIEN_ATTACK_SLOT_WHICH_ANIMATION] = 0u;
        slot[ALIEN_ATTACK_SLOT_CURRENT_MODE] = ALIEN_ATTACK_FOLLOWUP_MODE;
        alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER1,
                                (uint16_t)alien_setup->followup_timer);
        alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER2, 0u);
    }
    *out_state = state;
    return 1;
}

int alien_attack_with_projectile_update(
    ObjectRuntime *objects, uint32_t slot_index, AlienRuntime *alien_runtime,
    ObjectAnimationRuntime *animation_runtime, LightingRuntime *lighting,
    const LevelRuntime *level, const AssetBlob *clips, const GameLink *game_link,
    GameProgression *progression, ObjectExplosionRuntime *explosion_runtime,
    const GameMath *math, GameRandom *random, const PlayerRuntime *player,
    const AlienSetup *alien_setup, const ObjectObservation *observation,
    GameAudioEvents *audio_events,
    AlienProjectileAttackState *out_state,
    char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *point;
    uint16_t point_index;
    uint16_t zone_index;
    int16_t point_x;
    int16_t point_z;
    AlienProjectileAttackState state;

    if (!objects || !alien_runtime || !animation_runtime || !lighting || !level || !clips ||
        !game_link || !progression || !explosion_runtime || !math || !random || !player ||
        !alien_setup || !out_state || slot_index == 0u ||
        slot_index >= objects->active_slot_count ||
        slot_index >= ALIEN_RUNTIME_ENTITY_COUNT ||
        player->zone_index >= level->zone_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
        alien_attack_set_error(error, error_size,
                               "ai_AttackWithProjectile received invalid source state");
        return 0;
    }
    if (!object_animation_runtime_reserve(animation_runtime, objects->active_slot_count,
                                          error, error_size)) {
        return 0;
    }
    point_index = alien_attack_read_be16(slot + ALIEN_ATTACK_SLOT_POINT_INDEX);
    zone_index = alien_attack_read_be16(slot + ALIEN_ATTACK_SLOT_ZONE_ID);
    if (point_index >= objects->point_count || zone_index >= level->zone_count ||
        !object_runtime_get_point_bytes(objects, point_index, &point)) {
        alien_attack_set_error(error, error_size,
                               "ai_AttackWithProjectile has an invalid source point or zone");
        return 0;
    }
    memset(&state, 0, sizeof(state));
    if (!alien_attack_setup_from_slot(objects, slot_index, game_link, &state.setup,
                                      error, error_size)) {
        return 0;
    }
    if (state.setup.is_hitscan != 0u) {
        alien_attack_set_error(error, error_size,
                               "ai_AttackWithProjectile selected a hitscan source bullet");
        return 0;
    }

    if (slot[ALIEN_ATTACK_SLOT_DAMAGE_TAKEN] != 0u) {
        state.damage_taken = UINT8_MAX;
        slot[ALIEN_ATTACK_SLOT_CURRENT_MODE] = ALIEN_ATTACK_DAMAGE_MODE;
        if (!alien_damage_take(objects, slot_index, alien_runtime, animation_runtime, math,
                               random, player, &state.damage, error, error_size)) {
            return 0;
        }
        if (state.damage.route == ALIEN_DAMAGE_ROUTE_JUST_DIED) {
            if (!alien_death_just_died(
                    objects, slot_index, alien_runtime, level, game_link, progression,
                    animation_runtime, explosion_runtime, math, random, &state.death,
                    error, error_size)) {
                return 0;
            }
            state.got_out = state.death.got_out;
        } else {
            state.got_out = state.damage.got_out;
        }
        if (state.got_out != 0u) {
            *out_state = state;
            return 1;
        }
    }

    if (!alien_animation_update_walk_or_attack(
            objects, slot_index, animation_runtime, game_link, math, alien_setup, player->yaw,
            &state.animation, error, error_size)) {
        return 0;
    }
    state.heading.old_x = alien_attack_read_be16s(point);
    state.heading.old_z = alien_attack_read_be16s(point + 4u);
    state.heading.new_x = player_runtime_position_to_world(player->x);
    state.heading.new_z = player_runtime_position_to_world(player->z);
    state.heading.range = -20;
    state.heading.speed = 20;
    state.heading.angle = alien_runtime->heading_angle;
    if (!object_heading_towards_angle(math, &state.heading, error, error_size)) {
        return 0;
    }
    alien_runtime->heading_angle = state.heading.angle;
    alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_CURRENT_ANGLE, state.heading.angle);
    if (!alien_memory_store_player_position(alien_runtime, objects, slot_index, level, player,
                                            error, error_size)) {
        return 0;
    }
    if (state.animation.action != 0u &&
        !alien_attack_fire_at_player_one(objects, slot_index, player, alien_setup, &state.setup,
                                         observation, audio_events, &state.projectile_spawned,
                                         error, error_size)) {
        return 0;
    }

    point_x = alien_attack_read_be16s(point);
    point_z = alien_attack_read_be16s(point + 4u);
    /* The source restores the alien point after FireAtPlayer1. */
    object_motion_runtime_set_new_words(&alien_runtime->motion, point_x, point_z);
    if (!alien_torch_apply(lighting, level, math, objects, slot_index, alien_setup,
                           point_x, point_z, error, error_size)) {
        return 0;
    }
    if (state.animation.finished != 0u) {
        slot[ALIEN_ATTACK_SLOT_WHICH_ANIMATION] = 0u;
        slot[ALIEN_ATTACK_SLOT_CURRENT_MODE] = ALIEN_ATTACK_FOLLOWUP_MODE;
        alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER1,
                                (uint16_t)alien_setup->followup_timer);
        alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER2, 0u);
        alien_attack_add_facing(slot, state.animation.facing);
        *out_state = state;
        return 1;
    }

    if (!alien_perception_look_for_player_one(alien_runtime, objects, slot_index,
                                              level, clips, player, zone_index, point_x, point_z,
                                              error, error_size)) {
        return 0;
    }
    slot[ALIEN_ATTACK_SLOT_CURRENT_MODE] = 0u;
    if (slot[ALIEN_ATTACK_SLOT_SEES_PLAYER] != 0u) {
        uint8_t in_front;

        if (!alien_decision_check_in_front(objects, slot_index, player, math, &in_front,
                                           error, error_size)) {
            return 0;
        }
        if (in_front != 0u) {
            slot[ALIEN_ATTACK_SLOT_WHICH_ANIMATION] = 1u;
            slot[ALIEN_ATTACK_SLOT_CURRENT_MODE] = ALIEN_ATTACK_RESPONSE_MODE;
            alien_attack_add_facing(slot, state.animation.facing);
            *out_state = state;
            return 1;
        }
    }
    slot[ALIEN_ATTACK_SLOT_WHICH_ANIMATION] = 0u;
    alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER2, 0u);
    alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER1,
                            (uint16_t)alien_setup->followup_timer);
    /* ai_AttackWithProjectile writes Timer2 twice on this source branch. */
    alien_attack_write_be16(slot + ALIEN_ATTACK_SLOT_TIMER2, 0u);
    alien_attack_add_facing(slot, state.animation.facing);
    *out_state = state;
    return 1;
}
