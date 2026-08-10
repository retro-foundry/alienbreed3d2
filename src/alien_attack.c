#include "alien_attack.h"

#include <limits.h>
#include <stdio.h>

#include "object_heading.h"

enum {
    /* defs.i: ObjT/EntT/ShotT fields used by ai_AttackCommon/FireAtPlayer1. */
    ALIEN_ATTACK_SLOT_POINT_INDEX = 0u,
    ALIEN_ATTACK_SLOT_Y_POSITION = 4u,
    ALIEN_ATTACK_SLOT_ZONE_ID = 12u,
    ALIEN_ATTACK_SLOT_TYPE_ID = 16u,
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

static int32_t alien_attack_muls16(int16_t left, int16_t right)
{
    return (int32_t)left * (int32_t)right;
}

static int alien_attack_divs16(int32_t dividend, int16_t divisor,
                               int16_t *out_quotient,
                               char *error, size_t error_size)
{
    int32_t quotient;

    if (!out_quotient || divisor == 0 ||
        (dividend == INT32_MIN && divisor == -1)) {
        alien_attack_set_error(error, error_size,
                               "FireAtPlayer1 DIVS received invalid source operands");
        return 0;
    }
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        alien_attack_set_error(error, error_size,
                               "FireAtPlayer1 DIVS quotient exceeds a source word");
        return 0;
    }
    *out_quotient = (int16_t)quotient;
    return 1;
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

    /* Source audio globals and MakeSomeNoise are deliberately left to audio work. */
    shot_slot[ALIEN_ATTACK_SLOT_TYPE_ID] = ALIEN_ATTACK_OBJECT_TYPE_PROJECTILE;
    alien_attack_write_be16(shot_slot + ALIEN_ATTACK_SHOT_LIFETIME, 0u);
    shot_slot[ALIEN_ATTACK_SHOT_SIZE] = attack_setup->shot_type;
    /* The source uses MOVE.B despite ShotT_Power_w's word declaration. */
    shot_slot[ALIEN_ATTACK_SHOT_POWER] = attack_setup->shot_power;

    approach.old_x = alien_attack_read_be16s(alien_point);
    approach.old_z = alien_attack_read_be16s(alien_point + 4u);
    approach.new_x = (int16_t)(uint16_t)player->x;
    approach.new_z = (int16_t)(uint16_t)player->z;
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
