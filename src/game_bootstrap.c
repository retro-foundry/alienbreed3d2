#include "game_bootstrap.h"

#include <stdio.h>
#include <string.h>

#include "object_handler.h"
#include "object_scene.h"
#include "player_entity.h"
#include "player_shoot.h"
#include "object_worry.h"

#define AB3D2_LEVEL_COUNT 16u

/* hireswall.s:Draw_Wall assigns Draw_ChunkPtr_l = Draw_PalettePtr_l + 64*32. */
#define AB3D2_WALL_PALETTE_BYTE_COUNT (64u * 32u)

static SceneMaterialSource game_bootstrap_floor_material_source(const GameBootstrap *game)
{
    /* Res_LoadLevelData selects the optional asset solely by its non-null pointer. */
    return game->level_floor_override.bytes != NULL ?
        SCENE_MATERIAL_SOURCE_LEVEL_FLOOR_TEXTURE_OVERRIDE :
        SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE;
}

static const AssetBlob *game_bootstrap_floor_material_asset(const GameBootstrap *game)
{
    return game->level_floor_override.bytes != NULL ?
        &game->level_floor_override : &game->shared_resources.floor_texture;
}

static SceneMaterialSource game_bootstrap_wall_material_source(const GameBootstrap *game,
                                                               uint32_t wall_texture_index)
{
    /* Res_LoadLevelData substitutes only the matching wall_N.256wad slot. */
    return wall_texture_index < GAME_LINK_WALL_COUNT &&
            game->level_wall_overrides[wall_texture_index].bytes != NULL ?
        SCENE_MATERIAL_SOURCE_LEVEL_WALL_TEXTURE_OVERRIDE :
        SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE;
}

static const AssetBlob *game_bootstrap_wall_material_asset(const GameBootstrap *game,
                                                           uint32_t wall_texture_index)
{
    if (wall_texture_index >= game->shared_resources.wall_texture_count) {
        return NULL;
    }
    return game->level_wall_overrides[wall_texture_index].bytes != NULL ?
        &game->level_wall_overrides[wall_texture_index] :
        &game->shared_resources.wall_textures[wall_texture_index];
}

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
    memset(&game->level_mechanisms, 0, sizeof(game->level_mechanisms));
    memset(&game->level_navigation, 0, sizeof(game->level_navigation));
    level_dynamic_state_destroy(&game->dynamic_level);
    mechanism_runtime_init(&game->mechanism_runtime);
    memset(&game->level_runtime, 0, sizeof(game->level_runtime));
    object_runtime_destroy(&game->object_runtime);
    object_observation_init(&game->object_observation);
    level_static_scene_destroy(&game->static_scene);
    memset(&game->player, 0, sizeof(game->player));
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
    int game_properties_found;

    if (!game) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "game bootstrap pointer is null");
        }
        return 0;
    }
    memset(game, 0, sizeof(*game));
    alien_runtime_init(&game->alien_runtime);

    /* c/game.c:Game_Init establishes default caps before optional game.props. */
    if (!asset_io_load_optional(data_root, "includes/game.props", &game->game_properties,
                                &game_properties_found, error, error_size) ||
        !game_inventory_decode_game_properties(game->game_properties.bytes,
                                               game->game_properties.size,
                                               &game->inventory_limits)) {
        goto fail;
    }
    /* controlloop.s:Game_Start: GLF_DatabaseName_vb */
    if (!asset_io_load(data_root, "includes/test.lnk", &game->game_link, error, error_size)) {
        goto fail;
    }
    if (!game_link_init(&game->game_link, &game->game_link_catalog, error, error_size)) {
        goto fail;
    }
    /* data/tables_data.s incbins this exact source table as SinCosTable_vw. */
    if (!asset_io_load(data_root, "includes/bigsine", &game->sine_table, error, error_size) ||
        !game_math_init(&game->sine_table, &game->math, error, error_size)) {
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
    game_controls_default(&game->controls);
    game_input_init(&game->input);
    game_preferences_default(&game->preferences);
    game_random_init(&game->random);
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
    alien_runtime_begin_single_player(&game->alien_runtime);
    level_index = game->session.active_level_index;
    return game_bootstrap_load_level(game, data_root, level_index, error, error_size);
}

