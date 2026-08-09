#ifndef AB3D2_PLAYER_SHOOT_H
#define AB3D2_PLAYER_SHOOT_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "object_observation.h"
#include "object_runtime.h"
#include "player_runtime.h"

/* newplayershoot.s:Plr1_Shot target candidate retained for its later fire path. */
typedef struct {
    uint8_t found;
    uint32_t slot_index;
    uint16_t point_index;
    uint16_t distance;
    int32_t vertical_difference;
    int16_t vertical_speed;
} PlayerShotTarget;

/*
 * Selects Plr1_Shot's closest in-line, visible, targetable ObjT record and
 * derives its original auto-aim vertical speed.  This has no firing, ammo,
 * random, projectile, sound, renderer, or visibility-side effects.
 */
int player_shoot_find_target_single_player(const ObjectRuntime *objects,
                                           const ObjectObservation *observation,
                                           const PlayerRuntime *player,
                                           const GameBulletDefinition *bullet,
                                           PlayerShotTarget *out_target,
                                           char *error, size_t error_size);

/*
 * newplayershoot.s:plr1_HitscanSucceded.  Creates the source impact ObjT
 * when a player-shot slot is free, then applies the source byte-sized damage
 * and impact direction to the selected target.  Hit probability, misses, and
 * projectile updates remain separate source paths.
 */
int player_shoot_apply_hitscan_success(ObjectRuntime *objects,
                                       const PlayerShotTarget *target,
                                       uint16_t bullet_type,
                                       const GameBulletDefinition *bullet,
                                       int16_t player_sine, int16_t player_cosine,
                                       uint8_t *out_impact_spawned,
                                       char *error, size_t error_size);

#endif
