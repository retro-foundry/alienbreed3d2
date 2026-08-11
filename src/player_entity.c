#include "player_entity.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    /* defs.i:ObjT/EntT/ShotT offsets used by hires.s:Plr1_Use. */
    PLAYER_ENTITY_POINT_INDEX_OFFSET = 0u,
    PLAYER_ENTITY_VERTICAL_POSITION_OFFSET = 4u,
    PLAYER_ENTITY_ZONE_ID_OFFSET = 12u,
    PLAYER_ENTITY_TYPE_ID_OFFSET = 16u,
    PLAYER_ENTITY_HIT_POINTS_OFFSET = 18u,
    PLAYER_ENTITY_DAMAGE_TAKEN_OFFSET = 19u,
    PLAYER_ENTITY_SEES_PLAYER_OFFSET = 17u,
    PLAYER_ENTITY_ENTITY_ZONE_ID_OFFSET = 26u,
    PLAYER_ENTITY_CURRENT_ANGLE_OFFSET = 30u,
    PLAYER_ENTITY_TIMER1_OFFSET = 34u,
    PLAYER_ENTITY_IMPACT_X_OFFSET = 42u,
    PLAYER_ENTITY_IMPACT_Z_OFFSET = 44u,
    PLAYER_ENTITY_IMPACT_Y_OFFSET = 46u,
    PLAYER_ENTITY_OBJECT_KIND_OFFSET = 54u,
    PLAYER_ENTITY_WHICH_ANIMATION_OFFSET = 55u,
    PLAYER_ENTITY_IN_UPPER_ZONE_OFFSET = 63u,
    PLAYER_ENTITY_TYPE_PLAYER1 = 4u,
    PLAYER_ENTITY_TYPE_OBJECT = 1u,
    PLAYER_ENTITY_WEAPON_SLOT_DISTANCE = 2u,
    /* data/tables_data.s:SINE_SIZE and SINTAB_MASK_ADR. */
    PLAYER_ENTITY_REVERSE_ANGLE = 4096u,
    PLAYER_ENTITY_ANGLE_MASK = 8190u,
    PLAYER_ENTITY_WEAPON_HEIGHT_OFFSET = 10 * 128
};

static void player_entity_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t player_entity_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t player_entity_read_be16s(const uint8_t *source)
{
    return (int16_t)player_entity_read_be16(source);
}

static void player_entity_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void player_entity_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static int32_t player_entity_asr32_7(int32_t value)
{
    if (value >= 0) {
        return value >> 7;
    }
    return -((-(int64_t)value + 127) >> 7);
}

static int32_t player_entity_asr32(int32_t value, unsigned int shift)
{
    if (value >= 0) {
        return value >> shift;
    }
    return -(((-(int64_t)value) + ((INT64_C(1) << shift) - 1)) >> shift);
}

static int32_t player_entity_add_high_word(int32_t value, int16_t addend)
{
    uint32_t bits = (uint32_t)value;
    uint16_t high = (uint16_t)((bits >> 16u) + (uint16_t)addend);

    return (int32_t)(((uint32_t)high << 16u) | (bits & UINT32_C(0xffff)));
}

