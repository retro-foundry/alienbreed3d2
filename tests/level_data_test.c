#include <stdio.h>
#include <string.h>

#include "alien_runtime.h"
#include "asset_io.h"
#include "game_bootstrap.h"
#include "game_link.h"
#include "game_inventory.h"
#include "game_menu.h"
#include "game_random.h"
#include "game_save.h"
#include "level_bootstrap.h"
#include "level_draw_graph.h"
#include "object_collectables.h"
#include "object_handler.h"
#include "object_movement.h"
#include "object_projectiles.h"
#include "object_scene.h"
#include "object_visibility.h"
#include "player_entity.h"
#include "player_shoot.h"
#include "scene_frame.h"

static uint16_t read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static int16_t source_asr16_2(int16_t value)
{
    if (value >= 0) {
        return (int16_t)(value / 4);
    }
    return (int16_t)-(((int32_t)-value + 3) / 4);
}

static int32_t source_asr32_7(int32_t value)
{
    if (value >= 0) {
        return value >> 7;
    }
    return -((-(int64_t)value + 127) >> 7);
}

static int32_t source_asr32_count(int32_t value, unsigned int count)
{
    if (value >= 0) {
        return value >> count;
    }
    return -(((-(int64_t)value) + ((INT64_C(1) << count) - 1)) >> count);
}

static int32_t source_asr32_6(int32_t value)
{
    if (value >= 0) {
        return value >> 6;
    }
    return -((-(int64_t)value + 63) >> 6);
}

static int16_t source_asr16_1(int16_t value)
{
    if (value >= 0) {
        return (int16_t)(value >> 1);
    }
    return (int16_t)-(((-(int32_t)value) + 1) >> 1);
}

