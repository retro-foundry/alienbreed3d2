#include "source_vector_model_scene.h"

#include "source_vector_projection.h"
#include "source_vector_model_transform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t point_count;
    size_t lines_offset;
    size_t frame_offset;
    size_t polygon_angle_offset;
    size_t point_data_offset;
} SourceVectorSceneFrame;

typedef struct {
    uint32_t source_part_index;
    int16_t relative_offset;
    int16_t sort_point_offset;
    int32_t sort_key;
    uint8_t active;
} SourceVectorScenePart;

typedef enum {
    SOURCE_VECTOR_SCENE_WORLD,
    SOURCE_VECTOR_SCENE_WORLD_RAY_TRACED,
    SOURCE_VECTOR_SCENE_VIEW_PROJECTED,
    SOURCE_VECTOR_SCENE_VIEW_CAMERA
} SourceVectorSceneSpace;

static void source_vector_scene_set_error(char *error, size_t error_size,
                                          const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t source_vector_scene_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8u) | source[1]);
}

static int16_t source_vector_scene_read_be16s(const uint8_t *source)
{
    return (int16_t)source_vector_scene_read_be16(source);
}

static uint32_t source_vector_scene_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24u) |
           ((uint32_t)source[1] << 16u) |
           ((uint32_t)source[2] << 8u) | source[3];
}

static int source_vector_scene_frame(const uint8_t *bytes, size_t size,
                                     uint16_t frame_index,
                                     SourceVectorSceneFrame *out_frame,
                                     char *error, size_t error_size)
{
    const size_t start_offset = 2u;
    const size_t pointer_table_offset = 6u;
    uint16_t frame_count;
    size_t frame_pointer_offset;

    if (!bytes || !out_frame || size < pointer_table_offset) {
        source_vector_scene_set_error(
            error, error_size, "source vector sprite descriptor is invalid");
        return 0;
    }
    out_frame->point_count = source_vector_scene_read_be16(bytes + 2u);
    frame_count = source_vector_scene_read_be16(bytes + 4u);
    if (out_frame->point_count == 0u || frame_count == 0u ||
        frame_index >= frame_count ||
        (size_t)frame_count > (size - pointer_table_offset) / 4u) {
        source_vector_scene_set_error(
            error, error_size,
            "source vector model header or frame is invalid");
        return 0;
    }
    out_frame->lines_offset = pointer_table_offset + (size_t)frame_count * 4u;
    frame_pointer_offset = pointer_table_offset + (size_t)frame_index * 4u;
    out_frame->frame_offset = start_offset +
        source_vector_scene_read_be16(bytes + frame_pointer_offset);
    out_frame->polygon_angle_offset = start_offset +
        source_vector_scene_read_be16(bytes + frame_pointer_offset + 2u);
    if (out_frame->frame_offset > size ||
        4u > size - out_frame->frame_offset ||
        out_frame->polygon_angle_offset >= size) {
        source_vector_scene_set_error(
            error, error_size,
            "source vector model frame data is outside the asset");
        return 0;
    }
    out_frame->point_data_offset = out_frame->frame_offset + 4u +
        (size_t)out_frame->point_count + (out_frame->point_count & 1u);
    if (out_frame->point_data_offset > size ||
        (size_t)out_frame->point_count >
            (size - out_frame->point_data_offset) / 6u) {
        source_vector_scene_set_error(
            error, error_size,
            "source vector model point table is malformed");
        return 0;
    }
    return 1;
}

static int source_vector_scene_display_color(const SceneSprite *sprite,
                                             uint8_t color_index,
                                             uint8_t out_color[4])
{
    size_t offset = (size_t)color_index * 6u;

    if (!sprite->source_display_palette_bytes ||
        offset > sprite->source_display_palette_byte_count ||
        6u > sprite->source_display_palette_byte_count - offset) {
        return 0;
    }
    out_color[0] = sprite->source_display_palette_bytes[offset + 1u];
    out_color[1] = sprite->source_display_palette_bytes[offset + 3u];
    out_color[2] = sprite->source_display_palette_bytes[offset + 5u];
    out_color[3] = UINT8_MAX;
    return 1;
}

static int source_vector_scene_model_point(
    const SceneSprite *sprite, const uint8_t *point_bytes,
    const uint8_t *previous_point_bytes, float interpolation_alpha,
    SourceVectorEyePoint *out_point)
{
    int16_t current_x, current_y, current_z;

    if (!sprite || !point_bytes || !out_point) return 0;
    current_x = source_vector_scene_read_be16s(point_bytes);
    current_y = source_vector_scene_read_be16s(point_bytes + 2u);
    current_z = source_vector_scene_read_be16s(point_bytes + 4u);
    if (!previous_point_bytes || interpolation_alpha <= 0.0f ||
        interpolation_alpha >= 1.0f) {
        if (previous_point_bytes && interpolation_alpha <= 0.0f) {
            current_x = source_vector_scene_read_be16s(previous_point_bytes);
            current_y = source_vector_scene_read_be16s(previous_point_bytes + 2u);
            current_z = source_vector_scene_read_be16s(previous_point_bytes + 4u);
        }
        return source_vector_transform_view_weapon_point(
            &sprite->view_weapon_projection,
            current_x, current_y, current_z, out_point);
    }
    return source_vector_transform_view_weapon_interpolated_point(
        &sprite->view_weapon_projection,
        (float)source_vector_scene_read_be16s(previous_point_bytes) +
            ((float)current_x -
             (float)source_vector_scene_read_be16s(previous_point_bytes)) *
                interpolation_alpha,
        (float)source_vector_scene_read_be16s(previous_point_bytes + 2u) +
            ((float)current_y -
             (float)source_vector_scene_read_be16s(previous_point_bytes + 2u)) *
                interpolation_alpha,
        (float)source_vector_scene_read_be16s(previous_point_bytes + 4u) +
            ((float)current_z -
             (float)source_vector_scene_read_be16s(previous_point_bytes + 4u)) *
                interpolation_alpha,
        out_point);
}

