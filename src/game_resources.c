#include "game_resources.h"

#include <stdio.h>
#include <string.h>

static void game_resources_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int game_resources_load_volume_asset(const char *data_root, const char *source_path,
                                            AssetBlob *out_blob, char *error, size_t error_size)
{
    char relative_path[256];
    char load_error[256];

    if (!source_path || !source_path[0]) {
        game_resources_set_error(error, error_size, "required source resource path is empty");
        return 0;
    }
    if (!game_link_resolve_staged_path(source_path, relative_path, sizeof(relative_path),
                                       error, error_size)) {
        return 0;
    }
    if (!asset_io_load(data_root, relative_path, out_blob, load_error, sizeof(load_error))) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "failed to load source resource %s: %s",
                           source_path, load_error);
        }
        return 0;
    }
    return 1;
}

static int game_resources_append_extension(const char *source_path, const char *extension,
                                           char *out_path, size_t out_path_size,
                                           char *error, size_t error_size)
{
    int written;

    if (!source_path || !source_path[0] || !extension || !extension[0]) {
        game_resources_set_error(error, error_size, "source resource extension inputs are empty");
        return 0;
    }
    written = snprintf(out_path, out_path_size, "%s%s", source_path, extension);
    if (written < 0 || (size_t)written >= out_path_size) {
        game_resources_set_error(error, error_size, "source resource path with extension is too long");
        return 0;
    }
    return 1;
}

static int game_resources_load_floor_and_textures(GameSharedResources *resources,
                                                   const GameLink *game_link,
                                                   const char *data_root,
                                                   char *error, size_t error_size)
{
    char source_path[256];
    char palette_path[256];

    /* modules/res.s:Res_LoadFloorsAndTextures. */
    if (!game_link_copy_floor_path(game_link, source_path, sizeof(source_path), error, error_size) ||
        !game_resources_load_volume_asset(data_root, source_path, &resources->floor_texture,
                                           error, error_size) ||
        !game_link_copy_texture_path(game_link, source_path, sizeof(source_path), error, error_size) ||
        !game_resources_load_volume_asset(data_root, source_path, &resources->texture_maps,
                                           error, error_size) ||
        !game_resources_append_extension(source_path, ".pal", palette_path, sizeof(palette_path),
                                         error, error_size) ||
        !game_resources_load_volume_asset(data_root, palette_path, &resources->texture_palette,
                                           error, error_size)) {
        return 0;
    }
    return 1;
}

static int game_resources_load_wall_textures(GameSharedResources *resources,
                                             const GameLink *game_link,
                                             const char *data_root,
                                             char *error, size_t error_size)
{
    uint16_t index;
    char source_path[128];

    /* modules/res.s:Res_LoadWallTextures stops at the first empty GLFT entry. */
    for (index = 0; index < GAME_LINK_WALL_COUNT; ++index) {
        if (!game_link_copy_wall_graphics_path(game_link, index, source_path,
                                                sizeof(source_path), error, error_size)) {
            return 0;
        }
        if (!source_path[0]) {
            resources->wall_texture_count = index;
            return 1;
        }
        if (!game_resources_load_volume_asset(data_root, source_path,
                                              &resources->wall_textures[index],
                                              error, error_size)) {
            return 0;
        }
    }
    resources->wall_texture_count = GAME_LINK_WALL_COUNT;
    return 1;
}

static int game_resources_load_objects(GameSharedResources *resources,
                                       const GameLink *game_link, const char *data_root,
                                       char *error, size_t error_size)
{
    uint16_t index;
    char source_path[128];
    char extended_path[160];

    /* modules/res.s:Res_LoadObjects loads WAD, PTR, and 256PAL per GLFT entry. */
    for (index = 0; index < GAME_LINK_OBJECT_COUNT; ++index) {
        if (!game_link_copy_object_graphics_path(game_link, index, source_path,
                                                  sizeof(source_path), error, error_size)) {
            return 0;
        }
        if (!source_path[0]) {
            resources->object_count = index;
            return 1;
        }
        if (!game_resources_append_extension(source_path, ".WAD", extended_path,
                                             sizeof(extended_path), error, error_size) ||
            !game_resources_load_volume_asset(data_root, extended_path,
                                              &resources->object_wads[index], error, error_size) ||
            !game_resources_append_extension(source_path, ".PTR", extended_path,
                                             sizeof(extended_path), error, error_size) ||
            !game_resources_load_volume_asset(data_root, extended_path,
                                              &resources->object_ptrs[index], error, error_size) ||
            !game_resources_append_extension(source_path, ".256PAL", extended_path,
                                             sizeof(extended_path), error, error_size) ||
            !game_resources_load_volume_asset(data_root, extended_path,
                                              &resources->object_palettes[index], error, error_size)) {
            return 0;
        }
    }
    resources->object_count = GAME_LINK_OBJECT_COUNT;
    return 1;
}

