#include "source_world_material.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void source_world_material_set_error(char *error, size_t error_size,
                                            const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t source_world_material_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8u) | source[1]);
}

static int source_world_material_display_color(
    const uint8_t *palette, size_t palette_size, uint8_t color_index,
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

static int source_world_material_decode_wall_indices(
    const SceneMaterial *material, const SceneTextureWindow *window,
    SourceWorldMaterialImage *out_image, char *error, size_t error_size)
{
    uint8_t *pixels;
    size_t pixel_count;

    if (!material->source_bytes || material->source_byte_count < 2048u ||
        window->u_period == 0u || window->v_period == 0u ||
        (size_t)window->u_period > SIZE_MAX / (size_t)window->v_period ||
        (size_t)window->u_period * window->v_period > SIZE_MAX / 4u) {
        source_world_material_set_error(
            error, error_size, "source wall material descriptor is invalid");
        return 0;
    }
    pixel_count = (size_t)window->u_period * window->v_period;
    pixels = calloc(pixel_count, 4u);
    if (!pixels) {
        source_world_material_set_error(
            error, error_size, "source wall material allocation failed");
        return 0;
    }
    for (uint16_t y = 0u; y < window->v_period; ++y) {
        for (uint16_t x = 0u; x < window->u_period; ++x) {
            uint16_t source_u = (uint16_t)(window->u_offset + x);
            size_t strip_offset = 2048u + (size_t)(source_u / 3u) *
                window->v_period * 2u + (size_t)y * 2u;
            uint8_t packed_texel;
            size_t pixel_offset = ((size_t)y * window->u_period + x) * 4u;

            if (strip_offset > material->source_byte_count ||
                2u > material->source_byte_count - strip_offset) {
                free(pixels);
                source_world_material_set_error(
                    error, error_size,
                    "source wall material strip is outside its WAD asset");
                return 0;
            }
            switch (source_u % 3u) {
            case 0u:
                packed_texel =
                    (uint8_t)(material->source_bytes[strip_offset + 1u] & 31u);
                break;
            case 1u:
                packed_texel = (uint8_t)((source_world_material_read_be16(
                    material->source_bytes + strip_offset) >> 5u) & 31u);
                break;
            default:
                packed_texel =
                    (uint8_t)((material->source_bytes[strip_offset] >> 2u) & 31u);
                break;
            }
            pixels[pixel_offset] = packed_texel;
            pixels[pixel_offset + 3u] = UINT8_MAX;
        }
    }
    out_image->rgba = pixels;
    out_image->width = window->u_period;
    out_image->height = window->v_period;
    return 1;
}

static int source_world_material_decode_flat_indices(
    const SceneMaterial *material, SourceWorldMaterialImage *out_image,
    char *error, size_t error_size)
{
    enum { LOGICAL_TILE_SIZE = 64u, TILE_ROW_STRIDE = 1024u };
    uint8_t *pixels;
    size_t tile_offset;

    if (!material->source_bytes ||
        material->source_byte_count < LOGICAL_TILE_SIZE * TILE_ROW_STRIDE) {
        source_world_material_set_error(
            error, error_size, "source flat material descriptor is invalid");
        return 0;
    }
    pixels = malloc(LOGICAL_TILE_SIZE * LOGICAL_TILE_SIZE * 4u);
    if (!pixels) {
        source_world_material_set_error(
            error, error_size, "source flat material allocation failed");
        return 0;
    }
    tile_offset = (size_t)material->source_asset_id %
        material->source_byte_count;
    for (uint16_t y = 0u; y < LOGICAL_TILE_SIZE; ++y) {
        for (uint16_t x = 0u; x < LOGICAL_TILE_SIZE; ++x) {
            size_t source_offset = (tile_offset + (size_t)y * TILE_ROW_STRIDE +
                                    (size_t)x * 4u) %
                material->source_byte_count;
            size_t pixel_offset =
                ((size_t)y * LOGICAL_TILE_SIZE + x) * 4u;

            pixels[pixel_offset] = material->source_bytes[source_offset];
            pixels[pixel_offset + 1u] = 0u;
            pixels[pixel_offset + 2u] = 0u;
            pixels[pixel_offset + 3u] = UINT8_MAX;
        }
    }
    out_image->rgba = pixels;
    out_image->width = LOGICAL_TILE_SIZE;
    out_image->height = LOGICAL_TILE_SIZE;
    return 1;
}

static int source_world_material_resolve_bright_row(
    const SceneMaterial *material, SceneGeometryPrimitive primitive,
    SourceWorldMaterialImage *image, char *error, size_t error_size)
{
    enum {
        WALL_PALETTE_WIDTH = 32u,
        FLAT_FIRST_SHADE_ROW = 32u,
        FLAT_PALETTE_WIDTH = 256u
    };
    size_t pixel_count = (size_t)image->width * image->height;

    if (!material->source_palette_bytes ||
        !material->source_display_palette_bytes ||
        (primitive == SCENE_GEOMETRY_PRIMITIVE_WALL &&
         material->source_palette_byte_count < WALL_PALETTE_WIDTH * 2u) ||
        (primitive != SCENE_GEOMETRY_PRIMITIVE_WALL &&
         material->source_palette_byte_count <
             (FLAT_FIRST_SHADE_ROW + 1u) * FLAT_PALETTE_WIDTH)) {
        source_world_material_set_error(
            error, error_size, "source material palette descriptor is invalid");
        return 0;
    }
    for (size_t pixel_index = 0u; pixel_index < pixel_count; ++pixel_index) {
        size_t pixel_offset = pixel_index * 4u;
        uint8_t source_index = image->rgba[pixel_offset];
        size_t palette_offset = primitive == SCENE_GEOMETRY_PRIMITIVE_WALL ?
            (size_t)source_index * 2u :
            (size_t)FLAT_FIRST_SHADE_ROW * FLAT_PALETTE_WIDTH + source_index;

        if ((primitive == SCENE_GEOMETRY_PRIMITIVE_WALL &&
             source_index >= WALL_PALETTE_WIDTH) ||
            palette_offset >= material->source_palette_byte_count ||
            !source_world_material_display_color(
                material->source_display_palette_bytes,
                material->source_display_palette_byte_count,
                material->source_palette_bytes[palette_offset],
                image->rgba + pixel_offset)) {
            source_world_material_set_error(
                error, error_size,
                "source material palette references an invalid display colour");
            return 0;
        }
    }
    return 1;
}

int source_world_material_decode(const SceneMaterial *material,
                                 const SceneGeometry *geometry,
                                 SourceWorldMaterialImage *out_image,
                                 char *error, size_t error_size)
{
    SourceWorldMaterialImage result = {0};
    int decoded;

    if (!material || !geometry || !out_image) {
        source_world_material_set_error(
            error, error_size, "source world material input is invalid");
        return 0;
    }
    decoded = geometry->primitive == SCENE_GEOMETRY_PRIMITIVE_WALL ?
        source_world_material_decode_wall_indices(
            material, &geometry->texture_window, &result, error, error_size) :
        source_world_material_decode_flat_indices(
            material, &result, error, error_size);
    if (!decoded || !source_world_material_resolve_bright_row(
            material, geometry->primitive, &result, error, error_size)) {
        source_world_material_image_destroy(&result);
        return 0;
    }
    *out_image = result;
    return 1;
}

void source_world_material_image_destroy(SourceWorldMaterialImage *image)
{
    if (!image) {
        return;
    }
    free(image->rgba);
    memset(image, 0, sizeof(*image));
}
