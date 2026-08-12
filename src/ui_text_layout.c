#include "ui_text_layout.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    /* Alien Breed 3D I display.c bitmap-atlas metadata. */
    UI_TEXT_ASCII_FIRST = 32,
    UI_TEXT_ASCII_LAST = 126,
    UI_TEXT_ASCII_COLUMNS = 16,
    UI_TEXT_ASCII_CELL_WIDTH = 12,
    UI_TEXT_ASCII_CELL_HEIGHT = 17,
    UI_TEXT_ASCII_DRAW_X = 2,
    UI_TEXT_ASCII_DRAW_Y = 2,
    UI_TEXT_ASCII_DRAW_WIDTH = 8,
    UI_TEXT_ASCII_DRAW_HEIGHT = 13,
    UI_TEXT_ASCII_ADVANCE = 8,
    UI_TEXT_DIGIT_COUNT = 10,
    UI_TEXT_DIGIT_CELL_WIDTH = 9,
    UI_TEXT_DIGIT_CELL_HEIGHT = 11,
    UI_TEXT_DIGIT_STRIDE = 11,
    UI_TEXT_COUNTER_LIMIT = 999
};

static void ui_text_layout_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int32_t ui_text_layout_round_scale(int32_t value, int32_t scale_numerator,
                                          int32_t scale_denominator)
{
    int64_t product = (int64_t)value * scale_numerator;

    return (int32_t)((product + scale_denominator / 2) / scale_denominator);
}

static int ui_text_layout_append(UiTextGlyph *glyphs, size_t glyph_capacity,
                                 size_t *glyph_count, const UiTextGlyph *glyph,
                                 char *error, size_t error_size)
{
    if (!glyphs || !glyph_count || !glyph || *glyph_count >= glyph_capacity) {
        ui_text_layout_set_error(error, error_size,
                                 "UI text glyph buffer has insufficient capacity");
        return 0;
    }
    glyphs[(*glyph_count)++] = *glyph;
    return 1;
}

static int ui_text_layout_validate_command(const SceneHudText *text,
                                           char *error, size_t error_size)
{
    if (!text || text->text_byte_count > SCENE_HUD_TEXT_CAPACITY) {
        ui_text_layout_set_error(error, error_size,
                                 "scene HUD text exceeds its retained command capacity");
        return 0;
    }
    if (text->layout == SCENE_HUD_LAYOUT_REFERENCE_POSITION) {
        if (text->font != SCENE_HUD_FONT_FIRST_PORT_ASCII ||
            text->reference_width == 0u || text->reference_height == 0u) {
            ui_text_layout_set_error(error, error_size,
                                     "reference-position HUD text has invalid font or canvas");
            return 0;
        }
    } else if ((text->layout == SCENE_HUD_LAYOUT_FIRST_PORT_HEALTH &&
                text->font != SCENE_HUD_FONT_FIRST_PORT_HEALTH_DIGITS) ||
               (text->layout == SCENE_HUD_LAYOUT_FIRST_PORT_AMMUNITION &&
                text->font != SCENE_HUD_FONT_FIRST_PORT_AMMO_DIGITS)) {
        ui_text_layout_set_error(error, error_size,
                                 "first-port HUD counter has the wrong digit atlas");
        return 0;
    } else if (text->layout != SCENE_HUD_LAYOUT_FIRST_PORT_HEALTH &&
               text->layout != SCENE_HUD_LAYOUT_FIRST_PORT_AMMUNITION) {
        ui_text_layout_set_error(error, error_size, "scene HUD text layout is unsupported");
        return 0;
    }
    return 1;
}

