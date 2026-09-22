#ifndef AB3D2_RENDERER_RESOURCES_H
#define AB3D2_RENDERER_RESOURCES_H

#include <stddef.h>
#include <stdint.h>

/*
 * Renderer-neutral source-resource catalog. Game_Start owns the original
 * bytes; a backend may convert them into API resources before gameplay but
 * must not retain ownership of or write back to the source blobs.
 */
typedef struct {
    uint32_t source_asset_id;
    const uint8_t *source_bytes;
    size_t source_byte_count;
} RendererVectorResource;

typedef struct {
    const RendererVectorResource *vector_resources;
    size_t vector_resource_count;
    /* objdrawhires.s:Draw_TextureMapsPtr_l. */
    const uint8_t *vector_texture_bytes;
    size_t vector_texture_byte_count;
    /* objdrawhires.s:Draw_TexturePalettePtr_l. */
    const uint8_t *vector_light_palette_bytes;
    size_t vector_light_palette_byte_count;
    /* data/draw_data.s:draw_Palette_vw. */
    const uint8_t *source_display_palette_bytes;
    size_t source_display_palette_byte_count;
    /*
     * ObjT bitmap graphics indices the level loaded. A backend that decodes
     * object art lazily uses these to do it before gameplay instead of inside
     * the frame that first shows a muzzle flash, an impact or a projectile.
     */
    const uint32_t *bitmap_asset_ids;
    size_t bitmap_asset_count;
} RendererResourceCatalog;

#endif