static void player_entity_apply_damage(uint8_t *slot, const LevelZone *zone,
                                       PlayerRuntime *player, GameInventory *inventory,
                                       GameRandom *random, GameAudioEvents *audio_events)
{
    uint16_t damage = slot[PLAYER_ENTITY_DAMAGE_TAKEN_OFFSET];

    if (damage != 0u) {
        int16_t impact_x = player_entity_read_be16s(slot + PLAYER_ENTITY_IMPACT_X_OFFSET);
        int16_t impact_z = player_entity_read_be16s(slot + PLAYER_ENTITY_IMPACT_Z_OFFSET);
        int16_t impact_y = player_entity_read_be16s(slot + PLAYER_ENTITY_IMPACT_Y_OFFSET);
        int16_t twist_damage = (impact_x != 0 || impact_z != 0) ? (int16_t)damage : 0;
        int32_t random_twist = (int32_t)(int16_t)game_random_next(random) * twist_damage;

        /*
         * hires.s:Plr1_Use uses ADD.W at the address of each 32-bit X/Z
         * velocity. On 68000 big-endian storage this changes the high word,
         * while ImpactY is sign-extended, shifted by eight, and added as a
         * complete longword.
         */
        player->snap_x_speed = player_entity_add_high_word(player->snap_x_speed, impact_x);
        player->snap_z_speed = player_entity_add_high_word(player->snap_z_speed, impact_z);
        player->snap_y_velocity = (int32_t)((uint32_t)player->snap_y_velocity +
            ((uint32_t)(int32_t)impact_y << 8u));
        random_twist = player_entity_asr32(random_twist, 8u);
        random_twist = player_entity_asr32(random_twist, 4u);
        player->snap_yaw_speed = (int16_t)((uint16_t)player->snap_yaw_speed +
                                           (uint16_t)random_twist);
        inventory->health = (uint16_t)(inventory->health - damage);
        player->health = inventory->health;
        player_entity_write_be16(slot + PLAYER_ENTITY_IMPACT_X_OFFSET, 0u);
        player_entity_write_be16(slot + PLAYER_ENTITY_IMPACT_Z_OFFSET, 0u);
        player_entity_write_be16(slot + PLAYER_ENTITY_IMPACT_Y_OFFSET, 0u);
        game_audio_events_emit_relative(
            audio_events, 19, 60, 0, 0, UINT16_C(0xfffa),
            GAME_AUDIO_RESTART_SOURCE, 0u, zone->echo);
    }
    slot[PLAYER_ENTITY_DAMAGE_TAKEN_OFFSET] = 0u;
}

int player_entity_disable_second_for_single_player(ObjectRuntime *objects,
                                                   char *error, size_t error_size)
{
    uint8_t *slot;

    if (!objects || !object_runtime_get_player2_slot_bytes(objects, &slot)) {
        player_entity_set_error(error, error_size,
                                "single-player loop has no player-two entity slot");
        return 0;
    }
    /* macros.i:FREE_ENT followed by hires.s:clr.b ObjT_SeePlayer_b. */
    player_entity_write_be16(slot + PLAYER_ENTITY_ZONE_ID_OFFSET, UINT16_MAX);
    player_entity_write_be16(slot + PLAYER_ENTITY_ENTITY_ZONE_ID_OFFSET, UINT16_MAX);
    slot[PLAYER_ENTITY_SEES_PLAYER_OFFSET] = 0u;
    return 1;
}

