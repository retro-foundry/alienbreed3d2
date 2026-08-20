#include "source_bitmap_sprite.h"

#include "bitmap_source_decode.h"
#include "scene_geometry_compile.h"
#include "source_bitmap_lighting.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const float source_bitmap_sprite_pi = 3.14159265358979323846f;
static const float source_bitmap_projectile_contact_epsilon = 16.0f;

static void source_bitmap_sprite_set_error(char *error, size_t error_size,
                                           const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t source_bitmap_sprite_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8u) | source[1]);
}

static uint32_t source_bitmap_sprite_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24u) |
           ((uint32_t)source[1] << 16u) |
           ((uint32_t)source[2] << 8u) | source[3];
}

static int source_bitmap_sprite_display_color(const uint8_t *palette,
                                              size_t palette_size,
                                              uint8_t color_index,
                                              uint8_t out_color[4])
{
    size_t offset = (size_t)color_index * 6u;

    if (!palette || offset > palette_size || 6u > palette_size - offset) {
        return 0;
    }
    out_color[0] = palette[offset + 1u];
    out_color[1] = palette[offset + 3u];
    out_color[2] = palette[offset + 5u];
    out_color[3] = UINT8_MAX;
    return 1;
}

static int source_bitmap_sprite_write_color(uint8_t *pixels,
                                            size_t pixel_offset,
                                            const SceneSprite *sprite,
                                            uint8_t color_index,
                                            int transparent)
{
    if (!source_bitmap_sprite_display_color(
            sprite->source_display_palette_bytes,
            sprite->source_display_palette_byte_count, color_index,
            pixels + pixel_offset)) {
        return 0;
    }
    if (transparent) {
        pixels[pixel_offset + 3u] = 0u;
    }
    return 1;
}

static int16_t source_bitmap_sprite_bright_to_add(const SceneSprite *sprite,
                                                   const SceneCamera *camera)
{
    float sprite_x = (float)(int16_t)(uint16_t)sprite->position.x;
    float sprite_z = (float)(int16_t)(uint16_t)sprite->position.z;
    float camera_x = (float)(int16_t)(uint16_t)camera->position.x;
    float camera_z = (float)(int16_t)(uint16_t)camera->position.z;
    float yaw = (float)camera->yaw * (2.0f * source_bitmap_sprite_pi / 8192.0f);
    int16_t source_depth = (int16_t)(((sprite_x - camera_x) * sinf(yaw) +
                                      (sprite_z - camera_z) * cosf(yaw)) /
                                     64.0f);

    return source_bitmap_bright_to_add(sprite->source_brightness,
                                       source_depth);
}

