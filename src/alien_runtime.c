#include "alien_runtime.h"

#include <string.h>

void alien_runtime_init(AlienRuntime *runtime)
{
    if (runtime) {
        /* bss/ai_bss.s is zero-initialized once when the source starts. */
        memset(runtime, 0, sizeof(*runtime));
        object_motion_runtime_init(&runtime->motion);
        object_visibility_runtime_init(&runtime->visibility);
    }
}

void alien_runtime_begin_single_player(AlienRuntime *runtime)
{
    if (runtime) {
        /* 68000 ST writes 0xff, not a host boolean value. */
        runtime->no_enemies = UINT8_MAX;
    }
}

void alien_runtime_begin_level(AlienRuntime *runtime)
{
    uint16_t index;

    if (!runtime) {
        return;
    }
    /* modules/ai.s:AI_InitAlienWorkspace. */
    for (index = 0u; index < ALIEN_RUNTIME_ENTITY_COUNT; ++index) {
        runtime->entity_workspace[index][0u] = 0;
        runtime->entity_workspace[index][1u] = 0;
        runtime->entity_workspace[index][2u] = -1;
        runtime->entity_workspace[index][3u] = -1;
        runtime->entity_workspace[index][4u] = -1;
        runtime->entity_workspace[index][5u] = -1;
    }
    for (index = 0u; index < ALIEN_RUNTIME_TEAM_COUNT; ++index) {
        runtime->team_workspace[index][0u] = 0;
        runtime->team_workspace[index][1u] = 0;
        runtime->team_workspace[index][2u] = -1;
        runtime->team_workspace[index][3u] = -1;
        runtime->team_workspace[index][4u] = -1;
        runtime->team_workspace[index][5u] = -1;
    }
    /* hires.s:Game_Begin:CLRDAM. */
    memset(runtime->damage, 0, sizeof(runtime->damage));
}