static void source_vector_scene_interpolated_source_point(
    const uint8_t *point_bytes, const uint8_t *previous_point_bytes,
    float interpolation_alpha, float *out_x, float *out_y, float *out_z)
{
    float x = (float)source_vector_scene_read_be16s(point_bytes);
    float y = (float)source_vector_scene_read_be16s(point_bytes + 2u);
    float z = (float)source_vector_scene_read_be16s(point_bytes + 4u);

    if (previous_point_bytes && interpolation_alpha < 1.0f) {
        float previous_x = (float)source_vector_scene_read_be16s(previous_point_bytes);
        float previous_y = (float)source_vector_scene_read_be16s(previous_point_bytes + 2u);
        float previous_z = (float)source_vector_scene_read_be16s(previous_point_bytes + 4u);
        x = previous_x + (x - previous_x) * interpolation_alpha;
        y = previous_y + (y - previous_y) * interpolation_alpha;
        z = previous_z + (z - previous_z) * interpolation_alpha;
    }
    *out_x = x;
    *out_y = y;
    *out_z = z;
}

static void source_vector_scene_world_point(
    const SceneSprite *sprite, const uint8_t *point_bytes,
    const uint8_t *previous_point_bytes, float interpolation_alpha,
    SourceVectorSceneVertex *out_vertex)
{
    SourceVectorModelWorldOffset local;
    float source_x, source_y, source_z;

    source_vector_scene_interpolated_source_point(
        point_bytes, previous_point_bytes, interpolation_alpha,
        &source_x, &source_y, &source_z);
    source_vector_model_world_offset(
        source_x, source_y, source_z, sprite->yaw, &local);
    out_vertex->x = (float)(int16_t)(uint16_t)sprite->position.x + local.x;
    out_vertex->y = -(float)sprite->position.y / 128.0f + local.y;
    out_vertex->z = (float)(int16_t)(uint16_t)sprite->position.z + local.z;
}

static int source_vector_scene_project_world(
    const SourceVectorSceneVertex *vertex, const SceneCamera *camera,
    const RenderView *view, float aspect, float *out_x, float *out_y)
{
    const float pi = 3.14159265358979323846f;
    const float source_depth_scale =
        4.0f * (32767.0f / 65536.0f) * (85.0f / 256.0f) * (927.0f / 1024.0f);
    float yaw = (float)camera->yaw * (2.0f * pi / 8192.0f);
    float pitch = view->pitch_degrees * (pi / 180.0f);
    float cos_pitch = cosf(pitch);
    float forward_x = sinf(yaw) * cos_pitch;
    float forward_y = sinf(pitch);
    float forward_z = cosf(yaw) * cos_pitch;
    float right_x = cosf(yaw);
    float right_z = -sinf(yaw);
    float up_x = right_z * forward_y;
    float up_y = forward_z * right_x - forward_x * right_z;
    float up_z = -right_x * forward_y;
    float camera_x = camera->has_source_position_16_16 ?
        (float)((double)camera->source_position_x_16_16 / 65536.0) :
        (float)(int16_t)(uint16_t)camera->position.x;
    float camera_y = -(float)camera->position.y / 128.0f;
    float camera_z = camera->has_source_position_16_16 ?
        (float)((double)camera->source_position_z_16_16 / 65536.0) :
        (float)(int16_t)(uint16_t)camera->position.z;
    float relative_x = vertex->x - camera_x;
    float relative_y = vertex->y - camera_y;
    float relative_z = vertex->z - camera_z;
    float depth = relative_x * forward_x + relative_y * forward_y +
        relative_z * forward_z;
    float focal = 16.0f / (15.0f * source_depth_scale);

    if (depth <= 0.05f || aspect <= 0.0f) return 0;
    *out_x = (relative_x * right_x + relative_z * right_z) * focal /
        (depth * aspect);
    *out_y = (relative_x * up_x + relative_y * up_y + relative_z * up_z) *
        focal / depth;
    return 1;
}