/* Exact state construction for objdrawhires.s:draw_bitmap_lighted. */
static int source_bitmap_sprite_build_lighted_palette(
    const SceneSprite *sprite, const SceneCamera *camera,
    uint8_t out_palette[256], char *error, size_t error_size)
{
    static const int16_t source_xz_angles[16][2] = {
        {0, 23}, {10, 20}, {16, 16}, {20, 10}, {23, 0}, {20, -10}, {16, -16}, {10, -20},
        {0, -23}, {-10, -20}, {-16, -16}, {-20, -10}, {-23, 0}, {-20, 10}, {-16, 16}, {-10, 20}
    };
    static const uint8_t source_brights[29] = {
        3u, 8u, 9u, 10u, 11u, 12u, 15u, 16u, 17u, 18u, 19u, 21u, 22u, 23u, 24u,
        25u, 26u, 27u, 29u, 30u, 31u, 32u, 33u, 36u, 37u, 38u, 39u, 40u, 45u
    };
    static const uint8_t source_brights_flipped[29] = {
        3u, 12u, 11u, 10u, 9u, 8u, 19u, 18u, 17u, 16u, 15u, 27u, 26u, 25u, 24u,
        23u, 22u, 21u, 33u, 32u, 31u, 30u, 29u, 40u, 39u, 38u, 37u, 36u, 45u
    };
    static const int16_t source_willy_bright[49] = {
        30, 30, 30, 30, 30, 30, 30,
        30, 20, 20, 20, 20, 20, 30,
        30, 20, 6, 3, 6, 20, 30,
        30, 20, 6, 0, 6, 20, 30,
        30, 20, 6, 6, 6, 20, 30,
        30, 20, 20, 20, 20, 20, 30,
        30, 30, 30, 30, 30, 30, 30
    };
    static const uint8_t source_rough_angle_map[16] = {
        3u, 2u, 0u, 1u, 4u, 5u, 7u, 6u,
        12u, 13u, 15u, 14u, 11u, 10u, 8u, 9u
    };
    const uint8_t *source_bright_table;
    uint8_t light_palette;
    int32_t top_x = 0, top_z = 0, bottom_x = 0, bottom_z = 0;
    int top_brightest = 0, bottom_brightest = 0, rough_bits = 0;
    int32_t rough_x, rough_z;
    int balance, strongest;
    int16_t bright_to_add;
    int willy[49];

    if (!sprite->source_light_palette_bytes ||
        sprite->source_light_palette_byte_count < 16u * 7u * 16u ||
        !sprite->source_palette_bytes) {
        source_bitmap_sprite_set_error(
            error, error_size,
            "source lighted bitmap has incomplete live palette state");
        return 0;
    }
    light_palette = (uint8_t)(sprite->source_effect & 0x7fu);
    if (light_palette < 2u || light_palette >= 6u ||
        (size_t)(light_palette - 2u) * 256u > sprite->source_palette_byte_count ||
        256u > sprite->source_palette_byte_count -
                   (size_t)(light_palette - 2u) * 256u) {
        source_bitmap_sprite_set_error(
            error, error_size,
            "source lighted bitmap palette selector is invalid");
        return 0;
    }
    for (uint32_t direction = 0u; direction < 16u; ++direction) {
        uint8_t upper = (uint8_t)sprite->source_bitmap_angle_brightness[16u + direction];
        uint8_t lower = (uint8_t)sprite->source_bitmap_angle_brightness[direction];

        if (upper != UINT8_C(0x80)) {
            int brightness = 48 - (int)upper;
            if (brightness > top_brightest) top_brightest = brightness;
            top_x += (int32_t)source_xz_angles[direction][0] * brightness;
            top_z += (int32_t)source_xz_angles[direction][1] * brightness;
        }
        if (lower != UINT8_C(0x80)) {
            int brightness = 48 - (int)lower;
            if (brightness > bottom_brightest) bottom_brightest = brightness;
            bottom_x += (int32_t)source_xz_angles[direction][0] * brightness;
            bottom_z += (int32_t)source_xz_angles[direction][1] * brightness;
        }
    }
    rough_x = top_x + bottom_x;
    rough_z = top_z + bottom_z;
    if (rough_x < 0) { rough_x = -rough_x; rough_bits += 8; }
    if (rough_z < 0) { rough_z = -rough_z; rough_bits += 4; }
    if (rough_x < rough_z) {
        int32_t swap = rough_x; rough_x = rough_z; rough_z = swap; rough_bits += 2;
    }
    if (rough_z > rough_x / 2) ++rough_bits;
    if (top_brightest == bottom_brightest) {
        balance = 7;
        strongest = top_brightest;
    } else {
        int total = top_brightest + bottom_brightest;
        if (total <= 0) {
            source_bitmap_sprite_set_error(
                error, error_size,
                "source lighted bitmap has no directional brightness samples");
            return 0;
        }
        balance = ((top_brightest << 4) - 1) / total;
        strongest = top_brightest > bottom_brightest ?
            top_brightest : bottom_brightest;
    }
    if (balance < 0 || balance >= 16) {
        source_bitmap_sprite_set_error(
            error, error_size,
            "source lighted bitmap directional balance is invalid");
        return 0;
    }
    bright_to_add = source_bitmap_sprite_bright_to_add(sprite, camera);
    for (uint32_t row = 0u; row < 7u; ++row) {
        uint8_t source_direction = (uint8_t)(((UINT16_C(8192) - camera->yaw) &
                                               UINT16_C(8190)) >> 9u);
        uint8_t direction = (uint8_t)((source_direction - 3u +
                                       source_rough_angle_map[rough_bits]) & 0x0fu);
        for (uint32_t column = 0u; column < 7u; ++column) {
            int8_t source_value = (int8_t)sprite->source_light_palette_bytes[
                (size_t)balance * 7u * 16u + row * 16u + direction];
            willy[row * 7u + column] = source_bitmap_lighted_palette_shade(
                source_value, (int16_t)strongest, bright_to_add,
                source_willy_bright[row * 7u + column]);
            direction = (uint8_t)((direction + 1u) & 0x0fu);
        }
    }
    memset(out_palette, 0, 256u);
    source_bright_table =
        (sprite->flags & SCENE_SPRITE_FLAG_FLIP_HORIZONTAL) != 0u ?
        source_brights_flipped : source_brights;
    for (uint32_t group = 0u; group < 29u; ++group) {
        int shade = willy[source_bright_table[group]];
        size_t source_offset;
        if (shade < 0) shade = 0;
        else if (shade > 31) shade = 31;
        source_offset = (size_t)(light_palette - 2u) * 256u +
                        (size_t)shade * 8u;
        memcpy(out_palette + group * 8u,
               sprite->source_palette_bytes + source_offset, 8u);
        out_palette[group * 8u + 3u] = 0u;
    }
    return 1;
}

