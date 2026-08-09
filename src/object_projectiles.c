#include "object_projectiles.h"

#include <stdio.h>

enum {
    /* defs.i:ObjT/EntT/ShotT fields used by newanims.s:ItsABullet. */
    OBJECT_PROJECTILE_GRAPHICS_WORD = 6u,
    OBJECT_PROJECTILE_GRAPHICS_LONG = 8u,
    OBJECT_PROJECTILE_ZONE_ID = 12u,
    OBJECT_PROJECTILE_TYPE_ID = 16u,
    OBJECT_PROJECTILE_ENTITY_ZONE_ID = 26u,
    OBJECT_PROJECTILE_STATUS = 30u,
    OBJECT_PROJECTILE_SIZE = 31u,
    OBJECT_PROJECTILE_ANIMATION = 52u,
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

int object_projectiles_update_impact_slot(ObjectRuntime *objects, uint32_t slot_index,
                                          const GameLink *game_link,
                                          char *error, size_t error_size)
{
    uint8_t *slot;
    uint16_t bullet_index;
    uint16_t frame_index;
    uint16_t next_frame;
    GameBulletDefinition bullet;
    GameBulletAnimationFrame frame;

    if (!objects || !game_link || slot_index >= objects->active_slot_count ||
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

    /* ItsABullet clears +8 before its bitmap/glare/additive pop descriptor. */
    object_projectiles_write_be32(slot + OBJECT_PROJECTILE_GRAPHICS_LONG, 0u);
    if ((int32_t)bullet.impact_graphics_type < 1) {
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 1u] = frame.byte_0;
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_GRAPHICS_WORD, frame.word_2);
    } else if (bullet.impact_graphics_type == 1u) {
        object_projectiles_write_be16(
            slot + OBJECT_PROJECTILE_GRAPHICS_LONG,
            (uint16_t)(int16_t)-(int16_t)(int8_t)frame.byte_0);
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_GRAPHICS_WORD, frame.word_2);
    } else {
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 1u] = frame.byte_0;
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 3u] = frame.byte_1;
        slot[OBJECT_PROJECTILE_GRAPHICS_LONG + 2u] = 6u;
        object_projectiles_write_be16(slot + OBJECT_PROJECTILE_GRAPHICS_WORD, frame.word_2);
    }

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
    }
    return 1;
}
