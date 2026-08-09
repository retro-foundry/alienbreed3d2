#include "game_bootstrap.h"

#include <stdio.h>
#include <string.h>

#define AB3D2_LEVEL_COUNT 16u

static void game_bootstrap_release_level(GameBootstrap *game)
{
    asset_blob_release(&game->level_map);
    asset_blob_release(&game->level_fly_map);
    asset_blob_release(&game->level_music);
    asset_blob_release(&game->level_data);
    asset_blob_release(&game->level_graphics);
    asset_blob_release(&game->level_clips);
    asset_blob_release(&game->level_floor_override);
    asset_blob_release(&game->level_property_overrides);
    asset_blob_release(&game->level_errata);
    for (uint16_t wall_index = 0; wall_index < GAME_LINK_WALL_COUNT; ++wall_index) {
        asset_blob_release(&game->level_wall_overrides[wall_index]);
    }
    memset(&game->level, 0, sizeof(game->level));
    memset(&game->level_graphics_header, 0, sizeof(game->level_graphics_header));
    memset(&game->level_runtime, 0, sizeof(game->level_runtime));
}

static int game_bootstrap_load_level_file(const char *data_root, const char *level_directory,
                                          const char *file_name, AssetBlob *out_blob,
                                          char *error, size_t error_size)
{
    char relative_path[128];
    char load_error[256];
    int written = snprintf(relative_path, sizeof(relative_path), "%s/%s",
                           level_directory, file_name);

    if (written < 0 || (size_t)written >= sizeof(relative_path)) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "level asset path is too long");
        }
        return 0;
    }
    if (asset_io_load(data_root, relative_path, out_blob, load_error, sizeof(load_error))) {
        return 1;
    }
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "failed to load %s: %s", relative_path, load_error);
    }
    return 0;
}

static int game_bootstrap_load_optional_level_file(const char *data_root,
                                                   const char *level_directory,
                                                   const char *file_name, AssetBlob *out_blob,
                                                   char *error, size_t error_size)
{
    char relative_path[128];
    char load_error[256];
    int found;
    int written = snprintf(relative_path, sizeof(relative_path), "%s/%s",
                           level_directory, file_name);

    if (written < 0 || (size_t)written >= sizeof(relative_path)) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "optional level asset path is too long");
        }
        return 0;
    }
    if (!asset_io_load_optional(data_root, relative_path, out_blob, &found,
                                load_error, sizeof(load_error))) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "failed to load optional %s: %s",
                           relative_path, load_error);
        }
        return 0;
    }
    return 1;
}

int game_bootstrap_init(GameBootstrap *game, const char *data_root,
                        char *error, size_t error_size)
{
    if (!game) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "game bootstrap pointer is null");
        }
        return 0;
    }
    memset(game, 0, sizeof(*game));

    /* controlloop.s:Game_Start: GLF_DatabaseName_vb */
    if (!asset_io_load(data_root, "includes/test.lnk", &game->game_link, error, error_size)) {
        goto fail;
    }
    if (!game_link_init(&game->game_link, &game->game_link_catalog, error, error_size)) {
        goto fail;
    }
    /* controlloop.s:Game_Start: Game_StoryFile_vb */
    if (!asset_io_load(data_root, "includes/text_file", &game->story_text, error, error_size)) {
        goto fail;
    }
    /* controlloop.s:Game_Start queues Res_LoadSoundFx through Res_LoadObjects. */
    if (!game_shared_resources_load(&game->shared_resources, &game->game_link_catalog,
                                    data_root, error, error_size)) {
        goto fail;
    }
    /* controlloop.s:DEFAULTGAME returns to the single-player menu at level A. */
    if (!game_session_default(&game->session, &game->game_link_catalog, error, error_size)) {
        goto fail;
    }
    return 1;

fail:
    game_bootstrap_destroy(game);
    return 0;
}

int game_bootstrap_start_selected_single_player(GameBootstrap *game, const char *data_root,
                                                char *error, size_t error_size)
{
    uint16_t level_index;

    if (!game || !data_root) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size,
                           "single-player start received null state or data root");
        }
        return 0;
    }
    /* game_ReadMainMenu:playgame then game_DoneMenu's Plr_ -> Plr1 copy. */
    game_session_begin_single_player(&game->session);
    level_index = game->session.active_level_index;
    return game_bootstrap_load_level(game, data_root, level_index, error, error_size);
}

