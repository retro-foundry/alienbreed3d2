#include <stdio.h>
#include <string.h>

#include "asset_io.h"
#include "game_bootstrap.h"
#include "game_link.h"
#include "level_bootstrap.h"
#include "scene_frame.h"

int main(int argc, char **argv)
{
    AssetBlob level_data = {0};
    LevelBootstrap level;
    AssetBlob graphics_data = {0};
    LevelGraphicsBootstrap graphics;
    SceneFrame frame;
    SceneCommand command;
    GameBootstrap game;
    GameLink game_link;
    AssetBlob game_link_blob = {0};
    const uint8_t *table_bytes;
    size_t table_size;
    uint16_t level_index;
    uint16_t zone_index;
    char text[128];
    LevelZone zone;
    char error[256];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <data-root>\n", argv[0]);
        return 2;
    }
    if (!asset_io_load(argv[1], "levels/level_a/twolev.bin", &level_data,
                       error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (!level_bootstrap_parse(&level_data, &level, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        asset_blob_release(&level_data);
        return 1;
    }
    if (level.zone_count == 0 || level.point_count == 0 ||
        level.player1_start_zone >= level.zone_count) {
        fprintf(stderr, "LEVEL_A TLBT values are inconsistent\n");
        asset_blob_release(&level_data);
        return 1;
    }

    if (!asset_io_load(argv[1], "levels/level_a/twolev.graph.bin", &graphics_data,
                       error, sizeof(error)) ||
        !level_graphics_bootstrap_parse(&graphics_data, &graphics, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        asset_blob_release(&graphics_data);
        asset_blob_release(&level_data);
        return 1;
    }
    if (graphics.zone_adds_table_offset != 16u) {
        fprintf(stderr, "LEVEL_A TLGT zone-table offset is inconsistent\n");
        asset_blob_release(&graphics_data);
        asset_blob_release(&level_data);
        return 1;
    }

    if (!scene_frame_init(&frame, 1)) {
        fprintf(stderr, "scene frame allocation failed\n");
        asset_blob_release(&graphics_data);
        asset_blob_release(&level_data);
        return 1;
    }
    command.type = SCENE_COMMAND_HUD_TEXT;
    command.data.hud_text.text = "test";
    command.data.hud_text.x = 0;
    command.data.hud_text.y = 0;
    command.data.hud_text.style_id = 0;
    if (!scene_frame_submit(&frame, &command) || scene_frame_submit(&frame, &command)) {
        fprintf(stderr, "scene frame command capacity is inconsistent\n");
        scene_frame_destroy(&frame);
        asset_blob_release(&graphics_data);
        asset_blob_release(&level_data);
        return 1;
    }
    scene_frame_begin(&frame);
    if (frame.count != 0) {
        fprintf(stderr, "scene frame begin did not reset the command count\n");
        scene_frame_destroy(&frame);
        asset_blob_release(&graphics_data);
        asset_blob_release(&level_data);
        return 1;
    }
    scene_frame_destroy(&frame);

    asset_blob_release(&graphics_data);
    asset_blob_release(&level_data);

    if (!asset_io_load(argv[1], "includes/test.lnk", &game_link_blob, error, sizeof(error)) ||
        !game_link_init(&game_link_blob, &game_link, error, sizeof(error)) ||
        game_link_blob.size != GAME_LINK_SIZE ||
        !game_link_table(&game_link, GAME_LINK_TABLE_BULLET_DEFINITIONS,
                         &table_bytes, &table_size) ||
        table_bytes == NULL || table_size != 20u * 300u ||
        !game_link_copy_level_name(&game_link, 0, text, sizeof(text), error, sizeof(error)) ||
        strcmp(text, "      LEVEL  A") != 0 ||
        !game_link_copy_level_music_path(&game_link, 0, text, sizeof(text), error, sizeof(error)) ||
        strcmp(text, "tkg2:music/packedtest") != 0 ||
        !game_link_resolve_staged_path(text, text, sizeof(text), error, sizeof(error)) ||
        strcmp(text, "music/packedtest") != 0 ||
        !game_link_copy_object_graphics_path(&game_link, 0, text, sizeof(text), error, sizeof(error)) ||
        strcmp(text, "TKG1:INCLUDES/ALIEN2") != 0 ||
        !game_link_copy_wall_graphics_path(&game_link, 0, text, sizeof(text), error, sizeof(error)) ||
        strcmp(text, "TKG1:WALLINC/STONEWALL.256WAD") != 0 ||
        !game_link_copy_sfx_path(&game_link, 0, text, sizeof(text), error, sizeof(error)) ||
        strcmp(text, "sfx:samples/scream.fib") != 0 ||
        !game_link_resolve_staged_path(text, text, sizeof(text), error, sizeof(error)) ||
        strcmp(text, "ab3dsfx/samples/scream.fib") != 0 ||
        !game_link_copy_sfx_path(&game_link, 1, text, sizeof(text), error, sizeof(error)) ||
        strcmp(text, "sfx:samples/fire!.fib") != 0 ||
        game_link_copy_sfx_path(&game_link, GAME_LINK_SFX_LOAD_COUNT, text, sizeof(text),
                                error, sizeof(error))) {
        fprintf(stderr, "GLFT catalog parsing is inconsistent: %s\n", error);
        asset_blob_release(&game_link_blob);
        return 1;
    }
    asset_blob_release(&game_link_blob);

    if (!game_bootstrap_init(&game, argv[1], error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (game.shared_resources.floor_texture.size != 65536u ||
        game.shared_resources.texture_maps.size != 131072u ||
        game.shared_resources.texture_palette.size != 16384u ||
        game.shared_resources.object_count != 14u ||
        game.shared_resources.vector_count != 22u ||
        game.shared_resources.wall_texture_count != 13u ||
        /* Res_LoadSoundFx scans 59 slots and skips 13 empty entries. */
        game.shared_resources.sound_effect_count != 46u ||
        game.shared_resources.backdrop_image.size == 0) {
        fprintf(stderr,
                "source-defined shared resources are inconsistent "
                "(floor=%zu maps=%zu palette=%zu objects=%u vectors=%u walls=%u sfx=%u backdrop=%zu)\n",
                game.shared_resources.floor_texture.size,
                game.shared_resources.texture_maps.size,
                game.shared_resources.texture_palette.size,
                game.shared_resources.object_count, game.shared_resources.vector_count,
                game.shared_resources.wall_texture_count, game.shared_resources.sound_effect_count,
                game.shared_resources.backdrop_image.size);
        game_bootstrap_destroy(&game);
        return 1;
    }
    if (game.level_data.size != 0 || game.session.menu_level_index != 0 ||
        game.session.campaign_inventory.health != 200u ||
        game.session.campaign_inventory.weapons[0] != 0x00ffu ||
        game.session.campaign_inventory.ammunition[7] != 20u ||
        game_session_select_level(&game.session, GAME_LINK_LEVEL_COUNT, error, sizeof(error))) {
        fprintf(stderr, "DEFAULTGAME single-player state is inconsistent\n");
        game_bootstrap_destroy(&game);
        return 1;
    }
    for (level_index = 0; level_index < 16u; ++level_index) {
        if (!game_session_select_level(&game.session, level_index, error, sizeof(error)) ||
            !game_bootstrap_start_selected_single_player(&game, argv[1], error, sizeof(error)) ||
            game.active_level_index != level_index ||
            game.session.active_level_index != level_index || game.level.zone_count == 0 ||
            game.level_music.size == 0 ||
            game.level_runtime.zone_count != game.level.zone_count ||
            level_runtime_get_zone(&game.level_runtime, game.level_runtime.zone_count,
                                   &zone, error, sizeof(error))) {
            fprintf(stderr, "campaign level %u could not be loaded: %s\n", level_index, error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (zone_index = 0; zone_index < game.level_runtime.zone_count; ++zone_index) {
            if (!level_runtime_get_zone(&game.level_runtime, zone_index, &zone,
                                        error, sizeof(error))) {
                fprintf(stderr, "campaign level %u zone %u is invalid: %s\n",
                        level_index, zone_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
    }
    game.session.player1_inventory.health = 199u;
    game_session_finish_single_player(&game.session, 0);
    if (game.session.campaign_inventory.health != 200u) {
        fprintf(stderr, "unfinished level unexpectedly changed campaign inventory\n");
        game_bootstrap_destroy(&game);
        return 1;
    }
    game_session_finish_single_player(&game.session, 1);
    if (game.session.campaign_inventory.health != 199u) {
        fprintf(stderr, "finished level did not preserve player-one inventory\n");
        game_bootstrap_destroy(&game);
        return 1;
    }
    game_bootstrap_destroy(&game);
    return 0;
}