static SourceVectorSceneVertex source_vector_scene_interpolate_vertex(
    const SourceVectorSceneVertex *a, const SourceVectorSceneVertex *b,
    float fraction)
{
    SourceVectorSceneVertex result;
#define INTERPOLATE(FIELD) result.FIELD = a->FIELD + (b->FIELD - a->FIELD) * fraction
    INTERPOLATE(x); INTERPOLATE(y); INTERPOLATE(z);
    INTERPOLATE(u); INTERPOLATE(v); INTERPOLATE(source_light);
#undef INTERPOLATE
    return result;
}

static uint32_t source_vector_scene_clip_polygon_y(
    const SourceVectorSceneVertex *input, uint32_t input_count,
    float plane_y, int keep_below, SourceVectorSceneVertex *output)
{
    uint32_t output_count = 0u;

    for (uint32_t index = 0u; index < input_count; ++index) {
        const SourceVectorSceneVertex *a = &input[index];
        const SourceVectorSceneVertex *b = &input[(index + 1u) % input_count];
        float a_distance = a->y - plane_y;
        float b_distance = b->y - plane_y;
        int a_inside = keep_below ? a_distance <= 0.0f : a_distance >= 0.0f;
        int b_inside = keep_below ? b_distance <= 0.0f : b_distance >= 0.0f;

        if (a_inside) output[output_count++] = *a;
        if (a_inside != b_inside) {
            output[output_count++] = source_vector_scene_interpolate_vertex(
                a, b, a_distance / (a_distance - b_distance));
        }
    }
    return output_count;
}

static uint32_t source_vector_scene_clip_triangle_to_sector(
    const SourceVectorSceneVertex triangle[3], float top_y, float bottom_y,
    SourceVectorSceneVertex output[5])
{
    SourceVectorSceneVertex intermediate[5];
    uint32_t count = source_vector_scene_clip_polygon_y(
        triangle, 3u, top_y, 1, intermediate);
    return count < 3u ? 0u : source_vector_scene_clip_polygon_y(
        intermediate, count, bottom_y, 0, output);
}

static int32_t source_vector_scene_asr32(int32_t value, unsigned shift)
{
    if (value >= 0) return value >> shift;
    return (int32_t)-((-(int64_t)value + (((int64_t)1 << shift) - 1)) >> shift);
}

static int32_t source_vector_scene_square_word(int32_t value)
{
    int32_t word = (int16_t)(uint16_t)value;
    return (int32_t)((uint32_t)(word * word));
}

static int source_vector_scene_compare_parts(const void *left,
                                             const void *right)
{
    const SourceVectorScenePart *a = left;
    const SourceVectorScenePart *b = right;
    if (a->sort_key > b->sort_key) return -1;
    if (a->sort_key < b->sort_key) return 1;
    if (a->source_part_index > b->source_part_index) return -1;
    if (a->source_part_index < b->source_part_index) return 1;
    return 0;
}

static int source_vector_scene_sort_parts(
    const SceneSprite *sprite, const uint8_t *bytes, size_t size,
    const SourceVectorSceneFrame *frame, const uint8_t *previous_points,
    SourceVectorScenePart *parts, uint32_t part_count,
    char *error, size_t error_size)
{
    for (uint32_t index = 0u; index < part_count; ++index) {
        size_t point_index;
        SourceVectorEyePoint point;
        int32_t x, y, z;

        if (parts[index].sort_point_offset < 0 ||
            parts[index].sort_point_offset % 10 != 0) {
            source_vector_scene_set_error(
                error, error_size,
                "source view weapon part has an invalid sort point");
            return 0;
        }
        point_index = (size_t)parts[index].sort_point_offset / 10u;
        if (point_index >= frame->point_count ||
            frame->point_data_offset > size ||
            !source_vector_scene_model_point(
                sprite,
                bytes + frame->point_data_offset + point_index * 6u,
                previous_points ? previous_points + point_index * 6u : NULL,
                sprite->presentation_frame_interpolation_alpha, &point)) {
            source_vector_scene_set_error(
                error, error_size,
                "source view weapon part sort point is outside the model");
            return 0;
        }
        x = source_vector_scene_asr32((int32_t)point.x, 7u);
        y = source_vector_scene_asr32((int32_t)-point.y, 7u);
        z = (int32_t)-point.z;
        parts[index].sort_key = (int32_t)(
            (uint32_t)source_vector_scene_square_word(x) +
            (uint32_t)source_vector_scene_square_word(y) +
            (uint32_t)source_vector_scene_square_word(z));
    }
    qsort(parts, part_count, sizeof(*parts), source_vector_scene_compare_parts);
    return 1;
}

static int source_vector_scene_map_offset(const SceneSprite *sprite,
                                          const uint8_t *face_bytes,
                                          size_t *out_offset,
                                          char *error, size_t error_size)
{
    int16_t source_word = source_vector_scene_read_be16s(face_bytes);
    size_t offset = source_word < 0 ?
        65536u + ((uint16_t)source_word & 0x7fffu) : (uint16_t)source_word;

    if (!sprite->source_palette_bytes ||
        offset >= sprite->source_palette_byte_count) {
        source_vector_scene_set_error(
            error, error_size, "source vector face has no texture map");
        return 0;
    }
    *out_offset = offset;
    return 1;
}

