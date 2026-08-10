#ifndef AB3D2_ALIEN_RUN_AROUND_H
#define AB3D2_ALIEN_RUN_AROUND_H

#include <stddef.h>
#include <stdint.h>

/*
 * newaliencontrol.s:RunAround source globals. `new_x`/`new_z` are both the
 * input target and the sideways-adjusted result. The calling AI mode owns
 * the preceding old-position global and object-point snapshot.
 */
typedef struct {
    int16_t old_x;
    int16_t old_z;
    int16_t new_x;
    int16_t new_z;
    int16_t player_sine;
    int16_t player_cosine;
    int16_t player_temporary_x;
    int16_t player_temporary_z;
    int16_t object_x;
    int16_t object_z;
} AlienRunAroundState;

/* Direct newaliencontrol.s:RunAround translation. */
int alien_run_around_apply(AlienRunAroundState *state, char *error, size_t error_size);

#endif
