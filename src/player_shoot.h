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

#endif
