#include "scene_frame.h"

#include <limits.h>
#include <stdlib.h>

int scene_frame_init(SceneFrame *frame, size_t command_capacity)
{
    if (!frame || command_capacity == 0) {
        return 0;
    }
    frame->commands = calloc(command_capacity, sizeof(*frame->commands));
    if (!frame->commands) {
        frame->count = 0;
        frame->capacity = 0;
        return 0;
    }
    frame->count = 0;
    frame->capacity = command_capacity;
    return 1;
}

void scene_frame_destroy(SceneFrame *frame)
{
    if (!frame) {
        return;
    }
    free(frame->commands);
    frame->commands = NULL;
    frame->count = 0;
    frame->capacity = 0;
}

void scene_frame_begin(SceneFrame *frame)
{
    if (frame) {
        frame->count = 0;
    }
}

int scene_frame_reserve(SceneFrame *frame, size_t command_capacity)
{
    SceneCommand *commands;

    if (!frame || command_capacity <= frame->capacity) {
        return frame != NULL;
    }
    if (command_capacity > SIZE_MAX / sizeof(*frame->commands)) {
        return 0;
    }
    commands = realloc(frame->commands, command_capacity * sizeof(*frame->commands));
    if (!commands) {
        return 0;
    }
    frame->commands = commands;
    frame->capacity = command_capacity;
    return 1;
}

int scene_frame_submit(SceneFrame *frame, const SceneCommand *command)
{
    if (!frame || !command || frame->count == frame->capacity) {
        return 0;
    }
    frame->commands[frame->count++] = *command;
    return 1;
}
