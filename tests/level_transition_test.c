#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_link.h"
#include "level_transition.h"
#include "scene_frame.h"

int main(void)
{
    SceneFrame frame;
    uint8_t *story;
    size_t story_size = level_transition_story_asset_size();
    size_t level_offset = 3u * LEVEL_TRANSITION_LINE_COUNT *
        LEVEL_TRANSITION_LINE_RECORD_BYTES;
    char error[256] = {0};

    story = malloc(story_size);
    if (!story || !scene_frame_init(&frame, LEVEL_TRANSITION_LINE_COUNT)) {
        fprintf(stderr, "level transition test allocation failed\n");
        free(story);
        return 1;
    }
    memset(story, ' ', story_size);
    story[level_offset] = 0u;
    story[level_offset + 1u] = 1u;
    memcpy(story + level_offset + 2u, "AUTHORED", 8u);

    if (story_size != (size_t)GAME_LINK_LEVEL_COUNT * 16u * 82u ||
        level_transition_alpha_for_step(-1) != 32u ||
        level_transition_alpha_for_step(0) != 32u ||
        level_transition_alpha_for_step(7) != 255u ||
        level_transition_alpha_for_step(99) != 255u ||
        !level_transition_submit_text(story, story_size, 3u, &frame,
                                      error, sizeof(error)) ||
        frame.count != LEVEL_TRANSITION_LINE_COUNT ||
        frame.commands[0u].type != SCENE_COMMAND_HUD_TEXT ||
        frame.commands[0u].data.hud_text.layout !=
            SCENE_HUD_LAYOUT_FIRST_PORT_LEVEL_TEXT ||
        frame.commands[0u].data.hud_text.text_byte_count !=
            LEVEL_TRANSITION_TEXT_BYTES ||
        memcmp(frame.commands[0u].data.hud_text.text, "AUTHORED", 8u) != 0) {
        fprintf(stderr, "authored level transition submission failed: %s\n", error);
        scene_frame_destroy(&frame);
        free(story);
        return 1;
    }
    scene_frame_begin(&frame);
    if (level_transition_submit_text(story, story_size - 1u, 3u, &frame,
                                     error, sizeof(error)) ||
        level_transition_submit_text(story, story_size, GAME_LINK_LEVEL_COUNT,
                                     &frame, error, sizeof(error))) {
        fprintf(stderr, "invalid level transition source was accepted\n");
        scene_frame_destroy(&frame);
        free(story);
        return 1;
    }

    scene_frame_destroy(&frame);
    free(story);
    return 0;
}
