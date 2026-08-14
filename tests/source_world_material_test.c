#include <stdio.h>
#include <string.h>

#include "source_world_material.h"

int main(void)
{
    uint8_t floor_pixels[64u * 1024u] = {0};
    uint8_t flat_palette[63u * 256u] = {0};
    uint8_t display_palette[256u * 6u] = {0};
    SceneMaterial material = {0};
    SceneGeometry geometry = {0};
    SourceWorldMaterialImage image = {0};
    char error[256] = {0};

    floor_pixels[0] = 7u;
    flat_palette[32u * 256u + 7u] = 42u;
    display_palette[42u * 6u + 1u] = 17u;
    display_palette[42u * 6u + 3u] = 34u;
    display_palette[42u * 6u + 5u] = 51u;
    material.source = SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE;
    material.source_asset_id = 0u;
    material.source_bytes = floor_pixels;
    material.source_byte_count = sizeof(floor_pixels);
    material.source_palette_bytes = flat_palette;
    material.source_palette_byte_count = sizeof(flat_palette);
    material.source_display_palette_bytes = display_palette;
    material.source_display_palette_byte_count = sizeof(display_palette);
    geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_FLOOR;
    if (!source_world_material_decode(
            &material, &geometry, &image, error, sizeof(error))) {
        fprintf(stderr, "source flat decode failed: %s\n", error);
        return 1;
    }
    if (image.width != 64u || image.height != 64u || !image.rgba ||
        image.rgba[0] != 17u || image.rgba[1] != 34u ||
        image.rgba[2] != 51u || image.rgba[3] != 255u) {
        fprintf(stderr, "source flat decode did not resolve the bright palette row\n");
        source_world_material_image_destroy(&image);
        return 1;
    }
    source_world_material_image_destroy(&image);
    return 0;
}
