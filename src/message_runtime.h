#ifndef AB3D2_MESSAGE_RUNTIME_H
#define AB3D2_MESSAGE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "scene_frame.h"

/* c/message.h and c/message.c: source line ring and length/tag encoding. */
enum {
    MESSAGE_RUNTIME_LINE_COUNT = 8u,
    MESSAGE_RUNTIME_MAX_LINES_SMALL = 4u,
    MESSAGE_RUNTIME_LENGTH_MASK = 0x3fffu,
    MESSAGE_RUNTIME_TAG_SHIFT = 14u,
    MESSAGE_RUNTIME_TAG_NARRATIVE = 0u,
    MESSAGE_RUNTIME_TAG_DEFAULT = 1u,
    MESSAGE_RUNTIME_TAG_OPTIONS = 2u,
    MESSAGE_RUNTIME_TAG_OTHER = 3u,
    MESSAGE_RUNTIME_LEVEL_MESSAGE_COUNT = 10u,
    MESSAGE_RUNTIME_LEVEL_MESSAGE_LENGTH = 160u,
    MESSAGE_RUNTIME_GLYPH_SPACING_BYTE_COUNT = 256u,
    /* c/message.h:MSG_DEDUPLICATION_PERIOD_MS. */
    MESSAGE_RUNTIME_DEDUPLICATION_PERIOD_MILLISECONDS = 2000u
};

typedef struct {
    const uint8_t *text;
    uint16_t length_and_tag;
} MessageRuntimeLine;

/*
 * c/message.c's small-screen Msg_Init/Msg_PushLine state. Game_Begin forces
 * Vid_FullScreen_b clear before this is initialized, so the current native
 * gameplay path preserves that source mode without choosing a GPU layout.
 */
typedef struct {
    MessageRuntimeLine lines[MESSAGE_RUNTIME_LINE_COUNT];
    const uint8_t *glyph_spacing;
    uint8_t fullscreen;
    uint8_t redraw_count;
    uint8_t lines_visible;
    uint8_t line_number;
    /* c/message.c:msg_Buffer's deduplication state, expressed in native monotonic ms. */
    const uint8_t *last_message;
    uint64_t next_duplicate_time_milliseconds;
} MessageRuntime;

/* c/message.c:Msg_Init, called after Res_LoadLevelData and before Game_Begin decodes the level. */
int message_runtime_init(MessageRuntime *runtime, uint8_t *level_bytes, size_t level_size,
                         const uint8_t *glyph_spacing, size_t glyph_spacing_size,
                         char *error, size_t error_size);

/* c/message.c:Msg_PushLine. `messages_enabled` is Prefs_ShowMessages_b. */
int message_runtime_push_line(MessageRuntime *runtime, const uint8_t *text,
                              uint16_t length_and_tag, uint8_t messages_enabled,
                              char *error, size_t error_size);

/* c/message.c:Msg_PushLineDedupLast with Sys_FrameTimeECV represented as monotonic ms. */
int message_runtime_push_line_dedup_last(MessageRuntime *runtime, const uint8_t *text,
                                         uint16_t length_and_tag, uint8_t messages_enabled,
                                         uint64_t current_time_milliseconds,
                                         char *error, size_t error_size);

/* c/message.c's small-screen render ordering, published as GPU-neutral HUD commands. */
int message_runtime_submit_hud(const MessageRuntime *runtime, SceneFrame *frame);
size_t message_runtime_visible_line_count(const MessageRuntime *runtime);

#endif