static float source_vector_scene_flat_light(
    const SceneSprite *sprite, const uint8_t *polygon_angles,
    size_t polygon_angle_count, const uint8_t *face_bytes,
    char *error, size_t error_size)
{
    uint8_t polygon_angle;
    uint8_t light_index;
    float shade;

    if (face_bytes[3u] >= polygon_angle_count) {
        source_vector_scene_set_error(
            error, error_size,
            "source vector face has no polygon light angle");
        return -1.0f;
    }
    shade = 31.0f -
        (float)face_bytes[2u] * (32.0f * 41.0f / 4096.0f);
    polygon_angle = polygon_angles[face_bytes[3u]];
    light_index = (uint8_t)((polygon_angle & 0xf0u) |
        (((uint8_t)(polygon_angle + (sprite->yaw >> 9u))) & 0x0fu));
    shade += (float)(uint8_t)
        sprite->source_point_and_polygon_brightness[light_index];
    if (shade < 0.0f) shade = 0.0f;
    else if (shade > 31.0f) shade = 31.0f;
    return 1.0f - shade / 31.0f;
}

static int source_vector_scene_point_light(
    const SceneSprite *sprite, const uint8_t *point_angles,
    size_t point_angle_count, uint16_t point_index, float *out_light,
    char *error, size_t error_size)
{
    uint8_t point_angle, light_index;
    int16_t shade;

    if (point_index >= point_angle_count) {
        source_vector_scene_set_error(
            error, error_size,
            "source Gouraud vector point has no directional light entry");
        return 0;
    }
    point_angle = point_angles[point_index];
    light_index = (uint8_t)((point_angle & 0xf0u) |
        (((uint8_t)(point_angle + (sprite->yaw >> 9u))) & 0x0fu));
    shade = sprite->source_point_and_polygon_brightness[light_index];
    if (shade < 0) shade = 0;
    else if (shade > 31) shade = 31;
    *out_light = 1.0f - (float)shade / 31.0f;
    return 1;
}

static int source_vector_scene_decode_material(
    const SceneSprite *sprite, size_t source_map_offset,
    uint8_t minimum_u, uint8_t maximum_u,
    uint8_t minimum_v, uint8_t maximum_v, int glare,
    SourceVectorSceneMaterial *out_material,
    char *error, size_t error_size)
{
    enum {
        LIGHT_BASE_ROW = 32u,
        LIGHT_ROW_WIDTH = 256u,
        TEXEL_STRIDE = 4u
    };
    SourceVectorSceneMaterial material = {0};

    material.width = (uint16_t)maximum_u - minimum_u + 1u;
    material.height = (uint16_t)maximum_v - minimum_v + 1u;
    material.source_map_offset = (uint32_t)source_map_offset;
    material.minimum_u = minimum_u;
    material.maximum_u = maximum_u;
    material.minimum_v = minimum_v;
    material.maximum_v = maximum_v;
    material.glare = (uint8_t)(glare != 0);
    if (!sprite->source_palette_bytes || !sprite->source_light_palette_bytes ||
        !sprite->source_display_palette_bytes ||
        source_map_offset >= sprite->source_palette_byte_count ||
        (size_t)material.width > SIZE_MAX / material.height / 4u ||
        sprite->source_light_palette_byte_count <
            (size_t)(LIGHT_BASE_ROW + 1u) * LIGHT_ROW_WIDTH) {
        source_vector_scene_set_error(
            error, error_size,
            "source vector texture descriptor is invalid");
        return 0;
    }
    material.rgba = malloc((size_t)material.width * material.height * 4u);
    if (!material.rgba) {
        source_vector_scene_set_error(
            error, error_size,
            "source vector texture conversion allocation failed");
        return 0;
    }
    for (uint16_t y = 0u; y < material.height; ++y) {
        for (uint16_t x = 0u; x < material.width; ++x) {
            uint8_t source_u = (uint8_t)(minimum_u + x);
            uint8_t source_v = (uint8_t)(minimum_v + y);
            int16_t coordinate = source_vector_texture_coordinate(
                source_u, source_v);
            int64_t signed_offset = (int64_t)source_map_offset +
                (int64_t)coordinate * TEXEL_STRIDE;
            size_t texel_offset;
            size_t palette_offset;
            uint8_t texel;
            uint8_t *destination = material.rgba +
                ((size_t)y * material.width + x) * 4u;

            if (signed_offset < 0 || (uint64_t)signed_offset > SIZE_MAX ||
                (texel_offset = (size_t)signed_offset) >=
                    sprite->source_palette_byte_count) {
                free(material.rgba);
                if (error && error_size > 0u) {
                    (void)snprintf(
                        error, error_size,
                        "source vector map %zu coordinate %d is outside its %zu-byte asset",
                        source_map_offset, (int)coordinate,
                        sprite->source_palette_byte_count);
                }
                return 0;
            }
            texel = sprite->source_palette_bytes[texel_offset];
            if (glare && texel == 0u) {
                memset(destination, 0, 4u);
                continue;
            }
            if (glare && texel <= 32u) {
                palette_offset = (size_t)(texel - 1u) * 512u;
                if (256u > sprite->source_light_palette_byte_count ||
                    palette_offset >
                        sprite->source_light_palette_byte_count - 256u) {
                    free(material.rgba);
                    source_vector_scene_set_error(
                        error, error_size,
                        "source vector glare palette is outside its asset");
                    return 0;
                }
            } else {
                palette_offset = (size_t)LIGHT_BASE_ROW * LIGHT_ROW_WIDTH +
                    texel;
            }
            if (palette_offset >= sprite->source_light_palette_byte_count ||
                !source_vector_scene_display_color(
                    sprite, sprite->source_light_palette_bytes[palette_offset],
                    destination)) {
                free(material.rgba);
                source_vector_scene_set_error(
                    error, error_size,
                    "source vector palette references an invalid display colour");
                return 0;
            }
        }
    }
    *out_material = material;
    return 1;
}

