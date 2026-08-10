#include "message_runtime.h"

#include <stdio.h>
#include <string.h>

enum {
    /* c/screen.h, c/draw.h, and c/message.c small-screen constants. */
    MESSAGE_RUNTIME_SCREEN_WIDTH = 320u,
    MESSAGE_RUNTIME_SMALL_HEIGHT = 160u,
    MESSAGE_RUNTIME_HUD_BORDER_WIDTH = 16u,
    MESSAGE_RUNTIME_DRAW_TEXT_MARGIN = 4u,
    MESSAGE_RUNTIME_DRAW_MSG_CHAR_WIDTH = 8u,
    MESSAGE_RUNTIME_DRAW_MSG_CHAR_HEIGHT = 8u,
    MESSAGE_RUNTIME_DRAW_TEXT_Y_SPACING = 2u,
    MESSAGE_RUNTIME_MAX_PROP_CHAR_WIDTH = 7u
};

static void message_runtime_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int message_runtime_is_printable(uint8_t character)
{
    /* c/draw.h:Draw_IsPrintable. */
    return (character > 0x20u && character < 0x7fu) || character > 0xa0u;
}

static uint8_t message_runtime_next_line(const MessageRuntime *runtime, uint8_t line_number)
{
    if (runtime->fullscreen != 0u) {
        return (uint8_t)((line_number + 1u) & (MESSAGE_RUNTIME_LINE_COUNT - 1u));
    }
    return line_number < MESSAGE_RUNTIME_MAX_LINES_SMALL ?
        (uint8_t)(line_number + 1u) : 0u;
}

static void message_runtime_push_raw(MessageRuntime *runtime, const uint8_t *text,
                                     uint16_t length_and_tag)
{
    uint8_t line_number = message_runtime_next_line(runtime, runtime->line_number);

    runtime->lines[line_number].text = text;
    runtime->lines[line_number].length_and_tag = length_and_tag;
    runtime->line_number = line_number;
    if (text) {
        runtime->lines_visible = UINT8_MAX;
    }
}

static void message_runtime_nudge_string(uint8_t *buffer, uint16_t buffer_length)
{
    uint8_t *to = buffer + buffer_length;
    uint8_t *from = to - 1u;

    /* c/message.c:msg_NudgeString. */
    while (from > buffer) {
        *to-- = *from--;
    }
    *buffer = (uint8_t)' ';
}

static void message_runtime_compact_string(uint8_t *buffer, uint16_t buffer_length)
{
    uint8_t *read = buffer;
    uint8_t *write = buffer;
    uint8_t *last = buffer + buffer_length;
    int skip = 1;

    /* c/message.c:msg_CompactString. */
    while (buffer_length-- > 0u) {
        uint8_t character = *read++;

        if (message_runtime_is_printable(character)) {
            skip = 0;
        } else if (!skip) {
            skip = 1;
        } else {
            continue;
        }
        *write++ = character;
    }
    if (skip) {
        if (write == buffer) {
            /*
             * Shipped levels contain unused all-blank records. The maintained
             * source decrements its write pointer before this buffer; retain
             * the intended empty-message result without an out-of-bounds write.
             */
            buffer[0] = 0u;
            return;
        }
        --write;
    }
    if (write < last) {
        *write = 0u;
    }
}

static uint16_t message_runtime_split(const MessageRuntime *runtime,
                                      const uint8_t **next_text, uint16_t text_length,
                                      uint16_t fit_width)
{
    const uint8_t *text = *next_text;
    const uint8_t *last_non_printing = NULL;
    uint16_t width = 0u;
    uint16_t characters_left = text_length;
    uint8_t character = 0u;

    /* c/draw.c:Draw_CalcPropTextSplit. */
    if ((uint32_t)text_length * MESSAGE_RUNTIME_DRAW_MSG_CHAR_WIDTH <= fit_width) {
        *next_text = NULL;
        return text_length;
    }
    while (characters_left != 0u && width < fit_width && (character = *text) != 0u) {
        if (!message_runtime_is_printable(character)) {
            last_non_printing = text;
        }
        width = (uint16_t)(width + (runtime->glyph_spacing[character] >> 4u));
        --characters_left;
        ++text;
    }
    if (width > fit_width) {
        character = *--text;
        ++characters_left;
    }
    if (last_non_printing && characters_left > 1u && message_runtime_is_printable(character)) {
        text = last_non_printing + 1u;
    }
    width = (uint16_t)(text - *next_text);
    if (character == 0u || characters_left == 0u) {
        text = NULL;
    }
    if (text) {
        while (characters_left > 0u && !message_runtime_is_printable(*text)) {
            ++text;
            --characters_left;
        }
    }
    *next_text = text;
    return width;
}