int player_entity_sync_single_player(ObjectRuntime *objects, const LevelRuntime *level,
                                     const GameLink *game_link, PlayerRuntime *player,
                                     GameInventory *inventory, GameRandom *random,
                                     GameAudioEvents *audio_events,
                                     char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *point;
    uint8_t *weapon_slot;
    uint8_t *weapon_point;
    uint16_t point_index;
    uint16_t weapon_point_index;
    uint16_t gun_object_type;
    LevelZone zone;
    int32_t middle_height;
    int32_t weapon_height;
    int32_t weapon_bobble;

    if (!objects || !level || !game_link || !player || !inventory || !random || !audio_events ||
        player->zone_index >= level->zone_count ||
        !object_runtime_get_player1_slot_bytes(objects, &slot)) {
        player_entity_set_error(error, error_size,
                                "Plr1_Use received an invalid player entity or source zone");
        return 0;
    }
    point_index = player_entity_read_be16(slot + PLAYER_ENTITY_POINT_INDEX_OFFSET);
    if (!object_runtime_get_point_bytes(objects, point_index, &point) ||
        !level_runtime_get_zone(level, player->zone_index, &zone, error, error_size)) {
        player_entity_set_error(error, error_size,
                                "Plr1_Use player entity references an invalid source point or zone");
        return 0;
    }

    /* hires.s:Plr1_Use consumes the prior object tick's player impact first. */
    player_entity_apply_damage(slot, &zone, player, inventory, random, audio_events);

    /* hires.s:Plr1_Use publishes current player position to its ObjT point. */
    player_entity_write_be32(point + 0u, (uint32_t)player->x);
    player_entity_write_be32(point + 4u, (uint32_t)player->z);
    slot[PLAYER_ENTITY_TYPE_ID_OFFSET] = PLAYER_ENTITY_TYPE_PLAYER1;
    slot[PLAYER_ENTITY_HIT_POINTS_OFFSET] = 10u;
    player_entity_write_be16(slot + PLAYER_ENTITY_CURRENT_ANGLE_OFFSET, player->tmp_yaw);
    slot[PLAYER_ENTITY_IN_UPPER_ZONE_OFFSET] = player->stood_in_top;
    player_entity_write_be16(slot + PLAYER_ENTITY_ZONE_ID_OFFSET, zone.id);
    middle_height = (int32_t)((uint32_t)player->tmp_y +
                              (uint32_t)(player->tmp_height / 2));
    player_entity_write_be16(slot + PLAYER_ENTITY_VERTICAL_POSITION_OFFSET,
                             (uint16_t)player_entity_asr32_7(middle_height));

    /* hires.s:Plr1_Use .notdead companion weapon entity, at ENT_NEXT_2. */
    if (objects->player1_slot > UINT32_MAX - PLAYER_ENTITY_WEAPON_SLOT_DISTANCE ||
        !object_runtime_get_slot_bytes(objects,
                                       objects->player1_slot + PLAYER_ENTITY_WEAPON_SLOT_DISTANCE,
                                       &weapon_slot) ||
        !game_link_get_gun_object_type(game_link, player->tmp_gun_selected, &gun_object_type,
                                       error, error_size)) {
        player_entity_set_error(error, error_size,
                                "Plr1_Use companion weapon is outside owned source state");
        return 0;
    }
    weapon_point_index = player_entity_read_be16(weapon_slot + PLAYER_ENTITY_POINT_INDEX_OFFSET);
    if (!object_runtime_get_point_bytes(objects, weapon_point_index, &weapon_point)) {
        player_entity_set_error(error, error_size,
                                "Plr1_Use companion weapon references an invalid source point");
        return 0;
    }
    player_entity_write_be16(
        weapon_slot + PLAYER_ENTITY_CURRENT_ANGLE_OFFSET,
        (uint16_t)((player_entity_read_be16(slot + PLAYER_ENTITY_CURRENT_ANGLE_OFFSET) +
                    PLAYER_ENTITY_REVERSE_ANGLE) & PLAYER_ENTITY_ANGLE_MASK));
    player_entity_write_be16(weapon_slot + PLAYER_ENTITY_ZONE_ID_OFFSET, zone.id);
    player_entity_write_be16(weapon_slot + PLAYER_ENTITY_ENTITY_ZONE_ID_OFFSET, zone.id);
    weapon_slot[PLAYER_ENTITY_OBJECT_KIND_OFFSET] = (uint8_t)gun_object_type;
    weapon_slot[PLAYER_ENTITY_TYPE_ID_OFFSET] = PLAYER_ENTITY_TYPE_OBJECT;
    memcpy(weapon_point, point, OBJECT_RUNTIME_POINT_BYTE_COUNT);
    weapon_slot[PLAYER_ENTITY_WHICH_ANIMATION_OFFSET] = UINT8_MAX;
    if (player->reset_weapon_animation != 0u) {
        /* modules/player.s:.pickweap clears ENT_NEXT_2+EntT_Timer1_w. */
        player_entity_write_be16(weapon_slot + PLAYER_ENTITY_TIMER1_OFFSET, 0u);
        player->reset_weapon_animation = 0u;
    }
    weapon_height = player_entity_asr32(
        (int32_t)((uint32_t)player->tmp_y +
                  (uint32_t)player_entity_asr32(player->tmp_height, 2u) +
                  PLAYER_ENTITY_WEAPON_HEIGHT_OFFSET),
        7u);
    weapon_bobble = player_entity_asr32(player->bobble_y, 8u);
    weapon_bobble = (int32_t)((uint32_t)weapon_bobble +
                               (uint32_t)player_entity_asr32(weapon_bobble, 1u));
    player_entity_write_be16(weapon_slot + PLAYER_ENTITY_VERTICAL_POSITION_OFFSET,
                             (uint16_t)((uint16_t)weapon_height + (uint16_t)weapon_bobble));
    weapon_slot[PLAYER_ENTITY_IN_UPPER_ZONE_OFFSET] = slot[PLAYER_ENTITY_IN_UPPER_ZONE_OFFSET];
    return 1;
}
