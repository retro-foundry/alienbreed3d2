#include "scene_frame.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int scene_frame_reserve_owned_vertices(SceneFrame *frame, size_t vertex_capacity)
{
    SceneVertex *vertices;

    if (!frame || vertex_capacity <= frame->owned_vertex_capacity) {
        return frame != NULL;
    }
    if (vertex_capacity > SIZE_MAX / sizeof(*frame->owned_vertices)) {
        return 0;
    }
    vertices = realloc(frame->owned_vertices, vertex_capacity * sizeof(*frame->owned_vertices));
    if (!vertices) {
        return 0;
    }
    frame->owned_vertices = vertices;
    frame->owned_vertex_capacity = vertex_capacity;
    return 1;
}

static int scene_frame_clone_geometry_vertices(SceneFrame *destination,
                                               SceneCommand *destination_command,
                                               const SceneCommand *source_command)
{
    const SceneGeometry *source_geometry;
    SceneGeometry *destination_geometry;
    size_t first_vertex;

    if (!destination || !destination_command || !source_command ||
        source_command->type != SCENE_COMMAND_GEOMETRY) {
        return 0;
    }
    source_geometry = &source_command->data.geometry;
    destination_geometry = &destination_command->data.geometry;
    if (source_geometry->vertex_count == 0u) {
        destination_geometry->vertices = NULL;
        return 1;
    }
    if (!source_geometry->vertices ||
        destination->owned_vertex_count > SIZE_MAX - source_geometry->vertex_count) {
        return 0;
    }
    first_vertex = destination->owned_vertex_count;
    if (!scene_frame_reserve_owned_vertices(destination,
                                            first_vertex + source_geometry->vertex_count)) {
        return 0;
    }
    memcpy(destination->owned_vertices + first_vertex, source_geometry->vertices,
           (size_t)source_geometry->vertex_count * sizeof(*source_geometry->vertices));
    destination->owned_vertex_count += source_geometry->vertex_count;
    destination_geometry->vertices = destination->owned_vertices + first_vertex;
    return 1;
}

