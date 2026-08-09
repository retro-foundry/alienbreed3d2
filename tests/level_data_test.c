#include <stdio.h>
#include <string.h>

#include "asset_io.h"
#include "game_bootstrap.h"
#include "game_link.h"
#include "game_menu.h"
#include "level_bootstrap.h"
#include "level_draw_graph.h"
#include "scene_frame.h"

static uint16_t read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static uint32_t read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static int liftable_matches_source(const LevelMechanisms *mechanisms,
                                   const LevelLiftable *liftable)
{
    const uint8_t *source;
    size_t header_offset;

    if (!mechanisms || !mechanisms->graphics_bytes ||
        liftable->wall_data_offset < 36u) {
        return 0;
    }
    header_offset = (size_t)liftable->wall_data_offset - 36u;
    if (header_offset > mechanisms->graphics_size ||
        36u > mechanisms->graphics_size - header_offset) {
        return 0;
    }
    source = mechanisms->graphics_bytes + header_offset;
    return liftable->bottom == (int16_t)read_be16(source + 0u) &&
        liftable->top == (int16_t)read_be16(source + 2u) &&
        liftable->opening_speed == (int16_t)read_be16(source + 4u) &&
        liftable->closing_speed == (int16_t)read_be16(source + 6u) &&
        liftable->open_duration == (int16_t)read_be16(source + 8u) &&
        liftable->opening_sound_fx == (int16_t)read_be16(source + 10u) &&
        liftable->closing_sound_fx == (int16_t)read_be16(source + 12u) &&
        liftable->opened_sound_fx == (int16_t)read_be16(source + 14u) &&
        liftable->closed_sound_fx == (int16_t)read_be16(source + 16u) &&
        liftable->word9 == (int16_t)read_be16(source + 18u) &&
        liftable->word10 == (int16_t)read_be16(source + 20u) &&
        liftable->word11 == (int16_t)read_be16(source + 22u) &&
        liftable->word12 == (int16_t)read_be16(source + 24u) &&
        liftable->graphics_offset == read_be32(source + 26u) &&
        liftable->zone_id == (int16_t)read_be16(source + 30u) &&
        liftable->word16 == (int16_t)read_be16(source + 32u) &&
        liftable->raise_condition == source[34u] &&
        liftable->lower_condition == source[35u];
}

static int object_definition_matches_source(const GameObjectDefinition *definition,
                                            const uint8_t *source)
{
    return definition && source &&
        definition->behaviour == read_be16(source + 0u) &&
        definition->graphics_type == read_be16(source + 2u) &&
        definition->active_timeout == (int16_t)read_be16(source + 4u) &&
        definition->hit_points == read_be16(source + 6u) &&
        definition->explosive_force == read_be16(source + 8u) &&
        definition->impassible == read_be16(source + 10u) &&
        definition->default_animation_length == read_be16(source + 12u) &&
        definition->collision_radius == read_be16(source + 14u) &&
        definition->collision_height == read_be16(source + 16u) &&
        definition->floor_ceiling == read_be16(source + 18u) &&
        definition->lock_to_wall == read_be16(source + 20u) &&
        definition->active_animation_length == read_be16(source + 22u) &&
        definition->sound_effect == (int16_t)read_be16(source + 24u);
}

static int object_animation_frame_matches_source(const GameObjectAnimationFrame *frame,
                                                 const uint8_t *source)
{
    return frame && source && frame->byte_0 == source[0u] &&
        frame->byte_1 == source[1u] && frame->word_2 == read_be16(source + 2u) &&
        frame->signed_byte_4 == (int8_t)source[4u] && frame->next_timer1 == source[5u];
}

static int object_frame_data_matches_source(const GameObjectFrameData *frame,
                                            const uint8_t *source)
{
    return frame && source && frame->pointer_table_index == read_be16(source + 0u) &&
        frame->down_strip == read_be16(source + 2u) &&
        frame->strip_count == read_be16(source + 4u) &&
        frame->line_count == read_be16(source + 6u);
}

static int shoot_definition_matches_source(const GameShootDefinition *definition,
                                           const uint8_t *source)
{
    return definition && source && definition->bullet_type == read_be16(source + 0u) &&
        definition->delay == read_be16(source + 2u) &&
        definition->bullet_count == read_be16(source + 4u) &&
        definition->sound_effect == read_be16(source + 6u);
}

static int alien_definition_matches_source(const GameAlienDefinition *definition,
                                           const uint8_t *source)
{
    return definition && source &&
        definition->graphics_type == read_be16(source + 0u) &&
        definition->default_behaviour == read_be16(source + 2u) &&
        definition->reaction_time == read_be16(source + 4u) &&
        definition->default_speed == read_be16(source + 6u) &&
        definition->response_behaviour == read_be16(source + 8u) &&
        definition->response_speed == read_be16(source + 10u) &&
        definition->response_timeout == read_be16(source + 12u) &&
        definition->damage_to_retreat == read_be16(source + 14u) &&
        definition->damage_to_followup == read_be16(source + 16u) &&
        definition->followup_behaviour == read_be16(source + 18u) &&
        definition->followup_speed == read_be16(source + 20u) &&
        definition->followup_timeout == read_be16(source + 22u) &&
        definition->retreat_behaviour == read_be16(source + 24u) &&
        definition->retreat_speed == read_be16(source + 26u) &&
        definition->retreat_timeout == read_be16(source + 28u) &&
        definition->bullet_type == read_be16(source + 30u) &&
        definition->hit_points == read_be16(source + 32u) &&
        definition->height == read_be16(source + 34u) &&
        definition->girth == read_be16(source + 36u) &&
        definition->splat_type == read_be16(source + 38u) &&
        definition->auxiliary_type == read_be16(source + 40u);
}

