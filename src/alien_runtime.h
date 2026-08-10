#ifndef AB3D2_ALIEN_RUNTIME_H
#define AB3D2_ALIEN_RUNTIME_H

#include <stdint.h>

#include "object_visibility.h"

enum {
    /* modules/ai.s and bss/ai_bss.s source allocation counts. */
    ALIEN_RUNTIME_ENTITY_COUNT = 300u,
    ALIEN_RUNTIME_TEAM_COUNT = 30u,
    ALIEN_RUNTIME_WORKSPACE_WORD_COUNT = 8u,
    ALIEN_RUNTIME_BOREDOM_WORD_COUNT = 4u
};

/*
 * Source-owned AI storage.  Values are host-endian words because this mirrors
 * the mutable bss state addressed by the 68000 routines, rather than a media
 * record.  No native AI behaviour is implied by owning this state.
 */
typedef struct {
    /*
     * controlloop.s:AI_NoEnemies_b.  Despite its source name, SETPLAYERS
     * asserts it for native single-player enemy updates and clears it for the
     * original master/slave handoff.
     */
    uint8_t no_enemies;
    /* bss/ai_bss.s: ai_AlienWorkspace_vl, 16 bytes per entity. */
    int16_t entity_workspace[ALIEN_RUNTIME_ENTITY_COUNT][ALIEN_RUNTIME_WORKSPACE_WORD_COUNT];
    /* bss/ai_bss.s: AI_AlienTeamWorkspace_vl, 16 bytes per team. */
    int16_t team_workspace[ALIEN_RUNTIME_TEAM_COUNT][ALIEN_RUNTIME_WORKSPACE_WORD_COUNT];
    /* bss/ai_bss.s: AI_Damaged_vw. */
    int16_t damage[ALIEN_RUNTIME_ENTITY_COUNT];
    /* bss/ai_bss.s: AI_BoredomSpace_vl, eight bytes per entity. */
    int16_t boredom[ALIEN_RUNTIME_ENTITY_COUNT][ALIEN_RUNTIME_BOREDOM_WORD_COUNT];
    /* objectmove.s:AngRet, retained across source AI mode calls. */
    uint16_t heading_angle;
    /* objectmove.s:Viewer* shared BSS, written by every live CanItBeSeen caller. */
    ObjectVisibilityRuntime visibility;
} AlienRuntime;

/* Source process/BSS initialization before the first Game_Begin. */
void alien_runtime_init(AlienRuntime *runtime);

/* controlloop.s:SETPLAYERS' single-player `st AI_NoEnemies_b`. */
void alien_runtime_begin_single_player(AlienRuntime *runtime);

/*
 * hires.s:Game_Begin's AI_InitAlienWorkspace followed by CLRDAM.  The source
 * overwrites words 0..5 of each workspace and all damage words, while leaving
 * workspace words 6..7 and AI_BoredomSpace_vl untouched across level loads.
 */
void alien_runtime_begin_level(AlienRuntime *runtime);

#endif