int game_bootstrap_update_single_player(GameBootstrap *game,
                                        char *error, size_t error_size)
{
    LevelZone player_zone;

    if (!game || game->level_data.size == 0u) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size,
                           "single-player update requires a loaded source level");
        }
        return 0;
    }
    /* endlevel returns through controlloop only once per successful level. */
    if (game->session.level_finished != 0u) {
        return 1;
    }
    if (!player_runtime_update_discrete_controls(&game->player, &game->input,
                                                 &game->controls, &game->dynamic_level.runtime,
                                                 &game->session.player1_inventory,
                                                 error, error_size) ||
        !player_runtime_update_spatial(&game->player, &game->input, &game->controls,
                                       &game->preferences, &game->math,
                                       &game->dynamic_level.runtime, &game->dynamic_level,
                                       error, error_size) ||
        !player_entity_sync_single_player(&game->object_runtime, &game->dynamic_level.runtime,
                                          &game->game_link_catalog, &game->player,
                                          error, error_size) ||
        !player_entity_disable_second_for_single_player(&game->object_runtime,
                                                        error, error_size) ||
        !player_shoot_update_single_player(
            &game->object_runtime, &game->dynamic_level, &game->object_observation,
            &game->player, &game->session.player1_inventory, &game->game_link_catalog,
            &game->preferences, &game->math, &game->random, 1u, error, error_size) ||
        !object_handler_update_single_player(
            &game->object_runtime, &game->dynamic_level, &game->mechanism_runtime,
            &game->alien_runtime,
            &game->game_link_catalog,
            &game->player, &game->session.player1_inventory, &game->inventory_limits,
            1u, NULL, error, error_size) ||
        !mechanism_runtime_update_doors_single_player(
            &game->mechanism_runtime, &game->dynamic_level, &game->level_mechanisms,
            &game->player, 1u, error, error_size) ||
        !mechanism_runtime_update_lifts_single_player(
            &game->mechanism_runtime, &game->dynamic_level, &game->level_mechanisms,
            &game->player, 1u, error, error_size) ||
        !object_worry_update_single_player(
            &game->object_runtime, &game->dynamic_level.runtime, &game->player,
            &game->alien_runtime, error, error_size) ||
        !object_observation_update_single_player(
            &game->object_observation, &game->object_runtime, &game->player, &game->math,
            error, error_size) ||
        !level_static_scene_apply_runtime(
            &game->static_scene, &game->dynamic_level.runtime,
            game->shared_resources.wall_texture_count,
            game->level_floor_override.bytes != NULL ? game->level_floor_override.size :
                                                       game->shared_resources.floor_texture.size,
            error, error_size)) {
        return 0;
    }
    /* Game_AddToInventory changes Plr1_Inventory; health drives next control tick. */
    game->player.health = game->session.player1_inventory.health;
    /* hires.s compares Lvl_ExitZoneID_w with the current ZoneT_ID_w, not its index. */
    if (!level_runtime_get_zone(&game->dynamic_level.runtime, game->player.zone_index,
                                &player_zone, error, error_size)) {
        return 0;
    }
    if (game->dynamic_level.runtime.exit_zone_id >= 0 &&
        player_zone.id == (uint16_t)game->dynamic_level.runtime.exit_zone_id) {
        game_session_finish_single_player(&game->session, 1);
    }
    return 1;
}