static uint32_t read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static int scene_sprite_commands_match_source(const SceneFrame *frame,
                                              size_t first_command,
                                              const GameBootstrap *game,
                                              uint32_t expected_count,
                                              char *error, size_t error_size)
{
    uint32_t command_count = 0u;

    if (!frame || !game || first_command > frame->count ||
        expected_count > frame->count - first_command) {
        return 0;
    }
    for (uint32_t slot_index = 0u;
         slot_index < game->object_runtime.active_slot_count; ++slot_index) {
        const uint8_t *slot = game->object_runtime.slot_bytes +
            (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
        const uint8_t *point;
        const SceneSprite *sprite;
        int16_t point_index = (int16_t)read_be16(slot + 0u);
        int16_t zone_id = (int16_t)read_be16(slot + 12u);
        int16_t graphics_type = (int16_t)read_be16(slot + 8u);
        uint16_t asset_index;
        uint8_t expected_flags = slot[63u] != 0u ? SCENE_SPRITE_FLAG_UPPER_ZONE : 0u;

        if (point_index < 0) {
            break;
        }
        if (zone_id < 0) {
            continue;
        }
        if ((uint16_t)point_index >= game->object_runtime.point_count ||
            command_count >= expected_count ||
            first_command + command_count >= frame->count ||
            frame->commands[first_command + command_count].type != SCENE_COMMAND_SPRITE) {
            return 0;
        }
        point = game->object_runtime.point_bytes +
            (size_t)(uint16_t)point_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;
        sprite = &frame->commands[first_command + command_count].data.sprite;
        if (sprite->source_record_id != slot_index ||
            sprite->position.x != (int16_t)read_be16(point + 0u) ||
            sprite->position.y != (int32_t)(int16_t)read_be16(slot + 4u) * 128 ||
            sprite->position.z != (int16_t)read_be16(point + 4u) ||
            sprite->source_brightness != read_be16(slot + 2u) ||
            sprite->yaw != read_be16(slot + 30u) ||
            sprite->source_aux_offset_x != (int16_t)read_be16(slot + 44u) ||
            sprite->source_aux_offset_y != (int16_t)read_be16(slot + 46u)) {
            return 0;
        }
        if (slot[6u] == UINT8_MAX) {
            asset_index = (uint16_t)graphics_type;
            if (asset_index >= game->shared_resources.vector_count ||
                sprite->source != SCENE_SPRITE_SOURCE_VECTOR_MODEL ||
                sprite->source_asset_id != asset_index ||
                sprite->frame_index != read_be16(slot + 10u) ||
                sprite->source_bytes != game->shared_resources.vector_models[asset_index].bytes ||
                sprite->source_byte_count != game->shared_resources.vector_models[asset_index].size ||
                sprite->source_aux_bytes != NULL || sprite->source_aux_byte_count != 0u ||
                sprite->source_palette_bytes != NULL || sprite->source_palette_byte_count != 0u ||
                sprite->source_width != 0u || sprite->source_height != 0u ||
                sprite->source_effect != 0u || sprite->flags != expected_flags ||
                sprite->frame_metrics.pointer_table_index != 0u ||
                sprite->frame_metrics.down_strip != 0u ||
                sprite->frame_metrics.strip_count != 0u ||
                sprite->frame_metrics.line_count != 0u) {
                return 0;
            }
        } else {
            GameObjectFrameData source_frame;
            uint16_t frame_index;
            int glare = graphics_type < 0;

            asset_index = glare != 0 ?
                (uint16_t)(0u - (uint16_t)graphics_type) : (uint16_t)graphics_type;
            frame_index = glare != 0 ? read_be16(slot + 10u) : slot[11u];
            if (asset_index >= game->shared_resources.object_count ||
                !game_link_get_object_frame_data(&game->game_link_catalog, asset_index,
                                                  frame_index, &source_frame,
                                                  error, error_size) ||
                sprite->source != (glare != 0 ? SCENE_SPRITE_SOURCE_GLARE_BITMAP :
                                                SCENE_SPRITE_SOURCE_OBJECT_BITMAP) ||
                sprite->source_asset_id != asset_index || sprite->frame_index != frame_index ||
                sprite->source_bytes != game->shared_resources.object_wads[asset_index].bytes ||
                sprite->source_byte_count != game->shared_resources.object_wads[asset_index].size ||
                sprite->source_aux_bytes != game->shared_resources.object_ptrs[asset_index].bytes ||
                sprite->source_aux_byte_count != game->shared_resources.object_ptrs[asset_index].size ||
                sprite->source_width != slot[6u] || sprite->source_height != slot[7u] ||
                sprite->frame_metrics.pointer_table_index != source_frame.pointer_table_index ||
                sprite->frame_metrics.down_strip != source_frame.down_strip ||
                sprite->frame_metrics.strip_count != source_frame.strip_count ||
                sprite->frame_metrics.line_count != source_frame.line_count) {
                return 0;
            }
            if (glare != 0) {
                if (sprite->source_palette_bytes != game->shared_resources.texture_palette.bytes ||
                    sprite->source_palette_byte_count != game->shared_resources.texture_palette.size ||
                    sprite->source_effect != 0u || sprite->flags != expected_flags) {
                    return 0;
                }
            } else {
                uint8_t effect = slot[10u];
                uint8_t effect_class = (uint8_t)(effect & 0x7fu);

                if ((effect & 0x80u) != 0u) {
                    expected_flags |= SCENE_SPRITE_FLAG_FLIP_HORIZONTAL;
                }
                if (effect_class >= 2u) {
                    expected_flags |= effect_class < 6u ? SCENE_SPRITE_FLAG_LIGHT_PALETTE :
                                                         SCENE_SPRITE_FLAG_ADDITIVE;
                }
                if (sprite->source_palette_bytes !=
                        game->shared_resources.object_palettes[asset_index].bytes ||
                    sprite->source_palette_byte_count !=
                        game->shared_resources.object_palettes[asset_index].size ||
                    sprite->source_effect != effect || sprite->flags != expected_flags) {
                    return 0;
                }
            }
        }
        ++command_count;
    }
    return command_count == expected_count;
}

static int object_observation_matches_source(const ObjectObservation *observation,
                                             const ObjectRuntime *objects,
                                             const PlayerRuntime *player,
                                             const GameMath *math,
                                             char *error, size_t error_size)
{
    uint32_t slot_index = 0u;
    uint32_t point_index = 0u;
    uint32_t output_index = 0u;
    int16_t sine;
    int16_t cosine;

    if (!observation || !objects || !objects->slot_bytes || !objects->point_bytes || !player ||
        !math || !game_math_sine(math, player->yaw, &sine, error, error_size) ||
        !game_math_cosine(math, player->yaw, &cosine, error, error_size)) {
        return 0;
    }
    while (point_index < objects->point_count) {
        const uint8_t *slot;
        const uint8_t *point;
        int16_t offset_x;
        int16_t offset_z;
        uint8_t expected_in_line;
        uint16_t expected_distance;

        if (slot_index >= objects->slot_count ||
            output_index >= OBJECT_OBSERVATION_DISTANCE_COUNT ||
            output_index >= OBJECT_OBSERVATION_IN_LINE_COUNT) {
            return 0;
        }
        slot = objects->slot_bytes + (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
        if (slot[16u] == 3u) {
            ++slot_index;
            continue;
        }
        point = objects->point_bytes + (size_t)point_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;
        offset_x = (int16_t)((int32_t)(int16_t)read_be16(point + 0u) -
                             (int16_t)(uint16_t)player->x);
        offset_z = (int16_t)((int32_t)(int16_t)read_be16(point + 4u) -
                             (int16_t)(uint16_t)player->z);
        expected_in_line = 0u;
        expected_distance = 0u;
        if ((int16_t)read_be16(slot + 12u) >= 0) {
            int32_t horizontal = (int32_t)((uint32_t)((int32_t)offset_x * cosine) -
                                           (uint32_t)((int32_t)offset_z * sine));
            int32_t depth = (int32_t)((uint32_t)((int32_t)offset_x * sine) +
                                      (uint32_t)((int32_t)offset_z * cosine));
            int16_t horizontal_word;
            int16_t depth_word;

            horizontal = (int32_t)((uint32_t)horizontal << 1);
            if (horizontal <= 0) {
                horizontal = (int32_t)(0u - (uint32_t)horizontal);
            }
            horizontal_word = (int16_t)(uint16_t)((uint32_t)horizontal >> 16);
            depth_word = (int16_t)(uint16_t)(((uint32_t)depth << 2) >> 16);
            expected_distance = (uint16_t)depth_word;
            if (depth_word > 0 && source_asr16_1(horizontal_word) <= 80) {
                expected_in_line = UINT8_MAX;
            }
        }
        if (observation->in_line[output_index] != expected_in_line ||
            observation->distances[output_index] != expected_distance) {
            return 0;
        }
        ++slot_index;
        ++point_index;
        ++output_index;
    }
    return 1;
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

static int bullet_animation_frame_matches_source(const GameBulletAnimationFrame *frame,
                                                 const uint8_t *source)
{
    return frame && source && frame->byte_0 == source[0u] &&
        frame->byte_1 == source[1u] && frame->word_2 == read_be16(source + 2u) &&
        frame->byte_4 == source[4u] && frame->byte_5 == source[5u];
}

static int object_inventory_grant_matches_source(const GameInventory *grant,
                                                 const uint8_t *ammunition_source,
                                                 const uint8_t *item_source)
{
    if (!grant || !ammunition_source || !item_source ||
        grant->health != read_be16(ammunition_source + 0u) ||
        grant->jetpack_fuel != read_be16(ammunition_source + 2u) ||
        grant->shield != read_be16(item_source + 0u) ||
        grant->jetpack != read_be16(item_source + 2u)) {
        return 0;
    }
    for (uint16_t index = 0u; index < GAME_INVENTORY_AMMUNITION_COUNT; ++index) {
        if (grant->ammunition[index] !=
            read_be16(ammunition_source + 4u + (size_t)index * 2u)) {
            return 0;
        }
    }
    for (uint16_t index = 0u; index < GAME_INVENTORY_WEAPON_COUNT; ++index) {
        if (grant->weapons[index] != read_be16(item_source + 4u + (size_t)index * 2u)) {
            return 0;
        }
    }
    return 1;
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
    const uint8_t *object_ammunition_grant_bytes;
    const uint8_t *object_item_grant_bytes;
    size_t table_size;
    size_t shoot_definition_size;
    size_t alien_definition_size;
    size_t object_definition_size;
    size_t object_default_animation_size;
    size_t object_action_animation_size;
    size_t object_frame_data_size;
    size_t object_ammunition_grant_size;
    size_t object_item_grant_size;
    uint16_t level_index;
    uint16_t zone_index;
    uint16_t object_definition_index;
    uint16_t object_animation_index;
    uint16_t object_frame_data_index;
    uint16_t shoot_definition_index;
    uint16_t alien_definition_index;
    uint16_t bullet_definition_index;
    uint16_t bullet_animation_index;
    int16_t trig_value;
    char text[128];
    uint8_t campaign_record[GAME_SESSION_RECORD_SIZE];
    static const uint8_t expected_control_defaults[GAME_CONTROL_PERSISTED_BYTE_COUNT] = {
        0x4fu, 0x4eu, 0x11u, 0x21u, 0x63u, 0x23u, 0x60u, 0x64u, 0x20u,
        0x22u, 0x33u, 0x28u, 0x40u, 0x0cu, 0x0bu, 0x29u, 0x0du, 0x00u
    };
    LevelZone zone;
    LevelPotentialVisibility potential_visibility;
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
    LevelWaterAnimation water_animation;
    LevelWaterAnimationTarget water_target;
    LevelSwitch switch_record;
    LevelObjectSlot object_slot;
    LevelObjectPoint object_point;
    GameObjectDefinition object_definition;
    GameObjectAnimationFrame object_animation_frame;
    GameObjectFrameData object_frame_data;
    GameShootDefinition shoot_definition;
    GameAlienDefinition alien_definition;
    GameBulletDefinition bullet_definition;
    GameBulletAnimationFrame bullet_animation_frame;
    GameInventory object_inventory_grant;
    GameInventory inventory_test;
    GameInventoryConsumableLimits inventory_limits;
    uint8_t game_properties_test[GAME_INVENTORY_GAME_PROPERTIES_SIZE] = {0};
    uint32_t zone_edge_count;
    uint32_t zone_edge_index;
    uint32_t world_point_index;
    uint32_t decoration_fixture_count = 0u;
    uint32_t destructible_fixture_count = 0u;
    uint32_t water_fixture_count = 0u;
    uint32_t draw_graph_record_count;
    uint32_t draw_graph_record_index;
    uint32_t static_wall_index;
    uint32_t static_flat_index;
    uint32_t active_sprite_count;
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
    GameSaveSlots archived_save_slots;
    GameSaveSlots saved_save_slots;
    GameRandom random;
    AlienRuntime alien_runtime;
    int should_quit;
    uint8_t override_marker[64u * 32u];
    AssetBlob saved_floor_override;
    AssetBlob saved_wall_override;
    int override_sources_ok;
    char save_path[1024];
    char error[256];

    if (argc != 3) {
        fprintf(stderr, "usage: %s <data-root> <archived-boot.dat>\n", argv[0]);
        return 2;
    }
    game_random_init(&random);
    if (random.state != 234u || game_random_next(&random) != 0x2a93u ||
        game_random_next(&random) != 0x77dcu || game_random_next(&random) != 0xe226u) {
        fprintf(stderr, "objectmove.s GetRand source sequence is inconsistent\n");
        return 1;
    }
    random.state = UINT16_MAX;
    if (game_random_next(&random) != 0x2342u || random.state != 0x2342u) {
        fprintf(stderr, "objectmove.s GetRand word wrapping is inconsistent\n");
        return 1;
    }
    alien_runtime_init(&alien_runtime);
    for (uint16_t workspace_index = 0u;
         workspace_index < ALIEN_RUNTIME_ENTITY_COUNT; ++workspace_index) {
        alien_runtime.entity_workspace[workspace_index][6u] =
            (int16_t)(workspace_index + 1u);
        alien_runtime.entity_workspace[workspace_index][7u] =
            (int16_t)-(int32_t)(workspace_index + 1u);
        alien_runtime.damage[workspace_index] = -1;
    }
    for (uint16_t workspace_index = 0u;
         workspace_index < ALIEN_RUNTIME_TEAM_COUNT; ++workspace_index) {
        alien_runtime.team_workspace[workspace_index][6u] =
            (int16_t)(workspace_index + 1u);
        alien_runtime.team_workspace[workspace_index][7u] =
            (int16_t)-(int32_t)(workspace_index + 1u);
    }
    alien_runtime.boredom[0u][0u] = 0x1234;
    alien_runtime_begin_level(&alien_runtime);
    for (uint16_t workspace_index = 0u;
         workspace_index < ALIEN_RUNTIME_ENTITY_COUNT; ++workspace_index) {
        if (alien_runtime.entity_workspace[workspace_index][0u] != 0 ||
            alien_runtime.entity_workspace[workspace_index][1u] != 0 ||
            alien_runtime.entity_workspace[workspace_index][2u] != -1 ||
            alien_runtime.entity_workspace[workspace_index][3u] != -1 ||
            alien_runtime.entity_workspace[workspace_index][4u] != -1 ||
            alien_runtime.entity_workspace[workspace_index][5u] != -1 ||
            alien_runtime.entity_workspace[workspace_index][6u] !=
                (int16_t)(workspace_index + 1u) ||
            alien_runtime.entity_workspace[workspace_index][7u] !=
                (int16_t)-(int32_t)(workspace_index + 1u) ||
            alien_runtime.damage[workspace_index] != 0) {
            fprintf(stderr, "Game_Begin alien workspace initialization is inconsistent\n");
            return 1;
        }
    }
    for (uint16_t workspace_index = 0u;
         workspace_index < ALIEN_RUNTIME_TEAM_COUNT; ++workspace_index) {
        if (alien_runtime.team_workspace[workspace_index][0u] != 0 ||
            alien_runtime.team_workspace[workspace_index][1u] != 0 ||
            alien_runtime.team_workspace[workspace_index][2u] != -1 ||
            alien_runtime.team_workspace[workspace_index][3u] != -1 ||
            alien_runtime.team_workspace[workspace_index][4u] != -1 ||
            alien_runtime.team_workspace[workspace_index][5u] != -1 ||
            alien_runtime.team_workspace[workspace_index][6u] !=
                (int16_t)(workspace_index + 1u) ||
            alien_runtime.team_workspace[workspace_index][7u] !=
                (int16_t)-(int32_t)(workspace_index + 1u)) {
            fprintf(stderr, "Game_Begin alien team workspace initialization is inconsistent\n");
            return 1;
        }
    }
    if (alien_runtime.boredom[0u][0u] != 0x1234) {
        fprintf(stderr, "AI_InitAlienWorkspace unexpectedly reset source boredom state\n");
        return 1;
    }
    if (!asset_io_join(argv[1], "test_boot.dat", save_path, sizeof(save_path))) {
        fprintf(stderr, "could not construct temporary source boot.dat path\n");
        return 1;
    }
    if (!game_save_load_file(&archived_save_slots, argv[2], error, sizeof(error)) ||
        !game_save_slot_level_index(&archived_save_slots, 0u, &level_index,
                                    error, sizeof(error)) ||
        level_index != GAME_LINK_LEVEL_COUNT ||
        !game_save_slot_level_index(&archived_save_slots, 1u, &level_index,
                                    error, sizeof(error)) ||
        level_index != 0u ||
        game_save_load_campaign_slot(&archived_save_slots, 0u, &decoded_session,
                                     error, sizeof(error)) ||
        !game_save_write_file(&archived_save_slots, save_path, error, sizeof(error))) {
        fprintf(stderr, "archived source boot.dat fixture is inconsistent: %s\n", error);
        return 1;
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
        !game_link_table(&game_link, GAME_LINK_TABLE_AMMO_GIVE,
                         &object_ammunition_grant_bytes, &object_ammunition_grant_size) ||
        object_ammunition_grant_size != (size_t)GAME_LINK_OBJECT_COUNT * 44u ||
        !game_link_table(&game_link, GAME_LINK_TABLE_GUN_GIVE,
                         &object_item_grant_bytes, &object_item_grant_size) ||
        object_item_grant_size != (size_t)GAME_LINK_OBJECT_COUNT * 24u ||
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
        for (bullet_animation_index = 0u;
             bullet_animation_index < GAME_LINK_BULLET_ANIMATION_FRAME_COUNT;
             ++bullet_animation_index) {
            size_t frame_offset = (size_t)bullet_animation_index *
                                  GAME_LINK_BULLET_ANIMATION_FRAME_SIZE;

            if (!game_link_get_bullet_animation_frame(
                    &game_link, GAME_LINK_BULLET_ANIMATION_FLIGHT,
                    bullet_definition_index, bullet_animation_index,
                    &bullet_animation_frame, error, sizeof(error)) ||
                !bullet_animation_frame_matches_source(&bullet_animation_frame,
                                                       bullet_definition.animation_data + frame_offset) ||
                !game_link_get_bullet_animation_frame(
                    &game_link, GAME_LINK_BULLET_ANIMATION_POP,
                    bullet_definition_index, bullet_animation_index,
                    &bullet_animation_frame, error, sizeof(error)) ||
                !bullet_animation_frame_matches_source(&bullet_animation_frame,
                                                       bullet_definition.pop_data + frame_offset)) {
                fprintf(stderr, "GLFT bullet animation %u frame %u is inconsistent: %s\n",
                        bullet_definition_index, bullet_animation_index, error);
                asset_blob_release(&game_link_blob);
                return 1;
            }
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
        if (!game_link_get_object_inventory_grant(&game_link, object_definition_index,
                                                  &object_inventory_grant,
                                                  error, sizeof(error)) ||
            !object_inventory_grant_matches_source(
                &object_inventory_grant,
                object_ammunition_grant_bytes + (size_t)object_definition_index * 44u,
                object_item_grant_bytes + (size_t)object_definition_index * 24u)) {
            fprintf(stderr, "GLFT object inventory grant %u is inconsistent: %s\n",
                    object_definition_index, error);
            asset_blob_release(&game_link_blob);
            return 1;
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
        game_link_get_object_inventory_grant(&game_link, GAME_LINK_OBJECT_COUNT,
                                             &object_inventory_grant,
                                             error, sizeof(error)) ||
        game_link_get_shoot_definition(&game_link, GAME_LINK_GUN_COUNT,
                                       &shoot_definition, error, sizeof(error)) ||
        game_link_get_alien_definition(&game_link, GAME_LINK_ALIEN_COUNT,
                                       &alien_definition, error, sizeof(error)) ||
        game_link_get_bullet_definition(&game_link, GAME_LINK_BULLET_COUNT,
                                        &bullet_definition, error, sizeof(error)) ||
        game_link_get_bullet_animation_frame(&game_link, GAME_LINK_BULLET_ANIMATION_FLIGHT,
                                             0u, GAME_LINK_BULLET_ANIMATION_FRAME_COUNT,
                                             &bullet_animation_frame, error, sizeof(error)) ||
        game_link_get_bullet_animation_frame(&game_link, (GameBulletAnimationKind)2,
                                             0u, 0u, &bullet_animation_frame,
                                             error, sizeof(error))) {
        fprintf(stderr, "GLFT definition bounds checks are inconsistent\n");
        asset_blob_release(&game_link_blob);
        return 1;
    }
    if (!game_inventory_decode_game_properties(NULL, 0u, &inventory_limits) ||
        inventory_limits.health != GAME_INVENTORY_DEFAULT_HEALTH_LIMIT ||
        inventory_limits.jetpack_fuel != GAME_INVENTORY_DEFAULT_FUEL_LIMIT ||
        inventory_limits.ammunition[0] != GAME_INVENTORY_DEFAULT_AMMUNITION_LIMIT) {
        fprintf(stderr, "source default inventory limits are inconsistent\n");
        asset_blob_release(&game_link_blob);
        return 1;
    }
    game_properties_test[0] = 0x00u;
    game_properties_test[1] = 0x32u;
    game_properties_test[2] = 0x7du;
    game_properties_test[3] = 0x00u;
    game_properties_test[4] = 0x7cu;
    game_properties_test[5] = 0xffu;
    game_properties_test[6] = 0xffu;
    game_properties_test[7] = 0xffu;
    if (!game_inventory_decode_game_properties(game_properties_test,
                                               sizeof(game_properties_test),
                                               &inventory_limits) ||
        inventory_limits.health != 50u ||
        inventory_limits.jetpack_fuel != GAME_INVENTORY_DEFAULT_FUEL_LIMIT ||
        inventory_limits.ammunition[0] != 31999u ||
        inventory_limits.ammunition[1] != GAME_INVENTORY_DEFAULT_AMMUNITION_LIMIT) {
        fprintf(stderr, "source game.props limit handling is inconsistent\n");
        asset_blob_release(&game_link_blob);
        return 1;
    }
    memset(&inventory_test, 0, sizeof(inventory_test));
    memset(&object_inventory_grant, 0, sizeof(object_inventory_grant));
    inventory_test.health = 49u;
    inventory_test.jetpack_fuel = GAME_INVENTORY_DEFAULT_FUEL_LIMIT;
    inventory_test.ammunition[0] = 31998u;
    inventory_test.weapons[3] = 0x0001u;
    object_inventory_grant.health = 10u;
    object_inventory_grant.jetpack_fuel = 1u;
    object_inventory_grant.ammunition[0] = 10u;
    object_inventory_grant.weapons[3] = 0x0001u;
    if (!game_inventory_can_collect_single_player(&inventory_test, &object_inventory_grant,
                                                  &inventory_limits)) {
        fprintf(stderr, "single-player inventory grant was unexpectedly rejected\n");
        asset_blob_release(&game_link_blob);
        return 1;
    }
    game_inventory_apply_grant(&inventory_test, &object_inventory_grant, &inventory_limits);
    if (inventory_test.health != 50u ||
        inventory_test.jetpack_fuel != GAME_INVENTORY_DEFAULT_FUEL_LIMIT ||
        inventory_test.ammunition[0] != 31999u || inventory_test.weapons[3] != 0x0001u) {
        fprintf(stderr, "source inventory saturated add is inconsistent\n");
        asset_blob_release(&game_link_blob);
        return 1;
    }
    memset(&object_inventory_grant, 0, sizeof(object_inventory_grant));
    if (!game_inventory_can_collect_single_player(&inventory_test, &object_inventory_grant,
                                                  &inventory_limits)) {
        fprintf(stderr, "empty source inventory grant was unexpectedly rejected\n");
        asset_blob_release(&game_link_blob);
        return 1;
    }
    asset_blob_release(&game_link_blob);

    if (!game_bootstrap_init(&game, argv[1], error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (game.random.state != 234u) {
        fprintf(stderr, "Game_Start source random seed is inconsistent\n");
        game_bootstrap_destroy(&game);
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
    (void)game_menu_init(&menu, &game, save_path, error, sizeof(error));
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
    (void)game_menu_init(&menu, &game, save_path, error, sizeof(error));
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
    (void)game_menu_init(&menu, &game, save_path, error, sizeof(error));
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
    (void)game_menu_init(&menu, &game, save_path, error, sizeof(error));
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
    (void)game_menu_init(&menu, &game, save_path, error, sizeof(error));
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
    if (!game_save_load_file(&saved_save_slots, save_path, error, sizeof(error)) ||
        memcmp(saved_save_slots.bytes, archived_save_slots.bytes,
               sizeof(saved_save_slots.bytes)) != 0 ||
        !game_save_store_campaign_slot(&saved_save_slots, 0u, &encoded_session,
                                       error, sizeof(error)) ||
        !game_save_write_file(&saved_save_slots, save_path, error, sizeof(error)) ||
        !game_save_load_file(&saved_save_slots, save_path, error, sizeof(error)) ||
        memcmp(saved_save_slots.bytes, archived_save_slots.bytes,
               GAME_SESSION_RECORD_SIZE) != 0 ||
        memcmp(saved_save_slots.bytes + 2u * GAME_SESSION_RECORD_SIZE,
               archived_save_slots.bytes + 2u * GAME_SESSION_RECORD_SIZE,
               GAME_SAVE_FILE_SIZE - 2u * GAME_SESSION_RECORD_SIZE) != 0 ||
        !game_save_load_campaign_slot(&saved_save_slots, 1u, &decoded_session,
                                      error, sizeof(error)) ||
        decoded_session.menu_level_index != encoded_session.menu_level_index ||
        memcmp(&decoded_session.campaign_inventory, &encoded_session.campaign_inventory,
               sizeof(encoded_session.campaign_inventory)) != 0 ||
        game_save_store_campaign_slot(&saved_save_slots, GAME_SAVE_USER_SLOT_COUNT,
                                      &encoded_session, error, sizeof(error)) ||
        game_save_slot_level_index(&saved_save_slots, GAME_SAVE_SLOT_COUNT, &level_index,
                                   error, sizeof(error))) {
        fprintf(stderr, "source boot.dat slot preservation is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    game.session = encoded_session;
    if (!game_menu_init(&menu, &game, save_path, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        menu.selection != 6u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE,
                                &should_quit, error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_SAVE_POSITION || menu.selection != 0u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE,
                                &should_quit, error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_MAIN ||
        !game_save_load_file(&saved_save_slots, save_path, error, sizeof(error)) ||
        !game_save_load_campaign_slot(&saved_save_slots, 1u, &decoded_session,
                                      error, sizeof(error)) ||
        decoded_session.menu_level_index != encoded_session.menu_level_index ||
        memcmp(&decoded_session.campaign_inventory, &encoded_session.campaign_inventory,
               sizeof(encoded_session.campaign_inventory)) != 0) {
        fprintf(stderr, "source save-position menu flow is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    if (!game_menu_init(&menu, &game, save_path, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        menu.selection != 5u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE,
                                &should_quit, error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_LOAD_POSITION || menu.selection != 0u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_DOWN,
                                &should_quit, error, sizeof(error)) ||
        menu.selection != 1u ||
        !game_menu_handle_input(&menu, &game, argv[1], GAME_MENU_INPUT_ACTIVATE,
                                &should_quit, error, sizeof(error)) ||
        menu.screen != GAME_MENU_SCREEN_MAIN || game.session.menu_level_index != 15u ||
        memcmp(&game.session.campaign_inventory, &encoded_session.campaign_inventory,
               sizeof(encoded_session.campaign_inventory)) != 0) {
        fprintf(stderr, "source load-position menu flow is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    if (!game_session_default(&game.session, &game.game_link_catalog, error, sizeof(error))) {
        fprintf(stderr, "could not restore DEFAULTGAME after save-menu test: %s\n", error);
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
            game.alien_runtime.entity_workspace[0u][0u] != 0 ||
            game.alien_runtime.entity_workspace[0u][2u] != -1 ||
            game.alien_runtime.team_workspace[0u][0u] != 0 ||
            game.alien_runtime.team_workspace[0u][2u] != -1 ||
            game.alien_runtime.damage[0u] != 0 ||
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
        if (!scene_frame_init(&frame, 1u) ||
            !object_scene_count_active(&game.object_runtime, &active_sprite_count,
                                       error, sizeof(error)) ||
            !game_bootstrap_submit_diagnostic_frame(&game, &frame) ||
            frame.count != 2u +
                ((size_t)game.static_scene.wall_count + game.static_scene.flat_count) * 2u +
                active_sprite_count ||
            !scene_sprite_commands_match_source(
                &frame, 1u +
                    ((size_t)game.static_scene.wall_count + game.static_scene.flat_count) * 2u,
                &game, active_sprite_count, error, sizeof(error)) ||
            frame.commands[frame.count - 1u].type != SCENE_COMMAND_HUD_TEXT) {
            fprintf(stderr, "campaign level %u source-object scene handoff is invalid: %s\n",
                    level_index, error);
            scene_frame_destroy(&frame);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint16_t pvs_zone_index = 0u; pvs_zone_index < game.level_runtime.zone_count;
             ++pvs_zone_index) {
            int pvs_terminated = 0;

            for (uint32_t pvs_entry_index = 0u;
                 pvs_entry_index < game.level_runtime.level_size / 8u;
                 ++pvs_entry_index) {
                if (!level_runtime_get_zone_potential_visibility(
                        &game.level_runtime, pvs_zone_index, pvs_entry_index,
                        &potential_visibility, error, sizeof(error))) {
                    fprintf(stderr, "campaign level %u PVST entry is invalid: %s\n",
                            level_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                if (potential_visibility.zone_index < 0) {
                    pvs_terminated = 1;
                    break;
                }
                if (!level_runtime_get_zone_draw_graph_streams(
                        &game.level_runtime, (uint16_t)potential_visibility.zone_index,
                        &draw_graph_streams, error, sizeof(error))) {
                    fprintf(stderr, "campaign level %u PVST target is invalid: %s\n",
                            level_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
            if (!pvs_terminated) {
                fprintf(stderr, "campaign level %u PVST has no source terminator\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        scene_frame_destroy(&frame);
        {
            const uint8_t *source_slot;
            const uint8_t *source_point;
            uint8_t *runtime_slot;
            uint8_t *runtime_point;
            uint8_t *weapon_slot;
            uint8_t *weapon_point;
            uint8_t *dynamic_level_byte;
            uint8_t *dynamic_graphics_byte;
            uint8_t original_level_byte;
            uint8_t original_graphics_byte;
            uint16_t original_edge_flags;
            uint16_t changed_edge_flags;
            uint16_t player_point_index;
            uint16_t weapon_point_index;
            uint16_t gun_object_type;
            int32_t weapon_height;
            int32_t weapon_bobble;

            if (game.object_runtime.player_shot_first_slot !=
                    (game.level_runtime.player_shot_offset -
                     game.level_runtime.object_data_offset) / OBJECT_RUNTIME_SLOT_BYTE_COUNT ||
                game.object_runtime.alien_shot_first_slot !=
                    (game.level_runtime.alien_shot_offset -
                     game.level_runtime.object_data_offset) / OBJECT_RUNTIME_SLOT_BYTE_COUNT ||
                game.object_runtime.player1_slot !=
                    (game.level_runtime.player1_object_offset -
                     game.level_runtime.object_data_offset) / OBJECT_RUNTIME_SLOT_BYTE_COUNT ||
                game.object_runtime.player2_slot !=
                    (game.level_runtime.player2_object_offset -
                     game.level_runtime.object_data_offset) / OBJECT_RUNTIME_SLOT_BYTE_COUNT ||
                !level_runtime_get_object_slot_bytes(
                    &game.level_runtime, game.object_runtime.player_shot_first_slot, &source_slot,
                    error, sizeof(error)) ||
                !object_runtime_get_player_shot_slot_bytes(&game.object_runtime, 0u,
                                                            &runtime_slot) ||
                memcmp(runtime_slot, source_slot, OBJECT_RUNTIME_SLOT_BYTE_COUNT) != 0 ||
                !level_runtime_get_object_slot_bytes(
                    &game.level_runtime,
                    game.object_runtime.alien_shot_first_slot +
                        OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT - 1u,
                    &source_slot, error, sizeof(error)) ||
                !object_runtime_get_alien_shot_slot_bytes(
                    &game.object_runtime, OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT - 1u,
                    &runtime_slot) ||
                memcmp(runtime_slot, source_slot, OBJECT_RUNTIME_SLOT_BYTE_COUNT) != 0 ||
                !level_runtime_get_object_slot_bytes(&game.level_runtime,
                                                     game.object_runtime.player1_slot, &source_slot,
                                                     error, sizeof(error)) ||
                !object_runtime_get_player1_slot_bytes(&game.object_runtime, &runtime_slot) ||
                memcmp(runtime_slot, source_slot, OBJECT_RUNTIME_SLOT_BYTE_COUNT) != 0 ||
                !level_runtime_get_object_slot_bytes(&game.level_runtime,
                                                     game.object_runtime.player2_slot, &source_slot,
                                                     error, sizeof(error)) ||
                !object_runtime_get_player2_slot_bytes(&game.object_runtime, &runtime_slot) ||
                memcmp(runtime_slot, source_slot, OBJECT_RUNTIME_SLOT_BYTE_COUNT) != 0 ||
                object_runtime_get_player_shot_slot_bytes(
                    &game.object_runtime, OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT, &runtime_slot) ||
                object_runtime_get_alien_shot_slot_bytes(
                    &game.object_runtime, OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT, &runtime_slot)) {
                fprintf(stderr, "campaign level %u Game_Begin object-pool mapping is invalid: %s\n",
                        level_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }

            if (!player_entity_sync_single_player(&game.object_runtime,
                                                  &game.dynamic_level.runtime,
                                                  &game.game_link_catalog, &game.player,
                                                  error, sizeof(error)) ||
                !object_runtime_get_player1_slot_bytes(&game.object_runtime, &runtime_slot) ||
                runtime_slot[16u] != 4u || runtime_slot[18u] != 10u ||
                read_be16(runtime_slot + 30u) != game.player.tmp_yaw ||
                runtime_slot[63u] != game.player.stood_in_top ||
                !level_runtime_get_zone(&game.dynamic_level.runtime, game.player.zone_index,
                                        &zone, error, sizeof(error)) ||
                read_be16(runtime_slot + 12u) != zone.id ||
                read_be16(runtime_slot + 4u) !=
                    (uint16_t)source_asr32_7((int32_t)((uint32_t)game.player.tmp_y +
                                                       (uint32_t)(game.player.tmp_height / 2)))) {
                fprintf(stderr, "campaign level %u Plr1_Use entity state is invalid: %s\n",
                        level_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            player_point_index = read_be16(runtime_slot);
            if (player_point_index >= game.object_runtime.point_count ||
                !object_runtime_get_point_bytes(&game.object_runtime, player_point_index,
                                                &runtime_point) ||
                read_be32(runtime_point + 0u) != (uint32_t)game.player.x ||
                read_be32(runtime_point + 4u) != (uint32_t)game.player.z) {
                fprintf(stderr, "campaign level %u Plr1_Use point publication is invalid\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (game.object_runtime.player1_slot > UINT32_MAX - 2u ||
                !object_runtime_get_slot_bytes(&game.object_runtime,
                                               game.object_runtime.player1_slot + 2u,
                                               &weapon_slot) ||
                !game_link_get_gun_object_type(&game.game_link_catalog,
                                               game.player.tmp_gun_selected, &gun_object_type,
                                               error, sizeof(error))) {
                fprintf(stderr, "campaign level %u Plr1_Use weapon setup is invalid: %s\n",
                        level_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            weapon_point_index = read_be16(weapon_slot + 0u);
            weapon_height = source_asr32_7(
                (int32_t)((uint32_t)game.player.tmp_y +
                          (uint32_t)source_asr32_count(game.player.tmp_height, 2u) +
                          10 * 128));
            weapon_bobble = source_asr32_count(game.player.bobble_y, 8u);
            weapon_bobble += source_asr32_count(weapon_bobble, 1u);
            if (weapon_point_index >= game.object_runtime.point_count ||
                !object_runtime_get_point_bytes(&game.object_runtime, weapon_point_index,
                                                &weapon_point) ||
                read_be16(weapon_slot + 12u) != zone.id ||
                read_be16(weapon_slot + 26u) != zone.id || weapon_slot[16u] != 1u ||
                weapon_slot[54u] != (uint8_t)gun_object_type || weapon_slot[55u] != UINT8_MAX ||
                read_be16(weapon_slot + 30u) !=
                    (uint16_t)((game.player.tmp_yaw + 4096u) & 8190u) ||
                read_be16(weapon_slot + 4u) !=
                    (uint16_t)((uint16_t)weapon_height + (uint16_t)weapon_bobble) ||
                weapon_slot[63u] != game.player.stood_in_top ||
                memcmp(weapon_point, runtime_point, OBJECT_RUNTIME_POINT_BYTE_COUNT) != 0) {
                fprintf(stderr, "campaign level %u Plr1_Use companion weapon is invalid\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (!object_observation_update_single_player(
                    &game.object_observation, &game.object_runtime, &game.player, &game.math,
                    error, sizeof(error)) ||
                !object_observation_matches_source(
                    &game.object_observation, &game.object_runtime, &game.player, &game.math,
                    error, sizeof(error))) {
                fprintf(stderr, "campaign level %u CalcPLR1InLine state is inconsistent: %s\n",
                        level_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }

            if (game.object_runtime.active_slot_count != game.level_runtime.object_record_count ||
                game.object_runtime.slot_count != game.level_runtime.object_record_count + 1u ||
                game.object_runtime.point_count != game.level_runtime.object_point_count ||
                !level_runtime_get_object_slot_bytes(&game.level_runtime, 0u, &source_slot,
                                                     error, sizeof(error)) ||
                !object_runtime_get_slot_bytes(&game.object_runtime, 0u, &runtime_slot) ||
                runtime_slot == source_slot ||
                memcmp(runtime_slot, source_slot, OBJECT_RUNTIME_SLOT_BYTE_COUNT) != 0 ||
                !level_runtime_get_object_slot_bytes(
                    &game.level_runtime, game.level_runtime.object_record_count,
                    &source_slot, error, sizeof(error)) ||
                !object_runtime_get_slot_bytes(&game.object_runtime,
                                               game.object_runtime.active_slot_count,
                                               &runtime_slot) ||
                (int16_t)read_be16(source_slot) >= 0 ||
                memcmp(runtime_slot, source_slot, OBJECT_RUNTIME_SLOT_BYTE_COUNT) != 0 ||
                level_runtime_get_object_slot_bytes(
                    &game.level_runtime, game.level_runtime.object_record_count + 1u,
                    &source_slot, error, sizeof(error)) ||
                object_runtime_get_slot_bytes(&game.object_runtime,
                                              game.object_runtime.slot_count,
                                              &runtime_slot) ||
                !level_runtime_get_object_point_bytes(&game.level_runtime, 0u, &source_point,
                                                      error, sizeof(error)) ||
                !object_runtime_get_point_bytes(&game.object_runtime, 0u, &runtime_point) ||
                runtime_point == source_point ||
                memcmp(runtime_point, source_point, OBJECT_RUNTIME_POINT_BYTE_COUNT) != 0 ||
                object_runtime_get_point_bytes(&game.object_runtime,
                                               game.object_runtime.point_count,
                                               &runtime_point)) {
                fprintf(stderr, "campaign level %u object runtime copy is invalid: %s\n",
                        level_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (game.dynamic_level.runtime.level_bytes == game.level_runtime.level_bytes ||
                game.dynamic_level.runtime.graphics_bytes == game.level_runtime.graphics_bytes ||
                game.dynamic_level.runtime.level_size != game.level_runtime.level_size ||
                game.dynamic_level.runtime.graphics_size != game.level_runtime.graphics_size ||
                memcmp(game.dynamic_level.runtime.level_bytes, game.level_runtime.level_bytes,
                       game.level_runtime.level_size) != 0 ||
                memcmp(game.dynamic_level.runtime.graphics_bytes,
                       game.level_runtime.graphics_bytes,
                       game.level_runtime.graphics_size) != 0 ||
                !level_dynamic_state_get_level_range(&game.dynamic_level, 0u, 1u,
                                                     &dynamic_level_byte) ||
                !level_dynamic_state_get_graphics_range(&game.dynamic_level, 0u, 1u,
                                                        &dynamic_graphics_byte) ||
                level_dynamic_state_get_level_range(&game.dynamic_level,
                                                    (uint32_t)game.level_runtime.level_size,
                                                    1u, &dynamic_level_byte) ||
                level_dynamic_state_get_graphics_range(
                    &game.dynamic_level, (uint32_t)game.level_runtime.graphics_size,
                    1u, &dynamic_graphics_byte)) {
                fprintf(stderr, "campaign level %u dynamic level state is invalid\n", level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            original_level_byte = game.level_runtime.level_bytes[0u];
            original_graphics_byte = game.level_runtime.graphics_bytes[0u];
            *dynamic_level_byte = (uint8_t)(original_level_byte ^ 0xffu);
            *dynamic_graphics_byte = (uint8_t)(original_graphics_byte ^ 0xffu);
            if (game.level_runtime.level_bytes[0u] != original_level_byte ||
                game.level_runtime.graphics_bytes[0u] != original_graphics_byte) {
                fprintf(stderr, "campaign level %u dynamic state altered staged media\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            *dynamic_level_byte = original_level_byte;
            *dynamic_graphics_byte = original_graphics_byte;
            if (game.level_runtime.edge_count != 0u &&
                (!level_dynamic_state_get_edge_flags(&game.dynamic_level, 0u,
                                                     &original_edge_flags) ||
                 !level_dynamic_state_or_edge_flags(&game.dynamic_level, 0u, 0x0100u) ||
                 !level_dynamic_state_get_edge_flags(&game.dynamic_level, 0u,
                                                     &changed_edge_flags) ||
                 changed_edge_flags != (uint16_t)(original_edge_flags | 0x0100u) ||
                 !level_dynamic_state_get_level_range(
                     &game.dynamic_level,
                     game.dynamic_level.runtime.edge_table_offset + 14u, 2u,
                     &dynamic_level_byte))) {
                fprintf(stderr, "campaign level %u dynamic EdgeT flags are invalid\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (game.level_runtime.edge_count != 0u) {
                dynamic_level_byte[0] = (uint8_t)(original_edge_flags >> 8);
                dynamic_level_byte[1] = (uint8_t)original_edge_flags;
            }
        }
        {
            uint32_t activatable_slot_index = 0u;
            uint8_t *activatable_slot = NULL;
            uint8_t *activatable_point = NULL;
            GameObjectDefinition activatable_definition;
            GameObjectAnimationFrame activatable_frame;

            while (activatable_slot_index < game.object_runtime.active_slot_count &&
                   (!object_runtime_get_slot_bytes(&game.object_runtime, activatable_slot_index,
                                                   &activatable_slot) ||
                    activatable_slot[16u] != 1u ||
                    !game_link_get_object_definition(
                        &game.game_link_catalog, activatable_slot[54u],
                        &activatable_definition, error, sizeof(error)) ||
                    activatable_definition.behaviour != 1u)) {
                ++activatable_slot_index;
            }
            if (activatable_slot_index < game.object_runtime.active_slot_count) {
                PlayerRuntime activatable_player = game.player;
                GameInventory activatable_inventory = game.session.player1_inventory;
                LevelZone activatable_zone;
                uint16_t activatable_point_index = read_be16(activatable_slot + 0u);

                if (!object_runtime_get_point_bytes(&game.object_runtime,
                                                    activatable_point_index,
                                                    &activatable_point) ||
                    !level_runtime_get_zone(&game.dynamic_level.runtime,
                                            activatable_player.zone_index, &activatable_zone,
                                            error, sizeof(error)) ||
                    !game_link_get_object_animation_frame(
                        &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                        activatable_slot[54u], 0u, &activatable_frame,
                        error, sizeof(error))) {
                    fprintf(stderr, "campaign level %u activatable fixture is invalid: %s\n",
                            level_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                write_be16(activatable_slot + 12u, activatable_player.zone_index);
                write_be16(activatable_slot + 34u, 0u);
                write_be16(activatable_slot + 40u, 0u);
                activatable_slot[55u] = 0u;
                activatable_slot[62u] = 0x80u;
                activatable_slot[63u] = activatable_player.stood_in_top;
                activatable_player.tmp_x = (int16_t)read_be16(activatable_point + 0u);
                activatable_player.tmp_z = (int16_t)read_be16(activatable_point + 4u);
                {
                    int32_t object_vertical = activatable_zone.floor >= 0 ?
                        activatable_zone.floor / 128 :
                        -((-(int64_t)activatable_zone.floor + 127) / 128);

                    object_vertical += (int16_t)activatable_frame.signed_byte_4 * 2;
                    activatable_player.tmp_y = object_vertical * 128 -
                        activatable_player.tmp_height / 2;
                }
                activatable_player.tmp_used = UINT8_MAX;
                if (!object_handler_update_single_player(
                        &game.object_runtime, &game.dynamic_level, &game.mechanism_runtime,
                        &game.game_link_catalog, &activatable_player,
                        &activatable_inventory, &game.inventory_limits, 1u,
                        NULL, error, sizeof(error)) ||
                    activatable_slot[55u] != UINT8_MAX ||
                    read_be16(activatable_slot + 34u) != 0u ||
                    read_be16(activatable_slot + 40u) != 0u ||
                    activatable_slot[62u] != 0x80u ||
                    read_be16(activatable_slot + 26u) != activatable_player.zone_index) {
                    fprintf(stderr, "campaign level %u ObjectHandler activatable dispatch is inconsistent: %s\n",
                            level_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
        }
        if (decoration_fixture_count == 0u || destructible_fixture_count == 0u) {
            uint32_t passive_slot_index;

            for (passive_slot_index = 0u;
                 passive_slot_index < game.object_runtime.active_slot_count;
                 ++passive_slot_index) {
                uint8_t *passive_slot;
                GameObjectDefinition passive_definition;
                GameObjectAnimationFrame passive_frame;
                LevelZone passive_zone;
                int32_t passive_height;

                if (!object_runtime_get_slot_bytes(&game.object_runtime, passive_slot_index,
                                                   &passive_slot) ||
                    passive_slot[16u] != 1u ||
                    (int16_t)read_be16(passive_slot + 12u) < 0 ||
                    !game_link_get_object_definition(&game.game_link_catalog, passive_slot[54u],
                                                     &passive_definition, error,
                                                     sizeof(error))) {
                    continue;
                }
                if (passive_definition.behaviour == 3u && decoration_fixture_count == 0u) {
                    if (!level_runtime_get_zone(&game.dynamic_level.runtime,
                                                read_be16(passive_slot + 12u), &passive_zone,
                                                error, sizeof(error)) ||
                        !game_link_get_object_animation_frame(
                            &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                            passive_slot[54u], 0u, &passive_frame, error, sizeof(error))) {
                        fprintf(stderr, "campaign level %u decoration fixture is invalid: %s\n",
                                level_index, error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    write_be16(passive_slot + 34u, 0u);
                    passive_slot[62u] = 0x80u;
                    passive_height = passive_definition.floor_ceiling == 0u ?
                        (passive_slot[63u] != 0u ? passive_zone.upper_floor : passive_zone.floor) :
                        (passive_slot[63u] != 0u ? passive_zone.upper_roof : passive_zone.roof);
                    passive_height = source_asr32_7(passive_height) +
                        (int16_t)passive_frame.signed_byte_4 * 2;
                    if (!object_handler_update_single_player(
                            &game.object_runtime, &game.dynamic_level,
                            &game.mechanism_runtime,
                            &game.game_link_catalog, &game.player,
                            &game.session.player1_inventory, &game.inventory_limits, 1u,
                            NULL, error, sizeof(error)) ||
                        read_be16(passive_slot + 4u) != (uint16_t)passive_height ||
                        read_be16(passive_slot + 34u) != passive_frame.next_timer1 ||
                        passive_slot[62u] != 0x80u) {
                        fprintf(stderr,
                                "campaign level %u Decoration ObjectHandler dispatch is inconsistent: %s\n",
                                level_index, error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    ++decoration_fixture_count;
                } else if (passive_definition.behaviour == 2u &&
                           passive_definition.hit_points <= UINT8_MAX &&
                           destructible_fixture_count == 0u) {
                    if (!level_runtime_get_zone(&game.dynamic_level.runtime,
                                                read_be16(passive_slot + 12u), &passive_zone,
                                                error, sizeof(error)) ||
                        !game_link_get_object_animation_frame(
                            &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_ACTION,
                            passive_slot[54u], 0u, &passive_frame, error, sizeof(error))) {
                        fprintf(stderr, "campaign level %u destructible fixture is invalid: %s\n",
                                level_index, error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    write_be16(passive_slot + 34u, 0u);
                    passive_slot[18u] = 1u;
                    passive_slot[19u] = (uint8_t)passive_definition.hit_points;
                    passive_slot[62u] = 0x80u;
                    passive_height = passive_definition.floor_ceiling == 0u ?
                        (passive_slot[63u] != 0u ? passive_zone.upper_floor : passive_zone.floor) :
                        (passive_slot[63u] != 0u ? passive_zone.upper_roof : passive_zone.roof);
                    passive_height = source_asr32_7(passive_height) +
                        (int16_t)passive_frame.signed_byte_4 * 2;
                    if (!object_handler_update_single_player(
                            &game.object_runtime, &game.dynamic_level,
                            &game.mechanism_runtime,
                            &game.game_link_catalog, &game.player,
                            &game.session.player1_inventory, &game.inventory_limits, 1u,
                            NULL, error, sizeof(error)) ||
                        passive_slot[18u] != 0u ||
                        read_be16(passive_slot + 4u) != (uint16_t)passive_height ||
                        read_be16(passive_slot + 34u) != passive_frame.next_timer1) {
                        fprintf(stderr,
                                "campaign level %u Destructable ObjectHandler dispatch is inconsistent: %s\n",
                                level_index, error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    ++destructible_fixture_count;
                }
                if (decoration_fixture_count != 0u && destructible_fixture_count != 0u) {
                    break;
                }
            }
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
            game.level_mechanisms.water_animation_count !=
                LEVEL_MECHANISMS_WATER_ANIMATION_COUNT ||
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
        if (water_fixture_count == 0u) {
            for (uint16_t water_index = 0u;
                 water_index < game.level_mechanisms.water_animation_count;
                 ++water_index) {
                uint8_t *water_state;
                uint8_t *water_graphics;
                LevelZone water_zone;
                int32_t expected_position;
                int16_t expected_velocity;
                int water_has_target;

                if (!level_mechanisms_get_water_animation(
                        &game.level_mechanisms, water_index, &water_animation,
                        error, sizeof(error)) ||
                    !level_dynamic_state_get_graphics_range(
                        &game.dynamic_level, water_animation.state_offset, 14u,
                        &water_state)) {
                    continue;
                }
                water_has_target = water_animation.target_count != 0u;
                if (water_has_target != 0 &&
                    (!level_mechanisms_get_water_animation_target(
                         &game.level_mechanisms, water_index, 0u, &water_target,
                         error, sizeof(error)) ||
                     water_target.zone_index >= game.dynamic_level.runtime.zone_count)) {
                    fprintf(stderr, "campaign level %u DoWaterAnims target is invalid: %s\n",
                            level_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                if (water_animation.lower_position != (int32_t)read_be32(water_state + 0u) ||
                    water_animation.upper_position != (int32_t)read_be32(water_state + 4u) ||
                    water_animation.position != (int32_t)read_be32(water_state + 8u) ||
                    water_animation.velocity != (int16_t)read_be16(water_state + 12u)) {
                    fprintf(stderr, "campaign level %u DoWaterAnims state view is inconsistent\n",
                            level_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                expected_position = (int32_t)((uint32_t)read_be32(water_state + 8u) +
                    (uint32_t)((int32_t)(int16_t)read_be16(water_state + 12u)));
                expected_velocity = (int16_t)read_be16(water_state + 12u);
                if (expected_position <= (int32_t)read_be32(water_state + 0u)) {
                    expected_position = (int32_t)read_be32(water_state + 0u);
                    expected_velocity = (int16_t)(0u - (uint16_t)expected_velocity);
                } else if (expected_position >= (int32_t)read_be32(water_state + 4u)) {
                    expected_position = (int32_t)read_be32(water_state + 4u);
                    expected_velocity = (int16_t)(0u - (uint16_t)expected_velocity);
                }
                if (!mechanism_runtime_update_water_animations(
                        &game.dynamic_level, &game.level_mechanisms, 1u,
                        error, sizeof(error)) ||
                    !level_dynamic_state_get_graphics_range(
                        &game.dynamic_level, water_animation.state_offset, 14u,
                        &water_state) ||
                    (int32_t)read_be32(water_state + 8u) != expected_position ||
                    (int16_t)read_be16(water_state + 12u) != expected_velocity ||
                    (water_has_target != 0 &&
                     (!level_dynamic_state_get_graphics_range(
                          &game.dynamic_level, water_target.graphics_offset + 2u, 2u,
                          &water_graphics) ||
                      !level_runtime_get_zone(&game.dynamic_level.runtime, water_target.zone_index,
                                              &water_zone, error, sizeof(error)) ||
                      (int16_t)read_be16(water_graphics) !=
                          (int16_t)source_asr32_6(expected_position) ||
                      water_zone.water != expected_position))) {
                    fprintf(stderr,
                            "campaign level %u DoWaterAnims update is inconsistent: %s\n",
                            level_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                ++water_fixture_count;
                break;
            }
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
        {
            LevelLiftable door;
            LevelLiftableWall door_wall;
            PlayerRuntime door_player = game.player;
            uint8_t *door_header;
            uint8_t *door_graphics;
            uint16_t activation_flags;
            int16_t expected_velocity;
            uint16_t observed_edge_flags;
            LevelZone dynamic_door_zone;
            uint16_t door_index = 0u;

            while (door_index < game.level_mechanisms.door_count &&
                   (!level_mechanisms_get_door(&game.level_mechanisms, door_index, &door,
                                               error, sizeof(error)) ||
                    door.wall_count == 0u)) {
                ++door_index;
            }
            if (door_index < game.level_mechanisms.door_count) {
                if (!level_mechanisms_get_door_wall(&game.level_mechanisms, door_index, 0u,
                                                    &door_wall, error, sizeof(error)) ||
                    door_wall.edge_index < 0 || door.wall_data_offset < 36u ||
                    !level_dynamic_state_get_graphics_range(
                        &game.dynamic_level, door.wall_data_offset - 36u, 36u,
                        &door_header) ||
                    !level_dynamic_state_get_graphics_range(
                        &game.dynamic_level, door.graphics_offset + 2u, 2u,
                        &door_graphics)) {
                    fprintf(stderr, "campaign level %u door runtime fixture is invalid: %s\n",
                            level_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                switch (door.raise_condition) {
                case 0u:
                    activation_flags = 0x0100u;
                    expected_velocity = (int16_t)(uint16_t)(0u - (uint16_t)door.opening_speed);
                    break;
                case 1u:
                    activation_flags = 0x0900u;
                    expected_velocity = (int16_t)(uint16_t)(0u - (uint16_t)door.opening_speed);
                    break;
                case 2u:
                    activation_flags = 0x0400u;
                    expected_velocity = (int16_t)(uint16_t)(0u - (uint16_t)door.opening_speed);
                    break;
                case 3u:
                    activation_flags = 0x0200u;
                    expected_velocity = (int16_t)(uint16_t)(0u - (uint16_t)door.opening_speed);
                    break;
                case 4u:
                    activation_flags = 0x8000u;
                    expected_velocity = (int16_t)(uint16_t)(0u - (uint16_t)door.opening_speed);
                    break;
                default:
                    /* DoorRoutine's player-in-door safety path opens a closed door. */
                    activation_flags = 0x8000u;
                    expected_velocity = -16;
                    door_player.zone_index = (uint16_t)door.zone_id;
                    break;
                }
                write_be16(door_header + 22u, (uint16_t)door.bottom);
                write_be16(door_header + 24u, 0u);
                door_player.tmp_used = UINT8_MAX;
                mechanism_runtime_init(&game.mechanism_runtime);
                if (!level_dynamic_state_set_edge_flags(
                        &game.dynamic_level, (uint16_t)door_wall.edge_index, activation_flags) ||
                    !mechanism_runtime_update_doors_single_player(
                        &game.mechanism_runtime, &game.dynamic_level, &game.level_mechanisms,
                        &door_player, 1u, error, sizeof(error)) ||
                    (int16_t)read_be16(door_header + 22u) != door.bottom ||
                    (int16_t)read_be16(door_header + 24u) != expected_velocity ||
                    (int16_t)read_be16(door_graphics) != door.bottom ||
                    !level_runtime_get_zone(&game.dynamic_level.runtime,
                                            (uint16_t)door.zone_id, &dynamic_door_zone,
                                            error, sizeof(error)) ||
                    dynamic_door_zone.roof != (int32_t)source_asr16_2(door.bottom) * 256 ||
                    !level_dynamic_state_get_edge_flags(
                        &game.dynamic_level, (uint16_t)door_wall.edge_index,
                        &observed_edge_flags) ||
                    observed_edge_flags != 0x8000u) {
                    fprintf(stderr, "campaign level %u DoorRoutine update is inconsistent: %s\n",
                            level_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                if (door.open_duration > 0) {
                    write_be16(door_header + 22u, (uint16_t)door.top);
                    write_be16(door_header + 24u, 0u);
                    game.mechanism_runtime.door_open_timers[door_index] =
                        (uint16_t)(door.open_duration - 1);
                    if (!level_dynamic_state_set_edge_flags(
                            &game.dynamic_level, (uint16_t)door_wall.edge_index, 0x8000u) ||
                        !mechanism_runtime_update_doors_single_player(
                            &game.mechanism_runtime, &game.dynamic_level,
                            &game.level_mechanisms, &door_player, 1u, error,
                            sizeof(error)) ||
                        (int16_t)read_be16(door_header + 22u) != door.top ||
                        (int16_t)read_be16(door_header + 24u) != door.closing_speed ||
                        game.mechanism_runtime.door_open_timers[door_index] !=
                            (uint16_t)door.open_duration) {
                        fprintf(stderr,
                                "campaign level %u DoorRoutine close timer is inconsistent: %s\n",
                                level_index, error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
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
        {
            LevelLiftable lift;
            LevelLiftableWall lift_wall;
            PlayerRuntime lift_player = game.player;
            LevelZone lift_player_zone;
            LevelZone lift_zone;
            uint8_t *lift_header;
            uint8_t *lift_graphics;
            uint16_t activation_flags;
            int16_t expected_velocity;
            uint16_t observed_edge_flags;
            uint16_t lift_index = 0u;
            int player_stood_on_lift;

            while (lift_index < game.level_mechanisms.lift_count &&
                   (!level_mechanisms_get_lift(&game.level_mechanisms, lift_index, &lift,
                                               error, sizeof(error)) ||
                    lift.wall_count == 0u || lift.raise_condition > 2u)) {
                ++lift_index;
            }
            if (lift_index < game.level_mechanisms.lift_count) {
                if (game.level_runtime.zone_count > 1u &&
                    lift_player.zone_index == (uint16_t)lift.zone_id) {
                    lift_player.zone_index =
                        (uint16_t)(((uint32_t)lift_player.zone_index + 1u) %
                                   game.level_runtime.zone_count);
                }
                if (!level_mechanisms_get_lift_wall(&game.level_mechanisms, lift_index, 0u,
                                                    &lift_wall, error, sizeof(error)) ||
                    lift_wall.edge_index < 0 || lift.wall_data_offset < 36u ||
                    !level_dynamic_state_get_graphics_range(
                        &game.dynamic_level, lift.wall_data_offset - 36u, 36u,
                        &lift_header) ||
                    !level_dynamic_state_get_graphics_range(
                        &game.dynamic_level, lift.graphics_offset + 2u, 2u,
                        &lift_graphics) ||
                    !level_runtime_get_zone(&game.dynamic_level.runtime, lift_player.zone_index,
                                            &lift_player_zone, error, sizeof(error)) ||
                    !level_runtime_get_zone(&game.dynamic_level.runtime, (uint16_t)lift.zone_id,
                                            &lift_zone, error, sizeof(error))) {
                    fprintf(stderr, "campaign level %u lift runtime fixture is invalid: %s\n",
                            level_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                player_stood_on_lift = lift_player_zone.id == lift_zone.id;
                lift_player.tmp_used = UINT8_MAX;
                switch (lift.raise_condition) {
                case 0u:
                    activation_flags = player_stood_on_lift != 0 ? 0x8000u : 0x0100u;
                    break;
                case 1u:
                    activation_flags = player_stood_on_lift != 0 ? 0x8000u : 0x0900u;
                    break;
                default:
                    activation_flags = 0x8000u;
                    break;
                }
                expected_velocity =
                    (int16_t)(uint16_t)(0u - (uint16_t)lift.opening_speed);
                write_be16(lift_header + 22u, (uint16_t)lift.bottom);
                write_be16(lift_header + 24u, 0u);
                mechanism_runtime_init(&game.mechanism_runtime);
                if (!level_dynamic_state_set_edge_flags(
                        &game.dynamic_level, (uint16_t)lift_wall.edge_index, activation_flags) ||
                    !mechanism_runtime_update_lifts_single_player(
                        &game.mechanism_runtime, &game.dynamic_level, &game.level_mechanisms,
                        &lift_player, 1u, error, sizeof(error)) ||
                    game.mechanism_runtime.lift_heights[lift_index] != lift.bottom ||
                    (int16_t)read_be16(lift_header + 22u) != lift.bottom ||
                    (int16_t)read_be16(lift_header + 24u) != expected_velocity ||
                    (int16_t)read_be16(lift_graphics) !=
                        (int16_t)((uint16_t)source_asr16_2(lift.bottom) << 2) ||
                    !level_runtime_get_zone(&game.dynamic_level.runtime, (uint16_t)lift.zone_id,
                                            &lift_zone, error, sizeof(error)) ||
                    lift_zone.floor != (int32_t)source_asr16_2(lift.bottom) * 256 ||
                    !level_dynamic_state_get_edge_flags(
                        &game.dynamic_level, (uint16_t)lift_wall.edge_index,
                        &observed_edge_flags) ||
                    observed_edge_flags != 0x8000u) {
                    fprintf(stderr, "campaign level %u LiftRoutine update is inconsistent: %s\n",
                            level_index, error);
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
        if (!level_static_scene_apply_runtime(
                &game.static_scene, &game.dynamic_level.runtime,
                game.shared_resources.wall_texture_count,
                game.shared_resources.floor_texture.size, error, sizeof(error))) {
            fprintf(stderr, "campaign level %u dynamic static-scene update failed: %s\n",
                    level_index, error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (static_wall_index = 0u; static_wall_index < game.static_scene.wall_count;
             ++static_wall_index) {
            const LevelStaticWallScene *scene_wall =
                &game.static_scene.walls[static_wall_index];
            const uint8_t *source = game.dynamic_level.runtime.graphics_bytes +
                scene_wall->source_record_offset;
            LevelWorldPoint left_point;
            LevelWorldPoint right_point;
            int32_t top;
            int32_t bottom;

            if (scene_wall->source_record_offset > game.dynamic_level.runtime.graphics_size ||
                30u > game.dynamic_level.runtime.graphics_size - scene_wall->source_record_offset ||
                (uint8_t)read_be16(source) != LEVEL_DRAW_GRAPH_TYPE_WALL ||
                scene_wall->material_id != read_be16(source + 14u) ||
                !level_runtime_get_world_point(&game.dynamic_level.runtime, read_be16(source + 2u),
                                               &left_point, error, sizeof(error)) ||
                !level_runtime_get_world_point(&game.dynamic_level.runtime, read_be16(source + 4u),
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

            if (scene_flat->source_record_offset > game.dynamic_level.runtime.graphics_size ||
                6u > game.dynamic_level.runtime.graphics_size - scene_flat->source_record_offset) {
                fprintf(stderr, "campaign level %u static flat %u has an invalid source range\n",
                        level_index, static_flat_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            source = game.dynamic_level.runtime.graphics_bytes + scene_flat->source_record_offset;
            flat_record.raw_tag = read_be16(source);
            flat_record.type = (uint8_t)flat_record.raw_tag;
            flat_record.source_offset = scene_flat->source_record_offset;
            flat_record.byte_count = scene_flat->source_record_byte_count;
            if ((flat_record.type != LEVEL_DRAW_GRAPH_TYPE_FLOOR &&
                 flat_record.type != LEVEL_DRAW_GRAPH_TYPE_CEILING &&
                 flat_record.type != LEVEL_DRAW_GRAPH_TYPE_WATER) ||
                !level_draw_graph_read_flat(&game.dynamic_level.runtime, &flat_record, &draw_flat,
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
                if (!level_draw_graph_get_flat_point(&game.dynamic_level.runtime, &draw_flat,
                                                    flat_point_index, &flat_raw_point_word,
                                                    &flat_world_point_index,
                                                    error, sizeof(error)) ||
                    !level_runtime_get_world_point(&game.dynamic_level.runtime, flat_world_point_index,
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
    if (decoration_fixture_count == 0u || destructible_fixture_count == 0u ||
        water_fixture_count == 0u) {
        fprintf(stderr, "campaign data does not contain all passive-object/water fixtures\n");
        game_bootstrap_destroy(&game);
        return 1;
    }
    if (!object_scene_count_active(&game.object_runtime, &active_sprite_count,
                                   error, sizeof(error)) ||
        !level_runtime_get_zone(&game.level_runtime, game.level.player1_start_zone,
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
            ((size_t)game.static_scene.wall_count + game.static_scene.flat_count) * 2u +
            active_sprite_count ||
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
        !scene_sprite_commands_match_source(
            &frame, 1u + ((size_t)game.static_scene.wall_count + game.static_scene.flat_count) * 2u,
            &game, active_sprite_count, error, sizeof(error)) ||
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
                                                 &game.session.player1_inventory,
                                                 error, sizeof(error)) ||
        controlled_player.used != UINT8_MAX ||
        controlled_player.previous_use_key_state != UINT8_MAX ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 &game.session.player1_inventory,
                                                 error, sizeof(error)) ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_OPERATE], 0,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 &game.session.player1_inventory,
                                                 error, sizeof(error)) ||
        controlled_player.previous_use_key_state != 0u ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_CROUCH], 1,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 &game.session.player1_inventory,
                                                 error, sizeof(error)) ||
        controlled_player.ducked != UINT8_MAX ||
        game_input_is_control_down(&control_input, &control_defaults, GAME_CONTROL_CROUCH) ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_FIRE], 1,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 &game.session.player1_inventory,
                                                 error, sizeof(error)) ||
        controlled_player.fire != UINT8_MAX || controlled_player.clicked != UINT8_MAX ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_FIRE], 0,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 &game.session.player1_inventory,
                                                 error, sizeof(error)) ||
        controlled_player.fire != 0u || controlled_player.clicked != UINT8_MAX ||
        controlled_player.x != game.player.x || controlled_player.y != game.player.y ||
        controlled_player.z != game.player.z || controlled_player.yaw != game.player.yaw) {
        fprintf(stderr, "plr_KeyboardControl discrete state is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    {
        PlayerRuntime weapon_selection_player = game.player;
        GameInput weapon_selection_input;
        GameInventory weapon_selection_inventory = {0};

        weapon_selection_player.gun_selected = 3u;
        weapon_selection_inventory.weapons[3u] = UINT8_MAX;
        weapon_selection_inventory.weapons[7u] = UINT8_MAX;
        game_input_init(&weapon_selection_input);
        if (!game_input_set_raw_key(
                &weapon_selection_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_NEXT_WEAPON], 1,
                error, sizeof(error)) ||
            !player_runtime_update_discrete_controls(
                &weapon_selection_player, &weapon_selection_input, &control_defaults,
                &game.level_runtime, &weapon_selection_inventory, error, sizeof(error)) ||
            weapon_selection_player.gun_selected != 7u ||
            weapon_selection_player.previous_next_weapon_key_state != UINT8_MAX ||
            !player_runtime_update_discrete_controls(
                &weapon_selection_player, &weapon_selection_input, &control_defaults,
                &game.level_runtime, &weapon_selection_inventory, error, sizeof(error)) ||
            weapon_selection_player.gun_selected != 7u ||
            !game_input_set_raw_key(
                &weapon_selection_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_NEXT_WEAPON], 0,
                error, sizeof(error)) ||
            !player_runtime_update_discrete_controls(
                &weapon_selection_player, &weapon_selection_input, &control_defaults,
                &game.level_runtime, &weapon_selection_inventory, error, sizeof(error)) ||
            weapon_selection_player.previous_next_weapon_key_state != 0u ||
            !game_input_set_raw_key(
                &weapon_selection_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_NEXT_WEAPON], 1,
                error, sizeof(error)) ||
            !player_runtime_update_discrete_controls(
                &weapon_selection_player, &weapon_selection_input, &control_defaults,
                &game.level_runtime, &weapon_selection_inventory, error, sizeof(error)) ||
            weapon_selection_player.gun_selected != 3u) {
            fprintf(stderr, "source next-weapon control is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        PlayerRuntime use_snapshot_player = game.player;
        GameInput use_snapshot_input;

        use_snapshot_player.gun_selected = 7u;
        use_snapshot_player.fire = UINT8_MAX;
        use_snapshot_player.clicked = UINT8_MAX;
        game_input_init(&use_snapshot_input);
        if (!game_input_set_raw_key(
                &use_snapshot_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_OPERATE], 1,
                error, sizeof(error)) ||
            !game_input_set_raw_key(
                &use_snapshot_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_FIRE], 1,
                error, sizeof(error)) ||
            !player_runtime_update_discrete_controls(&use_snapshot_player,
                                                     &use_snapshot_input, &control_defaults,
                                                     &game.level_runtime,
                                                     &game.session.player1_inventory, error,
                                                     sizeof(error)) ||
            !player_runtime_update_spatial(&use_snapshot_player, &use_snapshot_input,
                                           &control_defaults, &game.preferences, &game.math,
                                           &game.level_runtime, NULL, error, sizeof(error)) ||
            use_snapshot_player.tmp_used != UINT8_MAX || use_snapshot_player.used != 0u ||
            use_snapshot_player.tmp_clicked != UINT8_MAX || use_snapshot_player.clicked != 0u ||
            use_snapshot_player.tmp_fire != UINT8_MAX || use_snapshot_player.fire != UINT8_MAX ||
            use_snapshot_player.tmp_gun_selected != 7u) {
            fprintf(stderr, "source transient player snapshot is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    /*
     * modules/player.s:plr_KeyboardControl then Plr1_Fall, followed by
     * hires.s:Plr1_Control/MoveObject.  The first grounded tick enables
     * deceleration; the second consumes the forward/turn controls into the
     * source fixed-point snap state.
     */
    controlled_player = game.player;
    controlled_player.health = 200u;
    game_input_init(&control_input);
    if (!game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_FORWARDS], 1,
                                error, sizeof(error)) ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_TURN_RIGHT], 1,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 &game.session.player1_inventory,
                                                 error, sizeof(error)) ||
        !player_runtime_update_spatial(&controlled_player, &control_input, &control_defaults,
                                       &game.preferences, &game.math, &game.level_runtime,
                                       NULL, error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 &game.session.player1_inventory,
                                                 error, sizeof(error)) ||
        !player_runtime_update_spatial(&controlled_player, &control_input, &control_defaults,
                                       &game.preferences, &game.math, &game.level_runtime,
                                       NULL, error, sizeof(error)) ||
        controlled_player.decelerate == 0u ||
        controlled_player.snap_yaw_speed == 0 ||
        controlled_player.snap_z_speed == 0) {
        fprintf(stderr, "source player spatial update is inconsistent: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    if (!game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_FORWARDS], 0,
                                error, sizeof(error)) ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_TURN_RIGHT], 0,
                                error, sizeof(error)) ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_LOOK_UP], 1,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 &game.session.player1_inventory,
                                                 error, sizeof(error)) ||
        !player_runtime_update_spatial(&controlled_player, &control_input, &control_defaults,
                                       &game.preferences, &game.math, &game.level_runtime,
                                       NULL, error, sizeof(error)) ||
        controlled_player.look_offset != -4 || controlled_player.aim_speed != -512 ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_LOOK_UP], 0,
                                error, sizeof(error)) ||
        !game_input_set_raw_key(&control_input,
                                control_defaults.assigned_raw_keys[GAME_CONTROL_CENTRE_VIEW], 1,
                                error, sizeof(error)) ||
        !player_runtime_update_discrete_controls(&controlled_player, &control_input,
                                                 &control_defaults, &game.level_runtime,
                                                 &game.session.player1_inventory,
                                                 error, sizeof(error)) ||
        !player_runtime_update_spatial(&controlled_player, &control_input, &control_defaults,
                                       &game.preferences, &game.math, &game.level_runtime,
                                       NULL, error, sizeof(error)) ||
        controlled_player.look_offset != 0 || controlled_player.aim_speed != 0) {
        fprintf(stderr, "source player keyboard look state is inconsistent: %s\n", error);
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
    /*
     * Level B's first authored collectable is slot 20: object definition 0
     * at point 20/zone 146. This exercises the exact ObjectHandler collectable
     * branch without invoking activation, animation, doors, AI, or projectiles.
     */
    if (!game_session_default(&game.session, &game.game_link_catalog, error, sizeof(error)) ||
        !game_session_select_level(&game.session, 1u, error, sizeof(error)) ||
        !game_bootstrap_start_selected_single_player(&game, argv[1], error, sizeof(error))) {
        fprintf(stderr, "could not load Level B collectable fixture: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    {
        uint8_t *collectable_slot;
        uint8_t *collectable_point;
        LevelZone collectable_zone;
        GameObjectDefinition collectable_definition;
        GameInventory collectable_grant;
        GameInventory expected_inventory;
        uint32_t collected_count;

        if (!object_runtime_get_slot_bytes(&game.object_runtime, 20u, &collectable_slot) ||
            !object_runtime_get_point_bytes(&game.object_runtime, 20u, &collectable_point) ||
            collectable_slot[16u] != 1u || collectable_slot[54u] != 0u ||
            (int16_t)read_be16(collectable_slot + 12u) != 146 ||
            collectable_slot[63u] != 0u ||
            !game_link_get_object_definition(&game.game_link_catalog, 0u,
                                             &collectable_definition,
                                             error, sizeof(error)) ||
            collectable_definition.behaviour != 0u ||
            collectable_definition.collision_radius != 150u ||
            collectable_definition.collision_height != 75u ||
            !game_link_get_object_inventory_grant(&game.game_link_catalog, 0u,
                                                  &collectable_grant,
                                                  error, sizeof(error)) ||
            !level_runtime_get_zone(&game.level_runtime, 146u, &collectable_zone,
                                    error, sizeof(error))) {
            fprintf(stderr, "Level B source collectable fixture is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        game.player.zone_index = 146u;
        game.player.stood_in_top = 0u;
        game.player.tmp_x = (int16_t)read_be16(collectable_point + 0u);
        game.player.tmp_z = (int16_t)read_be16(collectable_point + 4u);
        game.player.tmp_height = 12 * 1024;
        game.player.tmp_y = collectable_zone.floor - game.player.tmp_height;
        expected_inventory = game.session.player1_inventory;
        game_inventory_apply_grant(&expected_inventory, &collectable_grant,
                                   &game.inventory_limits);
        if (!object_collectables_update_single_player(
                &game.object_runtime, &game.level_runtime, &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits,
                &collected_count, error, sizeof(error)) ||
            collected_count != 1u ||
            (int16_t)read_be16(collectable_slot + 12u) != -1 ||
            collectable_slot[62u] != 0u ||
            memcmp(&game.session.player1_inventory, &expected_inventory,
                   sizeof(expected_inventory)) != 0) {
            fprintf(stderr, "Level B source collectable update is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    /* hires.s:game_main_loop ends a single-player level on Lvl_ExitZoneID_w. */
    if (!game_session_default(&game.session, &game.game_link_catalog, error, sizeof(error)) ||
        !game_bootstrap_start_selected_single_player(&game, argv[1], error, sizeof(error)) ||
        game.level_runtime.exit_zone_id < 0 ||
        !level_runtime_get_zone(&game.level_runtime,
                                (uint16_t)game.level_runtime.exit_zone_id,
                                &zone, error, sizeof(error))) {
        fprintf(stderr, "could not load source exit-zone fixture: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    {
        int16_t authored_exit_zone_id = game.dynamic_level.runtime.exit_zone_id;
        uint8_t *player2_slot = NULL;

        /* Settle any authored spawn teleport before exercising post-control comparison. */
        game.dynamic_level.runtime.exit_zone_id = -1;
        game_input_init(&game.input);
        if (!game_bootstrap_update_single_player(&game, error, sizeof(error)) ||
            !object_runtime_get_player2_slot_bytes(&game.object_runtime, &player2_slot) ||
            (int16_t)read_be16(player2_slot + 12u) != -1 ||
            (int16_t)read_be16(player2_slot + 26u) != -1 || player2_slot[17u] != 0u ||
            !object_observation_matches_source(
                &game.object_observation, &game.object_runtime, &game.player, &game.math,
                error, sizeof(error))) {
            fprintf(stderr, "source exit-zone setup is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if (!level_runtime_get_zone(&game.level_runtime, game.player.zone_index,
                                    &zone, error, sizeof(error))) {
            fprintf(stderr, "source player-zone fixture is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        game.dynamic_level.runtime.exit_zone_id = (int16_t)zone.id;
        if (!game_bootstrap_update_single_player(&game, error, sizeof(error)) ||
            game.session.level_finished == 0u ||
            memcmp(&game.session.campaign_inventory, &game.session.player1_inventory,
                   sizeof(game.session.campaign_inventory)) != 0) {
            fprintf(stderr, "source exit-zone completion is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        game.dynamic_level.runtime.exit_zone_id = authored_exit_zone_id;
    }
    {
        uint8_t slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[2u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime shot_objects = {0};
        ObjectObservation shot_observation;
        PlayerRuntime shot_player = {0};
        GameBulletDefinition shot_bullet = {0};
        PlayerShotTarget shot_target;
        GameRandom hitscan_random;
        GameRandom expected_hitscan_random;
        uint8_t hitscan_hit = 0u;

        shot_objects.slot_bytes = slot_bytes;
        shot_objects.slot_count = 2u;
        shot_objects.active_slot_count = 2u;
        shot_objects.point_bytes = point_bytes;
        shot_objects.point_count = 2u;
        for (uint32_t slot_index = 0u; slot_index < 2u; ++slot_index) {
            uint8_t *slot = slot_bytes + (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

            write_be16(slot + 0u, (uint16_t)slot_index);
            write_be16(slot + 4u, 4u);
            write_be16(slot + 12u, 0u);
            slot[16u] = 1u;
            slot[17u] = 1u;
            slot[18u] = 1u;
        }
        object_observation_init(&shot_observation);
        shot_observation.in_line[0u] = UINT8_MAX;
        shot_observation.in_line[1u] = UINT8_MAX;
        shot_observation.distances[0u] = 50u;
        shot_observation.distances[1u] = 50u;
        shot_player.height = 12 * 1024;
        if (!player_shoot_find_target_single_player(
                &shot_objects, &shot_observation, &shot_player, &shot_bullet,
                &shot_target, error, sizeof(error)) || shot_target.found != UINT8_MAX ||
            shot_target.slot_index != 1u || shot_target.point_index != 1u ||
            shot_target.distance != 50u || shot_target.vertical_difference != 512 ||
            shot_target.vertical_speed != -143) {
            fprintf(stderr, "Plr1_Shot target selection/tie handling is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be32(point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u,
                   UINT32_C(10) << 16);
        write_be32(point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   UINT32_C(20) << 16);
        game_random_init(&hitscan_random);
        expected_hitscan_random = hitscan_random;
        (void)game_random_next(&expected_hitscan_random);
        if (!player_shoot_hitscan_roll_is_hit(
                &shot_objects, &shot_target, &shot_player, &hitscan_random,
                &hitscan_hit, error, sizeof(error)) || hitscan_hit != UINT8_MAX ||
            hitscan_random.state != expected_hitscan_random.state) {
            fprintf(stderr, "Plr1_Shot hitscan hit roll is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be32(point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u,
                   UINT32_C(30000) << 16);
        write_be32(point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   UINT32_C(30000) << 16);
        game_random_init(&hitscan_random);
        expected_hitscan_random = hitscan_random;
        (void)game_random_next(&expected_hitscan_random);
        hitscan_hit = UINT8_MAX;
        if (!player_shoot_hitscan_roll_is_hit(
                &shot_objects, &shot_target, &shot_player, &hitscan_random,
                &hitscan_hit, error, sizeof(error)) || hitscan_hit != 0u ||
            hitscan_random.state != expected_hitscan_random.state) {
            fprintf(stderr, "Plr1_Shot hitscan miss roll is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 4u;
        if (!player_shoot_find_target_single_player(
                &shot_objects, &shot_observation, &shot_player, &shot_bullet,
                &shot_target, error, sizeof(error)) || shot_target.found != UINT8_MAX ||
            shot_target.slot_index != 0u) {
            fprintf(stderr, "Plr1_Shot target type mask is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        slot_bytes[17u] = 0u;
        if (!player_shoot_find_target_single_player(
                &shot_objects, &shot_observation, &shot_player, &shot_bullet,
                &shot_target, error, sizeof(error)) || shot_target.found != 0u) {
            fprintf(stderr, "Plr1_Shot sight gate is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        enum {
            PARENT_TARGET_SLOT = 0u,
            PARENT_SHOT_FIRST_SLOT = 1u,
            PARENT_PLAYER_SLOT = PARENT_SHOT_FIRST_SLOT + OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT,
            PARENT_WEAPON_SLOT = PARENT_PLAYER_SLOT + 2u,
            PARENT_SLOT_COUNT = PARENT_WEAPON_SLOT + 1u
        };
        uint8_t slot_bytes[PARENT_SLOT_COUNT * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[PARENT_SLOT_COUNT * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime parent_objects = {0};
        ObjectObservation parent_observation;
        PlayerRuntime parent_player = {0};
        GameInventory parent_inventory = {0};
        GameShootDefinition parent_shoot;
        GameBulletDefinition parent_bullet;
        GameRandom parent_random;
        GameRandom expected_parent_random;

        if (!game_link_get_shoot_definition(&game.game_link_catalog, 0u, &parent_shoot,
                                            error, sizeof(error)) ||
            !game_link_get_bullet_definition(&game.game_link_catalog, parent_shoot.bullet_type,
                                             &parent_bullet, error, sizeof(error)) ||
            (uint16_t)parent_bullet.is_hitscan == 0u || parent_shoot.bullet_count == 0u) {
            fprintf(stderr, "could not prepare source Plr1_Shot hitscan fixture: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        parent_objects.slot_bytes = slot_bytes;
        parent_objects.slot_count = PARENT_SLOT_COUNT;
        parent_objects.active_slot_count = 1u;
        parent_objects.player_shot_first_slot = PARENT_SHOT_FIRST_SLOT;
        parent_objects.player1_slot = PARENT_PLAYER_SLOT;
        parent_objects.point_bytes = point_bytes;
        parent_objects.point_count = PARENT_SLOT_COUNT;
        write_be16(slot_bytes + PARENT_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   PARENT_TARGET_SLOT);
        write_be16(slot_bytes + PARENT_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u, 0u);
        write_be16(slot_bytes + PARENT_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
        slot_bytes[PARENT_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 1u;
        slot_bytes[PARENT_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 17u] = UINT8_MAX;
        slot_bytes[PARENT_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 10u;
        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            uint8_t *shot_slot = slot_bytes +
                (PARENT_SHOT_FIRST_SLOT + shot_index) * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

            write_be16(shot_slot + 0u,
                       (uint16_t)(PARENT_SHOT_FIRST_SLOT + shot_index));
            write_be16(shot_slot + 12u, UINT16_MAX);
        }
        object_observation_init(&parent_observation);
        parent_observation.in_line[PARENT_TARGET_SLOT] = UINT8_MAX;
        parent_observation.distances[PARENT_TARGET_SLOT] = 50u;
        parent_player.height = 12 * 1024;
        parent_player.tmp_gun_selected = 0u;
        parent_player.tmp_fire = UINT8_MAX;
        parent_player.zone_index = 0u;
        parent_inventory.ammunition[parent_shoot.bullet_type] = 8u;
        game_random_init(&parent_random);
        expected_parent_random = parent_random;
        for (uint16_t shot_index = 0u; shot_index < parent_shoot.bullet_count; ++shot_index) {
            (void)game_random_next(&expected_parent_random);
        }
        if (!player_shoot_update_single_player(
                &parent_objects, &game.dynamic_level, &parent_observation, &parent_player,
                &parent_inventory, &game.game_link_catalog, &game.preferences, &game.math,
                &parent_random, 1u, error, sizeof(error)) ||
            parent_player.time_to_shoot != (int16_t)parent_shoot.delay ||
            parent_inventory.ammunition[parent_shoot.bullet_type] !=
                (uint16_t)(8u - parent_shoot.bullet_count) ||
            slot_bytes[PARENT_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] !=
                (uint8_t)((uint8_t)parent_bullet.hit_damage *
                          (uint8_t)parent_shoot.bullet_count) ||
            read_be16(slot_bytes + PARENT_WEAPON_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 34u) !=
                1u || parent_random.state != expected_parent_random.state) {
            fprintf(stderr, "Plr1_Shot fire setup is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint16_t shot_index = 0u; shot_index < parent_shoot.bullet_count; ++shot_index) {
            const uint8_t *shot_slot = slot_bytes +
                (PARENT_SHOT_FIRST_SLOT + shot_index) * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

            if (read_be16(shot_slot + 12u) != 0u || shot_slot[30u] != 1u ||
                shot_slot[31u] != (uint8_t)parent_shoot.bullet_type) {
                fprintf(stderr, "Plr1_Shot hitscan dispatch is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        if (!player_shoot_update_single_player(
                &parent_objects, &game.dynamic_level, &parent_observation, &parent_player,
                &parent_inventory, &game.game_link_catalog, &game.preferences, &game.math,
                &parent_random, 1u, error, sizeof(error)) ||
            parent_player.time_to_shoot != (int16_t)(parent_shoot.delay - 1u) ||
            parent_random.state != expected_parent_random.state) {
            fprintf(stderr, "Plr1_Shot source cooldown is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        uint8_t slot_bytes[(1u + OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT) *
                           OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[(1u + OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT) *
                            OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime hit_objects = {0};
        GameBulletDefinition hit_bullet = {0};
        PlayerShotTarget hit_target = {0};
        uint8_t impact_spawned = 0u;

        hit_objects.slot_bytes = slot_bytes;
        hit_objects.slot_count = 1u + OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
        hit_objects.active_slot_count = 1u;
        hit_objects.player_shot_first_slot = 1u;
        hit_objects.point_bytes = point_bytes;
        hit_objects.point_count = 1u + OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 4u, 4u);
        write_be16(slot_bytes + 12u, 0u);
        slot_bytes[19u] = 250u;
        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            uint8_t *shot_slot = slot_bytes +
                (size_t)(1u + shot_index) * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

            write_be16(shot_slot + 0u, (uint16_t)(1u + shot_index));
            write_be16(shot_slot + 12u, UINT16_MAX);
        }
        write_be32(point_bytes + 0u, UINT32_C(0x12345678));
        write_be32(point_bytes + 4u, UINT32_C(0x9abcdef0));
        hit_target.found = UINT8_MAX;
        hit_target.slot_index = 0u;
        hit_target.point_index = 0u;
        hit_bullet.hit_damage = 6u;
        if (!player_shoot_apply_hitscan_success(
                &hit_objects, &hit_target, 7u, &hit_bullet, 16384, -16384,
                &impact_spawned, error, sizeof(error)) || impact_spawned != UINT8_MAX ||
            slot_bytes[19u] != 0u ||
            read_be16(slot_bytes + 42u) != 2u ||
            (int16_t)read_be16(slot_bytes + 44u) != -2 ||
            slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] != 2u ||
            slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 30u] != 1u ||
            slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 31u] != 7u ||
            read_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u) != 0u ||
            slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 52u] != 0u ||
            read_be32(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 44u) != 512u ||
            read_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) != 0u ||
            slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 62u] != UINT8_MAX ||
            read_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u) != 4u ||
            read_be32(point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u) !=
                UINT32_C(0x12345678) ||
            read_be32(point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u) !=
                UINT32_C(0x9abcdef0)) {
            fprintf(stderr, "plr1_HitscanSucceded source mutation is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            write_be16(slot_bytes +
                           (size_t)(1u + shot_index) * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                       0u);
        }
        slot_bytes[19u] = 1u;
        impact_spawned = UINT8_MAX;
        if (!player_shoot_apply_hitscan_success(
                &hit_objects, &hit_target, 7u, &hit_bullet, 16384, -16384,
                &impact_spawned, error, sizeof(error)) || impact_spawned != 0u ||
            slot_bytes[19u] != 1u) {
            fprintf(stderr, "plr1_HitscanSucceded pool exhaustion is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        uint8_t slot_bytes[OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT *
                           OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT *
                            OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime projectile_objects = {0};
        PlayerRuntime projectile_player = {0};
        GameBulletDefinition projectile_bullet = {0};
        GameBulletAnimationFrame projectile_frame;
        uint16_t projectile_bullet_index = UINT16_MAX;
        int16_t first_sine;
        int16_t first_cosine;
        int16_t second_sine;
        int16_t second_cosine;
        int16_t projectile_speed;
        uint32_t spawned_count = 0u;

        for (uint16_t bullet_index = 0u; bullet_index < GAME_LINK_BULLET_COUNT;
             ++bullet_index) {
            if (!game_link_get_bullet_definition(&game.game_link_catalog, bullet_index,
                                                 &projectile_bullet, error, sizeof(error))) {
                fprintf(stderr, "could not read source projectile bullet definition: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if ((uint16_t)projectile_bullet.is_hitscan == 0u) {
                projectile_bullet_index = bullet_index;
                break;
            }
        }
        if (projectile_bullet_index == UINT16_MAX) {
            fprintf(stderr, "source non-hitscan projectile fixture is unavailable\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        projectile_objects.slot_bytes = slot_bytes;
        projectile_objects.slot_count = OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
        projectile_objects.active_slot_count = OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
        projectile_objects.player_shot_first_slot = 0u;
        projectile_objects.point_bytes = point_bytes;
        projectile_objects.point_count = OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            uint8_t *slot = slot_bytes + (size_t)shot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
            uint8_t *point = point_bytes + (size_t)shot_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;

            write_be16(slot + 0u, (uint16_t)shot_index);
            write_be16(slot + 12u, UINT16_MAX);
            write_be32(point + 0u, UINT32_C(0x11112222));
            write_be32(point + 4u, UINT32_C(0x33334444));
        }
        projectile_player.x = 300;
        projectile_player.y = 400;
        projectile_player.z = -500;
        projectile_player.yaw = 0u;
        projectile_player.zone_index = 4u;
        projectile_player.stood_in_top = UINT8_MAX;
        projectile_speed = (int16_t)(uint16_t)projectile_bullet.speed;
        if (!game_math_sine(&game.math, UINT16_C(0xff80), &first_sine,
                            error, sizeof(error)) ||
            !game_math_cosine(&game.math, UINT16_C(0xff80), &first_cosine,
                               error, sizeof(error)) ||
            !game_math_sine(&game.math, UINT16_C(0x0080), &second_sine,
                            error, sizeof(error)) ||
            !game_math_cosine(&game.math, UINT16_C(0x0080), &second_cosine,
                               error, sizeof(error)) ||
            !player_shoot_spawn_projectile_volley(
                &projectile_objects, &game.math, &projectile_player,
                projectile_bullet_index, &projectile_bullet, 2u, 5000,
                &spawned_count, error, sizeof(error)) ||
            spawned_count != 2u) {
            fprintf(stderr, "firefive source projectile volley is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint32_t shot_index = 0u; shot_index < 2u; ++shot_index) {
            const uint8_t *slot = slot_bytes +
                (size_t)shot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
            const uint8_t *point = point_bytes +
                (size_t)shot_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;
            int16_t sine = shot_index == 0u ? first_sine : second_sine;
            int16_t cosine = shot_index == 0u ? first_cosine : second_cosine;
            uint32_t expected_velocity_x =
                (uint32_t)((int32_t)((int64_t)sine * projectile_speed) * 2);
            uint32_t expected_velocity_z =
                (uint32_t)((int32_t)((int64_t)cosine * projectile_speed) * 2);

            if (slot[16u] != 2u || read_be16(slot + 12u) != 4u ||
                slot[31u] != (uint8_t)projectile_bullet_index ||
                slot[28u] != (uint8_t)projectile_bullet.hit_damage ||
                read_be16(slot + 54u) != (uint16_t)projectile_bullet.gravity ||
                slot[60u] != (uint8_t)projectile_bullet.bounce_horizontal ||
                slot[61u] != (uint8_t)projectile_bullet.bounce_vertical ||
                read_be32(slot + 18u) != expected_velocity_x ||
                read_be32(slot + 22u) != expected_velocity_z ||
                read_be16(slot + 42u) != 2560u || slot[63u] != UINT8_MAX ||
                read_be16(slot + 58u) != 0u || read_be32(slot + 36u) != 0x23u ||
                read_be32(slot + 44u) != 4240u || read_be16(slot + 4u) != 33u ||
                slot[62u] != UINT8_MAX ||
                read_be32(point + 0u) != UINT32_C(0x012c2222) ||
                read_be32(point + 4u) != UINT32_C(0xfe0c4444)) {
                fprintf(stderr, "firefive source projectile launch state is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        if (!game_link_get_bullet_animation_frame(
                &game.game_link_catalog, GAME_LINK_BULLET_ANIMATION_FLIGHT,
                projectile_bullet_index, 0u, &projectile_frame, error, sizeof(error)) ||
            !object_projectiles_update_flight_animation_slot(
                &projectile_objects, 0u, &game.dynamic_level, &game.game_link_catalog, 1u,
                error, sizeof(error)) ||
            read_be16(slot_bytes + 6u) != projectile_frame.word_2 ||
            slot_bytes[11u] != projectile_frame.byte_1 ||
            slot_bytes[52u] != ((int16_t)(uint16_t)projectile_bullet.animation_frames < 1 ?
                                     0u : 1u)) {
            fprintf(stderr, "ItsABullet source flight animation is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if ((int32_t)projectile_bullet.graphics_type < 1) {
            if (slot_bytes[9u] != projectile_frame.byte_0 || slot_bytes[10u] != 0u) {
                fprintf(stderr, "ItsABullet bitmap flight descriptor is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        } else if (projectile_bullet.graphics_type == 1u) {
            if ((int16_t)read_be16(slot_bytes + 8u) !=
                    (int16_t)-(int16_t)(int8_t)projectile_frame.byte_0 ||
                slot_bytes[10u] != 0u) {
                fprintf(stderr, "ItsABullet glare flight descriptor is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        } else if (slot_bytes[9u] != projectile_frame.byte_0 || slot_bytes[10u] != 6u) {
            fprintf(stderr, "ItsABullet additive flight descriptor is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        slot_bytes[52u] = 0u;
        if (!object_handler_update_single_player(
                &projectile_objects, &game.dynamic_level, &game.mechanism_runtime,
                &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            slot_bytes[52u] != ((int16_t)(uint16_t)projectile_bullet.animation_frames < 1 ?
                                     0u : 1u)) {
            fprintf(stderr, "ObjectHandler flight projectile dispatch is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        {
            /*
             * ItsABullet moves through a Vec2L segment before testing the
             * source object list. This isolated one-zone source layout makes
             * that segment hit a type-zero target at its midpoint.
             */
            uint8_t flight_level_bytes[64u] = {0};
            uint8_t flight_graphics_bytes[4u] = {0};
            uint8_t flight_slot_bytes[3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
            uint8_t flight_point_bytes[2u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
            LevelRuntime flight_level = {0};
            LevelDynamicState flight_dynamic = {0};
            ObjectRuntime flight_objects = {0};
            int16_t expected_target_height = source_asr32_7(
                (int16_t)(uint16_t)projectile_bullet.gravity);

            flight_level.level_bytes = flight_level_bytes;
            flight_level.level_size = sizeof(flight_level_bytes);
            flight_level.graphics_bytes = flight_graphics_bytes;
            flight_level.graphics_size = sizeof(flight_graphics_bytes);
            flight_level.zone_offsets_table_offset = 0u;
            flight_level.zone_count = 1u;
            write_be32(flight_graphics_bytes, 0u);
            write_be16(flight_level_bytes + 0u, 0u);
            write_be32(flight_level_bytes + 2u, 100000u);
            write_be32(flight_level_bytes + 6u, 100000u);
            write_be32(flight_level_bytes + 10u, 100000u);
            write_be32(flight_level_bytes + 14u, 100000u);
            write_be16(flight_level_bytes + 32u, 48u);
            write_be16(flight_level_bytes + 48u, UINT16_MAX);
            if (!level_dynamic_state_init(&flight_dynamic, &flight_level, error, sizeof(error))) {
                fprintf(stderr, "could not initialize ItsABullet source fixture: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            flight_objects.slot_bytes = flight_slot_bytes;
            flight_objects.slot_count = 3u;
            flight_objects.active_slot_count = 3u;
            flight_objects.point_bytes = flight_point_bytes;
            flight_objects.point_count = 2u;
            write_be16(flight_slot_bytes + 0u, 0u);
            write_be16(flight_slot_bytes + 12u, 0u);
            flight_slot_bytes[16u] = 2u;
            flight_slot_bytes[28u] = 7u;
            flight_slot_bytes[31u] = (uint8_t)projectile_bullet_index;
            write_be16(flight_slot_bytes + 58u, UINT16_MAX);
            write_be32(flight_slot_bytes + 18u, UINT32_C(0x00640000));
            write_be32(flight_slot_bytes + 36u, 1u);
            write_be32(flight_point_bytes + 0u, 0u);
            write_be32(flight_point_bytes + 4u, 0u);
            write_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 1u);
            write_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                       (uint16_t)expected_target_height);
            write_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
            flight_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
            flight_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] = 3u;
            write_be32(flight_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u,
                       UINT32_C(0x00320000));
            write_be32(flight_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u, 0u);
            write_be16(flight_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                       UINT16_MAX);
            if (!object_projectiles_update_flight_animation_slot(
                    &flight_objects, 0u, &flight_dynamic, &game.game_link_catalog, 1u,
                    error, sizeof(error)) ||
                read_be16(flight_slot_bytes + 12u) != 0u ||
                read_be16(flight_slot_bytes + 26u) != 0u ||
                read_be32(flight_point_bytes + 0u) != UINT32_C(0x00640000) ||
                read_be32(flight_point_bytes + 4u) != 0u ||
                flight_slot_bytes[30u] != 1u || flight_slot_bytes[52u] != 0u ||
                flight_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] != 10u ||
                read_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 42u) != 100u ||
                read_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 44u) != 0u) {
                fprintf(stderr, "ItsABullet source movement/target collision is inconsistent: %s\n",
                        error);
                level_dynamic_state_destroy(&flight_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
            level_dynamic_state_destroy(&flight_dynamic);
        }
        {
            /* Source-shaped BulT variants exercise the remaining ItsABullet branches. */
            uint8_t flight_link_bytes[GAME_LINK_SIZE];
            uint8_t flight_level_bytes[96u] = {0};
            uint8_t flight_graphics_bytes[4u] = {0};
            uint8_t flight_slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
            uint8_t flight_point_bytes[OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
            const uint8_t *bullet_table;
            size_t bullet_table_size;
            size_t bullet_table_offset;
            uint8_t *bullet_definition;
            GameLink flight_link = game.game_link_catalog;
            LevelRuntime flight_level = {0};
            LevelDynamicState flight_dynamic = {0};
            ObjectRuntime flight_objects = {0};

            if (!game_link_table(&game.game_link_catalog, GAME_LINK_TABLE_BULLET_DEFINITIONS,
                                 &bullet_table, &bullet_table_size) ||
                bullet_table_size != GAME_LINK_BULLET_COUNT * GAME_LINK_BULLET_DEFINITION_SIZE) {
                fprintf(stderr, "could not locate ItsABullet source definition table\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
            bullet_table_offset = (size_t)(bullet_table - game.game_link_catalog.bytes);
            memcpy(flight_link_bytes, game.game_link_catalog.bytes, sizeof(flight_link_bytes));
            flight_link.bytes = flight_link_bytes;
            bullet_definition = flight_link_bytes + bullet_table_offset +
                (size_t)projectile_bullet_index * GAME_LINK_BULLET_DEFINITION_SIZE;
            /* BulT: non-hitscan, zero gravity, no finite life unless stated below. */
            write_be32(bullet_definition + 0u, 0u);
            write_be32(bullet_definition + 4u, 0u);
            write_be32(bullet_definition + 8u, UINT32_MAX);
            write_be32(bullet_definition + 16u, 1u);
            write_be32(bullet_definition + 20u, 0u);
            flight_level.level_bytes = flight_level_bytes;
            flight_level.level_size = sizeof(flight_level_bytes);
            flight_level.graphics_bytes = flight_graphics_bytes;
            flight_level.graphics_size = sizeof(flight_graphics_bytes);
            flight_level.zone_offsets_table_offset = 0u;
            flight_level.edge_table_offset = 64u;
            flight_level.edge_count = 1u;
            flight_level.zone_count = 1u;
            write_be32(flight_graphics_bytes, 0u);
            write_be16(flight_level_bytes + 0u, 0u);
            write_be32(flight_level_bytes + 2u, 100000u);
            write_be32(flight_level_bytes + 6u, 200000u);
            write_be32(flight_level_bytes + 10u, 100000u);
            write_be32(flight_level_bytes + 14u, 200000u);
            write_be16(flight_level_bytes + 32u, 48u);
            write_be16(flight_level_bytes + 48u, 0u);
            write_be16(flight_level_bytes + 50u, UINT16_MAX);
            /* The same vertical solid edge used by the MoveObject source fixture. */
            write_be16(flight_level_bytes + 64u, 10u);
            write_be16(flight_level_bytes + 66u, 20u);
            write_be16(flight_level_bytes + 68u, 0u);
            write_be16(flight_level_bytes + 70u, UINT16_C(0xffec));
            write_be16(flight_level_bytes + 72u, UINT16_MAX);
            write_be16(flight_level_bytes + 74u, 20u);
            if (!level_dynamic_state_init(&flight_dynamic, &flight_level, error, sizeof(error))) {
                fprintf(stderr, "could not initialize ItsABullet collision fixture: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            flight_objects.slot_bytes = flight_slot_bytes;
            flight_objects.slot_count = 2u;
            flight_objects.active_slot_count = 2u;
            flight_objects.point_bytes = flight_point_bytes;
            flight_objects.point_count = 1u;
            write_be16(flight_slot_bytes + 0u, 0u);
            write_be16(flight_slot_bytes + 12u, 0u);
            flight_slot_bytes[16u] = 2u;
            flight_slot_bytes[31u] = (uint8_t)projectile_bullet_index;
            write_be16(flight_slot_bytes + 58u, UINT16_MAX);
            write_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, UINT16_MAX);
            write_be32(flight_point_bytes + 0u, 0u);
            write_be32(flight_point_bytes + 4u, UINT32_C(0x000a0000));
            write_be32(flight_slot_bytes + 18u, UINT32_C(0x00140000));
            if (!object_projectiles_update_flight_animation_slot(
                    &flight_objects, 0u, &flight_dynamic, &flight_link, 1u,
                    error, sizeof(error)) ||
                flight_slot_bytes[30u] != 0u ||
                read_be32(flight_point_bytes + 0u) != UINT32_C(0x000a0000) ||
                read_be16(flight_slot_bytes + 18u) != UINT16_C(0xffec)) {
                fprintf(stderr, "ItsABullet source horizontal bounce is inconsistent: %s\n",
                        error);
                level_dynamic_state_destroy(&flight_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
            write_be32(bullet_definition + 16u, 0u);
            flight_slot_bytes[30u] = 0u;
            flight_slot_bytes[52u] = 0u;
            write_be32(flight_slot_bytes + 18u, UINT32_C(0x00140000));
            write_be32(flight_slot_bytes + 44u, 0u);
            write_be32(flight_point_bytes + 0u, 0u);
            if (!object_projectiles_update_flight_animation_slot(
                    &flight_objects, 0u, &flight_dynamic, &flight_link, 1u,
                    error, sizeof(error)) ||
                flight_slot_bytes[30u] != 1u ||
                read_be32(flight_point_bytes + 0u) != UINT32_C(0x000a0000) ||
                read_be32(flight_slot_bytes + 44u) != (uint32_t)-320) {
                fprintf(stderr,
                        "ItsABullet source horizontal impact is inconsistent: %s "
                        "(status %u, x %08x, acc-y %08x)\n",
                        error, flight_slot_bytes[30u], read_be32(flight_point_bytes + 0u),
                        read_be32(flight_slot_bytes + 44u));
                level_dynamic_state_destroy(&flight_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
            /* Floor response uses BulT_BounceVert_l rather than ShotT's roof flag byte. */
            write_be32(bullet_definition + 20u, 1u);
            flight_slot_bytes[30u] = 0u;
            flight_slot_bytes[52u] = 0u;
            write_be32(flight_slot_bytes + 18u, 0u);
            write_be16(flight_slot_bytes + 42u, 256u);
            write_be32(flight_slot_bytes + 44u, 99000u);
            if (!object_projectiles_update_flight_animation_slot(
                    &flight_objects, 0u, &flight_dynamic, &flight_link, 1u,
                    error, sizeof(error)) ||
                flight_slot_bytes[30u] != 0u ||
                read_be16(flight_slot_bytes + 42u) != UINT16_C(0xff80) ||
                read_be32(flight_slot_bytes + 44u) != 98592u) {
                fprintf(stderr, "ItsABullet source floor bounce is inconsistent: %s\n", error);
                level_dynamic_state_destroy(&flight_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
            /* A finite source lifetime marks the same impact state after movement. */
            write_be32(bullet_definition + 8u, 0u);
            write_be32(bullet_definition + 20u, 0u);
            flight_slot_bytes[30u] = 0u;
            flight_slot_bytes[52u] = 0u;
            write_be16(flight_slot_bytes + 58u, 1u);
            write_be16(flight_slot_bytes + 42u, 0u);
            write_be32(flight_slot_bytes + 44u, 0u);
            if (!object_projectiles_update_flight_animation_slot(
                    &flight_objects, 0u, &flight_dynamic, &flight_link, 1u,
                    error, sizeof(error)) ||
                flight_slot_bytes[30u] != 1u || read_be16(flight_slot_bytes + 58u) != 1u) {
                fprintf(stderr, "ItsABullet source lifetime expiry is inconsistent: %s\n", error);
                level_dynamic_state_destroy(&flight_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
            level_dynamic_state_destroy(&flight_dynamic);
        }
        if (!player_shoot_spawn_projectile_volley(
                &projectile_objects, &game.math, &projectile_player,
                projectile_bullet_index, &projectile_bullet, 0u, 0,
                &spawned_count, error, sizeof(error)) || spawned_count != 1u ||
            read_be16(slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) != 4u) {
            fprintf(stderr, "firefive source zero-count projectile attempt is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            write_be16(slot_bytes + (size_t)shot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                       0u);
        }
        if (!player_shoot_spawn_projectile_volley(
                &projectile_objects, &game.math, &projectile_player,
                projectile_bullet_index, &projectile_bullet, 1u, 0,
                &spawned_count, error, sizeof(error)) || spawned_count != 0u) {
            fprintf(stderr, "firefive source projectile pool exhaustion is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* ObjectHandler:JUMPALIEN ORs a living entity's held door/lift mask first. */
        uint8_t slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime lock_objects = {0};
        MechanismRuntime lock_runtime;

        lock_objects.slot_bytes = slot_bytes;
        lock_objects.slot_count = 2u;
        lock_objects.active_slot_count = 2u;
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 12u, 0u);
        slot_bytes[16u] = 0u;
        slot_bytes[18u] = UINT8_MAX;
        write_be32(slot_bytes + 50u, 0x00000005u);
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        mechanism_runtime_init(&lock_runtime);
        lock_runtime.door_and_lift_locks = 0x0002u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            lock_runtime.door_and_lift_locks != 0x0007u ||
            read_be16(slot_bytes + 26u) != 0u) {
            fprintf(stderr, "ObjectHandler alien lock preamble is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        slot_bytes[18u] = 0u;
        lock_runtime.door_and_lift_locks = 0u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            lock_runtime.door_and_lift_locks != 0u) {
            fprintf(stderr, "ObjectHandler dead alien lock suppression is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime impact_objects = {0};
        GameBulletDefinition impact_bullet = {0};
        GameBulletAnimationFrame impact_frame;
        uint16_t impact_bullet_index = UINT16_MAX;

        /* Choose an authored pop sequence that remains inside BulT's 20 records. */
        for (uint16_t bullet_index = 0u; bullet_index < GAME_LINK_BULLET_COUNT;
             ++bullet_index) {
            if (!game_link_get_bullet_definition(&game.game_link_catalog, bullet_index,
                                                 &impact_bullet, error, sizeof(error))) {
                fprintf(stderr, "could not read source impact bullet definition: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if ((uint16_t)impact_bullet.pop_frames > 0u &&
                (uint16_t)impact_bullet.pop_frames < GAME_LINK_BULLET_ANIMATION_FRAME_COUNT) {
                impact_bullet_index = bullet_index;
                break;
            }
        }
        if (impact_bullet_index == UINT16_MAX ||
            !game_link_get_bullet_animation_frame(
                &game.game_link_catalog, GAME_LINK_BULLET_ANIMATION_POP,
                impact_bullet_index, 0u, &impact_frame, error, sizeof(error))) {
            fprintf(stderr, "source impact animation fixture is unavailable: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        impact_objects.slot_bytes = slot_bytes;
        impact_objects.slot_count = 1u;
        impact_objects.active_slot_count = 1u;
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 12u, 0u);
        slot_bytes[16u] = 2u;
        slot_bytes[30u] = 1u;
        slot_bytes[31u] = (uint8_t)impact_bullet_index;
        if (!object_projectiles_update_impact_slot(
                &impact_objects, 0u, &game.game_link_catalog, error, sizeof(error)) ||
            read_be16(slot_bytes + 6u) != impact_frame.word_2 ||
            slot_bytes[11u] != impact_frame.byte_1 || slot_bytes[52u] != 1u) {
            fprintf(stderr, "ItsABullet source impact animation start is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if ((int32_t)impact_bullet.impact_graphics_type < 1) {
            if (slot_bytes[9u] != impact_frame.byte_0 || slot_bytes[10u] != 0u) {
                fprintf(stderr, "ItsABullet bitmap impact descriptor is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        } else if (impact_bullet.impact_graphics_type == 1u) {
            if ((int16_t)read_be16(slot_bytes + 8u) !=
                (int16_t)-(int16_t)(int8_t)impact_frame.byte_0 || slot_bytes[10u] != 0u) {
                fprintf(stderr, "ItsABullet glare impact descriptor is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        } else if (slot_bytes[9u] != impact_frame.byte_0 || slot_bytes[10u] != 6u) {
            fprintf(stderr, "ItsABullet additive impact descriptor is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint16_t update_count = 0u; update_count < GAME_LINK_BULLET_ANIMATION_FRAME_COUNT;
             ++update_count) {
            if (slot_bytes[30u] == 0u ||
                !object_projectiles_update_impact_slot(
                    &impact_objects, 0u, &game.game_link_catalog, error, sizeof(error))) {
                break;
            }
        }
        if (slot_bytes[30u] != 0u || slot_bytes[52u] != 0u ||
            (int16_t)read_be16(slot_bytes + 12u) != -1 ||
            (int16_t)read_be16(slot_bytes + 26u) != -1) {
            fprintf(stderr, "ItsABullet source impact release is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /*
         * objectmove.s:MoveObject fixture for the zero-extension trace used
         * by newplayershoot.s:plr1_HitscanFailed. A solid edge stops at its
         * source contact point; opening the same edge then crosses zone 0 to
         * zone 1 and computes the source lower/top layer from the crossing.
         */
        uint8_t level_bytes[256u] = {0};
        uint8_t graphics_bytes[16u] = {0};
        LevelRuntime movement_level = {0};
        LevelDynamicState movement_state = {0};
        ObjectMovementTrace movement_trace = {0};
        uint16_t edge_flags = 0u;

        movement_level.level_bytes = level_bytes;
        movement_level.level_size = sizeof(level_bytes);
        movement_level.graphics_bytes = graphics_bytes;
        movement_level.graphics_size = sizeof(graphics_bytes);
        movement_level.zone_offsets_table_offset = 0u;
        movement_level.edge_table_offset = 200u;
        movement_level.edge_count = 1u;
        movement_level.zone_count = 2u;
        write_be32(graphics_bytes + 0u, 0u);
        write_be32(graphics_bytes + 4u, 100u);
        write_be16(level_bytes + 0u, 0u);
        write_be16(level_bytes + 100u, 1u);
        write_be32(level_bytes + 102u, 10000u);
        write_be32(level_bytes + 106u, 0u);
        write_be32(level_bytes + 110u, 10000u);
        write_be32(level_bytes + 114u, 0u);
        write_be16(level_bytes + 32u, 64u);
        write_be16(level_bytes + 132u, 64u);
        write_be16(level_bytes + 64u, 0u);
        write_be16(level_bytes + 66u, UINT16_MAX);
        write_be16(level_bytes + 164u, UINT16_MAX);
        write_be16(level_bytes + 200u, 10u);
        write_be16(level_bytes + 202u, 20u);
        write_be16(level_bytes + 204u, 0u);
        write_be16(level_bytes + 206u, UINT16_C(0xffec));
        write_be16(level_bytes + 208u, UINT16_MAX);
        write_be16(level_bytes + 210u, 20u);
        if (!level_dynamic_state_init(&movement_state, &movement_level,
                                      error, sizeof(error))) {
            fprintf(stderr, "could not initialize MoveObject source fixture: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        movement_trace.zone_index = 0u;
        movement_trace.old_x = 0;
        movement_trace.old_z = 10;
        movement_trace.new_x = 20;
        movement_trace.new_z = 10;
        /* Tests MoveObject's full-long DIVS height interpolation, not a word delta. */
        movement_trace.old_y = 1000;
        movement_trace.new_y = 66536;
        movement_trace.wall_flags = 0x0400u;
        movement_trace.away_from_wall = -1;
        movement_trace.exit_first = UINT8_MAX;
        if (!object_movement_trace_zero_extension(&movement_state, &movement_trace,
                                                  error, sizeof(error)) ||
            movement_trace.hit_wall != UINT8_MAX || movement_trace.new_x != 10 ||
            movement_trace.new_z != 10 || movement_trace.wall_hit_height != 33776 ||
            movement_trace.wall_x_size != 0 || movement_trace.wall_z_size != 0 ||
            movement_trace.wall_length != 0 ||
            !level_dynamic_state_get_edge_flags(&movement_state, 0u, &edge_flags) ||
            edge_flags != 0x0400u) {
            fprintf(stderr, "MoveObject zero-extension wall trace is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        {
            uint8_t miss_slot_bytes[OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT *
                                    OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
            uint8_t miss_point_bytes[OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
            ObjectRuntime miss_objects = {0};
            PlayerRuntime miss_player = {0};
            GameRandom miss_random;
            GameRandom expected_random;
            uint16_t spread_word;
            int16_t sine;
            int16_t cosine;
            int16_t ray_z;
            int32_t spread;
            int32_t expected_hit_height;
            uint8_t miss_spawned = 0u;

            /* A horizontal solid edge intersects Player 1's source yaw-zero ray. */
            write_be16(movement_state.level_bytes + 200u, UINT16_C(0xffec));
            write_be16(movement_state.level_bytes + 202u, 20u);
            write_be16(movement_state.level_bytes + 204u, 40u);
            write_be16(movement_state.level_bytes + 206u, 0u);
            write_be16(movement_state.level_bytes + 208u, UINT16_MAX);
            write_be16(movement_state.level_bytes + 210u, 40u);
            if (!level_dynamic_state_set_edge_flags(&movement_state, 0u, 0u) ||
                !game_math_sine(&game.math, 0u, &sine, error, sizeof(error)) ||
                !game_math_cosine(&game.math, 0u, &cosine, error, sizeof(error))) {
                fprintf(stderr, "could not prepare plr1_HitscanFailed fixture: %s\n", error);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }
            ray_z = (int16_t)source_asr32_7(cosine);
            if (sine != 0 || ray_z != 255) {
                fprintf(stderr, "plr1_HitscanFailed fixture yaw is inconsistent (%d, %d, %d)\n",
                        sine, cosine, ray_z);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }
            miss_objects.slot_bytes = miss_slot_bytes;
            miss_objects.slot_count = OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
            miss_objects.active_slot_count = OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
            miss_objects.player_shot_first_slot = 0u;
            miss_objects.point_bytes = miss_point_bytes;
            miss_objects.point_count = 1u;
            write_be16(miss_slot_bytes + 0u, 0u);
            write_be16(miss_slot_bytes + 12u, UINT16_MAX);
            miss_slot_bytes[16u] = 0x7eu;
            write_be16(miss_slot_bytes + 26u, UINT16_C(0x1234));
            write_be32(miss_point_bytes + 0u, UINT32_C(0x12345678));
            write_be32(miss_point_bytes + 4u, UINT32_C(0x9abcdef0));
            miss_player.x = 0;
            miss_player.y = 1000;
            miss_player.z = 10;
            miss_player.yaw = 0u;
            miss_player.zone_index = 0u;
            game_random_init(&miss_random);
            expected_random = miss_random;
            spread_word = game_random_next(&expected_random);
            spread = (int32_t)(spread_word & 0x0fffu) - 0x0800;
            expected_hit_height = miss_player.y + 10 * 128 + spread +
                (spread / ray_z) * (10 - ray_z);
            if (!player_shoot_apply_hitscan_miss(
                    &miss_objects, &movement_state, &miss_player, &game.math,
                    &miss_random, 7u, &miss_spawned, error, sizeof(error)) ||
                miss_spawned != UINT8_MAX || miss_random.state != expected_random.state ||
                miss_slot_bytes[16u] != 0x7eu ||
                read_be16(miss_slot_bytes + 12u) != 0u ||
                miss_slot_bytes[30u] != 1u || miss_slot_bytes[31u] != 7u ||
                miss_slot_bytes[52u] != 0u || read_be16(miss_slot_bytes + 54u) != 0u ||
                miss_slot_bytes[62u] != UINT8_MAX ||
                read_be32(miss_slot_bytes + 44u) != (uint32_t)expected_hit_height ||
                read_be16(miss_slot_bytes + 4u) !=
                    (uint16_t)source_asr32_7(expected_hit_height) ||
                read_be16(miss_slot_bytes + 26u) != UINT16_C(0x1234) ||
                read_be32(miss_point_bytes + 0u) != UINT32_C(0x00005678) ||
                read_be32(miss_point_bytes + 4u) != UINT32_C(0x0014def0) ||
                !level_dynamic_state_get_edge_flags(&movement_state, 0u, &edge_flags) ||
                edge_flags != 0x0400u) {
                fprintf(stderr, "plr1_HitscanFailed source miss state is inconsistent: %s\n",
                        error);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        write_be16(movement_state.level_bytes + 200u, 10u);
        write_be16(movement_state.level_bytes + 202u, 20u);
        write_be16(movement_state.level_bytes + 204u, 0u);
        write_be16(movement_state.level_bytes + 206u, UINT16_C(0xffec));
        write_be16(movement_state.level_bytes + 208u, 1u);
        write_be16(movement_state.level_bytes + 210u, 20u);
        if (!level_dynamic_state_set_edge_flags(&movement_state, 0u, 0u)) {
            fprintf(stderr, "could not reset MoveObject source fixture edge flags\n");
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        memset(&movement_trace, 0, sizeof(movement_trace));
        movement_trace.zone_index = 0u;
        movement_trace.old_x = 0;
        movement_trace.old_z = 10;
        movement_trace.new_x = 20;
        movement_trace.new_z = 10;
        movement_trace.old_y = 1000;
        movement_trace.new_y = 1000;
        movement_trace.away_from_wall = -1;
        if (!object_movement_trace_zero_extension(&movement_state, &movement_trace,
                                                  error, sizeof(error)) ||
            movement_trace.hit_wall != 0u || movement_trace.zone_index != 1u ||
            movement_trace.stood_in_top != 0u || movement_trace.new_x != 20 ||
            movement_trace.new_z != 10 ||
            !level_dynamic_state_get_edge_flags(&movement_state, 0u, &edge_flags) ||
            edge_flags != 0u) {
            fprintf(stderr, "MoveObject zero-extension zone crossing is inconsistent: %s\n",
                    error);
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        level_dynamic_state_destroy(&movement_state);
    }
    {
        /*
         * objectmove.s:CanItBeSeen fixture: authored PVST target zone 1,
         * empty left/right clip lists, then a single edge joining the two
         * lower layers at source height 10 << 7.
         */
        uint8_t level_bytes[256u] = {0};
        uint8_t graphics_bytes[64u] = {0};
        uint8_t clip_bytes[6u] = {0xffu, 0xffu, 0xffu, 0xffu, 0u, 0u};
        AssetBlob clips = {clip_bytes, 4u};
        LevelRuntime visibility_level = {0};
        ObjectVisibilityQuery visibility_query = {0};
        uint8_t can_see = 0u;

        visibility_level.level_bytes = level_bytes;
        visibility_level.level_size = sizeof(level_bytes);
        visibility_level.graphics_bytes = graphics_bytes;
        visibility_level.graphics_size = sizeof(graphics_bytes);
        visibility_level.world_point_count = 1u;
        visibility_level.world_points_offset = 232u;
        visibility_level.zone_graph_adds_offset = 8u;
        visibility_level.zone_offsets_table_offset = 0u;
        visibility_level.edge_table_offset = 200u;
        visibility_level.edge_count = 1u;
        visibility_level.zone_count = 2u;
        write_be32(graphics_bytes + 0u, 0u);
        write_be32(graphics_bytes + 4u, 100u);
        write_be32(graphics_bytes + 8u, 24u);
        write_be32(graphics_bytes + 16u, 26u);
        write_be16(graphics_bytes + 24u, 0u);
        write_be16(graphics_bytes + 26u, 1u);
        for (uint32_t zone_index = 0u; zone_index < 2u; ++zone_index) {
            uint32_t zone_offset = zone_index * 100u;

            write_be16(level_bytes + zone_offset + 0u, (uint16_t)zone_index);
            write_be32(level_bytes + zone_offset + 2u, 2560u);
            write_be16(level_bytes + zone_offset + 32u, 64u);
            write_be16(level_bytes + zone_offset + 48u, UINT16_MAX);
        }
        write_be16(level_bytes + 48u, 1u);
        write_be16(level_bytes + 50u, 0u);
        write_be16(level_bytes + 56u, UINT16_MAX);
        write_be16(level_bytes + 64u, 0u);
        write_be16(level_bytes + 66u, UINT16_MAX);
        write_be16(level_bytes + 164u, UINT16_MAX);
        write_be16(level_bytes + 200u, 10u);
        write_be16(level_bytes + 202u, 20u);
        write_be16(level_bytes + 204u, 0u);
        write_be16(level_bytes + 206u, UINT16_C(0xffec));
        write_be16(level_bytes + 208u, 1u);
        write_be16(level_bytes + 210u, 20u);
        visibility_query.viewer_zone_index = 0u;
        visibility_query.viewer_x = 0;
        visibility_query.viewer_z = 10;
        visibility_query.viewer_y = 10;
        visibility_query.target_zone_index = 1u;
        visibility_query.target_x = 20;
        visibility_query.target_z = 10;
        visibility_query.target_y = 10;
        error[0] = '\0';
        if (!object_visibility_can_see(&visibility_level, &clips, &visibility_query,
                                       &can_see, error, sizeof(error)) ||
            can_see != UINT8_MAX) {
            fprintf(stderr, "CanItBeSeen PVST/clip/join fixture is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        visibility_query.target_zone_index = 0u;
        visibility_query.target_in_upper_zone = 0u;
        if (!object_visibility_can_see(&visibility_level, &clips, &visibility_query,
                                       &can_see, error, sizeof(error)) ||
            can_see != UINT8_MAX) {
            fprintf(stderr, "CanItBeSeen same-zone lower layer is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        visibility_query.target_in_upper_zone = UINT8_MAX;
        if (!object_visibility_can_see(&visibility_level, &clips, &visibility_query,
                                       &can_see, error, sizeof(error)) || can_see != 0u) {
            fprintf(stderr, "CanItBeSeen same-zone layer split is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        visibility_query.viewer_zone_index = 1u;
        visibility_query.target_zone_index = 0u;
        visibility_query.target_in_upper_zone = 0u;
        if (!object_visibility_can_see(&visibility_level, &clips, &visibility_query,
                                       &can_see, error, sizeof(error)) || can_see != 0u) {
            fprintf(stderr, "CanItBeSeen PVST negative terminator is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        visibility_query.viewer_zone_index = 0u;
        visibility_query.target_zone_index = 1u;
        write_be16(level_bytes + 232u, 10u);
        write_be16(level_bytes + 234u, 0u);
        write_be16(clip_bytes + 0u, 0u);
        write_be16(clip_bytes + 2u, UINT16_MAX);
        write_be16(clip_bytes + 4u, UINT16_MAX);
        clips.size = sizeof(clip_bytes);
        if (!object_visibility_can_see(&visibility_level, &clips, &visibility_query,
                                       &can_see, error, sizeof(error)) || can_see != 0u) {
            fprintf(stderr, "CanItBeSeen left clip rejection is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    game_bootstrap_destroy(&game);
    (void)remove(save_path);
    return 0;
}