int game_bootstrap_load_level(GameBootstrap *game, const char *data_root,
                              uint16_t level_index, char *error, size_t error_size)
{
    char level_directory[32];
    char wall_file_name[32];
    char source_music_path[64];
    char staged_music_path[64];
    uint16_t wall_index;
    int written;

    if (!game || !data_root) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "level bootstrap received null state or data root");
        }
        return 0;
    }
    if (level_index >= AB3D2_LEVEL_COUNT) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "requested level index %u is outside the 16-level campaign",
                           level_index);
        }
        return 0;
    }

    /*
     * SETPLAYERS writes lowercase a..p into the Amiga path string. The source
     * media directories are staged as lower-case, so the native path preserves
     * the same index mapping while remaining valid on case-sensitive hosts.
     */
    written = snprintf(level_directory, sizeof(level_directory), "levels/level_%c",
                       (char)('a' + level_index));
    if (written < 0 || (size_t)written >= sizeof(level_directory)) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "level directory name is too long");
        }
        return 0;
    }

    game_bootstrap_release_level(game);

    /* modules/res.s:Res_LoadLevelData reads GLFT_LevelMusic_l before twolev.bin. */
    if (!game_link_copy_level_music_path(&game->game_link_catalog, level_index,
                                         source_music_path, sizeof(source_music_path),
                                         error, error_size) ||
        !game_link_resolve_staged_path(source_music_path, staged_music_path,
                                       sizeof(staged_music_path), error, error_size)) {
        return 0;
    }
    if (!game_bootstrap_load_level_file(data_root, level_directory, "twolev.map",
                                        &game->level_map, error, error_size) ||
        !game_bootstrap_load_level_file(data_root, level_directory, "twolev.flymap",
                                        &game->level_fly_map, error, error_size) ||
        !asset_io_load(data_root, staged_music_path, &game->level_music, error, error_size) ||
        !game_bootstrap_load_level_file(data_root, level_directory, "twolev.bin",
                                        &game->level_data, error, error_size) ||
        !game_bootstrap_load_level_file(data_root, level_directory, "twolev.graph.bin",
                                        &game->level_graphics, error, error_size) ||
        !game_bootstrap_load_level_file(data_root, level_directory, "twolev.clips",
                                        &game->level_clips, error, error_size) ||
        !game_bootstrap_load_optional_level_file(data_root, level_directory, "floortile",
                                                 &game->level_floor_override, error, error_size) ||
        !game_bootstrap_load_optional_level_file(data_root, level_directory, "properties.dat",
                                                 &game->level_property_overrides, error, error_size) ||
        !game_bootstrap_load_optional_level_file(data_root, level_directory, "errata.dat",
                                                 &game->level_errata, error, error_size)) {
        game_bootstrap_release_level(game);
        return 0;
    }
    /* Res_LoadLevelData tests wall_0.256wad through wall_F.256wad in order. */
    for (wall_index = 0; wall_index < GAME_LINK_WALL_COUNT; ++wall_index) {
        written = snprintf(wall_file_name, sizeof(wall_file_name), "wall_%x.256wad", wall_index);
        if (written < 0 || (size_t)written >= sizeof(wall_file_name) ||
            !game_bootstrap_load_optional_level_file(data_root, level_directory, wall_file_name,
                                                     &game->level_wall_overrides[wall_index],
                                                     error, error_size)) {
            game_bootstrap_release_level(game);
            return 0;
        }
    }
    if (!level_bootstrap_parse(&game->level_data, &game->level, error, error_size) ||
        !level_graphics_bootstrap_parse(&game->level_graphics,
                                        &game->level_graphics_header, error, error_size) ||
        !level_runtime_init(&game->level_data, &game->level_graphics, &game->level,
                            &game->level_runtime, error, error_size)) {
        game_bootstrap_release_level(game);
        return 0;
    }

    game->active_level_index = level_index;
    return 1;
}

void game_bootstrap_destroy(GameBootstrap *game)
{
    if (!game) {
        return;
    }
    game_shared_resources_destroy(&game->shared_resources);
    asset_blob_release(&game->game_link);
    memset(&game->game_link_catalog, 0, sizeof(game->game_link_catalog));
    memset(&game->session, 0, sizeof(game->session));
    asset_blob_release(&game->story_text);
    game_bootstrap_release_level(game);
    game->active_level_index = 0;
}

int game_bootstrap_submit_diagnostic_frame(const GameBootstrap *game, SceneFrame *frame)
{
    static const char status[] = "AB3D2 PC: source assets loaded; GPU renderer pending";
    SceneCommand command;

    if (!game || !frame || game->game_link.size == 0 || game->story_text.size == 0) {
        return 0;
    }
    command.type = SCENE_COMMAND_HUD_TEXT;
    command.data.hud_text.text = status;
    command.data.hud_text.x = 0;
    command.data.hud_text.y = 0;
    command.data.hud_text.style_id = 0;
    return scene_frame_submit(frame, &command);
}