static int source_bitmap_sprite_effect_texel(uint8_t *pixels,
                                             size_t pixel_offset,
                                             const SceneSprite *sprite,
                                             uint8_t source_texel,
                                             int glare)
{
    size_t table_offset;
    uint8_t output_index;

    if (source_texel == 0u) {
        memset(pixels + pixel_offset, 0, 4u);
        return 1;
    }
    table_offset = glare ? (size_t)(source_texel - 1u) * 512u :
                           (size_t)source_texel * 256u;
    if (table_offset > sprite->source_palette_byte_count ||
        256u > sprite->source_palette_byte_count - table_offset) {
        return 0;
    }
    output_index = sprite->source_palette_bytes[table_offset];
    return source_bitmap_sprite_write_color(
        pixels, pixel_offset, sprite, output_index, output_index == 0u);
}

void source_bitmap_sprite_image_destroy(SourceBitmapSpriteImage *image)
{
    if (!image) return;
    free(image->rgba);
    memset(image, 0, sizeof(*image));
}

static uint32_t source_bitmap_scene_material_mode(const SceneSprite *sprite)
{
    if (sprite->source == SCENE_SPRITE_SOURCE_GLARE_BITMAP) {
        return 7u;
    }
    if ((sprite->flags & SCENE_SPRITE_FLAG_ADDITIVE) != 0u) {
        return 6u;
    }
    if ((sprite->flags & SCENE_SPRITE_FLAG_LIGHT_PALETTE) != 0u) {
        return (uint32_t)(sprite->source_effect & 0x7fu);
    }
    return 0u;
}