static int bullet_definition_matches_source(const GameBulletDefinition *definition,
                                            const uint8_t *source)
{
    return definition && source &&
        definition->is_hitscan == read_be32(source + 0u) &&
        definition->gravity == read_be32(source + 4u) &&
        definition->lifetime == read_be32(source + 8u) &&
        definition->ammunition_in_clip == read_be32(source + 12u) &&
        definition->bounce_horizontal == read_be32(source + 16u) &&
        definition->bounce_vertical == read_be32(source + 20u) &&
        definition->hit_damage == read_be32(source + 24u) &&
        definition->explosive_force == read_be32(source + 28u) &&
        definition->speed == read_be32(source + 32u) &&
        definition->animation_frames == read_be32(source + 36u) &&
        definition->pop_frames == read_be32(source + 40u) &&
        definition->bounce_sound_effect == read_be32(source + 44u) &&
        definition->impact_sound_effect == read_be32(source + 48u) &&
        definition->graphics_type == read_be32(source + 52u) &&
        definition->impact_graphics_type == read_be32(source + 56u) &&
        definition->animation_data == source + 60u &&
        definition->pop_data == source + 180u;
}

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
    const uint8_t *shoot_definition_bytes;
    const uint8_t *alien_definition_bytes;
    const uint8_t *object_definition_bytes;
    const uint8_t *object_default_animation_bytes;
    const uint8_t *object_action_animation_bytes;
    const uint8_t *object_frame_data_bytes;
    size_t table_size;
    size_t shoot_definition_size;
    size_t alien_definition_size;
    size_t object_definition_size;
    size_t object_default_animation_size;
    size_t object_action_animation_size;
    size_t object_frame_data_size;
    uint16_t level_index;
    uint16_t zone_index;
    uint16_t object_definition_index;
    uint16_t object_animation_index;
    uint16_t object_frame_data_index;
    uint16_t shoot_definition_index;
    uint16_t alien_definition_index;
    uint16_t bullet_definition_index;
    int16_t trig_value;
    char text[128];
    uint8_t campaign_record[GAME_SESSION_RECORD_SIZE];
    static const uint8_t expected_control_defaults[GAME_CONTROL_PERSISTED_BYTE_COUNT] = {
        0x4fu, 0x4eu, 0x11u, 0x21u, 0x63u, 0x23u, 0x60u, 0x64u, 0x20u,
        0x22u, 0x33u, 0x28u, 0x40u, 0x0cu, 0x0bu, 0x29u, 0x0du, 0x00u
    };
    LevelZone zone;
    LevelDrawGraphStreams draw_graph_streams;
    LevelDrawGraphRecord draw_graph_record;
    LevelDrawWall draw_wall;
    LevelDrawFlat draw_flat;
    LevelEdge edge;
    LevelControlPoint control_point;
    LevelWorldPoint world_point;
    LevelNarrativeMessage narrative_message;
    LevelNavigationLink navigation_link;
    LevelLiftable liftable;
    LevelLiftableWall liftable_wall;
    LevelSwitch switch_record;
    LevelObjectSlot object_slot;
    LevelObjectPoint object_point;
    GameObjectDefinition object_definition;
    GameObjectAnimationFrame object_animation_frame;
    GameObjectFrameData object_frame_data;
    GameShootDefinition shoot_definition;
    GameAlienDefinition alien_definition;
    GameBulletDefinition bullet_definition;
    uint32_t zone_edge_count;
    uint32_t zone_edge_index;
    uint32_t world_point_index;
    uint32_t draw_graph_record_count;
    uint32_t draw_graph_record_index;
    uint32_t static_wall_index;
    uint32_t static_flat_index;
    uint16_t flat_point_index;
    uint16_t flat_raw_point_word;
    uint16_t flat_world_point_index;
    uint16_t mechanism_index;
    uint16_t wall_index;
    GameSession encoded_session;
    GameSession decoded_session;
    GameControls control_defaults;
    GameInput control_input;
    PlayerRuntime controlled_player;
    GameMenu menu;
    int should_quit;
    uint8_t override_marker[64u * 32u];
    AssetBlob saved_floor_override;
    AssetBlob saved_wall_override;
    int override_sources_ok;
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
        table_bytes == NULL || table_size != (size_t)GAME_LINK_BULLET_COUNT *
                                               GAME_LINK_BULLET_DEFINITION_SIZE ||
        !game_link_table(&game_link, GAME_LINK_TABLE_SHOOT_DEFINITIONS,
                         &shoot_definition_bytes, &shoot_definition_size) ||
        shoot_definition_size != (size_t)GAME_LINK_GUN_COUNT *
                                      GAME_LINK_SHOOT_DEFINITION_SIZE ||
        !game_link_table(&game_link, GAME_LINK_TABLE_ALIEN_DEFINITIONS,
                         &alien_definition_bytes, &alien_definition_size) ||
        alien_definition_size != (size_t)GAME_LINK_ALIEN_COUNT *
                                      GAME_LINK_ALIEN_DEFINITION_SIZE ||
        !game_link_table(&game_link, GAME_LINK_TABLE_OBJECT_DEFINITIONS,
                         &object_definition_bytes, &object_definition_size) ||
        object_definition_size != (size_t)GAME_LINK_OBJECT_COUNT *
                                      GAME_LINK_OBJECT_DEFINITION_SIZE ||
        !game_link_table(&game_link, GAME_LINK_TABLE_OBJECT_DEFINITION_ANIMATIONS,
                         &object_default_animation_bytes, &object_default_animation_size) ||
        object_default_animation_size != (size_t)GAME_LINK_OBJECT_COUNT *
                                            GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT *
                                            GAME_LINK_OBJECT_ANIMATION_FRAME_SIZE ||
        !game_link_table(&game_link, GAME_LINK_TABLE_OBJECT_ACTION_ANIMATIONS,
                         &object_action_animation_bytes, &object_action_animation_size) ||
        object_action_animation_size != object_default_animation_size ||
        !game_link_table(&game_link, GAME_LINK_TABLE_FRAME_DATA,
                         &object_frame_data_bytes, &object_frame_data_size) ||
        object_frame_data_size != (size_t)GAME_LINK_OBJECT_COUNT *
                                      GAME_LINK_OBJECT_FRAME_DATA_COUNT *
                                      GAME_LINK_OBJECT_FRAME_DATA_SIZE ||
        !game_link_copy_level_name(&game_link, 0, text, sizeof(text), error, sizeof(error)) ||
        strcmp(text, "      LEVEL  A") != 0 ||
        !game_link_copy_object_name(&game_link, 0, text, sizeof(text), error, sizeof(error)) ||
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
    for (bullet_definition_index = 0u;
         bullet_definition_index < GAME_LINK_BULLET_COUNT;
         ++bullet_definition_index) {
        if (!game_link_get_bullet_definition(&game_link, bullet_definition_index,
                                             &bullet_definition, error, sizeof(error)) ||
            !bullet_definition_matches_source(
                &bullet_definition,
                table_bytes + (size_t)bullet_definition_index *
                    GAME_LINK_BULLET_DEFINITION_SIZE)) {
            fprintf(stderr, "GLFT bullet definition %u is inconsistent: %s\n",
                    bullet_definition_index, error);
            asset_blob_release(&game_link_blob);
            return 1;
        }
    }
    for (alien_definition_index = 0u;
         alien_definition_index < GAME_LINK_ALIEN_COUNT;
         ++alien_definition_index) {
        if (!game_link_get_alien_definition(&game_link, alien_definition_index,
                                            &alien_definition, error, sizeof(error)) ||
            !alien_definition_matches_source(
                &alien_definition,
                alien_definition_bytes + (size_t)alien_definition_index *
                    GAME_LINK_ALIEN_DEFINITION_SIZE)) {
            fprintf(stderr, "GLFT alien definition %u is inconsistent: %s\n",
                    alien_definition_index, error);
            asset_blob_release(&game_link_blob);
            return 1;
        }
    }
    for (shoot_definition_index = 0u;
         shoot_definition_index < GAME_LINK_GUN_COUNT;
         ++shoot_definition_index) {
        if (!game_link_get_shoot_definition(&game_link, shoot_definition_index,
                                            &shoot_definition, error, sizeof(error)) ||
            !shoot_definition_matches_source(
                &shoot_definition,
                shoot_definition_bytes + (size_t)shoot_definition_index *
                    GAME_LINK_SHOOT_DEFINITION_SIZE)) {
            fprintf(stderr, "GLFT shoot definition %u is inconsistent: %s\n",
                    shoot_definition_index, error);
            asset_blob_release(&game_link_blob);
            return 1;
        }
    }
    for (object_definition_index = 0u;
         object_definition_index < GAME_LINK_OBJECT_COUNT;
         ++object_definition_index) {
        if (!game_link_get_object_definition(&game_link, object_definition_index,
                                             &object_definition, error, sizeof(error)) ||
            !object_definition_matches_source(
                &object_definition,
                object_definition_bytes + (size_t)object_definition_index *
                    GAME_LINK_OBJECT_DEFINITION_SIZE)) {
            fprintf(stderr, "GLFT object definition %u is inconsistent: %s\n",
                    object_definition_index, error);
            asset_blob_release(&game_link_blob);
            return 1;
        }
        for (object_animation_index = 0u;
             object_animation_index < GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT;
             ++object_animation_index) {
            size_t frame_offset = ((size_t)object_definition_index *
                                   GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT +
                                   object_animation_index) *
                                  GAME_LINK_OBJECT_ANIMATION_FRAME_SIZE;

            if (!game_link_get_object_animation_frame(
                    &game_link, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                    object_definition_index, object_animation_index,
                    &object_animation_frame, error, sizeof(error)) ||
                !object_animation_frame_matches_source(&object_animation_frame,
                                                       object_default_animation_bytes + frame_offset) ||
                !game_link_get_object_animation_frame(
                    &game_link, GAME_LINK_OBJECT_ANIMATION_ACTION,
                    object_definition_index, object_animation_index,
                    &object_animation_frame, error, sizeof(error)) ||
                !object_animation_frame_matches_source(&object_animation_frame,
                                                       object_action_animation_bytes + frame_offset)) {
                fprintf(stderr, "GLFT object animation %u frame %u is inconsistent: %s\n",
                        object_definition_index, object_animation_index, error);
                asset_blob_release(&game_link_blob);
                return 1;
            }
        }
        for (object_frame_data_index = 0u;
             object_frame_data_index < GAME_LINK_OBJECT_FRAME_DATA_COUNT;
             ++object_frame_data_index) {
            size_t frame_data_offset = ((size_t)object_definition_index *
                                        GAME_LINK_OBJECT_FRAME_DATA_COUNT +
                                        object_frame_data_index) *
                                       GAME_LINK_OBJECT_FRAME_DATA_SIZE;

            if (!game_link_get_object_frame_data(&game_link, object_definition_index,
                                                 object_frame_data_index,
                                                 &object_frame_data, error, sizeof(error)) ||
                !object_frame_data_matches_source(&object_frame_data,
                                                  object_frame_data_bytes + frame_data_offset)) {
                fprintf(stderr, "GLFT frame data %u frame %u is inconsistent: %s\n",
                        object_definition_index, object_frame_data_index, error);
                asset_blob_release(&game_link_blob);
                return 1;
            }
        }
    }
    if (game_link_get_object_definition(&game_link, GAME_LINK_OBJECT_COUNT,
                                        &object_definition, error, sizeof(error)) ||
        game_link_get_object_animation_frame(&game_link, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                                             0u, GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT,
                                             &object_animation_frame, error, sizeof(error)) ||
        game_link_get_object_animation_frame(&game_link, (GameObjectAnimationKind)2,
                                             0u, 0u, &object_animation_frame,
                                             error, sizeof(error)) ||
        game_link_get_object_frame_data(&game_link, 0u, GAME_LINK_OBJECT_FRAME_DATA_COUNT,
                                        &object_frame_data, error, sizeof(error)) ||
        game_link_get_shoot_definition(&game_link, GAME_LINK_GUN_COUNT,
                                       &shoot_definition, error, sizeof(error)) ||
        game_link_get_alien_definition(&game_link, GAME_LINK_ALIEN_COUNT,
                                       &alien_definition, error, sizeof(error)) ||
        game_link_get_bullet_definition(&game_link, GAME_LINK_BULLET_COUNT,
                                        &bullet_definition, error, sizeof(error))) {
        fprintf(stderr, "GLFT definition bounds checks are inconsistent\n");
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
        game.sine_table.size != GAME_MATH_SINE_TABLE_BYTES ||
        game_math_wrap_angle_address(0x3fffu) != 0x1ffeu ||
        !game_math_sine(&game.math, 0u, &trig_value, error, sizeof(error)) ||
        trig_value != 0 ||
        !game_math_sine(&game.math, 2u, &trig_value, error, sizeof(error)) ||
        trig_value != 50 ||
        !game_math_cosine(&game.math, 0u, &trig_value, error, sizeof(error)) ||
        trig_value != 32767 ||
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
            !level_runtime_get_zone_draw_graph_streams(&game.level_runtime, 0u,
                                                       &draw_graph_streams,
                                                       error, sizeof(error)) ||
            draw_graph_streams.lower_zone_id != 0 ||
            !level_navigation_get_next(&game.level_navigation, 0u, 0u, 0,
                                       &navigation_link, error, sizeof(error)) ||
            navigation_link.next_control_point != 0u || navigation_link.only_see != 0u ||
            level_navigation_get_next(&game.level_navigation,
                                      LEVEL_NAVIGATION_CONTROL_POINT_LIMIT, 0u, 0,
                                      &navigation_link, error, sizeof(error)) ||
            game.level_runtime.object_point_count != (uint32_t)game.level.object_count + 1u ||
            game.level_runtime.world_point_count != (uint32_t)game.level.point_count + 1u ||
            game.level_runtime.object_record_count == 0u ||
            game.static_scene.wall_count == 0u ||
            game.level_runtime.control_point_count != game.level.control_point_count ||
            !level_runtime_get_narrative_message(&game.level_runtime, 0u, &narrative_message,
                                                 error, sizeof(error)) ||
            narrative_message.bytes != game.level_runtime.level_bytes ||
            narrative_message.byte_count != AB3D2_LEVEL_MESSAGE_LENGTH ||
            !level_runtime_get_narrative_message(&game.level_runtime,
                                                 AB3D2_LEVEL_MESSAGE_COUNT - 1u,
                                                 &narrative_message, error, sizeof(error)) ||
            narrative_message.bytes != game.level_runtime.level_bytes +
                (AB3D2_LEVEL_MESSAGE_COUNT - 1u) * AB3D2_LEVEL_MESSAGE_LENGTH ||
            level_runtime_get_narrative_message(&game.level_runtime,
                                                AB3D2_LEVEL_MESSAGE_COUNT,
                                                &narrative_message, error, sizeof(error)) ||
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
            !level_runtime_get_world_point(&game.level_runtime, 0u, &world_point,
                                           error, sizeof(error)) ||
            !level_runtime_get_world_point(&game.level_runtime,
                                           game.level_runtime.world_point_count - 1u,
                                           &world_point, error, sizeof(error)) ||
            level_runtime_get_world_point(&game.level_runtime,
                                          game.level_runtime.world_point_count,
                                          &world_point, error, sizeof(error)) ||
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
        if (game.level_mechanisms.door_count > LEVEL_MECHANISMS_MAX_DOORS ||
            game.level_mechanisms.lift_count > LEVEL_MECHANISMS_MAX_LIFTS ||
            level_mechanisms_get_door(&game.level_mechanisms,
                                      game.level_mechanisms.door_count, &liftable,
                                      error, sizeof(error)) ||
            level_mechanisms_get_lift(&game.level_mechanisms,
                                      game.level_mechanisms.lift_count, &liftable,
                                      error, sizeof(error)) ||
            level_mechanisms_get_switch(&game.level_mechanisms,
                                        LEVEL_MECHANISMS_SWITCH_COUNT, &switch_record,
                                        error, sizeof(error))) {
            fprintf(stderr, "campaign level %u mechanism bounds are invalid: %s\n",
                    level_index, error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (mechanism_index = 0u;
             mechanism_index < game.level_mechanisms.door_count;
             ++mechanism_index) {
            if (!level_mechanisms_get_door(&game.level_mechanisms, mechanism_index,
                                           &liftable, error, sizeof(error)) ||
                !liftable_matches_source(&game.level_mechanisms, &liftable) ||
                level_mechanisms_get_door_wall(&game.level_mechanisms, mechanism_index,
                                                liftable.wall_count, &liftable_wall,
                                                error, sizeof(error))) {
                fprintf(stderr, "campaign level %u door %u is invalid: %s\n",
                        level_index, mechanism_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            for (wall_index = 0u; wall_index < liftable.wall_count; ++wall_index) {
                const uint8_t *wall_source = game.level_mechanisms.graphics_bytes +
                    liftable.wall_data_offset + (size_t)wall_index * 10u;
                if (!level_mechanisms_get_door_wall(&game.level_mechanisms, mechanism_index,
                                                    wall_index, &liftable_wall,
                                                    error, sizeof(error)) ||
                    liftable_wall.edge_index != (int16_t)read_be16(wall_source + 0u) ||
                    liftable_wall.graphics_offset != read_be32(wall_source + 2u) ||
                    liftable_wall.unknown_long != read_be32(wall_source + 6u)) {
                    fprintf(stderr, "campaign level %u door %u wall %u is invalid: %s\n",
                            level_index, mechanism_index, wall_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
        }
        for (mechanism_index = 0u;
             mechanism_index < game.level_mechanisms.lift_count;
             ++mechanism_index) {
            if (!level_mechanisms_get_lift(&game.level_mechanisms, mechanism_index,
                                           &liftable, error, sizeof(error)) ||
                !liftable_matches_source(&game.level_mechanisms, &liftable) ||
                level_mechanisms_get_lift_wall(&game.level_mechanisms, mechanism_index,
                                                liftable.wall_count, &liftable_wall,
                                                error, sizeof(error))) {
                fprintf(stderr, "campaign level %u lift %u is invalid: %s\n",
                        level_index, mechanism_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            for (wall_index = 0u; wall_index < liftable.wall_count; ++wall_index) {
                const uint8_t *wall_source = game.level_mechanisms.graphics_bytes +
                    liftable.wall_data_offset + (size_t)wall_index * 10u;
                if (!level_mechanisms_get_lift_wall(&game.level_mechanisms, mechanism_index,
                                                    wall_index, &liftable_wall,
                                                    error, sizeof(error)) ||
                    liftable_wall.edge_index != (int16_t)read_be16(wall_source + 0u) ||
                    liftable_wall.graphics_offset != read_be32(wall_source + 2u) ||
                    liftable_wall.unknown_long != read_be32(wall_source + 6u)) {
                    fprintf(stderr, "campaign level %u lift %u wall %u is invalid: %s\n",
                            level_index, mechanism_index, wall_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
        }
        for (mechanism_index = 0u; mechanism_index < LEVEL_MECHANISMS_SWITCH_COUNT;
             ++mechanism_index) {
            const uint8_t *switch_source = game.level_mechanisms.graphics_bytes +
                game.level_graphics_header.switch_data_offset + (size_t)mechanism_index * 14u;
            if (!level_mechanisms_get_switch(&game.level_mechanisms, mechanism_index,
                                             &switch_record, error, sizeof(error)) ||
                switch_record.word0 != (int16_t)read_be16(switch_source + 0u) ||
                switch_record.byte2 != switch_source[2u] ||
                switch_record.byte3 != switch_source[3u] ||
                switch_record.point_index != read_be16(switch_source + 4u) ||
                switch_record.graphics_offset != read_be32(switch_source + 6u) ||
                switch_record.byte10 != switch_source[10u] ||
                memcmp(switch_record.bytes11_to_13, switch_source + 11u,
                       sizeof(switch_record.bytes11_to_13)) != 0) {
                fprintf(stderr, "campaign level %u switch %u is invalid: %s\n",
                        level_index, mechanism_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        for (zone_index = 0; zone_index < game.level_runtime.zone_count; ++zone_index) {
            const uint8_t *draw_graph_table = game.level_runtime.graphics_bytes +
                game.level_runtime.zone_graph_adds_offset + (size_t)zone_index * 8u;

            if (!level_runtime_get_zone(&game.level_runtime, zone_index, &zone,
                                        error, sizeof(error)) ||
                !level_runtime_get_zone_draw_graph_streams(&game.level_runtime, zone_index,
                                                           &draw_graph_streams,
                                                           error, sizeof(error)) ||
                draw_graph_streams.lower_stream_offset != read_be32(draw_graph_table) ||
                draw_graph_streams.upper_stream_offset != read_be32(draw_graph_table + 4u) ||
                draw_graph_streams.lower_zone_id != (int16_t)zone_index ||
                (draw_graph_streams.has_upper_stream != 0u &&
                 draw_graph_streams.upper_zone_id != (int16_t)zone_index) ||
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
            for (uint8_t upper_stream = 0u;
                 upper_stream <= (draw_graph_streams.has_upper_stream != 0u ? 1u : 0u);
                 ++upper_stream) {
                if (!level_draw_graph_record_count(&game.level_runtime, zone_index,
                                                   upper_stream, &draw_graph_record_count,
                                                   error, sizeof(error)) ||
                    level_draw_graph_get_record(&game.level_runtime, zone_index, upper_stream,
                                                draw_graph_record_count, &draw_graph_record,
                                                error, sizeof(error))) {
                    fprintf(stderr, "campaign level %u zone %u draw graph is invalid: %s\n",
                            level_index, zone_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                for (draw_graph_record_index = 0u;
                     draw_graph_record_index < draw_graph_record_count;
                     ++draw_graph_record_index) {
                    const uint8_t *source;

                    if (!level_draw_graph_get_record(&game.level_runtime, zone_index,
                                                     upper_stream, draw_graph_record_index,
                                                     &draw_graph_record, error, sizeof(error)) ||
                        draw_graph_record.byte_count < 2u ||
                        draw_graph_record.raw_tag != read_be16(
                            game.level_runtime.graphics_bytes +
                            draw_graph_record.source_offset)) {
                        fprintf(stderr,
                                "campaign level %u zone %u draw record %u is invalid: %s\n",
                                level_index, zone_index, draw_graph_record_index, error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    if (draw_graph_record.type == LEVEL_DRAW_GRAPH_TYPE_WALL) {
                        source = game.level_runtime.graphics_bytes + draw_graph_record.source_offset;
                        if (!level_draw_graph_read_wall(&game.level_runtime, &draw_graph_record,
                                                        &draw_wall, error, sizeof(error)) ||
                            draw_wall.left_point_index >= game.level_runtime.world_point_count ||
                            draw_wall.right_point_index >= game.level_runtime.world_point_count ||
                            draw_wall.left_point_index != read_be16(source + 2u) ||
                            draw_wall.right_point_index != read_be16(source + 4u) ||
                            draw_wall.texture_id != read_be16(source + 14u) ||
                            draw_wall.top != (int32_t)read_be32(source + 20u) ||
                            draw_wall.bottom != (int32_t)read_be32(source + 24u)) {
                            fprintf(stderr,
                                    "campaign level %u zone %u wall record %u is invalid: %s\n",
                                    level_index, zone_index, draw_graph_record_index, error);
                            game_bootstrap_destroy(&game);
                            return 1;
                        }
                        continue;
                    }
                    if (draw_graph_record.type != LEVEL_DRAW_GRAPH_TYPE_FLOOR &&
                        draw_graph_record.type != LEVEL_DRAW_GRAPH_TYPE_CEILING &&
                        draw_graph_record.type != LEVEL_DRAW_GRAPH_TYPE_WATER) {
                        continue;
                    }
                    source = game.level_runtime.graphics_bytes + draw_graph_record.source_offset;
                    if (!level_draw_graph_read_flat(&game.level_runtime, &draw_graph_record,
                                                    &draw_flat, error, sizeof(error)) ||
                        draw_flat.height != (int16_t)read_be16(source + 2u) ||
                        draw_flat.point_count != (uint16_t)(read_be16(source + 4u) + 1u) ||
                        draw_flat.points_offset != draw_graph_record.source_offset + 6u ||
                        draw_flat.skipped_word != read_be16(source + 6u +
                                                             (size_t)draw_flat.point_count * 2u) ||
                        draw_flat.texture_scale != read_be16(source + 8u +
                                                              (size_t)draw_flat.point_count * 2u) ||
                        draw_flat.texture_offset != read_be16(source + 10u +
                                                               (size_t)draw_flat.point_count * 2u) ||
                        draw_flat.brightness_offset != (int16_t)read_be16(
                            source + 12u + (size_t)draw_flat.point_count * 2u) ||
                        level_draw_graph_get_flat_point(&game.level_runtime, &draw_flat,
                                                        draw_flat.point_count, &flat_raw_point_word,
                                                        &flat_world_point_index,
                                                        error, sizeof(error))) {
                        fprintf(stderr,
                                "campaign level %u zone %u flat record %u is invalid: %s\n",
                                level_index, zone_index, draw_graph_record_index, error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    for (flat_point_index = 0u; flat_point_index < draw_flat.point_count;
                         ++flat_point_index) {
                        if (!level_draw_graph_get_flat_point(&game.level_runtime, &draw_flat,
                                                            flat_point_index, &flat_raw_point_word,
                                                            &flat_world_point_index,
                                                            error, sizeof(error)) ||
                            flat_raw_point_word != read_be16(source + 6u +
                                                              (size_t)flat_point_index * 2u) ||
                            flat_world_point_index != (flat_raw_point_word & 0x0fffu)) {
                            fprintf(stderr,
                                    "campaign level %u zone %u flat point %u is invalid: %s\n",
                                    level_index, zone_index, flat_point_index, error);
                            game_bootstrap_destroy(&game);
                            return 1;
                        }
                    }
                }
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
        for (static_wall_index = 0u; static_wall_index < game.static_scene.wall_count;
             ++static_wall_index) {
            const LevelStaticWallScene *scene_wall =
                &game.static_scene.walls[static_wall_index];
            const uint8_t *source = game.level_runtime.graphics_bytes +
                scene_wall->source_record_offset;
            LevelWorldPoint left_point;
            LevelWorldPoint right_point;
            int32_t top;
            int32_t bottom;

            if (scene_wall->source_record_offset > game.level_runtime.graphics_size ||
                30u > game.level_runtime.graphics_size - scene_wall->source_record_offset ||
                (uint8_t)read_be16(source) != LEVEL_DRAW_GRAPH_TYPE_WALL ||
                scene_wall->material_id != read_be16(source + 14u) ||
                !level_runtime_get_world_point(&game.level_runtime, read_be16(source + 2u),
                                               &left_point, error, sizeof(error)) ||
                !level_runtime_get_world_point(&game.level_runtime, read_be16(source + 4u),
                                               &right_point, error, sizeof(error))) {
                fprintf(stderr, "campaign level %u static wall %u is invalid: %s\n",
                        level_index, static_wall_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            top = (int32_t)read_be32(source + 20u);
            bottom = (int32_t)read_be32(source + 24u);
            if (scene_wall->vertices[0].position.x != left_point.x ||
                scene_wall->vertices[0].position.y != top ||
                scene_wall->vertices[0].position.z != left_point.z ||
                scene_wall->vertices[1].position.x != right_point.x ||
                scene_wall->vertices[1].position.y != top ||
                scene_wall->vertices[1].position.z != right_point.z ||
                scene_wall->vertices[2].position.x != right_point.x ||
                scene_wall->vertices[2].position.y != bottom ||
                scene_wall->vertices[2].position.z != right_point.z ||
                scene_wall->vertices[5].position.x != left_point.x ||
                scene_wall->vertices[5].position.y != bottom ||
                scene_wall->vertices[5].position.z != left_point.z ||
                scene_wall->vertices[0].texture_u != 0 ||
                scene_wall->vertices[0].texture_v != 0) {
                fprintf(stderr, "campaign level %u static wall %u geometry is inconsistent\n",
                        level_index, static_wall_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        for (static_flat_index = 0u; static_flat_index < game.static_scene.flat_count;
             ++static_flat_index) {
            const LevelStaticFlatScene *scene_flat =
                &game.static_scene.flats[static_flat_index];
            const uint8_t *source;
            LevelDrawGraphRecord flat_record;

            if (scene_flat->source_record_offset > game.level_runtime.graphics_size ||
                6u > game.level_runtime.graphics_size - scene_flat->source_record_offset) {
                fprintf(stderr, "campaign level %u static flat %u has an invalid source range\n",
                        level_index, static_flat_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            source = game.level_runtime.graphics_bytes + scene_flat->source_record_offset;
            flat_record.raw_tag = read_be16(source);
            flat_record.type = (uint8_t)flat_record.raw_tag;
            flat_record.source_offset = scene_flat->source_record_offset;
            flat_record.byte_count = 16u + (uint32_t)read_be16(source + 4u) * 2u;
            if ((flat_record.type != LEVEL_DRAW_GRAPH_TYPE_FLOOR &&
                 flat_record.type != LEVEL_DRAW_GRAPH_TYPE_CEILING &&
                 flat_record.type != LEVEL_DRAW_GRAPH_TYPE_WATER) ||
                !level_draw_graph_read_flat(&game.level_runtime, &flat_record, &draw_flat,
                                            error, sizeof(error)) ||
                scene_flat->vertices == NULL ||
                scene_flat->vertex_count != draw_flat.point_count ||
                scene_flat->material_id != draw_flat.texture_offset ||
                scene_flat->texture_scale != draw_flat.texture_scale ||
                scene_flat->brightness_offset != draw_flat.brightness_offset ||
                (flat_record.type == LEVEL_DRAW_GRAPH_TYPE_FLOOR &&
                 scene_flat->primitive != SCENE_GEOMETRY_PRIMITIVE_FLOOR) ||
                (flat_record.type == LEVEL_DRAW_GRAPH_TYPE_CEILING &&
                 scene_flat->primitive != SCENE_GEOMETRY_PRIMITIVE_CEILING) ||
                (flat_record.type == LEVEL_DRAW_GRAPH_TYPE_WATER &&
                 scene_flat->primitive != SCENE_GEOMETRY_PRIMITIVE_WATER)) {
                fprintf(stderr, "campaign level %u static flat %u is invalid: %s\n",
                        level_index, static_flat_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            for (flat_point_index = 0u; flat_point_index < draw_flat.point_count;
                 ++flat_point_index) {
                if (!level_draw_graph_get_flat_point(&game.level_runtime, &draw_flat,
                                                    flat_point_index, &flat_raw_point_word,
                                                    &flat_world_point_index,
                                                    error, sizeof(error)) ||
                    !level_runtime_get_world_point(&game.level_runtime, flat_world_point_index,
                                                   &world_point, error, sizeof(error)) ||
                    scene_flat->vertices[flat_point_index].position.x != world_point.x ||
                    scene_flat->vertices[flat_point_index].position.y !=
                        (int32_t)draw_flat.height * 64 ||
                    scene_flat->vertices[flat_point_index].position.z != world_point.z ||
                    scene_flat->vertices[flat_point_index].texture_u != 0 ||
                    scene_flat->vertices[flat_point_index].texture_v != 0) {
                    fprintf(stderr,
                            "campaign level %u static flat %u point %u is invalid: %s\n",
                            level_index, static_flat_index, flat_point_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
        }
        for (uint32_t object_index = 0;
             object_index < game.level_runtime.object_record_count; ++object_index) {
            const uint8_t *source = game.level_runtime.level_bytes +
                game.level_runtime.object_data_offset + (size_t)object_index * 64u;

            if (!level_runtime_get_object_record(&game.level_runtime, object_index,
                                                 &object_slot, error, sizeof(error)) ||
                object_slot.point_index >= game.level_runtime.object_point_count ||
                !level_runtime_get_object_point(&game.level_runtime, object_slot.point_index,
                                                &object_point, error, sizeof(error)) ||
                object_slot.type_id != source[16u] ||
                object_slot.sees_player != source[17u] ||
                object_slot.entity_type != source[54u] ||
                object_slot.which_animation != source[55u] ||
                (object_slot.type_id == 0u &&
                 object_slot.entity_type >= GAME_LINK_ALIEN_COUNT) ||
                (object_slot.type_id == 1u &&
                 object_slot.entity_type >= GAME_LINK_OBJECT_COUNT)) {
                fprintf(stderr, "campaign level %u object %u is invalid: %s\n",
                        level_index, object_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        for (world_point_index = 0u;
             world_point_index < game.level_runtime.world_point_count;
             ++world_point_index) {
            const uint8_t *source = game.level_runtime.level_bytes + game.level.points_offset +
                (size_t)world_point_index * 4u;

            if (!level_runtime_get_world_point(&game.level_runtime, world_point_index,
                                               &world_point, error, sizeof(error)) ||
                world_point.x != (int16_t)read_be16(source + 0u) ||
                world_point.z != (int16_t)read_be16(source + 2u)) {
                fprintf(stderr, "campaign level %u world point %u is invalid: %s\n",
                        level_index, world_point_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
    }
    if (!level_runtime_get_zone(&game.level_runtime, game.level.player1_start_zone,
                                &zone, error, sizeof(error)) ||
        game.player.x != game.level.player1_start_x ||
        game.player.z != game.level.player1_start_z ||
        game.player.y != zone.floor - 12 * 1024 ||
        game.player.snap_x != game.player.x || game.player.snap_y != game.player.y ||
        game.player.snap_z != game.player.z || game.player.snap_target_y != game.player.y ||
        game.player.height != 12 * 1024 ||
        game.player.default_enemy_flags != 0x23u || !scene_frame_init(&frame, 2) ||
        !game_bootstrap_submit_diagnostic_frame(&game, &frame) ||
        frame.count != 2u +
            ((size_t)game.static_scene.wall_count + game.static_scene.flat_count) * 2u ||
        frame.commands[0].type != SCENE_COMMAND_CAMERA ||
        frame.commands[0].data.camera.position.x != game.player.x ||
        game.static_scene.wall_count == 0u || game.static_scene.flat_count == 0u ||
        frame.commands[1].type != SCENE_COMMAND_MATERIAL ||
        frame.commands[1].data.material.source != SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE ||
        frame.commands[1].data.material.source_asset_id != game.static_scene.walls[0].material_id ||
        frame.commands[1].data.material.source_bytes !=
            game.shared_resources.wall_textures[game.static_scene.walls[0].material_id].bytes ||
        frame.commands[1].data.material.source_byte_count !=
            game.shared_resources.wall_textures[game.static_scene.walls[0].material_id].size ||
        frame.commands[1].data.material.source_palette_bytes !=
            game.shared_resources.wall_textures[game.static_scene.walls[0].material_id].bytes ||
        frame.commands[1].data.material.source_palette_byte_count != 64u * 32u ||
        frame.commands[2].type != SCENE_COMMAND_GEOMETRY ||
        frame.commands[2].data.geometry.vertices != game.static_scene.walls[0].vertices ||
        frame.commands[2].data.geometry.vertex_count != 6u ||
        frame.commands[2].data.geometry.topology != SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST ||
        frame.commands[2].data.geometry.primitive != SCENE_GEOMETRY_PRIMITIVE_WALL ||
        frame.commands[2].data.geometry.material_id != game.static_scene.walls[0].material_id ||
        frame.commands[2].data.geometry.source_record_id !=
            game.static_scene.walls[0].source_record_offset ||
        frame.commands[2].data.geometry.flags != SCENE_GEOMETRY_TEXTURE_COORDS_UNRESOLVED ||
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].type !=
            SCENE_COMMAND_MATERIAL ||
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source !=
            SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE ||
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source_asset_id !=
            game.static_scene.flats[0].material_id ||
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source_bytes !=
            game.shared_resources.floor_texture.bytes ||
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source_byte_count !=
            game.shared_resources.floor_texture.size ||
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source_palette_bytes !=
            game.shared_resources.texture_palette.bytes ||
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source_palette_byte_count !=
            game.shared_resources.texture_palette.size ||
        frame.commands[2u + (size_t)game.static_scene.wall_count * 2u].type !=
            SCENE_COMMAND_GEOMETRY ||
        frame.commands[2u + (size_t)game.static_scene.wall_count * 2u].data.geometry.vertices !=
            game.static_scene.flats[0].vertices ||
        frame.commands[2u + (size_t)game.static_scene.wall_count * 2u].data.geometry.vertex_count !=
            game.static_scene.flats[0].vertex_count ||
        frame.commands[2u + (size_t)game.static_scene.wall_count * 2u].data.geometry.topology !=
            SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY ||
        frame.commands[2u + (size_t)game.static_scene.wall_count * 2u].data.geometry.primitive !=
            game.static_scene.flats[0].primitive ||
        frame.commands[2u + (size_t)game.static_scene.wall_count * 2u].data.geometry.material_id !=
            game.static_scene.flats[0].material_id ||
        frame.commands[2u + (size_t)game.static_scene.wall_count * 2u].data.geometry.source_record_id !=
            game.static_scene.flats[0].source_record_offset ||
        frame.commands[2u + (size_t)game.static_scene.wall_count * 2u].data.geometry.flags !=
            SCENE_GEOMETRY_TEXTURE_COORDS_UNRESOLVED ||
        frame.commands[frame.count - 1u].type != SCENE_COMMAND_HUD_TEXT) {
        fprintf(stderr, "Plr_Initialise camera state is inconsistent: %s\n", error);
        scene_frame_destroy(&frame);
        game_bootstrap_destroy(&game);
        return 1;
    }
    saved_floor_override = game.level_floor_override;
    saved_wall_override = game.level_wall_overrides[game.static_scene.walls[0].material_id];
    memset(override_marker, 0, sizeof(override_marker));
    game.level_floor_override.bytes = override_marker;
    game.level_floor_override.size = sizeof(override_marker);
    game.level_wall_overrides[game.static_scene.walls[0].material_id].bytes = override_marker;
    game.level_wall_overrides[game.static_scene.walls[0].material_id].size = sizeof(override_marker);
    scene_frame_begin(&frame);
    override_sources_ok = game_bootstrap_submit_diagnostic_frame(&game, &frame) &&
        frame.commands[1].data.material.source ==
            SCENE_MATERIAL_SOURCE_LEVEL_WALL_TEXTURE_OVERRIDE &&
        frame.commands[1].data.material.source_bytes == override_marker &&
        frame.commands[1].data.material.source_byte_count == sizeof(override_marker) &&
        frame.commands[1].data.material.source_palette_bytes == override_marker &&
        frame.commands[1].data.material.source_palette_byte_count == sizeof(override_marker) &&
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source ==
            SCENE_MATERIAL_SOURCE_LEVEL_FLOOR_TEXTURE_OVERRIDE &&
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source_bytes ==
            override_marker &&
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source_byte_count ==
            sizeof(override_marker) &&
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source_palette_bytes ==
            game.shared_resources.texture_palette.bytes &&
        frame.commands[1u + (size_t)game.static_scene.wall_count * 2u].data.material.source_palette_byte_count ==
            game.shared_resources.texture_palette.size;
    game.level_floor_override = saved_floor_override;
    game.level_wall_overrides[game.static_scene.walls[0].material_id] = saved_wall_override;
    if (!override_sources_ok) {
        fprintf(stderr, "Res_LoadLevelData material override selection is inconsistent\n");
        scene_frame_destroy(&frame);
        game_bootstrap_destroy(&game);
        return 1;
    }
    scene_frame_destroy(&frame);
    game_controls_default(&control_defaults);
    game_input_init(&control_input);
    controlled_player = game.player;
    if (!game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_OPERATE], 1,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 error, sizeof(error)) ||
        controlled_player.used != UINT8_MAX ||
        controlled_player.previous_use_key_state != UINT8_MAX ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 error, sizeof(error)) ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_OPERATE], 0,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 error, sizeof(error)) ||
        controlled_player.previous_use_key_state != 0u ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_CROUCH], 1,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 error, sizeof(error)) ||
        controlled_player.ducked != UINT8_MAX ||
        game_input_is_control_down(&control_input, &control_defaults, GAME_CONTROL_CROUCH) ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_FIRE], 1,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 error, sizeof(error)) ||
        controlled_player.fire != UINT8_MAX || controlled_player.clicked != UINT8_MAX ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_FIRE], 0,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 error, sizeof(error)) ||
        controlled_player.fire != 0u || controlled_player.clicked != UINT8_MAX ||
        controlled_player.x != game.player.x || controlled_player.y != game.player.y ||
        controlled_player.z != game.player.z || controlled_player.yaw != game.player.yaw) {
        fprintf(stderr, "plr_KeyboardControl discrete state is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
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
