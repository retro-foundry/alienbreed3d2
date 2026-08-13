#ifndef AB3D2_LEVEL_TRANSITION_H
#define AB3D2_LEVEL_TRANSITION_H

#include <stddef.h>
#include <stdint.h>

#include "scene_frame.h"

enum {
    /* hires.s:Game_ShowIntroText reads 16 fixed 82-byte records per level. */
    LEVEL_TRANSITION_LINE_COUNT = 16,
    LEVEL_TRANSITION_LINE_RECORD_BYTES = 82,
    LEVEL_TRANSITION_TEXT_BYTES = 80,
    /* Alien Breed 3D I control_loop.c transition timing. */
    LEVEL_TRANSITION_FADE_STEPS = 8,
    LEVEL_TRANSITION_FADE_FRAME_MS = 20,
    LEVEL_TRANSITION_MIN_DISMISS_MS = 750
};

size_t level_transition_story_asset_size(void);
uint8_t level_transition_alpha_for_step(int step);

/* Submit one exact AB3D2 TEXT_FILE record as grouped first-port HUD text. */
int level_transition_submit_text(const uint8_t *story_bytes, size_t story_size,
                                 uint16_t level_index, SceneFrame *frame,
                                 char *error, size_t error_size);

#endif