int game_bootstrap_load_level_definition(GameBootstrap *game, const char *data_root,
                                         uint16_t selected_level_index,
                                         char *error, size_t error_size)
{
    AssetBlob definition = {0};
    char level_directory[32];
    char load_error[256];
    int found;
    int written;

    if (!game || !data_root) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size,
                           "level definition load received null state or data root");
        }
        return 0;
    }
    if (selected_level_index >= AB3D2_LEVEL_COUNT) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size,
                           "level definition selection %u is outside the 16-level campaign",
                           selected_level_index);
        }
        return 0;
    }
    written = snprintf(level_directory, sizeof(level_directory),
                       "levels/level_%c/deflev.dat", (char)('a' + selected_level_index));
    if (written < 0 || (size_t)written >= sizeof(level_directory)) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "level definition path is too long");
        }
        return 0;
    }
    if (!asset_io_load_optional(data_root, level_directory, &definition, &found,
                                load_error, sizeof(load_error))) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "failed to load %s: %s",
                           level_directory, load_error);
        }
        return 0;
    }
    if (!game_session_load_level_definition(&game->session, &game->game_link_catalog,
                                            definition.bytes, definition.size, found,
                                            error, error_size)) {
        asset_blob_release(&definition);
        return 0;
    }
    asset_blob_release(&definition);
    return 1;
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
        !level_mechanisms_init(&game->level_graphics, &game->level_graphics_header,
                               &game->level_mechanisms, error, error_size) ||
        !level_navigation_init(&game->level_map, &game->level_fly_map,
                               &game->level_navigation, error, error_size) ||
        !level_runtime_init(&game->level_data, &game->level_graphics, &game->level,
                            &game->level_graphics_header, &game->level_runtime,
                            error, error_size) ||
        !level_dynamic_state_init(&game->dynamic_level, &game->level_runtime,
                                  error, error_size) ||
        !object_runtime_init(&game->object_runtime, &game->level_runtime,
                             error, error_size) ||
        !player_runtime_init_single_player(&game->level, &game->dynamic_level.runtime,
                                           &game->player,
                                           error, error_size) ||
        !object_observation_update_single_player(
            &game->object_observation, &game->object_runtime, &game->player, &game->math,
            error, error_size) ||
        !level_static_scene_build(&game->level_runtime,
                                  game->shared_resources.wall_texture_count,
                                  game->level_floor_override.bytes != NULL
                                      ? game->level_floor_override.size
                                      : game->shared_resources.floor_texture.size,
                                  &game->static_scene, error, error_size)) {
        game_bootstrap_release_level(game);
        return 0;
    }
    mechanism_runtime_init(&game->mechanism_runtime);

    /* hires.s:Game_Begin initializes this before objmoveanim can dispatch AI. */
    alien_runtime_begin_level(&game->alien_runtime);

    /* game_DoneMenu copies the selected single-player inventory before Game_Begin. */
    game->player.health = game->session.player1_inventory.health;

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
    asset_blob_release(&game->game_properties);
    memset(&game->inventory_limits, 0, sizeof(game->inventory_limits));
    asset_blob_release(&game->sine_table);
    memset(&game->math, 0, sizeof(game->math));
    alien_runtime_init(&game->alien_runtime);
    memset(&game->session, 0, sizeof(game->session));
    memset(&game->preferences, 0, sizeof(game->preferences));
    asset_blob_release(&game->story_text);
    game_bootstrap_release_level(game);
    game->active_level_index = 0;
}

