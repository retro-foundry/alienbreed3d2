#include "alien_attack.h"

#include <stdio.h>

enum {
    /* defs.i: EntT_Type_b. */
    ALIEN_ATTACK_SLOT_ENTITY_TYPE = 54u
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