int source_bitmap_scene_compile_world(const SceneSprite *sprite,
                                      const SceneCamera *camera,
                                      SourceBitmapSceneMesh *out_mesh,
                                      char *error, size_t error_size)
{
    SourceBitmapSceneMesh mesh = {0};
    SceneRenderPoint center;
    float yaw;
    float right_x;
    float right_z;
    float half_width;
    float half_height;
    float full_top_y;
    float full_bottom_y;
    float top_y;
    float bottom_y;
    float top_v;
    float bottom_v;
    float clip_top_y;
    float clip_bottom_y;
    float left_u;
    float right_u;

    if (!sprite || !camera || !out_mesh ||
        sprite->presentation != SCENE_SPRITE_PRESENTATION_WORLD_OBJECT ||
        (sprite->source != SCENE_SPRITE_SOURCE_OBJECT_BITMAP &&
         sprite->source != SCENE_SPRITE_SOURCE_GLARE_BITMAP)) {
        source_bitmap_sprite_set_error(
            error, error_size, "source world bitmap descriptor is invalid");
        return 0;
    }
    mesh.material_mode = source_bitmap_scene_material_mode(sprite);
    mesh.additive = (uint8_t)(mesh.material_mode == 6u ||
                              mesh.material_mode == 7u);
    if ((sprite->flags & SCENE_SPRITE_FLAG_LIGHT_PALETTE) != 0u &&
        (mesh.material_mode < 2u || mesh.material_mode > 5u)) {
        source_bitmap_sprite_set_error(
            error, error_size,
            "source world bitmap light-palette selector is invalid");
        return 0;
    }

    center = scene_render_world_point(sprite->position);
    yaw = (float)camera->yaw *
        (2.0f * source_bitmap_sprite_pi / 8192.0f);
    right_x = cosf(yaw);
    right_z = -sinf(yaw);
    center.x += right_x * (float)sprite->source_aux_offset_x;
    center.z += right_z * (float)sprite->source_aux_offset_x;
    center.y -= (float)sprite->source_aux_offset_y;
    if ((sprite->flags & SCENE_SPRITE_FLAG_PROJECTILE_CONTACT) != 0u) {
        center.x -= sinf(yaw) * source_bitmap_projectile_contact_epsilon;
        center.z -= cosf(yaw) * source_bitmap_projectile_contact_epsilon;
    }

    half_width = (float)sprite->source_width;
    half_height = (float)sprite->source_height;
    if (sprite->surface_attachment == SCENE_SPRITE_SURFACE_FLOOR) {
        full_bottom_y = -(float)sprite->source_clip_bottom_y *
            SCENE_RENDER_SOURCE_Y_UNIT;
        full_top_y = full_bottom_y + half_height * 2.0f;
    } else if (sprite->surface_attachment == SCENE_SPRITE_SURFACE_CEILING) {
        full_top_y = -(float)sprite->source_clip_top_y *
            SCENE_RENDER_SOURCE_Y_UNIT;
        full_bottom_y = full_top_y - half_height * 2.0f;
    } else {
        full_top_y = center.y + half_height;
        full_bottom_y = center.y - half_height;
    }
    top_y = full_top_y;
    bottom_y = full_bottom_y;
    clip_top_y = -(float)sprite->source_clip_top_y *
        SCENE_RENDER_SOURCE_Y_UNIT;
    clip_bottom_y = -(float)sprite->source_clip_bottom_y *
        SCENE_RENDER_SOURCE_Y_UNIT;
    if (top_y > clip_top_y) {
        top_y = clip_top_y;
    }
    if (bottom_y < clip_bottom_y) {
        bottom_y = clip_bottom_y;
    }
    left_u = (sprite->flags & SCENE_SPRITE_FLAG_FLIP_HORIZONTAL) != 0u ?
        1.0f : 0.0f;
    right_u = 1.0f - left_u;

    if (sprite->source_width == 0u || sprite->source_height == 0u ||
        top_y <= bottom_y || full_top_y <= full_bottom_y) {
        for (size_t index = 0u; index < 6u; ++index) {
            mesh.vertices[index].x = center.x;
            mesh.vertices[index].y = center.y;
            mesh.vertices[index].z = center.z;
            mesh.vertices[index].u = left_u;
            mesh.vertices[index].v = 0.0f;
        }
        *out_mesh = mesh;
        return 1;
    }

    top_v = (full_top_y - top_y) / (full_top_y - full_bottom_y);
    bottom_v = (full_top_y - bottom_y) / (full_top_y - full_bottom_y);
#define SOURCE_BITMAP_VERTEX(index, horizontal, vertical, texture_u, texture_v) \
    do {                                                                       \
        mesh.vertices[(index)].x = center.x + right_x * (horizontal);           \
        mesh.vertices[(index)].y = (vertical);                                 \
        mesh.vertices[(index)].z = center.z + right_z * (horizontal);           \
        mesh.vertices[(index)].u = (texture_u);                                \
        mesh.vertices[(index)].v = (texture_v);                                \
    } while (0)
    SOURCE_BITMAP_VERTEX(0u, -half_width, top_y, left_u, top_v);
    SOURCE_BITMAP_VERTEX(1u, half_width, top_y, right_u, top_v);
    SOURCE_BITMAP_VERTEX(2u, half_width, bottom_y, right_u, bottom_v);
    mesh.vertices[3u] = mesh.vertices[0u];
    mesh.vertices[4u] = mesh.vertices[2u];
    SOURCE_BITMAP_VERTEX(5u, -half_width, bottom_y, left_u, bottom_v);
#undef SOURCE_BITMAP_VERTEX
    mesh.visible = 1u;
    *out_mesh = mesh;
    return 1;
}

