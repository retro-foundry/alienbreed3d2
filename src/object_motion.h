#ifndef AB3D2_OBJECT_MOTION_H
#define AB3D2_OBJECT_MOTION_H

#include <stdint.h>

/*
 * objectmove.s:newx/newz are process-lifetime shared BSS words. The native
 * port retains the coordinate words that current translated consumers read;
 * other objectmove BSS fields remain with their owning source translations.
 */
typedef struct {
    int16_t new_x;
    int16_t new_z;
} ObjectMotionRuntime;

static inline void object_motion_runtime_init(ObjectMotionRuntime *runtime)
{
    if (runtime) {
        runtime->new_x = 0;
        runtime->new_z = 0;
    }
}

/* Source `move.w ...,newx/newz` publication. */
static inline void object_motion_runtime_set_new_words(ObjectMotionRuntime *runtime,
                                                       int16_t new_x, int16_t new_z)
{
    if (runtime) {
        runtime->new_x = new_x;
        runtime->new_z = new_z;
    }
}

#endif
