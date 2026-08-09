#ifndef AB3D2_OBJECT_PROJECTILES_H
#define AB3D2_OBJECT_PROJECTILES_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "object_runtime.h"

/*
 * newanims.s:ItsABullet's ShotT_Status_b != 0 pop branch.  This is the
 * stationary impact state created by newplayershoot.s:plr1_HitscanSucceded.
 * Flight, wall collision, blast damage, lighting, and audio retain their
 * separate source owners.
 */
int object_projectiles_update_impact_slot(ObjectRuntime *objects, uint32_t slot_index,
                                          const GameLink *game_link,
                                          char *error, size_t error_size);

#endif
