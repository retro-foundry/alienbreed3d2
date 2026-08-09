#ifndef AB3D2_OBJECT_PROJECTILES_H
#define AB3D2_OBJECT_PROJECTILES_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "object_runtime.h"

/*
 * newanims.s:ItsABullet's ShotT_Status_b != 0 pop branch. This is the
 * stationary impact state created by newplayershoot.s:plr1_HitscanSucceded.
 */
int object_projectiles_update_impact_slot(ObjectRuntime *objects, uint32_t slot_index,
                                          const GameLink *game_link,
                                          char *error, size_t error_size);

/*
 * newanims.s:ItsABullet's `notpopping` BulT_AnimData_vb descriptor/frame
 * path for a live projectile. Motion, surface collision, blasts, lighting,
 * and audio remain with the later portions of that same source routine.
 */
int object_projectiles_update_flight_animation_slot(ObjectRuntime *objects, uint32_t slot_index,
                                                     const GameLink *game_link,
                                                     char *error, size_t error_size);

#endif