static int32_t scene_frame_interpolate_i32(int32_t previous, int32_t current, float alpha)
{
    double value = (double)previous + ((double)current - (double)previous) * (double)alpha;

    if (value >= (double)INT32_MAX) {
        return INT32_MAX;
    }
    if (value <= (double)INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

static int16_t scene_frame_interpolate_i16(int16_t previous, int16_t current, float alpha)
{
    int32_t value = scene_frame_interpolate_i32(previous, current, alpha);

    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static int8_t scene_frame_interpolate_i8(int8_t previous, int8_t current, float alpha)
{
    int16_t value = scene_frame_interpolate_i16(previous, current, alpha);

    if (value > INT8_MAX) {
        return INT8_MAX;
    }
    if (value < INT8_MIN) {
        return INT8_MIN;
    }
    return (int8_t)value;
}

static uint16_t scene_frame_interpolate_angle(uint16_t previous, uint16_t current, float alpha)
{
    int32_t delta = (int32_t)((uint16_t)(current - previous) & UINT16_C(8190));

    if (delta > 4096) {
        delta -= 8192;
    }
    return (uint16_t)((previous + scene_frame_interpolate_i32(0, delta, alpha)) &
                      UINT16_C(8190));
}

static int scene_frame_commands_match(const SceneCommand *previous,
                                      const SceneCommand *current)
{
    if (!previous || !current || previous->type != current->type) {
        return 0;
    }
    switch (current->type) {
    case SCENE_COMMAND_CAMERA:
        return 1;
    case SCENE_COMMAND_GEOMETRY:
        return previous->data.geometry.source_record_id ==
                   current->data.geometry.source_record_id &&
               previous->data.geometry.primitive == current->data.geometry.primitive &&
               previous->data.geometry.vertex_count == current->data.geometry.vertex_count;
    case SCENE_COMMAND_SPRITE:
        return previous->data.sprite.source_record_id ==
                   current->data.sprite.source_record_id &&
               previous->data.sprite.presentation == current->data.sprite.presentation;
    default:
        return 0;
    }
}

static const SceneCommand *scene_frame_find_previous_command(const SceneFrame *previous,
                                                             const SceneCommand *current,
                                                             size_t current_index)
{
    if (!previous || !current) {
        return NULL;
    }
    if (current_index < previous->count &&
        scene_frame_commands_match(&previous->commands[current_index], current)) {
        return &previous->commands[current_index];
    }
    if (current->type == SCENE_COMMAND_SPRITE) {
        for (size_t index = 0u; index < previous->count; ++index) {
            if (scene_frame_commands_match(&previous->commands[index], current)) {
                return &previous->commands[index];
            }
        }
    }
    return NULL;
}

static void scene_frame_interpolate_vertex(SceneVertex *destination,
                                           const SceneVertex *previous,
                                           const SceneVertex *current, float alpha)
{
    destination->position.x = scene_frame_interpolate_i32(previous->position.x,
                                                           current->position.x, alpha);
    destination->position.y = scene_frame_interpolate_i32(previous->position.y,
                                                           current->position.y, alpha);
    destination->position.z = scene_frame_interpolate_i32(previous->position.z,
                                                           current->position.z, alpha);
    destination->texture_u = scene_frame_interpolate_i32(previous->texture_u,
                                                          current->texture_u, alpha);
    destination->texture_v = scene_frame_interpolate_i32(previous->texture_v,
                                                          current->texture_v, alpha);
    destination->source_light_level = scene_frame_interpolate_i16(
        previous->source_light_level, current->source_light_level, alpha);
}

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
    frame->owned_vertices = NULL;
    frame->owned_vertex_count = 0u;
    frame->owned_vertex_capacity = 0u;
    return 1;
}

void scene_frame_destroy(SceneFrame *frame)
{
    if (!frame) {
        return;
    }
    free(frame->commands);
    free(frame->owned_vertices);
    frame->commands = NULL;
    frame->count = 0;
    frame->capacity = 0;
    frame->owned_vertices = NULL;
    frame->owned_vertex_count = 0u;
    frame->owned_vertex_capacity = 0u;
}

void scene_frame_begin(SceneFrame *frame)
{
    if (frame) {
        frame->count = 0;
        frame->owned_vertex_count = 0u;
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

int scene_frame_clone(SceneFrame *destination, const SceneFrame *source)
{
    size_t source_vertex_count = 0u;

    if (!destination || !source || destination == source ||
        !scene_frame_reserve(destination, source->count)) {
        return 0;
    }
    /* Reserve once before assigning any command-owned vertex pointers. A
     * later realloc would otherwise invalidate earlier geometry commands. */
    for (size_t index = 0u; index < source->count; ++index) {
        const SceneCommand *command = &source->commands[index];

        if (command->type != SCENE_COMMAND_GEOMETRY) {
            continue;
        }
        if (command->data.geometry.vertex_count != 0u &&
            !command->data.geometry.vertices) {
            return 0;
        }
        if (source_vertex_count > SIZE_MAX - command->data.geometry.vertex_count) {
            return 0;
        }
        source_vertex_count += command->data.geometry.vertex_count;
    }
    if (!scene_frame_reserve_owned_vertices(destination, source_vertex_count)) {
        return 0;
    }
    scene_frame_begin(destination);
    for (size_t index = 0u; index < source->count; ++index) {
        SceneCommand command = source->commands[index];

        if (!scene_frame_submit(destination, &command) ||
            (command.type == SCENE_COMMAND_GEOMETRY &&
             !scene_frame_clone_geometry_vertices(destination,
                                                 &destination->commands[destination->count - 1u],
                                                 &source->commands[index]))) {
            scene_frame_begin(destination);
            return 0;
        }
    }
    return 1;
}

int scene_frame_interpolate(SceneFrame *destination, const SceneFrame *previous,
                            const SceneFrame *current, float alpha)
{
    if (!destination || !previous || !current || destination == previous ||
        destination == current || !scene_frame_clone(destination, current)) {
        return 0;
    }
    if (alpha < 0.0f) {
        alpha = 0.0f;
    } else if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    for (size_t index = 0u; index < destination->count; ++index) {
        SceneCommand *destination_command = &destination->commands[index];
        const SceneCommand *current_command = &current->commands[index];
        const SceneCommand *previous_command = scene_frame_find_previous_command(
            previous, current_command, index);

        if (!previous_command) {
            continue;
        }
        if (destination_command->type == SCENE_COMMAND_CAMERA) {
            destination_command->data.camera.position.x = scene_frame_interpolate_i32(
                previous_command->data.camera.position.x,
                current_command->data.camera.position.x, alpha);
            destination_command->data.camera.position.y = scene_frame_interpolate_i32(
                previous_command->data.camera.position.y,
                current_command->data.camera.position.y, alpha);
            destination_command->data.camera.position.z = scene_frame_interpolate_i32(
                previous_command->data.camera.position.z,
                current_command->data.camera.position.z, alpha);
            destination_command->data.camera.yaw = scene_frame_interpolate_angle(
                previous_command->data.camera.yaw, current_command->data.camera.yaw, alpha);
            destination_command->data.camera.look_offset = scene_frame_interpolate_i16(
                previous_command->data.camera.look_offset,
                current_command->data.camera.look_offset, alpha);
        } else if (destination_command->type == SCENE_COMMAND_GEOMETRY) {
            const SceneGeometry *previous_geometry = &previous_command->data.geometry;
            const SceneGeometry *current_geometry = &current_command->data.geometry;
            SceneGeometry *destination_geometry = &destination_command->data.geometry;

            if (!previous_geometry->vertices || !current_geometry->vertices ||
                !destination_geometry->vertices) {
                return 0;
            }
            for (uint32_t vertex_index = 0u;
                 vertex_index < destination_geometry->vertex_count; ++vertex_index) {
                scene_frame_interpolate_vertex(
                    (SceneVertex *)&destination_geometry->vertices[vertex_index],
                    &previous_geometry->vertices[vertex_index],
                    &current_geometry->vertices[vertex_index], alpha);
            }
        } else if (destination_command->type == SCENE_COMMAND_SPRITE) {
            SceneSprite *destination_sprite = &destination_command->data.sprite;
            const SceneSprite *previous_sprite = &previous_command->data.sprite;
            const SceneSprite *current_sprite = &current_command->data.sprite;

            destination_sprite->position.x = scene_frame_interpolate_i32(
                previous_sprite->position.x, current_sprite->position.x, alpha);
            destination_sprite->position.y = scene_frame_interpolate_i32(
                previous_sprite->position.y, current_sprite->position.y, alpha);
            destination_sprite->position.z = scene_frame_interpolate_i32(
                previous_sprite->position.z, current_sprite->position.z, alpha);
            destination_sprite->yaw = scene_frame_interpolate_angle(
                previous_sprite->yaw, current_sprite->yaw, alpha);
            destination_sprite->source_brightness = (uint16_t)scene_frame_interpolate_i32(
                previous_sprite->source_brightness, current_sprite->source_brightness, alpha);
            destination_sprite->source_light_level = scene_frame_interpolate_i16(
                previous_sprite->source_light_level, current_sprite->source_light_level, alpha);
            destination_sprite->source_clip_top_y = scene_frame_interpolate_i32(
                previous_sprite->source_clip_top_y, current_sprite->source_clip_top_y, alpha);
            destination_sprite->source_clip_bottom_y = scene_frame_interpolate_i32(
                previous_sprite->source_clip_bottom_y, current_sprite->source_clip_bottom_y,
                alpha);
            for (size_t brightness_index = 0u;
                 brightness_index < sizeof(destination_sprite->source_bitmap_angle_brightness);
                 ++brightness_index) {
                destination_sprite->source_bitmap_angle_brightness[brightness_index] =
                    scene_frame_interpolate_i8(
                        previous_sprite->source_bitmap_angle_brightness[brightness_index],
                        current_sprite->source_bitmap_angle_brightness[brightness_index], alpha);
            }
            for (size_t brightness_index = 0u;
                 brightness_index < sizeof(destination_sprite->source_point_and_polygon_brightness);
                 ++brightness_index) {
                destination_sprite->source_point_and_polygon_brightness[brightness_index] =
                    scene_frame_interpolate_i8(
                        previous_sprite->source_point_and_polygon_brightness[brightness_index],
                        current_sprite->source_point_and_polygon_brightness[brightness_index],
                        alpha);
            }
        }
    }
    return 1;
}
