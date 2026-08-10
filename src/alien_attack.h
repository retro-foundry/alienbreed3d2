#ifndef AB3D2_ALIEN_ATTACK_H
#define AB3D2_ALIEN_ATTACK_H

#include <stddef.h>
#include <stdint.h>

#include "alien_setup.h"
#include "game_link.h"
#include "object_runtime.h"
#include "player_runtime.h"

/*
 * modules/ai.s:ai_AttackCommon's mutable SHOT* globals.  These are the
 * source-sized results prepared before it chooses the hitscan or projectile
 * attack body; firing is deliberately owned by those later routines.
 */
typedef struct {
    uint8_t shot_type;
    uint8_t shot_power;
    uint16_t shot_speed;
    uint16_t shot_shift;
    uint8_t is_hitscan;
} AlienAttackSetup;

/* modules/ai.s:ai_AttackCommon through its BulT_IsHitScan_l branch. */
int alien_attack_setup_from_slot(const ObjectRuntime *objects, uint32_t slot_index,
                                const GameLink *game_link,
                                AlienAttackSetup *out_setup,
                                char *error, size_t error_size);

/*
 * newaliencontrol.s:FireAtPlayer1. `attack_setup` is the preceding
 * ai_AttackCommon handoff and `alien_setup` is ItsAnAlien's SHOTYOFF/
 * SHOTOFFMULT handoff. Audio writes/MakeSomeNoise remain absent with the
 * unported audio backend.
 */
int alien_attack_fire_at_player_one(ObjectRuntime *objects, uint32_t alien_slot_index,
                                    const PlayerRuntime *player,
                                    const AlienSetup *alien_setup,
                                    const AlienAttackSetup *attack_setup,
                                    uint8_t *out_spawned,
                                    char *error, size_t error_size);

#endif