static int source_vector_scene_append_material(
    SourceVectorSceneMesh *mesh, SourceVectorSceneMaterial *material,
    uint32_t *out_index, char *error, size_t error_size)
{
    SourceVectorSceneMaterial *grown;

    if (mesh->material_count >= UINT32_MAX ||
        mesh->material_count == SIZE_MAX / sizeof(*grown)) {
        source_vector_scene_set_error(
            error, error_size, "source vector material count is too large");
        return 0;
    }
    grown = realloc(mesh->materials,
                    (mesh->material_count + 1u) * sizeof(*grown));
    if (!grown) {
        source_vector_scene_set_error(
            error, error_size, "source vector material allocation failed");
        return 0;
    }
    mesh->materials = grown;
    *out_index = (uint32_t)mesh->material_count;
    mesh->materials[mesh->material_count++] = *material;
    memset(material, 0, sizeof(*material));
    return 1;
}

static int source_vector_scene_append_triangle(
    SourceVectorSceneMesh *mesh, const SourceVectorSceneTriangle *triangle,
    char *error, size_t error_size)
{
    SourceVectorSceneTriangle *grown;

    if (mesh->triangle_count == SIZE_MAX / sizeof(*grown)) {
        source_vector_scene_set_error(
            error, error_size, "source vector triangle count is too large");
        return 0;
    }
    grown = realloc(mesh->triangles,
                    (mesh->triangle_count + 1u) * sizeof(*grown));
    if (!grown) {
        source_vector_scene_set_error(
            error, error_size, "source vector triangle allocation failed");
        return 0;
    }
    mesh->triangles = grown;
    mesh->triangles[mesh->triangle_count++] = *triangle;
    return 1;
}

void source_vector_scene_mesh_destroy(SourceVectorSceneMesh *mesh)
{
    if (!mesh) return;
    for (size_t index = 0u; index < mesh->material_count; ++index) {
        free(mesh->materials[index].rgba);
    }
    free(mesh->materials);
    free(mesh->triangles);
    memset(mesh, 0, sizeof(*mesh));
}