static int ui_text_layout_reference_text(const SceneHudText *text,
                                         int32_t drawable_width, int32_t drawable_height,
                                         UiTextGlyph *glyphs, size_t glyph_capacity,
                                         size_t *glyph_count, char *error, size_t error_size)
{
    int32_t scale_numerator;
    int32_t scale_denominator = 256;
    int32_t canvas_width;
    int32_t canvas_height;
    int32_t canvas_x;
    int32_t canvas_y;
    int32_t pen_x;
    int32_t pen_y;
    int32_t draw_width;
    int32_t draw_height;
    int32_t advance;

    scale_numerator = (int32_t)((int64_t)drawable_width * scale_denominator /
                                text->reference_width);
    {
        int32_t height_scale = (int32_t)((int64_t)drawable_height * scale_denominator /
                                         text->reference_height);
        if (height_scale < scale_numerator) {
            scale_numerator = height_scale;
        }
    }
    if (scale_numerator < 1) {
        scale_numerator = 1;
    }
    if (scale_numerator > 4 * scale_denominator) {
        scale_numerator = 4 * scale_denominator;
    }
    /* AB3D1 display_text_crisp_scale_q: whole-pixel magnification above 1x. */
    if (scale_numerator > scale_denominator) {
        scale_numerator = (scale_numerator / scale_denominator) * scale_denominator;
    }
    canvas_width = ui_text_layout_round_scale(text->reference_width,
                                               scale_numerator, scale_denominator);
    canvas_height = ui_text_layout_round_scale(text->reference_height,
                                                scale_numerator, scale_denominator);
    canvas_x = (drawable_width - canvas_width) / 2;
    canvas_y = (drawable_height - canvas_height) / 2;
    pen_x = canvas_x + ui_text_layout_round_scale(text->x, scale_numerator,
                                                   scale_denominator);
    pen_y = canvas_y + ui_text_layout_round_scale(text->y, scale_numerator,
                                                   scale_denominator);
    draw_width = ui_text_layout_round_scale(UI_TEXT_ASCII_DRAW_WIDTH,
                                             scale_numerator, scale_denominator);
    draw_height = ui_text_layout_round_scale(UI_TEXT_ASCII_DRAW_HEIGHT,
                                              scale_numerator, scale_denominator);
    advance = ui_text_layout_round_scale(UI_TEXT_ASCII_ADVANCE,
                                          scale_numerator, scale_denominator);
    if (draw_width < 1) draw_width = 1;
    if (draw_height < 1) draw_height = 1;
    if (advance < 1) advance = 1;

    for (uint16_t index = 0u; index < text->text_byte_count; ++index) {
        uint8_t character = (uint8_t)text->text[index];

        if (character < UI_TEXT_ASCII_FIRST || character > UI_TEXT_ASCII_LAST) {
            character = (uint8_t)'?';
        }
        if (character != (uint8_t)' ') {
            uint16_t glyph_index = (uint16_t)(character - UI_TEXT_ASCII_FIRST);
            UiTextGlyph glyph;

            memset(&glyph, 0, sizeof(glyph));
            glyph.font = SCENE_HUD_FONT_FIRST_PORT_ASCII;
            glyph.glyph_index = glyph_index;
            glyph.x = pen_x;
            glyph.y = pen_y;
            glyph.width = draw_width;
            glyph.height = draw_height;
            glyph.source_x = (uint16_t)((glyph_index % UI_TEXT_ASCII_COLUMNS) *
                                        UI_TEXT_ASCII_CELL_WIDTH + UI_TEXT_ASCII_DRAW_X);
            glyph.source_y = (uint16_t)((glyph_index / UI_TEXT_ASCII_COLUMNS) *
                                        UI_TEXT_ASCII_CELL_HEIGHT + UI_TEXT_ASCII_DRAW_Y);
            glyph.source_width = UI_TEXT_ASCII_DRAW_WIDTH;
            glyph.source_height = UI_TEXT_ASCII_DRAW_HEIGHT;
            if (!ui_text_layout_append(glyphs, glyph_capacity, glyph_count, &glyph,
                                       error, error_size)) {
                return 0;
            }
        }
        if (pen_x > INT32_MAX - advance) {
            ui_text_layout_set_error(error, error_size,
                                     "reference-position HUD text exceeds drawable coordinates");
            return 0;
        }
        pen_x += advance;
    }
    return 1;
}

static int ui_text_layout_counter_value(const SceneHudText *text, int *out_value,
                                        char *error, size_t error_size)
{
    int value = 0;

    if (!text || !out_value || text->text_byte_count == 0u ||
        text->text_byte_count > 3u) {
        ui_text_layout_set_error(error, error_size,
                                 "first-port HUD counter is not a one-to-three digit value");
        return 0;
    }
    for (uint16_t index = 0u; index < text->text_byte_count; ++index) {
        uint8_t character = (uint8_t)text->text[index];

        if (character < (uint8_t)'0' || character > (uint8_t)'9') {
            ui_text_layout_set_error(error, error_size,
                                     "first-port HUD counter contains a non-digit");
            return 0;
        }
        value = value * 10 + (int)(character - (uint8_t)'0');
    }
    if (value > UI_TEXT_COUNTER_LIMIT) {
        ui_text_layout_set_error(error, error_size,
                                 "first-port HUD counter exceeds three digits");
        return 0;
    }
    *out_value = value;
    return 1;
}

