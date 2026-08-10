#ifndef AB3D2_GAME_RESOURCES_H
#define AB3D2_GAME_RESOURCES_H

#include <stdint.h>

#include "asset_io.h"
#include "game_link.h"

/*
 * Raw source assets owned across the original game-menu lifetime. This is not
 * a renderer cache: the eventual GPU backend receives these source records
 * through an explicit conversion layer rather than owning their file bytes.
 */
typedef struct {
    /* data/draw_data.s:draw_Palette_vw, used by every indexed source image. */
    AssetBlob main_palette;
    AssetBlob floor_texture;
    AssetBlob texture_maps;
    AssetBlob texture_palette;
    AssetBlob backdrop_image;
    /* data/draw_data.s:draw_WaterFrames_vb's 64 KiB source distortion table. */
    AssetBlob water_frames;

    AssetBlob object_wads[GAME_LINK_OBJECT_COUNT];
    AssetBlob object_ptrs[GAME_LINK_OBJECT_COUNT];
    AssetBlob object_palettes[GAME_LINK_OBJECT_COUNT];
    uint16_t object_count;

    AssetBlob vector_models[GAME_LINK_OBJECT_COUNT];
    uint16_t vector_count;

    AssetBlob wall_textures[GAME_LINK_WALL_COUNT];
    uint16_t wall_texture_count;

    /* Indexed by modules/res.s:Res_LoadSoundFx slot 0-58. */
    AssetBlob sound_effects[GAME_LINK_SFX_LOAD_COUNT];
    uint16_t sound_effect_count;
} GameSharedResources;

void game_shared_resources_init(GameSharedResources *resources);
int game_shared_resources_load(GameSharedResources *resources, const GameLink *game_link,
                               const char *data_root, char *error, size_t error_size);
void game_shared_resources_destroy(GameSharedResources *resources);

#endif
