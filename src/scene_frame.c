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

static int scene_frame_reserve_owned_mesh_surfaces(SceneFrame *frame, size_t surface_capacity)
{
    SceneMeshSurface *surfaces;

    if (!frame || surface_capacity <= frame->owned_mesh_surface_capacity) {
        return frame != NULL;
    }
    if (surface_capacity > SIZE_MAX / sizeof(*frame->owned_mesh_surfaces)) {
        return 0;
    }
    surfaces = realloc(frame->owned_mesh_surfaces,
                       surface_capacity * sizeof(*frame->owned_mesh_surfaces));
    if (!surfaces) {
        return 0;
    }
    frame->owned_mesh_surfaces = surfaces;
    frame->owned_mesh_surface_capacity = surface_capacity;
    return 1;
}

static int scene_frame_clone_geometry_instance(SceneFrame *destination,
                                               SceneCommand *destination_command,
                                               const SceneCommand *source_command)
{
    const SceneGeometryInstance *source_instance;
    SceneGeometryInstance *destination_instance;
    size_t first_surface;

    if (!destination || !destination_command || !source_command ||
        source_command->type != SCENE_COMMAND_GEOMETRY_INSTANCE) {
        return 0;
    }
    source_instance = &source_command->data.geometry_instance;
    destination_instance = &destination_command->data.geometry_instance;
    if (source_instance->mesh.surface_count == 0u) {
        destination_instance->mesh.surfaces = NULL;
        return 1;
    }
    if (!source_instance->mesh.surfaces ||
        destination->owned_mesh_surface_count > SIZE_MAX -
            source_instance->mesh.surface_count) {
        return 0;
    }
    first_surface = destination->owned_mesh_surface_count;
    memcpy(destination->owned_mesh_surfaces + first_surface, source_instance->mesh.surfaces,
           (size_t)source_instance->mesh.surface_count * sizeof(*source_instance->mesh.surfaces));
    destination->owned_mesh_surface_count += source_instance->mesh.surface_count;
    destination_instance->mesh.surfaces = destination->owned_mesh_surfaces + first_surface;
    for (uint32_t surface_index = 0u;
         surface_index < destination_instance->mesh.surface_count; ++surface_index) {
        const SceneGeometry *source_geometry =
            &source_instance->mesh.surfaces[surface_index].geometry;
        SceneGeometry *destination_geometry =
            &((SceneMeshSurface *)destination_instance->mesh.surfaces)[surface_index].geometry;
        size_t first_vertex;

        if (source_geometry->vertex_count == 0u) {
            destination_geometry->vertices = NULL;
            continue;
        }
        if (!source_geometry->vertices ||
            destination->owned_vertex_count > SIZE_MAX - source_geometry->vertex_count) {
            return 0;
        }
        first_vertex = destination->owned_vertex_count;
        memcpy(destination->owned_vertices + first_vertex, source_geometry->vertices,
               (size_t)source_geometry->vertex_count * sizeof(*source_geometry->vertices));
        destination->owned_vertex_count += source_geometry->vertex_count;
        destination_geometry->vertices = destination->owned_vertices + first_vertex;
    }
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
    case SCENE_COMMAND_GEOMETRY_INSTANCE:
        if (previous->data.geometry_instance.source_instance_id !=
                current->data.geometry_instance.source_instance_id ||
            previous->data.geometry_instance.mesh.source_mesh_id !=
                current->data.geometry_instance.mesh.source_mesh_id ||
            previous->data.geometry_instance.mesh.surface_count !=
                current->data.geometry_instance.mesh.surface_count ||
            (current->data.geometry_instance.mesh.surface_count != 0u &&
             (!previous->data.geometry_instance.mesh.surfaces ||
              !current->data.geometry_instance.mesh.surfaces))) {
            return 0;
        }
        for (uint32_t surface_index = 0u;
             surface_index < current->data.geometry_instance.mesh.surface_count;
             ++surface_index) {
            const SceneGeometry *previous_geometry =
                &previous->data.geometry_instance.mesh.surfaces[surface_index].geometry;
            const SceneGeometry *current_geometry =
                &current->data.geometry_instance.mesh.surfaces[surface_index].geometry;

            if (previous_geometry->source_record_id != current_geometry->source_record_id ||
                previous_geometry->primitive != current_geometry->primitive ||
                previous_geometry->vertex_count != current_geometry->vertex_count) {
                return 0;
            }
        }
        return 1;
    case SCENE_COMMAND_SPRITE_INSTANCE:
        /*
         * ObjT/ShotT slots, not their selected drawable resource, are the
         * source TLAS identities.  modules/ai.s:ai_DoWalkAnim rewrites the
         * display graphics/frame fields as an alien changes facing or walks;
         * that must retain the preceding slot transform so presentation can
         * blend it, while the cloned current command remains authoritative
         * for the newly selected art.
         */
        return previous->data.sprite_instance.sprite.source_record_id ==
                   current->data.sprite_instance.sprite.source_record_id &&
               previous->data.sprite_instance.sprite.presentation ==
                   current->data.sprite_instance.sprite.presentation;
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
    if (current->type == SCENE_COMMAND_SPRITE_INSTANCE) {
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
    frame->owned_mesh_surfaces = NULL;
    frame->owned_mesh_surface_count = 0u;
    frame->owned_mesh_surface_capacity = 0u;
    return 1;
}

void scene_frame_destroy(SceneFrame *frame)
{
    if (!frame) {
        return;
    }
    free(frame->commands);
    free(frame->owned_vertices);
    free(frame->owned_mesh_surfaces);
    frame->commands = NULL;
    frame->count = 0;
    frame->capacity = 0;
    frame->owned_vertices = NULL;
    frame->owned_vertex_count = 0u;
    frame->owned_vertex_capacity = 0u;
    frame->owned_mesh_surfaces = NULL;
    frame->owned_mesh_surface_count = 0u;
    frame->owned_mesh_surface_capacity = 0u;
}

void scene_frame_begin(SceneFrame *frame)
{
    if (frame) {
        frame->count = 0;
        frame->owned_vertex_count = 0u;
        frame->owned_mesh_surface_count = 0u;
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

int scene_frame_reserve_mesh_surfaces(SceneFrame *frame, size_t surface_capacity)
{
    return scene_frame_reserve_owned_mesh_surfaces(frame, surface_capacity);
}

int scene_frame_submit(SceneFrame *frame, const SceneCommand *command)
{
    if (!frame || !command || frame->count == frame->capacity) {
        return 0;
    }
    frame->commands[frame->count++] = *command;
    return 1;
}

int scene_hud_text_set(SceneHudText *destination, const void *text, size_t text_byte_count)
{
    if (!destination || (text_byte_count != 0u && !text) ||
        text_byte_count > SCENE_HUD_TEXT_CAPACITY || text_byte_count > UINT16_MAX) {
        return 0;
    }
    memset(destination->text, 0, sizeof(destination->text));
    if (text_byte_count != 0u) {
        memcpy(destination->text, text, text_byte_count);
    }
    destination->text_byte_count = (uint16_t)text_byte_count;
    return 1;
}

SceneMeshSurface *scene_frame_allocate_mesh_surfaces(SceneFrame *frame,
                                                     uint32_t surface_count)
{
    size_t first_surface;

    if (!frame || surface_count == 0u ||
        frame->owned_mesh_surface_count > SIZE_MAX - surface_count ||
        frame->owned_mesh_surface_count + surface_count > frame->owned_mesh_surface_capacity) {
        return NULL;
    }
    first_surface = frame->owned_mesh_surface_count;
    frame->owned_mesh_surface_count += surface_count;
    memset(frame->owned_mesh_surfaces + first_surface, 0,
           (size_t)surface_count * sizeof(*frame->owned_mesh_surfaces));
    return frame->owned_mesh_surfaces + first_surface;
}

int scene_frame_clone(SceneFrame *destination, const SceneFrame *source)
{
    size_t source_vertex_count = 0u;
    size_t source_surface_count = 0u;

    if (!destination || !source || destination == source ||
        !scene_frame_reserve(destination, source->count)) {
        return 0;
    }
    /* Reserve once before assigning any command-owned vertex pointers. A
     * later realloc would otherwise invalidate earlier geometry commands. */
    for (size_t index = 0u; index < source->count; ++index) {
        const SceneCommand *command = &source->commands[index];

        if (command->type != SCENE_COMMAND_GEOMETRY_INSTANCE) {
            continue;
        }
        if (command->data.geometry_instance.mesh.surface_count != 0u &&
            !command->data.geometry_instance.mesh.surfaces) {
            return 0;
        }
        if (source_surface_count > SIZE_MAX -
            command->data.geometry_instance.mesh.surface_count) {
            return 0;
        }
        source_surface_count += command->data.geometry_instance.mesh.surface_count;
        for (uint32_t surface_index = 0u;
             surface_index < command->data.geometry_instance.mesh.surface_count;
             ++surface_index) {
            const SceneGeometry *geometry =
                &command->data.geometry_instance.mesh.surfaces[surface_index].geometry;

            if (geometry->vertex_count != 0u && !geometry->vertices) {
                return 0;
            }
            if (source_vertex_count > SIZE_MAX - geometry->vertex_count) {
                return 0;
            }
            source_vertex_count += geometry->vertex_count;
        }
    }
    if (!scene_frame_reserve_owned_vertices(destination, source_vertex_count) ||
        !scene_frame_reserve_owned_mesh_surfaces(destination, source_surface_count)) {
        return 0;
    }
    scene_frame_begin(destination);
    for (size_t index = 0u; index < source->count; ++index) {
        SceneCommand command = source->commands[index];

        if (!scene_frame_submit(destination, &command) ||
            (command.type == SCENE_COMMAND_GEOMETRY_INSTANCE &&
             !scene_frame_clone_geometry_instance(
                 destination, &destination->commands[destination->count - 1u],
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

        if (destination_command->type == SCENE_COMMAND_SPRITE_INSTANCE) {
            SceneSprite *destination_sprite = &destination_command->data.sprite_instance.sprite;

            /* Source-submitted frames never carry presentation state. */
            destination_sprite->presentation_previous_frame_index = 0u;
            destination_sprite->presentation_frame_interpolation_alpha = 0.0f;
            destination_sprite->presentation_interpolate_vector_frame = 0u;
        }
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
            if (previous_command->data.camera.has_source_position_16_16 != 0u &&
                current_command->data.camera.has_source_position_16_16 != 0u) {
                destination_command->data.camera.source_position_x_16_16 =
                    scene_frame_interpolate_i32(
                        previous_command->data.camera.source_position_x_16_16,
                        current_command->data.camera.source_position_x_16_16, alpha);
                destination_command->data.camera.source_position_z_16_16 =
                    scene_frame_interpolate_i32(
                        previous_command->data.camera.source_position_z_16_16,
                        current_command->data.camera.source_position_z_16_16, alpha);
            }
            destination_command->data.camera.yaw = scene_frame_interpolate_angle(
                previous_command->data.camera.yaw, current_command->data.camera.yaw, alpha);
            destination_command->data.camera.look_offset = scene_frame_interpolate_i16(
                previous_command->data.camera.look_offset,
                current_command->data.camera.look_offset, alpha);
        } else if (destination_command->type == SCENE_COMMAND_GEOMETRY_INSTANCE) {
            const SceneMesh *previous_mesh = &previous_command->data.geometry_instance.mesh;
            const SceneMesh *current_mesh = &current_command->data.geometry_instance.mesh;
            SceneMesh *destination_mesh = &destination_command->data.geometry_instance.mesh;

            for (uint32_t surface_index = 0u; surface_index < destination_mesh->surface_count;
                 ++surface_index) {
                const SceneGeometry *previous_geometry =
                    &previous_mesh->surfaces[surface_index].geometry;
                const SceneGeometry *current_geometry =
                    &current_mesh->surfaces[surface_index].geometry;
                SceneGeometry *destination_geometry =
                    &((SceneMeshSurface *)destination_mesh->surfaces)[surface_index].geometry;

                if (destination_geometry->vertex_count == 0u) {
                    continue;
                }
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
            }
        } else if (destination_command->type == SCENE_COMMAND_SPRITE_INSTANCE) {
            SceneSprite *destination_sprite = &destination_command->data.sprite_instance.sprite;
            const SceneSprite *previous_sprite =
                &previous_command->data.sprite_instance.sprite;
            const SceneSprite *current_sprite =
                &current_command->data.sprite_instance.sprite;

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
            destination_sprite->view_weapon_projection.y_offset = scene_frame_interpolate_i32(
                previous_sprite->view_weapon_projection.y_offset,
                current_sprite->view_weapon_projection.y_offset, alpha);
            destination_sprite->view_weapon_projection.sine = scene_frame_interpolate_i16(
                previous_sprite->view_weapon_projection.sine,
                current_sprite->view_weapon_projection.sine, alpha);
            destination_sprite->view_weapon_projection.cosine = scene_frame_interpolate_i16(
                previous_sprite->view_weapon_projection.cosine,
                current_sprite->view_weapon_projection.cosine, alpha);
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
            /*
             * Do not create source frames at host display rate.  The action
             * frame selected by ACTANIMOBJ remains the current endpoint; the
             * renderer receives the preceding frame only when both completed
             * snapshots describe the same vector model.  This is the
             * presentation counterpart for modules/ai.s:ai_DoWalkAnim and
             * ai_DoAttackAnim as well as the Player 1 companion: source ObjT
             * frame selection and all AI still advance only at 50 Hz.
             */
            if ((destination_sprite->presentation ==
                     SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON ||
                 destination_sprite->presentation ==
                     SCENE_SPRITE_PRESENTATION_WORLD_OBJECT) &&
                previous_sprite->source == SCENE_SPRITE_SOURCE_VECTOR_MODEL &&
                current_sprite->source == SCENE_SPRITE_SOURCE_VECTOR_MODEL &&
                previous_sprite->source_asset_id == current_sprite->source_asset_id &&
                previous_sprite->source_bytes == current_sprite->source_bytes &&
                previous_sprite->source_byte_count == current_sprite->source_byte_count &&
                previous_sprite->frame_index != current_sprite->frame_index) {
                destination_sprite->presentation_previous_frame_index =
                    previous_sprite->frame_index;
                destination_sprite->presentation_frame_interpolation_alpha = alpha;
                destination_sprite->presentation_interpolate_vector_frame = UINT8_MAX;
            }
        }
    }
    return 1;
}