static int game_resources_load_vectors(GameSharedResources *resources,
                                       const GameLink *game_link, const char *data_root,
                                       char *error, size_t error_size)
{
    uint16_t index;
    char source_path[128];

    /* modules/res.s:Res_LoadObjects vector loop stops at the first empty entry. */
    for (index = 0; index < GAME_LINK_OBJECT_COUNT; ++index) {
        if (!game_link_copy_vector_path(game_link, index, source_path, sizeof(source_path),
                                        error, error_size)) {
            return 0;
        }
        if (!source_path[0]) {
            resources->vector_count = index;
            return 1;
        }
        if (!game_resources_load_volume_asset(data_root, source_path,
                                              &resources->vector_models[index],
                                              error, error_size)) {
            return 0;
        }
    }
    resources->vector_count = GAME_LINK_OBJECT_COUNT;
    return 1;
}

static int game_resources_load_sound_effects(GameSharedResources *resources,
                                             const GameLink *game_link,
                                             const char *data_root,
                                             char *error, size_t error_size)
{
    uint16_t index;
    char source_path[128];

    /* modules/res.s:Res_LoadSoundFx skips empty entries but retains slot indexes. */
    for (index = 0; index < GAME_LINK_SFX_LOAD_COUNT; ++index) {
        if (!game_link_copy_sfx_path(game_link, index, source_path, sizeof(source_path),
                                     error, error_size)) {
            return 0;
        }
        if (!source_path[0]) {
            continue;
        }
        if (!game_resources_load_volume_asset(data_root, source_path,
                                              &resources->sound_effects[index],
                                              error, error_size)) {
            return 0;
        }
        ++resources->sound_effect_count;
    }
    return 1;
}

void game_shared_resources_init(GameSharedResources *resources)
{
    if (resources) {
        memset(resources, 0, sizeof(*resources));
    }
}

void game_shared_resources_destroy(GameSharedResources *resources)
{
    uint16_t index;

    if (!resources) {
        return;
    }
    asset_blob_release(&resources->floor_texture);
    asset_blob_release(&resources->main_palette);
    asset_blob_release(&resources->texture_maps);
    asset_blob_release(&resources->texture_palette);
    asset_blob_release(&resources->bitmap_light_curve);
    asset_blob_release(&resources->backdrop_image);
    asset_blob_release(&resources->water_frames);
    for (index = 0; index < GAME_LINK_OBJECT_COUNT; ++index) {
        asset_blob_release(&resources->object_wads[index]);
        asset_blob_release(&resources->object_ptrs[index]);
        asset_blob_release(&resources->object_palettes[index]);
        asset_blob_release(&resources->vector_models[index]);
    }
    for (index = 0; index < GAME_LINK_WALL_COUNT; ++index) {
        asset_blob_release(&resources->wall_textures[index]);
    }
    for (index = 0; index < GAME_LINK_SFX_LOAD_COUNT; ++index) {
        asset_blob_release(&resources->sound_effects[index]);
    }
    memset(resources, 0, sizeof(*resources));
}

int game_shared_resources_load(GameSharedResources *resources, const GameLink *game_link,
                               const char *data_root, char *error, size_t error_size)
{
    /* controlloop.s:Game_Start queues these in this order before IO_FlushQueue. */
    if (!resources || !game_link || !data_root) {
        game_resources_set_error(error, error_size, "shared resource loader received null state");
        return 0;
    }
    game_shared_resources_destroy(resources);
    if (!asset_io_load(data_root, "includes/256pal", &resources->main_palette, error, error_size) ||
        !game_resources_load_sound_effects(resources, game_link, data_root, error, error_size) ||
        !game_resources_load_wall_textures(resources, game_link, data_root, error, error_size) ||
        !game_resources_load_floor_and_textures(resources, game_link, data_root, error, error_size) ||
        !asset_io_load(data_root, "includes/guff", &resources->bitmap_light_curve,
                       error, error_size) ||
        !game_resources_load_objects(resources, game_link, data_root, error, error_size) ||
        !game_resources_load_vectors(resources, game_link, data_root, error, error_size) ||
        /* data/draw_data.s:draw_BackdropImageName_vb, queued in Game_Start. */
        !game_resources_load_volume_asset(data_root, "ab3:includes/rawbackpacked",
                                          &resources->backdrop_image, error, error_size) ||
        /* data/draw_data.s:draw_WaterFrames_vb. */
        !asset_io_load(data_root, "includes/waterfile", &resources->water_frames,
                       error, error_size)) {
        game_shared_resources_destroy(resources);
        return 0;
    }
    if (resources->main_palette.size != 256u * 3u * sizeof(uint16_t)) {
        game_resources_set_error(error, error_size, "source 256pal has an invalid byte count");
        game_shared_resources_destroy(resources);
        return 0;
    }
    if (resources->bitmap_light_curve.size != 16u * 7u * 16u ||
        resources->backdrop_image.size != 648u * 240u ||
        resources->water_frames.size != 256u * 256u) {
        game_resources_set_error(error, error_size,
                                 "source bitmap-light, backdrop, or water asset has an invalid byte count");
        game_shared_resources_destroy(resources);
        return 0;
    }
    return 1;
}
