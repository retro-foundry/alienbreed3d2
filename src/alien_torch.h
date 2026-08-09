#ifndef AB3D2_ALIEN_TORCH_H
#define AB3D2_ALIEN_TORCH_H

#include <stddef.h>
#include <stdint.h>

#include "alien_setup.h"
#include "lighting_runtime.h"
#include "object_runtime.h"

/*
 * modules/ai.s:ai_DoTorch. `new_x` and `new_z` are the source globals owned
 * by the calling AI mode; this helper only applies the authored alien light.
 */
int alien_torch_apply(LightingRuntime *lighting, const LevelRuntime *level,
                      const GameMath *math, const ObjectRuntime *objects,
                      uint32_t slot_index, const AlienSetup *setup,
                      int16_t new_x, int16_t new_z,
                      char *error, size_t error_size);

#endif
