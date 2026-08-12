#include <stdio.h>
#include <string.h>

#include "scene_frame.h"
#include "ui_text_layout.h"

static int submit_text(SceneFrame *frame, const char *text,
                       SceneHudFont font, SceneHudLayout layout)
{
    SceneCommand command;
    size_t length = strlen(text);

    memset(&command, 0, sizeof(command));
    command.type = SCENE_COMMAND_HUD_TEXT;
    if (!scene_hud_text_set(&command.data.hud_text, text, length)) {
        return 0;
    }
    command.data.hud_text.font = font;
    command.data.hud_text.layout = layout;
    return scene_frame_submit(frame, &command);
}

int main(void)
{
    SceneFrame source;
    SceneFrame clone;
    UiTextGlyph glyphs[8];
    size_t glyph_count = 0u;
    char error[256] = {0};

    if (!scene_frame_init(&source, 3u) || !scene_frame_init(&clone, 3u) ||
        !submit_text(&source, "100", SCENE_HUD_FONT_FIRST_PORT_HEALTH_DIGITS,
                     SCENE_HUD_LAYOUT_FIRST_PORT_HEALTH) ||
        !submit_text(&source, "20", SCENE_HUD_FONT_FIRST_PORT_AMMO_DIGITS,
                     SCENE_HUD_LAYOUT_FIRST_PORT_AMMUNITION) ||
        !scene_frame_clone(&clone, &source)) {
        fprintf(stderr, "first-port HUD test setup failed\n");
        return 1;
    }
    source.commands[0u].data.hud_text.text[0u] = '0';
    if (memcmp(clone.commands[0u].data.hud_text.text, "100", 3u) != 0 ||
        ui_text_layout_glyph_capacity(&clone) != 5u ||
        !ui_text_layout_frame(&clone, 1280, 720, glyphs, 8u, &glyph_count,
                              error, sizeof(error)) ||
        glyph_count != 5u) {
        fprintf(stderr, "retained first-port HUD layout failed: %s\n", error);
        return 1;
    }

    /* AB3D1 display.c:hud_key_row_layout and hud_draw_three_slot_value at 720p. */
    if (glyphs[0u].font != SCENE_HUD_FONT_FIRST_PORT_AMMO_DIGITS ||
        glyphs[0u].glyph_index != 2u || glyphs[0u].x != 1133 || glyphs[0u].y != 687 ||
        glyphs[0u].width != 18 || glyphs[0u].height != 22 || glyphs[0u].source_x != 22u ||
        glyphs[1u].glyph_index != 0u || glyphs[1u].x != 1152 ||
        glyphs[2u].font != SCENE_HUD_FONT_FIRST_PORT_HEALTH_DIGITS ||
        glyphs[2u].glyph_index != 1u || glyphs[2u].x != 1053 ||
        glyphs[3u].glyph_index != 0u || glyphs[3u].x != 1072 ||
        glyphs[4u].glyph_index != 0u || glyphs[4u].x != 1091) {
        fprintf(stderr, "first-port health/ammo glyph placement is inconsistent\n");
        return 1;
    }

    scene_frame_begin(&source);
    {
        SceneCommand command;

        memset(&command, 0, sizeof(command));
        command.type = SCENE_COMMAND_HUD_TEXT;
        if (!scene_hud_text_set(&command.data.hud_text, "A B", 3u)) {
            fprintf(stderr, "first-port ASCII command setup failed\n");
            return 1;
        }
        command.data.hud_text.x = 20;
        command.data.hud_text.y = 164;
        command.data.hud_text.reference_width = 320u;
        command.data.hud_text.reference_height = 256u;
        command.data.hud_text.font = SCENE_HUD_FONT_FIRST_PORT_ASCII;
        command.data.hud_text.layout = SCENE_HUD_LAYOUT_REFERENCE_POSITION;
        if (!scene_frame_submit(&source, &command) ||
            !ui_text_layout_frame(&source, 1280, 720, glyphs, 8u, &glyph_count,
                                  error, sizeof(error)) ||
            glyph_count != 2u || glyphs[0u].glyph_index != 33u ||
            glyphs[0u].x != 360 || glyphs[0u].y != 432 ||
            glyphs[0u].width != 16 || glyphs[0u].height != 26 ||
            glyphs[0u].source_x != 14u || glyphs[0u].source_y != 36u ||
            glyphs[1u].glyph_index != 34u || glyphs[1u].x != 392) {
            fprintf(stderr, "first-port ASCII atlas layout is inconsistent: %s\n", error);
            return 1;
        }
    }

    scene_frame_begin(&source);
    {
        SceneCommand command;

        memset(&command, 0, sizeof(command));
        command.type = SCENE_COMMAND_HUD_TEXT;
        if (!scene_hud_text_set(&command.data.hud_text, "A B", 3u)) {
            fprintf(stderr, "top-centred message command setup failed\n");
            return 1;
        }
        command.data.hud_text.y = 0;
        command.data.hud_text.reference_width = 320u;
        command.data.hud_text.reference_height = 256u;
        command.data.hud_text.style_id = 2u;
        command.data.hud_text.font = SCENE_HUD_FONT_FIRST_PORT_ASCII;
        command.data.hud_text.layout = SCENE_HUD_LAYOUT_TOP_CENTER;
        if (!scene_frame_submit(&source, &command) ||
            !ui_text_layout_frame(&source, 1280, 720, glyphs, 8u, &glyph_count,
                                  error, sizeof(error)) ||
            glyph_count != 2u || glyphs[0u].glyph_index != 33u ||
            glyphs[0u].style_id != 2u ||
            glyphs[0u].x != 616 || glyphs[0u].y != 30 ||
            glyphs[0u].width != 16 || glyphs[0u].height != 26 ||
            glyphs[1u].glyph_index != 34u || glyphs[1u].x != 648 ||
            glyphs[1u].y != 30) {
            fprintf(stderr, "top-centred first-port message layout is inconsistent: %s\n",
                    error);
            return 1;
        }
    }

    scene_frame_destroy(&clone);
    scene_frame_destroy(&source);
    return 0;
}