static int32_t ui_text_layout_crisp_digit_height(int32_t digit_height)
{
    if (digit_height <= UI_TEXT_DIGIT_CELL_HEIGHT) {
        return digit_height;
    }
    return (digit_height / UI_TEXT_DIGIT_CELL_HEIGHT) * UI_TEXT_DIGIT_CELL_HEIGHT;
}

static int ui_text_layout_append_counter(SceneHudFont font, int value,
                                         int32_t x, int32_t y, int32_t digit_height,
                                         int32_t slot_width, int32_t gap,
                                         UiTextGlyph *glyphs, size_t glyph_capacity,
                                         size_t *glyph_count, char *error, size_t error_size)
{
    char digits[4];
    int start;

    (void)snprintf(digits, sizeof(digits), "%03d", value);
    start = value == 0 ? 2 : 0;
    while (start < 2 && digits[start] == '0') {
        ++start;
    }
    for (int slot = start; slot < 3; ++slot) {
        uint16_t digit = (uint16_t)(digits[slot] - '0');
        UiTextGlyph glyph;

        memset(&glyph, 0, sizeof(glyph));
        glyph.font = font;
        glyph.glyph_index = digit;
        glyph.x = x + slot * (slot_width + gap);
        glyph.y = y;
        glyph.width = slot_width;
        glyph.height = digit_height;
        glyph.source_x = (uint16_t)(digit * UI_TEXT_DIGIT_STRIDE);
        glyph.source_y = 0u;
        glyph.source_width = UI_TEXT_DIGIT_CELL_WIDTH;
        glyph.source_height = UI_TEXT_DIGIT_CELL_HEIGHT;
        if (!ui_text_layout_append(glyphs, glyph_capacity, glyph_count, &glyph,
                                   error, error_size)) {
            return 0;
        }
    }
    return 1;
}

static int ui_text_layout_first_port_status(const SceneHudText *health,
                                            const SceneHudText *ammunition,
                                            int32_t drawable_width, int32_t drawable_height,
                                            UiTextGlyph *glyphs, size_t glyph_capacity,
                                            size_t *glyph_count, char *error, size_t error_size)
{
    int health_value = 0;
    int ammunition_value = 0;
    int32_t margin;
    int32_t key_height;
    int32_t key_gap;
    int32_t key_group_width;
    int32_t key_x;
    int32_t y;
    int32_t stat_gap;
    int32_t digit_gap;
    int32_t digit_height;
    int32_t slot_width;
    int32_t counter_width;
    int32_t right;

    if ((health && !ui_text_layout_counter_value(health, &health_value, error, error_size)) ||
        (ammunition && !ui_text_layout_counter_value(ammunition, &ammunition_value,
                                                     error, error_size))) {
        return 0;
    }
    /* AB3D1 display.c:hud_key_row_layout; keys stay reserved but are not drawn. */
    margin = drawable_height / 64;
    if (margin < 2) margin = 2;
    key_height = drawable_height / 32;
    if (key_height < UI_TEXT_DIGIT_CELL_HEIGHT) key_height = UI_TEXT_DIGIT_CELL_HEIGHT;
    key_height = ui_text_layout_crisp_digit_height(key_height);
    key_gap = key_height / 12;
    if (key_gap < 2) key_gap = 2;
    key_group_width = 4 * key_height + 3 * key_gap;
    key_x = drawable_width - margin - key_group_width;
    if (key_x < 0) key_x = 0;
    y = drawable_height - margin - key_height;

    stat_gap = key_height / 4;
    if (stat_gap < 4) stat_gap = 4;
    digit_gap = key_height / 16;
    if (digit_gap < 1) digit_gap = 1;
    digit_height = ui_text_layout_crisp_digit_height(key_height);
    slot_width = (UI_TEXT_DIGIT_CELL_WIDTH * digit_height +
                  UI_TEXT_DIGIT_CELL_HEIGHT / 2) / UI_TEXT_DIGIT_CELL_HEIGHT;
    counter_width = slot_width * 3 + digit_gap * 2;

    /* Match AB3D1's narrow-window shrink while retaining its original gaps. */
    for (int attempt = 0; attempt < 10; ++attempt) {
        int32_t counter_count = (health ? 1 : 0) + (ammunition ? 1 : 0);
        int32_t total = counter_count * counter_width +
            (counter_count == 2 ? stat_gap : 0);

        if (key_x - stat_gap - total >= margin) {
            break;
        }
        digit_height = ui_text_layout_crisp_digit_height(digit_height * 9 / 10);
        if (digit_height < 6) {
            break;
        }
        slot_width = (UI_TEXT_DIGIT_CELL_WIDTH * digit_height +
                      UI_TEXT_DIGIT_CELL_HEIGHT / 2) / UI_TEXT_DIGIT_CELL_HEIGHT;
        counter_width = slot_width * 3 + digit_gap * 2;
    }

    right = key_x - stat_gap;
    if (ammunition) {
        int32_t x = right - counter_width;

        if (!ui_text_layout_append_counter(
                SCENE_HUD_FONT_FIRST_PORT_AMMO_DIGITS, ammunition_value,
                x, y, digit_height, slot_width, digit_gap,
                glyphs, glyph_capacity, glyph_count, error, error_size)) {
            return 0;
        }
        right = x - stat_gap;
    }
    if (health) {
        int32_t x = right - counter_width;

        if (!ui_text_layout_append_counter(
                SCENE_HUD_FONT_FIRST_PORT_HEALTH_DIGITS, health_value,
                x, y, digit_height, slot_width, digit_gap,
                glyphs, glyph_capacity, glyph_count, error, error_size)) {
            return 0;
        }
    }
    return 1;
}

