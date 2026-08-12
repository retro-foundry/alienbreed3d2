#ifndef AB3D2_UI_TEXT_LAYOUT_H
#define AB3D2_UI_TEXT_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#include "scene_frame.h"

/* Renderer-independent atlas identity. Backends own decoding and upload. */
typedef struct {
    SceneHudFont font;
    /* c/message.c tag identity; non-text atlases leave this unused. */
    uint32_t style_id;
    uint16_t glyph_index;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint16_t source_x;
    uint16_t source_y;
    uint16_t source_width;
    uint16_t source_height;
} UiTextGlyph;

/* An upper bound suitable for one allocation before ui_text_layout_frame. */
size_t ui_text_layout_glyph_capacity(const SceneFrame *frame);

/*
 * Port of Alien Breed 3D I display.c's bitmap-text and bottom status layout.
 * The output uses top-left drawable pixels and contains no graphics-API state.
 */
int ui_text_layout_frame(const SceneFrame *frame, int32_t drawable_width,
                         int32_t drawable_height, UiTextGlyph *glyphs,
                         size_t glyph_capacity, size_t *out_glyph_count,
                         char *error, size_t error_size);

#endif