int source_bitmap_sprite_decode(const SceneSprite *sprite,
                                const SceneCamera *camera,
                                SourceBitmapSpriteImage *out_image,
                                char *error, size_t error_size)
{
    SourceBitmapSpriteImage image = {0};
    size_t table_offset, palette_offset = 0u;
    int lighted, source_blend;
    uint8_t normal_palette_row = 0u;
    uint8_t lighted_palette[256];

    if (!sprite || !camera || !out_image || !sprite->source_bytes ||
        !sprite->source_aux_bytes || !sprite->source_palette_bytes ||
        !sprite->source_display_palette_bytes ||
        (sprite->source != SCENE_SPRITE_SOURCE_OBJECT_BITMAP &&
         sprite->source != SCENE_SPRITE_SOURCE_GLARE_BITMAP) ||
        sprite->frame_metrics.strip_count == 0u ||
        sprite->frame_metrics.line_count == 0u) {
        source_bitmap_sprite_set_error(
            error, error_size, "source bitmap sprite descriptor is invalid");
        return 0;
    }
    if (!bitmap_source_expand_half_span(sprite->frame_metrics.strip_count,
                                        &image.width) ||
        !bitmap_source_expand_half_span(sprite->frame_metrics.line_count,
                                        &image.height) ||
        (size_t)image.width > SIZE_MAX / image.height ||
        (size_t)image.width * image.height > SIZE_MAX / 4u) {
        source_bitmap_sprite_set_error(
            error, error_size, "source bitmap sprite dimensions are invalid");
        return 0;
    }
    lighted = (sprite->flags & SCENE_SPRITE_FLAG_LIGHT_PALETTE) != 0u;
    source_blend = lighted == 0 &&
        (sprite->source == SCENE_SPRITE_SOURCE_GLARE_BITMAP ||
         (sprite->flags & SCENE_SPRITE_FLAG_ADDITIVE) != 0u);
    image.additive = (uint8_t)source_blend;
    if (lighted) {
        uint8_t light_palette = (uint8_t)(sprite->source_effect & 0x7fu);
        if (light_palette < 2u || light_palette >= 6u ||
            !source_bitmap_sprite_build_lighted_palette(
                sprite, camera, lighted_palette, error, error_size)) {
            if (light_palette < 2u || light_palette >= 6u) {
                source_bitmap_sprite_set_error(
                    error, error_size,
                    "source bitmap light-palette selector is invalid");
            }
            return 0;
        }
        palette_offset = (size_t)(light_palette - 2u) * 256u;
        if (palette_offset > sprite->source_palette_byte_count ||
            256u > sprite->source_palette_byte_count - palette_offset) {
            source_bitmap_sprite_set_error(
                error, error_size,
                "source bitmap light palette is outside its asset");
            return 0;
        }
    } else if (!source_blend) {
        normal_palette_row = source_bitmap_direct_palette_row(
            source_bitmap_sprite_bright_to_add(sprite, camera));
        palette_offset = (size_t)normal_palette_row * 64u;
        if (palette_offset > sprite->source_palette_byte_count ||
            64u > sprite->source_palette_byte_count - palette_offset) {
            source_bitmap_sprite_set_error(
                error, error_size,
                "source bitmap direct palette row is outside its asset");
            return 0;
        }
    }
    table_offset = (size_t)sprite->frame_metrics.pointer_table_index * 4u;
    if (table_offset > sprite->source_aux_byte_count ||
        (size_t)image.width >
            (sprite->source_aux_byte_count - table_offset) / 4u) {
        source_bitmap_sprite_set_error(
            error, error_size, "source bitmap sprite PTR table is invalid");
        return 0;
    }
    image.rgba = calloc((size_t)image.width * image.height, 4u);
    if (!image.rgba) {
        source_bitmap_sprite_set_error(
            error, error_size,
            "source bitmap sprite conversion allocation failed");
        return 0;
    }
    for (uint16_t x = 0u; x < image.width; ++x) {
        uint32_t source_pointer = source_bitmap_sprite_read_be32(
            sprite->source_aux_bytes + table_offset + (size_t)x * 4u);
        uint8_t pack = (uint8_t)(source_pointer >> 24u);
        size_t column_offset = lighted ? source_pointer :
            source_pointer & UINT32_C(0x00ffffff);

        if (source_pointer == 0u) continue;
        if (column_offset > sprite->source_byte_count ||
            sprite->frame_metrics.down_strip > UINT16_MAX - image.height ||
            (lighted ?
                (size_t)sprite->frame_metrics.down_strip + image.height >
                    sprite->source_byte_count - column_offset :
                pack > 2u ||
                (size_t)sprite->frame_metrics.down_strip + image.height >
                    (sprite->source_byte_count - column_offset) / 2u)) {
            source_bitmap_sprite_image_destroy(&image);
            source_bitmap_sprite_set_error(
                error, error_size, "source bitmap sprite WAD column is invalid");
            return 0;
        }
        for (uint16_t y = 0u; y < image.height; ++y) {
            uint8_t source_texel;
            uint8_t color_index;
            size_t pixel_offset = ((size_t)y * image.width + x) * 4u;

            if (lighted) {
                source_texel = sprite->source_bytes[
                    column_offset + sprite->frame_metrics.down_strip + y];
            } else {
                size_t word_offset = column_offset +
                    ((size_t)sprite->frame_metrics.down_strip + y) * 2u;
                source_texel = bitmap_source_decode_packed_texel(
                    source_bitmap_sprite_read_be16(
                        sprite->source_bytes + word_offset), pack);
            }
            if (!lighted && !source_blend &&
                (palette_offset > sprite->source_palette_byte_count ||
                 (size_t)source_texel * 2u + 2u >
                     sprite->source_palette_byte_count - palette_offset)) {
                source_bitmap_sprite_image_destroy(&image);
                source_bitmap_sprite_set_error(
                    error, error_size,
                    "source bitmap sprite palette is invalid");
                return 0;
            }
            if (source_blend) {
                if (!source_bitmap_sprite_effect_texel(
                        image.rgba, pixel_offset, sprite, source_texel,
                        sprite->source == SCENE_SPRITE_SOURCE_GLARE_BITMAP)) {
                    source_bitmap_sprite_image_destroy(&image);
                    source_bitmap_sprite_set_error(
                        error, error_size,
                        "source bitmap effect blend table is invalid");
                    return 0;
                }
                continue;
            }
            color_index = lighted ? lighted_palette[source_texel] :
                sprite->source_palette_bytes[
                    palette_offset + (size_t)source_texel * 2u];
            if (!source_bitmap_sprite_write_color(
                    image.rgba, pixel_offset, sprite, color_index,
                    source_texel == 0u)) {
                source_bitmap_sprite_image_destroy(&image);
                source_bitmap_sprite_set_error(
                    error, error_size,
                    "source bitmap palette references invalid display colour");
                return 0;
            }
        }
    }
    *out_image = image;
    return 1;
}
