#include "game_bootstrap.h"

#include "game_hud.h"

#include <stdio.h>
#include <limits.h>
#include <string.h>

#include "object_handler.h"
#include "object_scene.h"
#include "object_animation.h"
#include "player_entity.h"
#include "player_shoot.h"
#include "object_worry.h"

#define AB3D2_LEVEL_COUNT 16u

/* hireswall.s:Draw_Wall assigns Draw_ChunkPtr_l = Draw_PalettePtr_l + 64*32. */
#define AB3D2_WALL_PALETTE_BYTE_COUNT (64u * 32u)

enum {
    /* One immutable complete-level BLAS candidate. */
    GAME_BOOTSTRAP_STATIC_WORLD_MESH_ID = 1u,
    /* Controller groups are emitted as independent dynamic BLAS candidates. */
    GAME_BOOTSTRAP_DYNAMIC_MESH_ID_BASE = UINT32_C(0x40000000),
    GAME_BOOTSTRAP_DYNAMIC_MESH_GROUP_CAPACITY =
        LEVEL_MECHANISMS_MAX_DOORS + LEVEL_MECHANISMS_MAX_LIFTS +
        LEVEL_MECHANISMS_WATER_ANIMATION_COUNT
};

typedef struct {
    uint8_t kind;
    uint16_t index;
    uint32_t surface_count;
} GameBootstrapDynamicMeshGroup;

static void game_bootstrap_apply_desktop_inventory_options(GameBootstrap *game)
{
    GameInventory *inventory;

    if (!game) {
        return;
    }
    inventory = &game->session.player1_inventory;
    if (game->desktop_settings.all_weapons != 0u) {
        uint16_t index;

        /* Match the first port's all_weapons setting: every source gun owns
         * a full legal supply of its source ammunition class. */
        for (index = 0u; index < GAME_INVENTORY_WEAPON_COUNT; ++index) {
            inventory->weapons[index] = UINT16_MAX;
        }
        for (index = 0u; index < GAME_INVENTORY_AMMUNITION_COUNT; ++index) {
            inventory->ammunition[index] = game->inventory_limits.ammunition[index];
        }
    }
    if (game->desktop_settings.infinite_health != 0u) {
        inventory->health = game->inventory_limits.health;
    }
}

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

static uint32_t game_bootstrap_dynamic_mesh_id(uint8_t kind, uint16_t index)
{
    return GAME_BOOTSTRAP_DYNAMIC_MESH_ID_BASE |
        ((uint32_t)kind << 16u) | (uint32_t)index;
}

static int game_bootstrap_make_wall_surface(const GameBootstrap *game,
                                            const LevelStaticWallScene *wall,
                                            SceneMeshSurface *out_surface)
{
    const AssetBlob *material_asset;

    if (!game || !wall || !out_surface) {
        return 0;
    }
    material_asset = game_bootstrap_wall_material_asset(game, wall->material_id);
    if (!material_asset || !material_asset->bytes ||
        material_asset->size < AB3D2_WALL_PALETTE_BYTE_COUNT ||
        !game->shared_resources.main_palette.bytes) {
        return 0;
    }
    memset(out_surface, 0, sizeof(*out_surface));
    out_surface->material.source = game_bootstrap_wall_material_source(game, wall->material_id);
    out_surface->material.source_asset_id = wall->material_id;
    out_surface->material.source_bytes = material_asset->bytes;
    out_surface->material.source_byte_count = material_asset->size;
    out_surface->material.source_palette_bytes = material_asset->bytes;
    out_surface->material.source_palette_byte_count = AB3D2_WALL_PALETTE_BYTE_COUNT;
    out_surface->material.source_display_palette_bytes = game->shared_resources.main_palette.bytes;
    out_surface->material.source_display_palette_byte_count =
        game->shared_resources.main_palette.size;
    out_surface->geometry.vertices = wall->vertices;
    out_surface->geometry.vertex_count = 6u;
    out_surface->geometry.topology = SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
    out_surface->geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_WALL;
    out_surface->geometry.material_id = wall->material_id;
    out_surface->geometry.source_record_id = wall->source_record_offset;
    out_surface->geometry.texture_window = wall->texture_window;
    out_surface->geometry.source_zone_index = wall->source_zone_index;
    out_surface->geometry.source_upper_zone = wall->source_upper_zone;
    return 1;
}