static int source_vector_scene_compile(
    const SceneSprite *sprite, const SceneCamera *camera,
    const RenderView *view, float drawable_aspect, SourceVectorSceneSpace space,
    SourceVectorSceneMesh *out_mesh, char *error, size_t error_size)
{
    SourceVectorSceneMesh mesh = {0};
    SourceVectorSceneFrame frame, previous_frame;
    const uint8_t *bytes;
    const uint8_t *previous_points = NULL;
    size_t size;
    float projection[16];
    SourceVectorScenePart parts[32] = {{0}};
    uint32_t part_count = 0u;
    uint32_t on_off;
    const int world = space == SOURCE_VECTOR_SCENE_WORLD ||
        space == SOURCE_VECTOR_SCENE_WORLD_RAY_TRACED;
    const int view_weapon = !world;
    const int projected_view_weapon =
        space == SOURCE_VECTOR_SCENE_VIEW_PROJECTED;
    const int stable_view_weapon =
        space == SOURCE_VECTOR_SCENE_VIEW_CAMERA;
    const int stable_geometry = stable_view_weapon ||
        space == SOURCE_VECTOR_SCENE_WORLD_RAY_TRACED;

    if (!sprite || !out_mesh ||
        (!stable_geometry && drawable_aspect <= 0.0f) ||
        sprite->source != SCENE_SPRITE_SOURCE_VECTOR_MODEL ||
        (view_weapon ?
            sprite->presentation != SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON :
            sprite->presentation != SCENE_SPRITE_PRESENTATION_WORLD_OBJECT) ||
        (space == SOURCE_VECTOR_SCENE_WORLD && (!camera || !view)) ||
        !sprite->source_bytes || sprite->source_byte_count < 6u ||
        (projected_view_weapon && !source_vector_make_view_weapon_matrix(
            &sprite->view_weapon_projection, drawable_aspect, projection))) {
        source_vector_scene_set_error(
            error, error_size,
            "source view weapon descriptor or projection is invalid");
        return 0;
    }
    bytes = sprite->source_bytes;
    size = sprite->source_byte_count;
    if (!source_vector_scene_frame(
            bytes, size, sprite->frame_index, &frame,
            error, error_size)) {
        return 0;
    }
    if (sprite->presentation_interpolate_vector_frame != 0u) {
        if (sprite->presentation_frame_interpolation_alpha < 0.0f ||
            sprite->presentation_frame_interpolation_alpha > 1.0f ||
            !source_vector_scene_frame(
                bytes, size, sprite->presentation_previous_frame_index,
                &previous_frame, error, error_size) ||
            previous_frame.point_count != frame.point_count) {
            source_vector_scene_set_error(
                error, error_size,
                "source vector interpolation endpoints are incompatible");
            return 0;
        }
        previous_points = bytes + previous_frame.point_data_offset;
    }
    on_off = source_vector_scene_read_be32(bytes + frame.frame_offset);
    for (uint32_t part_index = 0u; ; ++part_index) {
        size_t list_offset = frame.lines_offset + (size_t)part_index * 4u;
        int16_t relative;

        if (list_offset > size || 4u > size - list_offset) {
            source_vector_scene_set_error(
                error, error_size,
                "source vector model has no part-list terminator");
            goto fail;
        }
        relative = source_vector_scene_read_be16s(bytes + list_offset);
        if (relative < 0) break;
        if (part_index >= 32u) {
            source_vector_scene_set_error(
                error, error_size,
                "source vector model has more than 32 parts");
            goto fail;
        }
        if ((on_off & (UINT32_C(1) << part_index)) != 0u ||
            stable_geometry) {
            parts[part_count].source_part_index = part_index;
            parts[part_count].relative_offset = relative;
            parts[part_count].sort_point_offset =
                source_vector_scene_read_be16s(bytes + list_offset + 2u);
            parts[part_count].active = (uint8_t)(
                (on_off & (UINT32_C(1) << part_index)) != 0u);
            ++part_count;
        }
    }
    if (view_weapon && !stable_view_weapon &&
        source_vector_scene_read_be16s(bytes) != 0 &&
        !source_vector_scene_sort_parts(
            sprite, bytes, size, &frame, previous_points,
            parts, part_count, error, error_size)) {
        goto fail;
    }
    for (uint32_t order = 0u; order < part_count; ++order) {
        size_t part_offset = 2u + (uint16_t)parts[order].relative_offset;

        for (;;) {
            uint16_t line_count_minus_one;
            uint32_t point_count;
            size_t polygon_byte_count;
            const uint8_t *point_entries;
            const uint8_t *face_bytes;
            uint8_t min_u = UINT8_MAX, max_u = 0u;
            uint8_t min_v = UINT8_MAX, max_v = 0u;
            int gouraud, glare;
            int front_facing = parts[order].active != 0u;
            float flat_light;
            size_t map_offset;
            SourceVectorSceneMaterial material = {0};
            uint32_t material_index;

            if (part_offset > size || 2u > size - part_offset) {
                source_vector_scene_set_error(
                    error, error_size,
                    "source vector model part is outside the asset");
                goto fail;
            }
            line_count_minus_one = source_vector_scene_read_be16(
                bytes + part_offset);
            if ((int16_t)line_count_minus_one < 0) break;
            point_count = (uint32_t)line_count_minus_one + 1u;
            if (point_count < 3u || line_count_minus_one >
                (size - part_offset < 18u ? 0u :
                 (size - part_offset - 18u) / 4u)) {
                source_vector_scene_set_error(
                    error, error_size,
                    "source vector model polygon is malformed");
                goto fail;
            }
            polygon_byte_count = 18u + (size_t)line_count_minus_one * 4u;
            point_entries = bytes + part_offset + 4u;
            face_bytes = point_entries + ((size_t)point_count + 1u) * 4u;
            for (uint32_t corner = 0u; corner < point_count; ++corner) {
                const uint8_t *entry = point_entries + (size_t)corner * 4u;
                if (source_vector_scene_read_be16(entry) >= frame.point_count) {
                    source_vector_scene_set_error(
                        error, error_size,
                        "source vector polygon references an invalid point");
                    goto fail;
                }
                if (entry[2u] < min_u) min_u = entry[2u];
                if (entry[2u] > max_u) max_u = entry[2u];
                if (entry[3u] < min_v) min_v = entry[3u];
                if (entry[3u] > max_v) max_v = entry[3u];
            }
            gouraud = face_bytes[5u] != 0u;
            glare = !gouraud && face_bytes[4u] != 0u;
            if (!source_vector_scene_map_offset(
                    sprite, face_bytes, &map_offset, error, error_size)) {
                goto fail;
            }
            if (stable_geometry) {
                /*
                 * The camera-local mesh is consumed only by the PBR/DXR path.
                 * Do not carry objdrawhires.s:doapoly's directional flat or
                 * Gouraud response into that mesh: incident illumination is
                 * traced. The projected/OpenGL source path below still uses
                 * the exact authored face and point lighting.
                 */
                flat_light = 1.0f;
            } else {
                flat_light = source_vector_scene_flat_light(
                    sprite, bytes + frame.polygon_angle_offset,
                    size - frame.polygon_angle_offset, face_bytes,
                    error, error_size);
                if (flat_light < 0.0f) goto fail;
            }
            if (!source_vector_scene_decode_material(
                    sprite, map_offset, min_u, max_u, min_v, max_v, glare,
                    &material, error, error_size) ||
                !source_vector_scene_append_material(
                    &mesh, &material, &material_index, error, error_size)) {
                free(material.rgba);
                goto fail;
            }
            for (uint32_t fan = 1u; fan + 1u < point_count; ++fan) {
                const uint32_t corners[3] = {0u, fan, fan + 1u};
                SourceVectorSceneTriangle triangle = {0};

                triangle.material_index = material_index;
                triangle.additive = (uint8_t)glare;
                for (uint32_t corner = 0u; corner < 3u; ++corner) {
                    const uint8_t *entry = point_entries +
                        (size_t)corners[corner] * 4u;
                    uint16_t point_index = source_vector_scene_read_be16(entry);
                    SourceVectorSceneVertex *vertex = &triangle.vertices[corner];
                    const uint8_t *point_bytes = bytes + frame.point_data_offset +
                        (size_t)point_index * 6u;
                    const uint8_t *previous_point_bytes = previous_points ?
                        previous_points + (size_t)point_index * 6u : NULL;

                    if (view_weapon) {
                        SourceVectorEyePoint point;

                        if (!source_vector_scene_model_point(
                                sprite, point_bytes, previous_point_bytes,
                                sprite->presentation_frame_interpolation_alpha,
                                &point)) {
                            source_vector_scene_set_error(
                                error, error_size,
                                "source view weapon point transform failed");
                            goto fail;
                        }
                        if (point.z >= -0.5f) {
                            front_facing = 0;
                            if (!stable_view_weapon) break;
                        }
                        if (projected_view_weapon) {
                            vertex->x = projection[0u] * point.x / -point.z;
                            vertex->y = projection[5u] * point.y / -point.z;
                            vertex->z = -point.z;
                        } else {
                            /*
                             * objdrawhires.s's full-screen model path makes
                             * one authored X/Y/Z unit exactly one quarter of
                             * one level unit.  rotate_object represents X/Y
                             * with 64 subunits and Z with one half-unit, so
                             * these divisors recover a uniform source scale
                             * without reverse-projecting a screen overlay.
                             */
                            vertex->x = point.x / 256.0f;
                            vertex->y = point.y / 256.0f;
                            vertex->z = -point.z / 2.0f;
                        }
                    } else {
                        source_vector_scene_world_point(
                            sprite, point_bytes, previous_point_bytes,
                            sprite->presentation_frame_interpolation_alpha,
                            vertex);
                    }
                    vertex->u = ((float)(entry[2u] - min_u) + 0.5f) /
                        ((float)(max_u - min_u) + 1.0f);
                    vertex->v = ((float)(entry[3u] - min_v) + 0.5f) /
                        ((float)(max_v - min_v) + 1.0f);
                    vertex->source_light = stable_geometry ? 1.0f :
                        (glare ? 1.0f : flat_light);
                    if (!stable_geometry && gouraud &&
                        !source_vector_scene_point_light(
                            sprite, bytes + frame.frame_offset + 4u,
                            frame.point_count, point_index,
                            &vertex->source_light, error, error_size)) {
                        goto fail;
                    }
                }
                if (!front_facing && !stable_geometry) break;
                if (!stable_geometry && front_facing && !glare && fan == 1u) {
                    float projected_x[3];
                    float projected_y[3];

                    for (uint32_t corner = 0u; corner < 3u; ++corner) {
                        if (view_weapon) {
                            if (projected_view_weapon) {
                                projected_x[corner] = triangle.vertices[corner].x;
                                projected_y[corner] = triangle.vertices[corner].y;
                            } else {
                                projected_x[corner] = triangle.vertices[corner].x /
                                    triangle.vertices[corner].z;
                                projected_y[corner] = triangle.vertices[corner].y /
                                    triangle.vertices[corner].z;
                            }
                        } else if (!source_vector_scene_project_world(
                                       &triangle.vertices[corner], camera, view,
                                       drawable_aspect, &projected_x[corner],
                                       &projected_y[corner])) {
                            front_facing = 0;
                            break;
                        }
                    }
                    if (front_facing) {
                        float area =
                            (projected_x[2u] - projected_x[1u]) *
                                (projected_y[0u] - projected_y[1u]) -
                            (projected_x[0u] - projected_x[1u]) *
                                (projected_y[2u] - projected_y[1u]);
                        front_facing = area < 0.0f;
                    }
                }
                if (!front_facing && !stable_geometry) break;
                if (!front_facing) {
                    /*
                     * The DXR BLAS must retain one immutable triangle layout
                     * across the source firing animation.  Preserve culled or
                     * disabled source faces as exact zero-area triangles; they
                     * cannot intersect a ray, but can become live vertices in
                     * the next dynamic BLAS update without a queue flush.
                     */
                    triangle.vertices[1u].x = triangle.vertices[0u].x;
                    triangle.vertices[1u].y = triangle.vertices[0u].y;
                    triangle.vertices[1u].z = triangle.vertices[0u].z;
                    triangle.vertices[2u].x = triangle.vertices[0u].x;
                    triangle.vertices[2u].y = triangle.vertices[0u].y;
                    triangle.vertices[2u].z = triangle.vertices[0u].z;
                }
                if (space == SOURCE_VECTOR_SCENE_WORLD_RAY_TRACED) {
                    SourceVectorSceneVertex clipped[5];
                    float top_y = -(float)sprite->source_clip_top_y / 128.0f;
                    float bottom_y = -(float)sprite->source_clip_bottom_y / 128.0f;
                    uint32_t clipped_count;

                    if (top_y < bottom_y) {
                        source_vector_scene_set_error(
                            error, error_size,
                            "source vector object has an inverted sector span");
                        goto fail;
                    }
                    clipped_count = source_vector_scene_clip_triangle_to_sector(
                        triangle.vertices, top_y, bottom_y, clipped);
                    /* A triangle clipped against two parallel planes becomes a
                     * polygon with at most five vertices: three fixed fan slots.
                     * Missing fans collapse to zero area so the BLAS layout is
                     * invariant across pose, part toggles, and sector contact. */
                    for (uint32_t slot = 0u; slot < 3u; ++slot) {
                        SourceVectorSceneTriangle clipped_triangle = triangle;
                        if (slot + 2u < clipped_count) {
                            clipped_triangle.vertices[0u] = clipped[0u];
                            clipped_triangle.vertices[1u] = clipped[slot + 1u];
                            clipped_triangle.vertices[2u] = clipped[slot + 2u];
                        } else {
                            clipped_triangle.vertices[1u] =
                                clipped_triangle.vertices[0u];
                            clipped_triangle.vertices[2u] =
                                clipped_triangle.vertices[0u];
                        }
                        if (!source_vector_scene_append_triangle(
                                &mesh, &clipped_triangle, error, error_size)) {
                            goto fail;
                        }
                    }
                } else if (!view_weapon) {
                    SourceVectorSceneVertex clipped[5];
                    float top_y = -(float)sprite->source_clip_top_y / 128.0f;
                    float bottom_y = -(float)sprite->source_clip_bottom_y / 128.0f;
                    uint32_t clipped_count;

                    if (top_y < bottom_y) {
                        source_vector_scene_set_error(
                            error, error_size,
                            "source vector object has an inverted sector span");
                        goto fail;
                    }
                    clipped_count = source_vector_scene_clip_triangle_to_sector(
                        triangle.vertices, top_y, bottom_y, clipped);
                    for (uint32_t clipped_fan = 1u;
                         clipped_fan + 1u < clipped_count; ++clipped_fan) {
                        SourceVectorSceneTriangle clipped_triangle = triangle;
                        clipped_triangle.vertices[0u] = clipped[0u];
                        clipped_triangle.vertices[1u] = clipped[clipped_fan];
                        clipped_triangle.vertices[2u] = clipped[clipped_fan + 1u];
                        if (!source_vector_scene_append_triangle(
                                &mesh, &clipped_triangle, error, error_size)) {
                            goto fail;
                        }
                    }
                } else if (!source_vector_scene_append_triangle(
                               &mesh, &triangle, error, error_size)) {
                    goto fail;
                }
            }
            part_offset += polygon_byte_count;
        }
    }
    *out_mesh = mesh;
    return 1;

fail:
    source_vector_scene_mesh_destroy(&mesh);
    return 0;
}

