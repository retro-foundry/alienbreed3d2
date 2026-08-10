#ifndef AB3D2_OBJECT_BLAST_H
#define AB3D2_OBJECT_BLAST_H

#include <stddef.h>
#include <stdint.h>

#include "asset_io.h"
#include "game_link.h"
#include "game_random.h"
#include "level_dynamic_state.h"
#include "object_runtime.h"
#include "object_visibility.h"

/* newanims.s:BLOODYGREATBOMB and anim_DoneFlames_w. */
typedef struct {
    uint8_t flame_bullet_index;
    uint16_t completed_flame_count;
} ObjectBlastRuntime;

void object_blast_runtime_init(ObjectBlastRuntime *runtime);

/* newanims.s:ItsABullet:notpopping's `move.b ShotT_Size_b,BLOODYGREATBOMB`. */
void object_blast_runtime_note_bullet(ObjectBlastRuntime *runtime, uint8_t bullet_index);

/*
 * Direct translation of newanims.s:ComputeBlast. The caller must first write
 * the source Viewer* words in `visibility` exactly as its own source branch
 * does; in particular, the direct-target ItsABullet caller retains ViewerTop.
 */
int object_blast_compute(ObjectBlastRuntime *runtime, ObjectRuntime *objects,
                         uint32_t explosive_slot_index,
                         LevelDynamicState *dynamic_level, const AssetBlob *clips,
                         const GameLink *game_link, GameRandom *random,
                         const ObjectVisibilityRuntime *visibility,
                         int16_t explosive_force,
                         char *error, size_t error_size);

#endif