static int game_bootstrap_make_flat_surface(const GameBootstrap *game,
                                            const LevelStaticFlatScene *flat,
                                            SceneMeshSurface *out_surface)
{
    const AssetBlob *material_asset;

    if (!game || !flat || !out_surface) {
        return 0;
    }
    material_asset = game_bootstrap_floor_material_asset(game);
    if (!material_asset || !material_asset->bytes ||
        !game->shared_resources.texture_palette.bytes ||
        !game->shared_resources.main_palette.bytes) {
        return 0;
    }
    memset(out_surface, 0, sizeof(*out_surface));
    out_surface->material.source = game_bootstrap_floor_material_source(game);
    out_surface->material.source_asset_id = flat->material_id;
    out_surface->material.source_bytes = material_asset->bytes;
    out_surface->material.source_byte_count = material_asset->size;
    out_surface->material.source_palette_bytes = game->shared_resources.texture_palette.bytes;
    out_surface->material.source_palette_byte_count = game->shared_resources.texture_palette.size;
    out_surface->material.source_display_palette_bytes = game->shared_resources.main_palette.bytes;
    out_surface->material.source_display_palette_byte_count =
        game->shared_resources.main_palette.size;
    out_surface->geometry.vertices = flat->vertices;
    out_surface->geometry.vertex_count = flat->vertex_count;
    out_surface->geometry.topology = SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY;
    out_surface->geometry.primitive = flat->primitive;
    out_surface->geometry.material_id = flat->material_id;
    out_surface->geometry.source_record_id = flat->source_record_offset;
    out_surface->geometry.source_zone_index = flat->source_zone_index;
    out_surface->geometry.source_upper_zone = flat->source_upper_zone;
    return 1;
}

static int game_bootstrap_add_dynamic_mesh_group(
    GameBootstrapDynamicMeshGroup *groups, uint32_t *group_count,
    uint8_t kind, uint16_t index)
{
    if (!groups || !group_count || kind == LEVEL_STATIC_DYNAMIC_SURFACE_NONE) {
        return 0;
    }
    for (uint32_t group_index = 0u; group_index < *group_count; ++group_index) {
        if (groups[group_index].kind == kind && groups[group_index].index == index) {
            if (groups[group_index].surface_count == UINT32_MAX) {
                return 0;
            }
            ++groups[group_index].surface_count;
            return 1;
        }
    }
    if (*group_count >= GAME_BOOTSTRAP_DYNAMIC_MESH_GROUP_CAPACITY) {
        return 0;
    }
    groups[*group_count].kind = kind;
    groups[*group_count].index = index;
    groups[*group_count].surface_count = 1u;
    ++*group_count;
    return 1;
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
    memset(&game->message_runtime, 0, sizeof(game->message_runtime));
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
    /* data/draw_data.s incbins this table for c/message.c's proportional splitting. */
    if (!asset_io_load(data_root, "includes/glyph_spacing.bin", &game->glyph_spacing,
                       error, error_size)) {
        goto fail;
    }
    if (game->glyph_spacing.size != MESSAGE_RUNTIME_GLYPH_SPACING_BYTE_COUNT) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size,
                           "glyph spacing table is not the source 256-byte payload");
        }
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
    game_audio_events_init(&game->audio_events);
    game_background_audio_runtime_init(&game->background_audio_runtime);
    game_input_init(&game->input);
    game_preferences_default(&game->preferences);
    desktop_settings_default(&game->desktop_settings);
    game_progression_init(&game->progression);
    game_random_init(&game->random);
    object_animation_runtime_init(&game->object_animation_runtime);
    lighting_runtime_init(&game->lighting_runtime);
    object_explosion_runtime_init(&game->object_explosion_runtime);
    player_hazard_runtime_init(&game->player_hazard_runtime);
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
    game_bootstrap_apply_desktop_inventory_options(game);
    alien_runtime_begin_single_player(&game->alien_runtime);
    game->message_time_milliseconds = 0u;
    level_index = game->session.active_level_index;
    return game_bootstrap_load_level(game, data_root, level_index, error, error_size);
}