int source_vector_scene_compile_view_weapon(
    const SceneSprite *sprite, float drawable_aspect,
    SourceVectorSceneMesh *out_mesh, char *error, size_t error_size)
{
    return source_vector_scene_compile(
        sprite, NULL, NULL, drawable_aspect,
        SOURCE_VECTOR_SCENE_VIEW_PROJECTED,
        out_mesh, error, error_size);
}

int source_vector_scene_compile_view_weapon_camera(
    const SceneSprite *sprite, SourceVectorSceneMesh *out_mesh,
    char *error, size_t error_size)
{
    return source_vector_scene_compile(
        sprite, NULL, NULL, 0.0f, SOURCE_VECTOR_SCENE_VIEW_CAMERA,
        out_mesh, error, error_size);
}

int source_vector_scene_compile_world(
    const SceneSprite *sprite, const SceneCamera *camera,
    const RenderView *view, float drawable_aspect,
    SourceVectorSceneMesh *out_mesh, char *error, size_t error_size)
{
    return source_vector_scene_compile(
        sprite, camera, view, drawable_aspect, SOURCE_VECTOR_SCENE_WORLD,
        out_mesh, error, error_size);
}

int source_vector_scene_compile_world_ray_traced(
    const SceneSprite *sprite, SourceVectorSceneMesh *out_mesh,
    char *error, size_t error_size)
{
    return source_vector_scene_compile(
        sprite, NULL, NULL, 0.0f,
        SOURCE_VECTOR_SCENE_WORLD_RAY_TRACED,
        out_mesh, error, error_size);
}