int game_bootstrap_submit_diagnostic_frame(const GameBootstrap *game, SceneFrame *frame)
{
    static const char menu_status[] = "AB3D2 PC: single-player menu state ready; GPU renderer pending";
    static const char level_status[] = "AB3D2 PC: source level loaded; GPU renderer pending";
    SceneCommand command;
    size_t primitive_count;
    uint32_t sprite_count;
    size_t required_commands;

    if (!game || !frame || game->game_link.size == 0 || game->story_text.size == 0) {
        return 0;
    }
    if (game->static_scene.flat_count > SIZE_MAX - game->static_scene.wall_count) {
        return 0;
    }
    primitive_count = (size_t)game->static_scene.wall_count + game->static_scene.flat_count;
    if (!object_scene_count_active(&game->object_runtime, &sprite_count, NULL, 0u) ||
        primitive_count > (SIZE_MAX - 2u) / 2u ||
        sprite_count > SIZE_MAX - (2u + primitive_count * 2u)) {
        return 0;
    }
    required_commands = 2u + primitive_count * 2u + sprite_count;
    if (!scene_frame_reserve(frame, required_commands)) {
        return 0;
    }
    if (game->level_data.size != 0) {
        command.type = SCENE_COMMAND_CAMERA;
        command.data.camera.position.x = game->player.x;
        command.data.camera.position.y = game->player.y;
        command.data.camera.position.z = game->player.z;
        command.data.camera.yaw = game->player.yaw;
        command.data.camera.look_offset = game->player.look_offset;
        if (!scene_frame_submit(frame, &command)) {
            return 0;
        }
        for (uint32_t wall_index = 0u; wall_index < game->static_scene.wall_count;
             ++wall_index) {
            const LevelStaticWallScene *wall = &game->static_scene.walls[wall_index];
            const AssetBlob *material_asset =
                game_bootstrap_wall_material_asset(game, wall->material_id);

            if (!material_asset || !material_asset->bytes ||
                material_asset->size < AB3D2_WALL_PALETTE_BYTE_COUNT) {
                return 0;
            }

            command.type = SCENE_COMMAND_MATERIAL;
            command.data.material.source =
                game_bootstrap_wall_material_source(game, wall->material_id);
            command.data.material.source_asset_id = wall->material_id;
            command.data.material.source_bytes = material_asset->bytes;
            command.data.material.source_byte_count = material_asset->size;
            command.data.material.source_palette_bytes = material_asset->bytes;
            command.data.material.source_palette_byte_count = AB3D2_WALL_PALETTE_BYTE_COUNT;
            if (!scene_frame_submit(frame, &command)) {
                return 0;
            }
            command.type = SCENE_COMMAND_GEOMETRY;
            command.data.geometry.vertices = wall->vertices;
            command.data.geometry.vertex_count = 6u;
            command.data.geometry.topology = SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
            command.data.geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_WALL;
            command.data.geometry.material_id = wall->material_id;
            command.data.geometry.source_record_id = wall->source_record_offset;
            command.data.geometry.flags = SCENE_GEOMETRY_TEXTURE_COORDS_UNRESOLVED;
            if (!scene_frame_submit(frame, &command)) {
                return 0;
            }
        }
        for (uint32_t flat_index = 0u; flat_index < game->static_scene.flat_count;
             ++flat_index) {
            const LevelStaticFlatScene *flat = &game->static_scene.flats[flat_index];
            const AssetBlob *material_asset = game_bootstrap_floor_material_asset(game);

            if (!material_asset || !material_asset->bytes ||
                !game->shared_resources.texture_palette.bytes) {
                return 0;
            }

            command.type = SCENE_COMMAND_MATERIAL;
            command.data.material.source = game_bootstrap_floor_material_source(game);
            command.data.material.source_asset_id = flat->material_id;
            command.data.material.source_bytes = material_asset->bytes;
            command.data.material.source_byte_count = material_asset->size;
            /* Res_LoadFloorsAndTextures loads this .pal once for all floor overrides. */
            command.data.material.source_palette_bytes = game->shared_resources.texture_palette.bytes;
            command.data.material.source_palette_byte_count =
                game->shared_resources.texture_palette.size;
            if (!scene_frame_submit(frame, &command)) {
                return 0;
            }
            command.type = SCENE_COMMAND_GEOMETRY;
            command.data.geometry.vertices = flat->vertices;
            command.data.geometry.vertex_count = flat->vertex_count;
            command.data.geometry.topology = SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY;
            command.data.geometry.primitive = flat->primitive;
            command.data.geometry.material_id = flat->material_id;
            command.data.geometry.source_record_id = flat->source_record_offset;
            command.data.geometry.flags = SCENE_GEOMETRY_TEXTURE_COORDS_UNRESOLVED;
            if (!scene_frame_submit(frame, &command)) {
                return 0;
            }
        }
        if (!object_scene_submit_active(&game->object_runtime, &game->game_link_catalog,
                                        &game->shared_resources, frame, NULL, 0u)) {
            return 0;
        }
    }
    command.type = SCENE_COMMAND_HUD_TEXT;
    command.data.hud_text.text = game->level_data.size != 0 ? level_status : menu_status;
    command.data.hud_text.x = 0;
    command.data.hud_text.y = 0;
    command.data.hud_text.style_id = 0;
    return scene_frame_submit(frame, &command);
}