void game_bootstrap_apply_desktop_settings(GameBootstrap *game,
                                           const DesktopSettings *settings)
{
    if (!game || !settings) {
        return;
    }
    game->desktop_settings = *settings;
    /* controlloop.s custom option one is the source's run-default switch. */
    game->preferences.always_run = settings->always_run != 0u ? UINT8_MAX : 0u;
}

int game_bootstrap_update_single_player_at_time(GameBootstrap *game,
                                                uint64_t message_time_milliseconds,
                                                char *error, size_t error_size)
{
    LevelZone player_zone;
    ObjectHandlerAlienContext alien_context;
    PlayerObjectCollisionContext player_collision;
    uint8_t *player_slot;

    if (!game || game->level_data.size == 0u) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size,
                           "single-player update requires a loaded source level");
        }
        return 0;
    }
    /* endlevel returns through controlloop only once per successful level. */
    if (game->session.level_ended != 0u) {
        return 1;
    }
    game->message_time_milliseconds = message_time_milliseconds;
    /*
     * c/screen.c:Vid_Present calls Msg_Tick after drawing the prior source
     * frame. Apply that mutation on entry to the next 50 Hz update, before
     * this frame's gameplay can push replacement text.
     */
    if (!message_runtime_tick(&game->message_runtime,
                              game->preferences.show_messages,
                              game->message_time_milliseconds,
                              error, error_size)) {
        return 0;
    }
    game_audio_events_begin(&game->audio_events);
    player_collision.objects = &game->object_runtime;
    player_collision.source_a2_words = &game->alien_runtime.team_workspace[0u][0u];
    player_collision.source_a2_word_count =
        ALIEN_RUNTIME_TEAM_COUNT * ALIEN_RUNTIME_WORKSPACE_WORD_COUNT;
    /* hires.s:VBlankInterrupt decrements Anim_Timer_w before frame work. */
    lighting_runtime_vblank(&game->lighting_runtime);
    /* hires.s:dosomething calls DOALLANIMS before its control/object work. */
    if (!object_animation_update_single_player_with_audio(
            &game->object_animation_runtime, &game->object_runtime,
            &game->game_link_catalog, &game->random, &game->object_observation,
            &game->audio_events,
            error, error_size) ||
        !level_runtime_get_zone(&game->dynamic_level.runtime, game->player.zone_index,
                                &player_zone, error, error_size)) {
        return 0;
    }
    if (!object_runtime_get_player1_slot_bytes(&game->object_runtime, &player_slot)) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size,
                           "dosomething floor damage has no Player 1 source entity");
        }
        return 0;
    }
    if (!player_hazard_runtime_update(
            &game->player_hazard_runtime, 1u, &game->player, &player_zone,
            &game->game_link_catalog, player_slot + 19u, error, error_size) ||
        !player_runtime_update_discrete_controls(&game->player, &game->input,
                                                 &game->controls, &game->dynamic_level.runtime,
                                                 &game->session.player1_inventory,
                                                 error, error_size) ||
        !player_runtime_update_spatial_with_motion_and_audio(
            &game->player, &game->input, &game->controls, &game->preferences, &game->math,
            &game->dynamic_level.runtime, &game->dynamic_level, &game->alien_runtime.motion,
            &player_collision,
            &game->session.player1_inventory, &game->game_link_catalog, &game->audio_events,
            error, error_size) ||
        !lighting_runtime_refresh_single_player(
            &game->lighting_runtime, &game->dynamic_level.runtime, &game->player,
            error, error_size) ||
        !lighting_runtime_refresh_all_zones(
            &game->lighting_runtime, &game->dynamic_level.runtime, error, error_size) ||
        !player_entity_sync_single_player(&game->object_runtime, &game->dynamic_level.runtime,
                                          &game->game_link_catalog, &game->player,
                                          &game->session.player1_inventory, &game->random,
                                          &game->audio_events,
                                          error, error_size) ||
        !player_entity_disable_second_for_single_player(&game->object_runtime,
                                                        error, error_size)) {
        return 0;
    }
    /* newanims.s:objmoveanim clears this immediately before Plr1_Shot. */
    game->player.noise_volume = 0;
    if (!level_runtime_get_zone(&game->dynamic_level.runtime, game->player.zone_index,
                                &player_zone, error, error_size) ||
        !game_background_audio_update(
            &game->background_audio_runtime, 1u, &player_zone,
            &game->game_link_catalog, &game->random, &game->audio_events,
            error, error_size)) {
        return 0;
    }
    alien_context.animation_runtime = &game->object_animation_runtime;
    alien_context.lighting_runtime = &game->lighting_runtime;
    alien_context.navigation = &game->level_navigation;
    alien_context.clips = &game->level_clips;
    alien_context.progression = &game->progression;
    alien_context.explosion_runtime = &game->object_explosion_runtime;
    alien_context.math = &game->math;
    alien_context.random = &game->random;
    /* ObjectHandler uses the source observation produced at the prior tick's tail. */
    alien_context.observation = &game->object_observation;
    alien_context.dispatch_workspace = &game->alien_dispatch_workspace;
    alien_context.messages = &game->message_runtime;
    alien_context.preferences = &game->preferences;
    alien_context.audio_events = &game->audio_events;
    alien_context.message_time_milliseconds = game->message_time_milliseconds;
    if (!player_shoot_update_single_player_with_motion_and_audio(
            &game->object_runtime, &game->dynamic_level, &game->object_observation,
            &game->player, &game->alien_runtime.motion, &game->session.player1_inventory,
            &game->game_link_catalog,
            &game->preferences, &game->math, &game->random, 1u,
            game->desktop_settings.infinite_ammo, &game->audio_events, error, error_size) ||
        !object_handler_update_single_player(
            &game->object_runtime, &game->dynamic_level, &game->mechanism_runtime,
            &game->alien_runtime,
            &game->game_link_catalog, &alien_context,
            &game->player, &game->session.player1_inventory, &game->inventory_limits,
            1u, NULL, error, error_size) ||
        !mechanism_runtime_update_doors_single_player_with_audio(
            &game->mechanism_runtime, &game->dynamic_level, &game->level_mechanisms,
            &game->player, &game->math, 1u, &game->audio_events, error, error_size) ||
        !mechanism_runtime_update_lifts_single_player_with_audio(
            &game->mechanism_runtime, &game->dynamic_level, &game->level_mechanisms,
            &game->player, &game->math, 1u, &game->audio_events, error, error_size)) {
        return 0;
    }
    /* newanims.s:objmoveanim advances brightanim after ObjectHandler/doors/lifts. */
    lighting_runtime_advance_animation(&game->lighting_runtime);
    if (!object_worry_update_single_player(
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
    /* The first port restores this PC option after the source damage/object phase. */
    if (game->desktop_settings.infinite_health != 0u) {
        game->session.player1_inventory.health = game->inventory_limits.health;
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
    } else if ((int16_t)game->player.health <= 0) {
        /* hires.s checks Player 1 death immediately after the exit-zone win. */
        game_session_finish_single_player(&game->session, 0);
    }
    /* hires.s:VBlankInterrupt advances water pointer/scroll once per source frame. */
    game->presentation_frame += 1u;
    return 1;
}

int game_bootstrap_update_single_player(GameBootstrap *game,
                                        char *error, size_t error_size)
{
    /* hires.s runs this work from PAL VBlank; retain a deterministic 20 ms test boundary. */
    if (!game || game->message_time_milliseconds > UINT64_MAX - 20u) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size,
                           "single-player message clock cannot advance one source VBlank");
        }
        return 0;
    }
    return game_bootstrap_update_single_player_at_time(
        game, game->message_time_milliseconds + 20u, error, error_size);
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
    if (!message_runtime_init(&game->message_runtime, game->level_data.bytes,
                              game->level_data.size, game->glyph_spacing.bytes,
                              game->glyph_spacing.size, error, error_size) ||
        !level_bootstrap_parse(&game->level_data, &game->level, error, error_size) ||
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
        !level_static_scene_build(&game->level_runtime, &game->level_mechanisms,
                                  game->shared_resources.wall_texture_count,
                                  game->level_floor_override.bytes != NULL
                                      ? game->level_floor_override.size
                                      : game->shared_resources.floor_texture.size,
                                  &game->static_scene, error, error_size)) {
        game_bootstrap_release_level(game);
        return 0;
    }
    mechanism_runtime_init(&game->mechanism_runtime);
    /* hires.s:Game_Begin resets timetodamage to 100 for every level. */
    player_hazard_runtime_init(&game->player_hazard_runtime);

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
    asset_blob_release(&game->glyph_spacing);
    alien_runtime_init(&game->alien_runtime);
    object_animation_runtime_destroy(&game->object_animation_runtime);
    lighting_runtime_init(&game->lighting_runtime);
    object_explosion_runtime_init(&game->object_explosion_runtime);
    player_hazard_runtime_init(&game->player_hazard_runtime);
    memset(&game->alien_dispatch_workspace, 0, sizeof(game->alien_dispatch_workspace));
    memset(&game->session, 0, sizeof(game->session));
    memset(&game->preferences, 0, sizeof(game->preferences));
    game_progression_init(&game->progression);
    game_background_audio_runtime_init(&game->background_audio_runtime);
    asset_blob_release(&game->story_text);
    game_bootstrap_release_level(game);
    game->active_level_index = 0;
}

