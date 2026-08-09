#include <stdio.h>
#include <string.h>

#include "asset_io.h"
#include "game_bootstrap.h"
#include "game_link.h"
#include "game_menu.h"
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
    uint8_t campaign_record[GAME_SESSION_RECORD_SIZE];
    static const uint8_t expected_control_defaults[GAME_CONTROL_PERSISTED_BYTE_COUNT] = {
        0x4fu, 0x4eu, 0x11u, 0x21u, 0x63u, 0x23u, 0x60u, 0x64u, 0x20u,
        0x22u, 0x33u, 0x28u, 0x40u, 0x0cu, 0x0bu, 0x29u, 0x0du, 0x00u
    };
    LevelZone zone;
    LevelEdge edge;
    LevelControlPoint control_point;
    LevelNavigationLink navigation_link;
    LevelObjectSlot object_slot;
    LevelObjectPoint object_point;
    uint32_t zone_edge_count;
    uint32_t zone_edge_index;
    GameSession encoded_session;
    GameSession decoded_session;
    GameMenu menu;
    int should_quit;
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
        memcmp(game.controls.assigned_raw_keys, expected_control_defaults,
               sizeof(expected_control_defaults)) != 0 ||
        !game_input_set_raw_key(&game.input, 0x11u, 1, error, sizeof(error)) ||
        !game_input_is_raw_key_down(&game.input, 0x11u) ||
        !game_input_is_control_down(&game.input, &game.controls,
                                    GAME_CONTROL_FORWARDS) ||
        game_input_take_last_pressed(&game.input) != 0x11u ||
        game_input_take_last_pressed(&game.input) != 0u ||
        !game_input_set_raw_key(&game.input, 0x11u, 0, error, sizeof(error)) ||
        game_input_is_raw_key_down(&game.input, 0x11u) ||
        game_input_set_raw_key(&game.input, GAME_INPUT_RAW_KEY_LIMIT, 1,
                               error, sizeof(error)) ||
        game_controls_assign_raw_key(&game.controls, GAME_CONTROL_BINDING_COUNT,
                                     0x12u, error, sizeof(error)) ||
        game_session_select_level(&game.session, GAME_LINK_LEVEL_COUNT, error, sizeof(error))) {
        fprintf(stderr, "DEFAULTGAME single-player state is inconsistent\n");
        game_bootstrap_destroy(&game);
        return 1;
    }
    game_menu_init(&menu, &game);
    if (menu.screen != GAME_MENU_SCREEN_MAIN || menu.selection != 0u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_UP, &should_quit,
                                error, sizeof(error)) ||
        menu.selection != 8u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN, &should_quit,
                                error, sizeof(error)) ||
        menu.selection != 0u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_LEVEL_ACTIVE || game.level_data.size == 0 ||
        game.session.active_level_index != 0u) {
        fprintf(stderr, "single-player main-menu start is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    game_menu_init(&menu, &game);
    if (!game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN, &should_quit,
                                error, sizeof(error)) ||
        menu.selection != 1u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_NOTICE ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_BACK, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_MAIN) {
        fprintf(stderr, "single-player multiplayer exclusion is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    if (!game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN, &should_quit,
                                error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN, &should_quit,
                                error, sizeof(error)) ||
        menu.selection != 2u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_LEVEL_PAGE_ONE) {
        fprintf(stderr, "source level-menu entry is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    for (uint16_t menu_step = 0; menu_step < 8u; ++menu_step) {
        if (!game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                    &should_quit, error, sizeof(error))) {
            fprintf(stderr, "source level-menu navigation failed: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    if (menu.selection != 8u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_LEVEL_PAGE_TWO ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN, &should_quit,
                                error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_UP, &should_quit,
                                error, sizeof(error)) ||
        menu.selection != 0u) {
        fprintf(stderr, "source level-menu page change is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    if (!game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_MAIN || menu.selection != 0u ||
        game.session.menu_level_index != 8u ||
        game.session.campaign_inventory.health != 200u) {
        fprintf(stderr, "source page-two DEFGAME selection is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    game_menu_init(&menu, &game);
    for (uint16_t menu_step = 0; menu_step < 3u; ++menu_step) {
        if (!game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                    &should_quit, error, sizeof(error))) {
            fprintf(stderr, "control-options navigation failed: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    if (menu.selection != 3u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_CONTROLS_PAGE_ONE || menu.selection != 0u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_CAPTURE_CONTROL ||
        !game_menu_capture_control_key(&menu, &game, 0x12u, error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_CONTROLS_PAGE_ONE || menu.selection != 0u ||
        game.controls.assigned_raw_keys[GAME_CONTROL_TURN_LEFT] != 0x12u) {
        fprintf(stderr, "source first control-options page is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    for (uint16_t menu_step = 0; menu_step < 11u; ++menu_step) {
        if (!game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                    &should_quit, error, sizeof(error))) {
            fprintf(stderr, "control-options page navigation failed: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    if (menu.selection != 11u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_CONTROLS_PAGE_TWO || menu.selection != 0u) {
        fprintf(stderr, "source second control-options page entry is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    for (uint16_t menu_step = 0; menu_step < 5u; ++menu_step) {
        if (!game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                    &should_quit, error, sizeof(error))) {
            fprintf(stderr, "second control-options navigation failed: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    if (menu.selection != 5u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_CAPTURE_CONTROL ||
        !game_menu_capture_control_key(&menu, &game, 0x24u, error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_CONTROLS_PAGE_TWO || menu.selection != 5u ||
        game.controls.assigned_raw_keys[GAME_CONTROL_NEXT_WEAPON] != 0x24u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN, &should_quit,
                                error, sizeof(error)) ||
        menu.selection != 6u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_MAIN) {
        fprintf(stderr, "source second control-options page is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    game_menu_init(&menu, &game);
    if (!game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_UP, &should_quit,
                                error, sizeof(error)) ||
        menu.selection != 8u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        !should_quit) {
        fprintf(stderr, "main-menu exit command is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    game_menu_init(&menu, &game);
    for (uint16_t menu_step = 0; menu_step < 7u; ++menu_step) {
        if (!game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                    &should_quit, error, sizeof(error))) {
            fprintf(stderr, "custom-options navigation failed: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    if (menu.selection != 7u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_CUSTOM_OPTIONS || menu.selection != 0u ||
        game.preferences.original_mouse != 0u || game.preferences.show_messages != UINT8_MAX ||
        game.preferences.play_music != UINT8_MAX ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        game.preferences.original_mouse != UINT8_MAX) {
        fprintf(stderr, "source custom-options toggle is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    for (uint16_t menu_step = 0; menu_step < 8u; ++menu_step) {
        if (!game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                    &should_quit, error, sizeof(error))) {
            fprintf(stderr, "custom-options return navigation failed: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    if (menu.selection != 8u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE, &should_quit,
                                error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_MAIN) {
        fprintf(stderr, "custom-options return is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    if (!game_session_default(&encoded_session, &game.game_link_catalog, error, sizeof(error))) {
        fprintf(stderr, "could not initialize campaign-record test: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    encoded_session.menu_level_index = 15u;
    encoded_session.campaign_inventory.health = 0x1234u;
    encoded_session.campaign_inventory.jetpack_fuel = 0x5678u;
    encoded_session.campaign_inventory.ammunition[0] = 0x9abcu;
    encoded_session.campaign_inventory.ammunition[19] = 0xdef0u;
    encoded_session.campaign_inventory.shield = 0x1357u;
    encoded_session.campaign_inventory.jetpack = 0x2468u;
    encoded_session.campaign_inventory.weapons[0] = 0xaaaau;
    encoded_session.campaign_inventory.weapons[9] = 0x5555u;
    memset(campaign_record, 0, sizeof(campaign_record));
    memset(&decoded_session, 0, sizeof(decoded_session));
    decoded_session.active_level_index = 6u;
    decoded_session.player1_inventory.health = 77u;
    if (!game_session_encode_campaign_record(&encoded_session, campaign_record,
                                             sizeof(campaign_record), error, sizeof(error)) ||
        campaign_record[0] != 0x00u || campaign_record[1] != 0x0fu ||
        campaign_record[2] != 0x12u || campaign_record[3] != 0x34u ||
        campaign_record[4] != 0x56u || campaign_record[5] != 0x78u ||
        campaign_record[6] != 0x9au || campaign_record[7] != 0xbcu ||
        campaign_record[44] != 0xdeu || campaign_record[45] != 0xf0u ||
        campaign_record[46] != 0x13u || campaign_record[47] != 0x57u ||
        campaign_record[48] != 0x24u || campaign_record[49] != 0x68u ||
        campaign_record[50] != 0xaau || campaign_record[51] != 0xaau ||
        campaign_record[68] != 0x55u || campaign_record[69] != 0x55u ||
        !game_session_decode_campaign_record(&decoded_session, campaign_record,
                                             sizeof(campaign_record), error, sizeof(error)) ||
        decoded_session.menu_level_index != 15u ||
        decoded_session.active_level_index != 6u ||
        decoded_session.player1_inventory.health != 77u ||
        memcmp(&decoded_session.campaign_inventory, &encoded_session.campaign_inventory,
               sizeof(encoded_session.campaign_inventory)) != 0 ||
        game_session_decode_campaign_record(&decoded_session, campaign_record,
                                            sizeof(campaign_record) - 1u,
                                            error, sizeof(error)) ||
        !game_session_load_level_definition(&decoded_session, &game.game_link_catalog,
                                            NULL, 0u, 0, error, sizeof(error)) ||
        decoded_session.menu_level_index != 0u ||
        decoded_session.campaign_inventory.health != 200u ||
        decoded_session.campaign_inventory.ammunition[7] != 20u ||
        !game_session_load_level_definition(&decoded_session, &game.game_link_catalog,
                                            campaign_record, sizeof(campaign_record), 1,
                                            error, sizeof(error)) ||
        decoded_session.menu_level_index != 15u ||
        !game_bootstrap_load_level_definition(&game, argv[1], 0u, error, sizeof(error)) ||
        game.session.menu_level_index != 0u ||
        !game_bootstrap_load_level_definition(&game, argv[1], 15u, error, sizeof(error)) ||
        game.session.menu_level_index != 0u ||
        game_bootstrap_load_level_definition(&game, argv[1], GAME_LINK_LEVEL_COUNT,
                                             error, sizeof(error))) {
        fprintf(stderr, "DEFGAME campaign-record behavior is inconsistent: %s\n", error);
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
            game.level_runtime.zone_offsets_table_offset !=
                game.level_graphics_header.zone_adds_table_offset ||
            game.level_runtime.zone_graph_adds_offset !=
                game.level_graphics_header.zone_graph_adds_offset ||
            !level_navigation_get_next(&game.level_navigation, 0u, 0u, 0,
                                       &navigation_link, error, sizeof(error)) ||
            navigation_link.next_control_point != 0u || navigation_link.only_see != 0u ||
            level_navigation_get_next(&game.level_navigation,
                                      LEVEL_NAVIGATION_CONTROL_POINT_LIMIT, 0u, 0,
                                      &navigation_link, error, sizeof(error)) ||
            game.level_runtime.object_point_count != (uint32_t)game.level.object_count + 1u ||
            game.level_runtime.object_record_count == 0u ||
            game.level_runtime.control_point_count != game.level.control_point_count ||
            (game.level_runtime.control_point_count == 0u
                ? level_runtime_get_control_point(&game.level_runtime, 0u, &control_point,
                                                  error, sizeof(error))
                : (!level_runtime_get_control_point(&game.level_runtime, 0u, &control_point,
                                                    error, sizeof(error)) ||
                   !level_runtime_get_control_point(&game.level_runtime,
                                                    game.level_runtime.control_point_count - 1u,
                                                    &control_point, error, sizeof(error)) ||
                   level_runtime_get_control_point(&game.level_runtime,
                                                   game.level_runtime.control_point_count,
                                                   &control_point, error, sizeof(error)))) ||
            game.level_runtime.edge_count == 0u ||
            game.level_runtime.edge_table_offset != game.level.floor_line_offset ||
            !level_runtime_get_edge(&game.level_runtime, 0u, &edge, error, sizeof(error)) ||
            !level_runtime_get_edge(&game.level_runtime,
                                    game.level_runtime.edge_count - 1u,
                                    &edge, error, sizeof(error)) ||
            level_runtime_get_edge(&game.level_runtime, game.level_runtime.edge_count,
                                   &edge, error, sizeof(error)) ||
            !level_runtime_get_object_record(&game.level_runtime, 0u, &object_slot,
                                             error, sizeof(error)) ||
            !level_runtime_get_object_record(&game.level_runtime,
                                             game.level_runtime.object_record_count - 1u,
                                             &object_slot, error, sizeof(error)) ||
            level_runtime_get_object_record(&game.level_runtime,
                                            game.level_runtime.object_record_count,
                                            &object_slot, error, sizeof(error)) ||
            level_runtime_get_zone(&game.level_runtime, game.level_runtime.zone_count,
                                   &zone, error, sizeof(error))) {
            fprintf(stderr, "campaign level %u could not be loaded: %s\n", level_index, error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint8_t current_control_point = 0u;
             current_control_point < LEVEL_NAVIGATION_CONTROL_POINT_LIMIT;
             ++current_control_point) {
            for (uint8_t target_control_point = 0u;
                 target_control_point < LEVEL_NAVIGATION_CONTROL_POINT_LIMIT;
                 ++target_control_point) {
                size_t navigation_offset = (size_t)current_control_point *
                    LEVEL_NAVIGATION_CONTROL_POINT_LIMIT + target_control_point;
                uint8_t walk_link = game.level_navigation.walk_links[navigation_offset];
                uint8_t fly_link = game.level_navigation.fly_links[navigation_offset];
                if (!level_navigation_get_next(&game.level_navigation,
                                               current_control_point, target_control_point, 0,
                                               &navigation_link, error, sizeof(error)) ||
                    (current_control_point != target_control_point &&
                     (navigation_link.next_control_point != (uint8_t)(walk_link & 0x7fu) ||
                      navigation_link.only_see !=
                          ((walk_link & 0x80u) != 0u ? UINT8_MAX : 0u))) ||
                    !level_navigation_get_next(&game.level_navigation,
                                               current_control_point, target_control_point, 1,
                                               &navigation_link, error, sizeof(error)) ||
                    (current_control_point != target_control_point &&
                     (navigation_link.next_control_point != (uint8_t)(fly_link & 0x7fu) ||
                      navigation_link.only_see !=
                          ((fly_link & 0x80u) != 0u ? UINT8_MAX : 0u)))) {
                    fprintf(stderr,
                            "campaign level %u navigation map %u to %u is invalid: %s\n",
                            level_index, current_control_point, target_control_point, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
        }
        for (zone_index = 0; zone_index < game.level_runtime.zone_count; ++zone_index) {
            if (!level_runtime_get_zone(&game.level_runtime, zone_index, &zone,
                                        error, sizeof(error)) ||
                !level_runtime_get_zone_edge_count(&game.level_runtime, zone_index,
                                                   &zone_edge_count, error, sizeof(error)) ||
                level_runtime_get_zone_edge_index(&game.level_runtime, zone_index,
                                                  zone_edge_count, &zone_edge_index,
                                                  error, sizeof(error))) {
                fprintf(stderr, "campaign level %u zone %u is invalid: %s\n",
                        level_index, zone_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            for (uint32_t list_index = 0; list_index < zone_edge_count; ++list_index) {
                if (!level_runtime_get_zone_edge_index(&game.level_runtime, zone_index,
                                                       list_index, &zone_edge_index,
                                                       error, sizeof(error)) ||
                    !level_runtime_get_edge(&game.level_runtime, zone_edge_index,
                                            &edge, error, sizeof(error))) {
                    fprintf(stderr, "campaign level %u zone %u edge %u is invalid: %s\n",
                            level_index, zone_index, list_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
        }
        for (uint32_t object_index = 0;
             object_index < game.level_runtime.object_record_count; ++object_index) {
            if (!level_runtime_get_object_record(&game.level_runtime, object_index,
                                                 &object_slot, error, sizeof(error)) ||
                object_slot.point_index >= game.level_runtime.object_point_count ||
                !level_runtime_get_object_point(&game.level_runtime, object_slot.point_index,
                                                &object_point, error, sizeof(error))) {
                fprintf(stderr, "campaign level %u object %u is invalid: %s\n",
                        level_index, object_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
    }
    if (!level_runtime_get_zone(&game.level_runtime, game.level.player1_start_zone,
                                &zone, error, sizeof(error)) ||
        game.player.x != game.level.player1_start_x ||
        game.player.z != game.level.player1_start_z ||
        game.player.y != zone.floor - 12 * 1024 || game.player.height != 12 * 1024 ||
        game.player.default_enemy_flags != 0x23u || !scene_frame_init(&frame, 2) ||
        !game_bootstrap_submit_diagnostic_frame(&game, &frame) || frame.count != 2 ||
        frame.commands[0].type != SCENE_COMMAND_CAMERA ||
        frame.commands[0].data.camera.position.x != game.player.x ||
        frame.commands[1].type != SCENE_COMMAND_HUD_TEXT) {
        fprintf(stderr, "Plr_Initialise camera state is inconsistent: %s\n", error);
        scene_frame_destroy(&frame);
        game_bootstrap_destroy(&game);
        return 1;
    }
    scene_frame_destroy(&frame);
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
