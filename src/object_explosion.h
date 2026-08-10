#ifndef AB3D2_OBJECT_EXPLOSION_H
#define AB3D2_OBJECT_EXPLOSION_H

#include <stddef.h>
#include <stdint.h>

#include "game_math.h"
#include "game_random.h"
#include "object_runtime.h"

/* newanims.s:anim_ExpRadius_w. */
typedef struct {
    int16_t radius;
} ObjectExplosionRuntime;

void object_explosion_runtime_init(ObjectExplosionRuntime *runtime);

/*
 * newanims.s:Anim_ExplodeIntoBits. `source_slot_index` is a0; new_x/new_z,
 * splat_type, requested_count, and radius are its caller-owned source globals
 * and registers. The routine allocates from the alien projectile pool only.
 */
int object_explosion_into_bits(ObjectExplosionRuntime *runtime,
                               ObjectRuntime *objects, uint32_t source_slot_index,
                               const GameMath *math, GameRandom *random,
                               int16_t new_x, int16_t new_z, uint8_t splat_type,
                               int16_t requested_count, int16_t radius,
                               uint32_t *out_spawned_count,
                               char *error, size_t error_size);

#endif
