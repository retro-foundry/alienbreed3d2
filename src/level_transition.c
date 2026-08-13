#include "level_transition.h"

#include <stdio.h>
#include <string.h>

#include "game_link.h"

static void level_transition_set_error(char *error, size_t error_size,
                                       const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

size_t level_transition_story_asset_size(void)
{
    return (size_t)GAME_LINK_LEVEL_COUNT * LEVEL_TRANSITION_LINE_COUNT *
        LEVEL_TRANSITION_LINE_RECORD_BYTES;
}

uint8_t level_transition_alpha_for_step(int step)
{
    if (step < 0) {
        step = 0;
    } else if (step >= LEVEL_TRANSITION_FADE_STEPS) {
        step = LEVEL_TRANSITION_FADE_STEPS - 1;
    }
    return (uint8_t)(((step + 1) * 255 + LEVEL_TRANSITION_FADE_STEPS - 1) /
                     LEVEL_TRANSITION_FADE_STEPS);
}

int level_transition_submit_text(const uint8_t *story_bytes, size_t story_size,
                                 uint16_t level_index, SceneFrame *frame,
                                 char *error, size_t error_size)
{
    size_t level_offset;

    if (!story_bytes || !frame || level_index >= GAME_LINK_LEVEL_COUNT ||
        story_size < level_transition_story_asset_size()) {
        level_transition_set_error(
            error, error_size,
            "level transition has no complete AB3D2 TEXT_FILE record");
        return 0;
    }
    level_offset = (size_t)level_index * LEVEL_TRANSITION_LINE_COUNT *
        LEVEL_TRANSITION_LINE_RECORD_BYTES;
    for (uint16_t line_index = 0u;
         line_index < LEVEL_TRANSITION_LINE_COUNT; ++line_index) {
        const uint8_t *line = story_bytes + level_offset +
            (size_t)line_index * LEVEL_TRANSITION_LINE_RECORD_BYTES;
        SceneCommand command;

        memset(&command, 0, sizeof(command));
        command.type = SCENE_COMMAND_HUD_TEXT;
        /* The first two source bytes are line controls; authored text is the
         * following fixed 80-byte field consumed by Game_ShowIntroText. */
        if (!scene_hud_text_set(&command.data.hud_text, line + 2u,
                                LEVEL_TRANSITION_TEXT_BYTES)) {
            level_transition_set_error(
                error, error_size,
                "level transition could not retain its authored story line");
            return 0;
        }
        command.data.hud_text.style_id = 0u;
        command.data.hud_text.font = SCENE_HUD_FONT_FIRST_PORT_ASCII;
        command.data.hud_text.layout = SCENE_HUD_LAYOUT_FIRST_PORT_LEVEL_TEXT;
        if (!scene_frame_submit(frame, &command)) {
            level_transition_set_error(
                error, error_size,
                "level transition scene command buffer is full");
            return 0;
        }
    }
    return 1;
}