size_t ui_text_layout_glyph_capacity(const SceneFrame *frame)
{
    size_t capacity = 0u;

    if (!frame) {
        return 0u;
    }
    for (size_t index = 0u; index < frame->count; ++index) {
        const SceneCommand *command = &frame->commands[index];

        if (command->type == SCENE_COMMAND_HUD_TEXT) {
            if (capacity > SIZE_MAX - command->data.hud_text.text_byte_count) {
                return SIZE_MAX;
            }
            capacity += command->data.hud_text.text_byte_count;
        }
    }
    return capacity;
}

int ui_text_layout_frame(const SceneFrame *frame, int32_t drawable_width,
                         int32_t drawable_height, UiTextGlyph *glyphs,
                         size_t glyph_capacity, size_t *out_glyph_count,
                         char *error, size_t error_size)
{
    const SceneHudText *health = NULL;
    const SceneHudText *ammunition = NULL;
    size_t glyph_count = 0u;

    if (!frame || drawable_width < 1 || drawable_height < 1 || !out_glyph_count ||
        (glyph_capacity != 0u && !glyphs)) {
        ui_text_layout_set_error(error, error_size, "UI text layout received invalid state");
        return 0;
    }
    for (size_t index = 0u; index < frame->count; ++index) {
        const SceneCommand *command = &frame->commands[index];
        const SceneHudText *text;

        if (command->type != SCENE_COMMAND_HUD_TEXT) {
            continue;
        }
        text = &command->data.hud_text;
        if (!ui_text_layout_validate_command(text, error, error_size)) {
            return 0;
        }
        if (text->layout == SCENE_HUD_LAYOUT_FIRST_PORT_HEALTH) {
            if (health) {
                ui_text_layout_set_error(error, error_size,
                                         "scene frame contains duplicate health HUD text");
                return 0;
            }
            health = text;
        } else if (text->layout == SCENE_HUD_LAYOUT_FIRST_PORT_AMMUNITION) {
            if (ammunition) {
                ui_text_layout_set_error(error, error_size,
                                         "scene frame contains duplicate ammunition HUD text");
                return 0;
            }
            ammunition = text;
        } else if (!ui_text_layout_reference_text(
                       text, drawable_width, drawable_height, glyphs, glyph_capacity,
                       &glyph_count, error, error_size)) {
            return 0;
        }
    }
    if ((health || ammunition) &&
        !ui_text_layout_first_port_status(
            health, ammunition, drawable_width, drawable_height,
            glyphs, glyph_capacity, &glyph_count, error, error_size)) {
        return 0;
    }
    *out_glyph_count = glyph_count;
    return 1;
}