int message_runtime_init(MessageRuntime *runtime, uint8_t *level_bytes, size_t level_size,
                         const uint8_t *glyph_spacing, size_t glyph_spacing_size,
                         char *error, size_t error_size)
{
    if (!runtime || !level_bytes ||
        level_size < MESSAGE_RUNTIME_LEVEL_MESSAGE_COUNT * MESSAGE_RUNTIME_LEVEL_MESSAGE_LENGTH ||
        !glyph_spacing ||
        glyph_spacing_size != MESSAGE_RUNTIME_GLYPH_SPACING_BYTE_COUNT) {
        message_runtime_set_error(error, error_size, "Msg_Init received invalid source state");
        return 0;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->glyph_spacing = glyph_spacing;
    runtime->line_number = MESSAGE_RUNTIME_LINE_COUNT - 1u;
    runtime->redraw_count = 1u;

    /* c/message.c: Msg_Init preprocesses all ten fixed level strings in place. */
    for (uint16_t message_index = 0u;
         message_index < MESSAGE_RUNTIME_LEVEL_MESSAGE_COUNT; ++message_index) {
        uint8_t *message = level_bytes +
            (size_t)message_index * MESSAGE_RUNTIME_LEVEL_MESSAGE_LENGTH;

        if (message_runtime_is_printable(message[79u]) &&
            message_runtime_is_printable(message[80u])) {
            message_runtime_nudge_string(message + 80u, 79u);
        }
        message_runtime_compact_string(message, MESSAGE_RUNTIME_LEVEL_MESSAGE_LENGTH);
    }
    return 1;
}

int message_runtime_push_line(MessageRuntime *runtime, const uint8_t *text,
                              uint16_t length_and_tag, uint8_t messages_enabled,
                              char *error, size_t error_size)
{
    uint16_t text_length;
    uint16_t max_fit;

    if (!runtime || !runtime->glyph_spacing || !text) {
        message_runtime_set_error(error, error_size,
                                  "Msg_PushLine received invalid source state");
        return 0;
    }
    if (messages_enabled == 0u) {
        return 1;
    }
    text_length = (uint16_t)(length_and_tag & MESSAGE_RUNTIME_LENGTH_MASK);
    max_fit = runtime->fullscreen != 0u ?
        (uint16_t)(MESSAGE_RUNTIME_SCREEN_WIDTH / MESSAGE_RUNTIME_MAX_PROP_CHAR_WIDTH - 2u) :
        (uint16_t)((MESSAGE_RUNTIME_SCREEN_WIDTH -
                    2u * MESSAGE_RUNTIME_HUD_BORDER_WIDTH) /
                       MESSAGE_RUNTIME_MAX_PROP_CHAR_WIDTH -
                   2u);
    runtime->redraw_count = 1u;
    if (text_length <= max_fit) {
        message_runtime_push_raw(runtime, text, length_and_tag);
    } else {
        const uint8_t *next_text = text;
        uint16_t lines = MESSAGE_RUNTIME_MAX_LINES_SMALL;
        uint16_t tag = (uint16_t)(length_and_tag & ~MESSAGE_RUNTIME_LENGTH_MASK);
        uint16_t fit_width = runtime->fullscreen != 0u ?
            (uint16_t)(MESSAGE_RUNTIME_SCREEN_WIDTH - 2u * MESSAGE_RUNTIME_DRAW_MSG_CHAR_WIDTH) :
            (uint16_t)(MESSAGE_RUNTIME_SCREEN_WIDTH -
                       2u * (MESSAGE_RUNTIME_HUD_BORDER_WIDTH +
                             MESSAGE_RUNTIME_DRAW_MSG_CHAR_WIDTH));

        do {
            uint16_t fit_length = message_runtime_split(runtime, &next_text, text_length,
                                                        fit_width);

            message_runtime_push_raw(runtime, text, (uint16_t)(fit_length | tag));
            text = next_text;
            text_length = (uint16_t)(text_length - fit_length);
        } while (next_text && lines-- != 0u);
    }
    return 1;
}

size_t message_runtime_visible_line_count(const MessageRuntime *runtime)
{
    uint8_t first;
    uint8_t line;
    size_t count = 0u;

    if (!runtime) {
        return 0u;
    }
    first = message_runtime_next_line(runtime, runtime->line_number);
    line = first;
    do {
        if (runtime->lines[line].text) {
            ++count;
        }
        line = message_runtime_next_line(runtime, line);
    } while (line != first);
    return count;
}

int message_runtime_submit_hud(const MessageRuntime *runtime, SceneFrame *frame)
{
    uint8_t first;
    uint8_t line;
    uint16_t y = (uint16_t)(MESSAGE_RUNTIME_SMALL_HEIGHT + MESSAGE_RUNTIME_DRAW_TEXT_MARGIN);
    uint8_t visible = 0u;

    if (!runtime || !frame) {
        return 0;
    }
    first = message_runtime_next_line(runtime, runtime->line_number);
    line = first;
    do {
        const MessageRuntimeLine *source = &runtime->lines[line];

        if (source->text) {
            SceneCommand command;

            command.type = SCENE_COMMAND_HUD_TEXT;
            command.data.hud_text.text = (const char *)source->text;
            command.data.hud_text.text_byte_count =
                (uint16_t)(source->length_and_tag & MESSAGE_RUNTIME_LENGTH_MASK);
            command.data.hud_text.x =
                (int16_t)(MESSAGE_RUNTIME_DRAW_TEXT_MARGIN + MESSAGE_RUNTIME_HUD_BORDER_WIDTH);
            command.data.hud_text.y = (int16_t)y;
            command.data.hud_text.style_id =
                (uint32_t)(source->length_and_tag >> MESSAGE_RUNTIME_TAG_SHIFT);
            if (!scene_frame_submit(frame, &command)) {
                return 0;
            }
            y = (uint16_t)(y + MESSAGE_RUNTIME_DRAW_MSG_CHAR_HEIGHT +
                           MESSAGE_RUNTIME_DRAW_TEXT_Y_SPACING);
            ++visible;
        }
        line = message_runtime_next_line(runtime, line);
    } while (line != first);
    (void)visible;
    return 1;
}