static int16_t game_bootstrap_scene_clamp_light(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static int game_bootstrap_scene_wall_light(const GameBootstrap *game,
                                           const LevelStaticWallScene *wall,
                                           uint8_t point_selector, uint8_t use_top_selector,
                                           int16_t *out_light)
{
    uint8_t selector = use_top_selector != 0u ?
        (uint8_t)(wall->point_brightness_selector >> 4u) :
        (uint8_t)(wall->point_brightness_selector & 0x0fu);
    uint16_t zone_index = wall->source_zone_index;
    uint32_t source_index;
    int32_t source_light;

    /* hireswall.s:Draw_Wall selects current/other ZoneT via selector bit 3. */
    if ((selector & 0x08u) != 0u) {
        zone_index = (uint16_t)wall->other_zone;
    }
    source_index = (uint32_t)(selector & 0x07u) + (uint32_t)point_selector * 4u +
        (wall->source_upper_zone != 0u ? 2u : 0u);
    if (!out_light || zone_index >= game->dynamic_level.runtime.zone_count ||
        zone_index >= LIGHTING_RUNTIME_POINT_ZONE_CAPACITY ||
        source_index >= LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT) {
        return 0;
    }
    source_light = game->lighting_runtime.current_point_brightness[zone_index][source_index];
    if (source_light < 0) {
        source_light = -source_light;
    }
    *out_light = game_bootstrap_scene_clamp_light(source_light + wall->brightness_offset);
    return 1;
}

static int game_bootstrap_scene_flat_point_light(const GameBootstrap *game,
                                                 const LevelStaticFlatScene *flat,
                                                 uint8_t point_selector,
                                                 int16_t *out_light)
{
    uint32_t component_index;
    int16_t source_light;

    if (!game || !flat || !out_light ||
        point_selector >= LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT ||
        flat->source_zone_index >= game->dynamic_level.runtime.zone_count ||
        flat->source_zone_index >= LIGHTING_RUNTIME_POINT_ZONE_CAPACITY) {
        return 0;
    }
    /*
     * draw_zone_graph.s:itsafloor selects the lower/upper CurrentPointBrights
     * pair for the stream, then adds one word for a ceiling. hires.s:goursides
     * indexes that pair with the upper nibble from each source point word.
     */
    component_index = (flat->source_upper_zone != 0u ? 2u : 0u) +
        (flat->primitive == SCENE_GEOMETRY_PRIMITIVE_CEILING ? 1u : 0u);
    source_light = game->lighting_runtime.current_point_brightness[flat->source_zone_index]
                                                                    [point_selector * 4u +
                                                                     component_index];
    /* goursides uses NEG.W when a source point value is negative. */
    if (source_light < 0) {
        source_light = (int16_t)(UINT16_C(0) - (uint16_t)source_light);
    }
    *out_light = source_light;
    return 1;
}

static int game_bootstrap_refresh_scene_lighting(GameBootstrap *game)
{
    for (uint32_t wall_index = 0u; wall_index < game->static_scene.wall_count; ++wall_index) {
        LevelStaticWallScene *wall = &game->static_scene.walls[wall_index];
        int16_t left_top;
        int16_t right_top;
        int16_t left_bottom;
        int16_t right_bottom;

        if (!game_bootstrap_scene_wall_light(game, wall, wall->left_point_brightness, 1u,
                                             &left_top) ||
            !game_bootstrap_scene_wall_light(game, wall, wall->right_point_brightness, 1u,
                                             &right_top) ||
            !game_bootstrap_scene_wall_light(game, wall, wall->left_point_brightness, 0u,
                                             &left_bottom) ||
            !game_bootstrap_scene_wall_light(game, wall, wall->right_point_brightness, 0u,
                                             &right_bottom)) {
            return 0;
        }
        wall->vertices[0].source_light_level = left_top;
        wall->vertices[1].source_light_level = right_top;
        wall->vertices[2].source_light_level = right_bottom;
        wall->vertices[3].source_light_level = left_top;
        wall->vertices[4].source_light_level = right_bottom;
        wall->vertices[5].source_light_level = left_bottom;
    }
    for (uint32_t flat_index = 0u; flat_index < game->static_scene.flat_count; ++flat_index) {
        LevelStaticFlatScene *flat = &game->static_scene.flats[flat_index];

        if (flat->source_zone_index >= game->dynamic_level.runtime.zone_count ||
            flat->source_zone_index >= LIGHTING_RUNTIME_ZONE_BRIGHTNESS_CAPACITY) {
            return 0;
        }
        if (flat->primitive == SCENE_GEOMETRY_PRIMITIVE_WATER) {
            int32_t source_light = 300 +
                game->lighting_runtime.zone_brightness[flat->source_zone_index]
                                                       [flat->source_upper_zone != 0u ? 1u : 0u] +
                flat->brightness_offset;

            /* draw_zone_graph.s disables Gouraud lighting for Draw_Flats water. */
            for (uint32_t vertex_index = 0u; vertex_index < flat->vertex_count; ++vertex_index) {
                flat->vertices[vertex_index].source_light_level =
                    game_bootstrap_scene_clamp_light(source_light);
            }
        } else {
            if (!flat->point_brightness_selectors) {
                return 0;
            }
            for (uint32_t vertex_index = 0u; vertex_index < flat->vertex_count; ++vertex_index) {
                if (!game_bootstrap_scene_flat_point_light(
                        game, flat, flat->point_brightness_selectors[vertex_index],
                        &flat->vertices[vertex_index].source_light_level)) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int game_bootstrap_scene_sky_enabled(const GameBootstrap *game)
{
    LevelZone zone;
    size_t disable_byte;

    if (!level_runtime_get_zone(&game->dynamic_level.runtime, game->player.zone_index, &zone,
                                NULL, 0u) || zone.draw_backdrop == 0u) {
        return 0;
    }
    /* modules/level.s copies optional properties.dat to Zone_BackdropDisable_vb. */
    disable_byte = (size_t)game->player.zone_index >> 3u;
    return disable_byte >= game->level_property_overrides.size ||
           (game->level_property_overrides.bytes[disable_byte] &
            (uint8_t)(1u << (game->player.zone_index & 7u))) == 0u;
}

int game_bootstrap_submit_scene_frame(GameBootstrap *game, SceneFrame *frame)
{
    SceneCommand command;
    size_t primitive_count;
    size_t hud_command_count;
    uint32_t sprite_count;
    size_t required_commands;
    size_t static_surface_count = 0u;
    GameBootstrapDynamicMeshGroup dynamic_groups[
        GAME_BOOTSTRAP_DYNAMIC_MESH_GROUP_CAPACITY] = {{0}};
    uint32_t dynamic_group_count = 0u;

    if (!game || !frame || game->game_link.size == 0 || game->story_text.size == 0) {
        return 0;
    }
    if (game->static_scene.flat_count > SIZE_MAX - game->static_scene.wall_count) {
        return 0;
    }
    primitive_count = (size_t)game->static_scene.wall_count + game->static_scene.flat_count;
    hud_command_count = 2u + message_runtime_visible_line_count(&game->message_runtime);
    if (!object_scene_count_active(&game->object_runtime, &sprite_count, NULL, 0u) ||
        hud_command_count > SIZE_MAX - 3u ||
        primitive_count > (SIZE_MAX - 3u - hud_command_count) / 2u ||
        sprite_count > SIZE_MAX - (3u + hud_command_count + primitive_count * 2u)) {
        return 0;
    }
    required_commands = 3u + primitive_count * 2u + sprite_count + hud_command_count;
    if (!scene_frame_reserve(frame, required_commands)) {
        return 0;
    }
    if (game->level_data.size != 0) {
        for (uint32_t wall_index = 0u; wall_index < game->static_scene.wall_count;
             ++wall_index) {
            const LevelStaticWallScene *wall = &game->static_scene.walls[wall_index];

            if (wall->mechanism_kind == LEVEL_STATIC_WALL_MECHANISM_NONE) {
                ++static_surface_count;
            } else if (!game_bootstrap_add_dynamic_mesh_group(
                           dynamic_groups, &dynamic_group_count, wall->mechanism_kind,
                           wall->mechanism_index)) {
                return 0;
            }
        }
        for (uint32_t flat_index = 0u; flat_index < game->static_scene.flat_count;
             ++flat_index) {
            const LevelStaticFlatScene *flat = &game->static_scene.flats[flat_index];

            if (flat->dynamic_surface_kind == LEVEL_STATIC_DYNAMIC_SURFACE_NONE) {
                ++static_surface_count;
            } else if (!game_bootstrap_add_dynamic_mesh_group(
                           dynamic_groups, &dynamic_group_count, flat->dynamic_surface_kind,
                           flat->dynamic_surface_index)) {
                return 0;
            }
        }
        if (static_surface_count > UINT32_MAX ||
            !scene_frame_reserve_mesh_surfaces(frame, primitive_count)) {
            return 0;
        }
        if (!game_bootstrap_refresh_scene_lighting(game)) {
            return 0;
        }
        command.type = SCENE_COMMAND_CAMERA;
        command.data.camera.position.x = player_runtime_position_to_world(game->player.x);
        /*
         * hires.s installs this exact Plr1_YOff_l value in Plr_YOff_l before
         * drawing.  newplayershoot.s uses the same value for every launch
         * height, so the eye, projectile and camera-space weapon must retain
         * this shared (including bobbed) source coordinate frame.
         */
        command.data.camera.position.y = game->player.y;
        command.data.camera.position.z = player_runtime_position_to_world(game->player.z);
        command.data.camera.source_position_x_16_16 = game->player.presentation_x;
        command.data.camera.source_position_z_16_16 = game->player.presentation_z;
        command.data.camera.yaw = game->player.yaw;
        command.data.camera.look_offset = game->player.look_offset;
        command.data.camera.has_source_position_16_16 = UINT8_MAX;
        if (!scene_frame_submit(frame, &command)) {
            return 0;
        }
        command.type = SCENE_COMMAND_LIGHTING;
        command.data.lighting.current_point_brightness =
            &game->lighting_runtime.current_point_brightness[0][0];
        command.data.lighting.point_zone_capacity = LIGHTING_RUNTIME_POINT_ZONE_CAPACITY;
        command.data.lighting.point_brightness_count = LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT;
        command.data.lighting.zone_brightness = game->lighting_runtime.zone_brightness;
        command.data.lighting.zone_count = game->dynamic_level.runtime.zone_count;
        if (!scene_frame_submit(frame, &command)) {
            return 0;
        }
        command.type = SCENE_COMMAND_ENVIRONMENT;
        command.data.environment.sky_enabled = (uint8_t)game_bootstrap_scene_sky_enabled(game);
        command.data.environment.water_frame = (uint8_t)(game->presentation_frame & 7u);
        command.data.environment.reserved = 0u;
        command.data.environment.water_scroll = game->presentation_frame & UINT32_C(0x3fff3fff);
        command.data.environment.backdrop_bytes = game->shared_resources.backdrop_image.bytes;
        command.data.environment.backdrop_byte_count = game->shared_resources.backdrop_image.size;
        command.data.environment.water_bytes = game->shared_resources.water_frames.bytes;
        command.data.environment.water_byte_count = game->shared_resources.water_frames.size;
        command.data.environment.source_display_palette_bytes =
            game->shared_resources.main_palette.bytes;
        command.data.environment.source_display_palette_byte_count =
            game->shared_resources.main_palette.size;
        if (!scene_frame_submit(frame, &command)) {
            return 0;
        }
        if (static_surface_count != 0u) {
            SceneMeshSurface *surfaces = scene_frame_allocate_mesh_surfaces(
                frame, (uint32_t)static_surface_count);
            uint32_t surface_index = 0u;

            if (!surfaces) {
                return 0;
            }
            for (uint32_t wall_index = 0u; wall_index < game->static_scene.wall_count;
                 ++wall_index) {
                const LevelStaticWallScene *wall = &game->static_scene.walls[wall_index];

                if (wall->mechanism_kind == LEVEL_STATIC_WALL_MECHANISM_NONE &&
                    !game_bootstrap_make_wall_surface(game, wall, &surfaces[surface_index++])) {
                    return 0;
                }
            }
            for (uint32_t flat_index = 0u; flat_index < game->static_scene.flat_count;
                 ++flat_index) {
                const LevelStaticFlatScene *flat = &game->static_scene.flats[flat_index];

                if (flat->dynamic_surface_kind == LEVEL_STATIC_DYNAMIC_SURFACE_NONE &&
                    !game_bootstrap_make_flat_surface(game, flat, &surfaces[surface_index++])) {
                    return 0;
                }
            }
            if (surface_index != static_surface_count) {
                return 0;
            }
            memset(&command, 0, sizeof(command));
            command.type = SCENE_COMMAND_GEOMETRY_INSTANCE;
            command.data.geometry_instance.source_instance_id = GAME_BOOTSTRAP_STATIC_WORLD_MESH_ID;
            command.data.geometry_instance.mesh.source_mesh_id = GAME_BOOTSTRAP_STATIC_WORLD_MESH_ID;
            command.data.geometry_instance.mesh.acceleration_class = SCENE_ACCELERATION_CLASS_STATIC;
            command.data.geometry_instance.mesh.surfaces = surfaces;
            command.data.geometry_instance.mesh.surface_count = surface_index;
            if (!scene_frame_submit(frame, &command)) {
                return 0;
            }
        }
        for (uint32_t group_index = 0u; group_index < dynamic_group_count; ++group_index) {
            const GameBootstrapDynamicMeshGroup *group = &dynamic_groups[group_index];
            SceneMeshSurface *surfaces = scene_frame_allocate_mesh_surfaces(
                frame, group->surface_count);
            uint32_t surface_index = 0u;
            uint32_t source_mesh_id = game_bootstrap_dynamic_mesh_id(group->kind, group->index);

            if (!surfaces) {
                return 0;
            }
            for (uint32_t wall_index = 0u; wall_index < game->static_scene.wall_count;
                 ++wall_index) {
                const LevelStaticWallScene *wall = &game->static_scene.walls[wall_index];

                if (wall->mechanism_kind == group->kind &&
                    wall->mechanism_index == group->index &&
                    !game_bootstrap_make_wall_surface(game, wall, &surfaces[surface_index++])) {
                    return 0;
                }
            }
            for (uint32_t flat_index = 0u; flat_index < game->static_scene.flat_count;
                 ++flat_index) {
                const LevelStaticFlatScene *flat = &game->static_scene.flats[flat_index];

                if (flat->dynamic_surface_kind == group->kind &&
                    flat->dynamic_surface_index == group->index &&
                    !game_bootstrap_make_flat_surface(game, flat, &surfaces[surface_index++])) {
                    return 0;
                }
            }
            if (surface_index != group->surface_count) {
                return 0;
            }
            memset(&command, 0, sizeof(command));
            command.type = SCENE_COMMAND_GEOMETRY_INSTANCE;
            command.data.geometry_instance.source_instance_id = source_mesh_id;
            command.data.geometry_instance.mesh.source_mesh_id = source_mesh_id;
            command.data.geometry_instance.mesh.acceleration_class = SCENE_ACCELERATION_CLASS_DYNAMIC;
            command.data.geometry_instance.mesh.surfaces = surfaces;
            command.data.geometry_instance.mesh.surface_count = surface_index;
            if (!scene_frame_submit(frame, &command)) {
                return 0;
            }
        }
        if (!object_scene_submit_active(&game->object_runtime, &game->game_link_catalog,
                                        &game->shared_resources, &game->dynamic_level.runtime,
                                        &game->lighting_runtime, &game->math, &game->preferences,
                                        game->player.y, game->player.yaw,
                                        frame, NULL, 0u)) {
            return 0;
        }
        if (!message_runtime_submit_hud(&game->message_runtime, frame) ||
            !game_hud_submit_first_port_status(
                &game->player, &game->session.player1_inventory,
                &game->game_link_catalog, game->desktop_settings.infinite_ammo,
                frame, NULL, 0u)) {
            return 0;
        }
    }
    return 1;
}
