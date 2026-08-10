#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alien_runtime.h"
#include "alien_animation.h"
#include "alien_damage.h"
#include "alien_decision.h"
#include "alien_death.h"
#include "alien_dark.h"
#include "alien_flight.h"
#include "alien_math.h"
#include "alien_main.h"
#include "alien_memory.h"
#include "alien_pause.h"
#include "alien_perception.h"
#include "alien_prowl.h"
#include "alien_setup.h"
#include "alien_spatial.h"
#include "alien_spawn.h"
#include "alien_torch.h"
#include "asset_io.h"
#include "game_bootstrap.h"
#include "game_link.h"
#include "game_inventory.h"
#include "game_menu.h"
#include "game_progression.h"
#include "game_random.h"
#include "game_save.h"
#include "level_bootstrap.h"
#include "level_draw_graph.h"
#include "lighting_runtime.h"
#include "object_collectables.h"
#include "object_collision.h"
#include "object_explosion.h"
#include "object_animation.h"
#include "object_handler.h"
#include "object_heading.h"
#include "object_movement.h"
#include "object_projectiles.h"
#include "object_scene.h"
#include "object_viewpoint.h"
#include "object_visibility.h"
#include "object_worry.h"
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

static int16_t source_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
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
    const uint8_t *alien_brightness_bytes;
    const uint8_t *alien_shoot_definition_bytes;
    const uint8_t *alien_animation_bytes;
    const uint8_t *object_definition_bytes;
    const uint8_t *object_default_animation_bytes;
    const uint8_t *object_action_animation_bytes;
    const uint8_t *object_frame_data_bytes;
    const uint8_t *object_ammunition_grant_bytes;
    const uint8_t *object_item_grant_bytes;
    size_t table_size;
    size_t shoot_definition_size;
    size_t alien_definition_size;
    size_t alien_animation_size;
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
    uint16_t alien_animation_option;
    uint16_t alien_animation_frame_index;
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
    GameShootDefinition alien_shoot_definition;
    GameAlienDefinition alien_definition;
    GameAlienAnimationFrame alien_animation_frame;
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
    AlienRuntime lock_alien_runtime;
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
    if (alien_runtime.no_enemies != 0u) {
        fprintf(stderr, "source AI_NoEnemies BSS initialization is inconsistent\n");
        return 1;
    }
    alien_runtime_begin_single_player(&alien_runtime);
    if (alien_runtime.no_enemies != UINT8_MAX) {
        fprintf(stderr, "SETPLAYERS single-player alien gate is inconsistent\n");
        return 1;
    }
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
        !game_link_table(&game_link, GAME_LINK_TABLE_ALIEN_BRIGHTNESS,
                         &alien_brightness_bytes, &table_size) ||
        table_size != (size_t)GAME_LINK_ALIEN_COUNT * 2u ||
        !game_link_table(&game_link, GAME_LINK_TABLE_ALIEN_SHOOT_DEFINITIONS,
                         &alien_shoot_definition_bytes, &table_size) ||
        table_size != (size_t)GAME_LINK_ALIEN_COUNT * GAME_LINK_SHOOT_DEFINITION_SIZE ||
        !game_link_table(&game_link, GAME_LINK_TABLE_ALIEN_ANIMATIONS,
                         &alien_animation_bytes, &alien_animation_size) ||
        alien_animation_size != (size_t)GAME_LINK_ALIEN_COUNT *
                                    GAME_LINK_ALIEN_ANIMATION_SIZE ||
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
            !game_link_get_alien_brightness(&game_link, alien_definition_index,
                                            &trig_value, error, sizeof(error)) ||
            trig_value != (int16_t)read_be16(
                alien_brightness_bytes + (size_t)alien_definition_index * 2u) ||
            !game_link_get_alien_shoot_definition(
                &game_link, alien_definition_index, &alien_shoot_definition,
                error, sizeof(error)) ||
            !shoot_definition_matches_source(
                &alien_shoot_definition,
                alien_shoot_definition_bytes + (size_t)alien_definition_index *
                    GAME_LINK_SHOOT_DEFINITION_SIZE) ||
            !alien_definition_matches_source(
                &alien_definition,
                alien_definition_bytes + (size_t)alien_definition_index *
                    GAME_LINK_ALIEN_DEFINITION_SIZE)) {
            fprintf(stderr, "GLFT alien definition %u is inconsistent: %s\n",
                    alien_definition_index, error);
            asset_blob_release(&game_link_blob);
            return 1;
        }
        for (alien_animation_option = 0u;
             alien_animation_option < GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT;
             ++alien_animation_option) {
            for (alien_animation_frame_index = 0u;
                 alien_animation_frame_index < GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT;
                 ++alien_animation_frame_index) {
                size_t frame_offset =
                    ((size_t)alien_definition_index * GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT +
                     alien_animation_option) * GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT +
                    alien_animation_frame_index;

                if (!game_link_get_alien_animation_frame(
                        &game_link, alien_definition_index, alien_animation_option,
                        alien_animation_frame_index, &alien_animation_frame,
                        error, sizeof(error)) ||
                    memcmp(alien_animation_frame.bytes,
                           alien_animation_bytes +
                               frame_offset * GAME_LINK_ALIEN_ANIMATION_FRAME_SIZE,
                           GAME_LINK_ALIEN_ANIMATION_FRAME_SIZE) != 0) {
                    fprintf(stderr, "GLFT alien animation %u option %u frame %u is inconsistent: %s\n",
                            alien_definition_index, alien_animation_option,
                            alien_animation_frame_index, error);
                    asset_blob_release(&game_link_blob);
                    return 1;
                }
            }
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
        game_link_get_alien_brightness(&game_link, GAME_LINK_ALIEN_COUNT,
                                       &trig_value, error, sizeof(error)) ||
        game_link_get_alien_shoot_definition(&game_link, GAME_LINK_ALIEN_COUNT,
                                             &alien_shoot_definition,
                                             error, sizeof(error)) ||
        game_link_get_alien_animation_frame(
            &game_link, 0u, GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT, 0u,
            &alien_animation_frame, error, sizeof(error)) ||
        game_link_get_alien_animation_frame(
            &game_link, 0u, 0u, GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT,
            &alien_animation_frame, error, sizeof(error)) ||
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
            game.alien_runtime.no_enemies != UINT8_MAX ||
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
                        &game.alien_runtime,
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
                            &game.alien_runtime,
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
                            &game.alien_runtime,
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
        controlled_player.look_offset != -4 ||
        controlled_player.aim_speed != (int32_t)UINT16_C(0xfe00) ||
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
    {
        PlayerRuntime mouse_player = game.player;
        GameInput mouse_input;
        uint16_t expected_mouse_yaw;

        /* c/system.c:Sys_ReadMouse then modules/player.s:plr_MouseControl. */
        game_input_init(&mouse_input);
        expected_mouse_yaw = game_math_wrap_angle_address(
            (uint16_t)((uint16_t)mouse_player.yaw + UINT16_C(5) * UINT16_C(4)));
        game_input_add_mouse_motion(&mouse_input, 5, -3);
        if (!player_runtime_update_spatial(&mouse_player, &mouse_input, &control_defaults,
                                           &game.preferences, &game.math, &game.level_runtime,
                                           NULL, error, sizeof(error)) ||
            mouse_player.yaw != expected_mouse_yaw || mouse_player.snap_yaw != expected_mouse_yaw ||
            mouse_player.look_offset != -3 ||
            mouse_player.aim_speed != (int32_t)UINT16_C(0xfe80) ||
            mouse_input.pending_mouse_x != 0 || mouse_input.mouse_y != -3 ||
            mouse_input.old_mouse_y != -3) {
            fprintf(stderr,
                    "source mouse control state is inconsistent: yaw=%u expected=%u snap=%u "
                    "look=%d aim=%ld x=%d y=%d old_y=%d: %s\n",
                    (unsigned int)mouse_player.yaw, (unsigned int)expected_mouse_yaw,
                    (unsigned int)mouse_player.snap_yaw,
                    mouse_player.look_offset, (long)mouse_player.aim_speed,
                    mouse_input.pending_mouse_x, mouse_input.mouse_y, mouse_input.old_mouse_y,
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        /* PlrT_InvMouse_b negates Sys_MouseY before the old-word subtraction. */
        mouse_player.invert_mouse = UINT8_MAX;
        game_input_add_mouse_motion(&mouse_input, 0, 4);
        if (!player_runtime_update_spatial(&mouse_player, &mouse_input, &control_defaults,
                                           &game.preferences, &game.math, &game.level_runtime,
                                           NULL, error, sizeof(error)) ||
            mouse_player.yaw != expected_mouse_yaw || mouse_player.look_offset != -1 ||
            mouse_player.aim_speed != (int32_t)UINT16_C(0xff80) || mouse_input.mouse_y != 1 ||
            mouse_input.old_mouse_y != -1) {
            fprintf(stderr, "source inverted mouse control state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
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
                &game.alien_runtime,
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
        uint8_t slot_bytes[3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime worry_objects = {0};
        AlienRuntime worry_alien_runtime;
        PlayerRuntime worry_player = game.player;
        uint16_t viewer_zone_index = UINT16_MAX;
        uint16_t visible_zone_index = UINT16_MAX;
        uint16_t hidden_zone_index = UINT16_MAX;

        if (game.dynamic_level.runtime.zone_count > 256u) {
            fprintf(stderr, "source worry fixture exceeds Sys_Workspace capacity\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint16_t candidate_viewer = 0u;
             candidate_viewer < game.dynamic_level.runtime.zone_count;
             ++candidate_viewer) {
            uint8_t visible_zone_bits[32u] = {0};
            uint16_t candidate_visible = UINT16_MAX;
            uint16_t candidate_hidden = UINT16_MAX;
            int pvs_terminated = 0;

            for (uint32_t pvs_entry_index = 0u;
                 pvs_entry_index <= game.dynamic_level.runtime.level_size / 8u;
                 ++pvs_entry_index) {
                LevelPotentialVisibility entry;

                if (!level_runtime_get_zone_potential_visibility(
                        &game.dynamic_level.runtime, candidate_viewer, pvs_entry_index,
                        &entry, error, sizeof(error))) {
                    fprintf(stderr, "source worry PVST fixture is invalid: %s\n", error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                if (entry.zone_index < 0) {
                    pvs_terminated = 1;
                    break;
                }
                visible_zone_bits[(uint16_t)entry.zone_index >> 3u] |=
                    (uint8_t)(UINT8_C(1) << ((uint16_t)entry.zone_index & 7u));
                if (candidate_visible == UINT16_MAX) {
                    candidate_visible = (uint16_t)entry.zone_index;
                }
            }
            if (pvs_terminated == 0 || candidate_visible == UINT16_MAX) {
                continue;
            }
            for (uint16_t candidate_target = 0u;
                 candidate_target < game.dynamic_level.runtime.zone_count;
                 ++candidate_target) {
                if ((visible_zone_bits[candidate_target >> 3u] &
                     (uint8_t)(UINT8_C(1) << (candidate_target & 7u))) == 0u) {
                    candidate_hidden = candidate_target;
                    break;
                }
            }
            if (candidate_hidden != UINT16_MAX) {
                viewer_zone_index = candidate_viewer;
                visible_zone_index = candidate_visible;
                hidden_zone_index = candidate_hidden;
                break;
            }
        }
        if (viewer_zone_index == UINT16_MAX || visible_zone_index == UINT16_MAX ||
            hidden_zone_index == UINT16_MAX) {
            fprintf(stderr, "source worry fixture could not find visible and hidden zones\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        worry_objects.slot_bytes = slot_bytes;
        worry_objects.slot_count = 3u;
        worry_objects.active_slot_count = 3u;
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 12u, visible_zone_index);
        slot_bytes[16u] = 1u;
        slot_bytes[62u] = 0x80u;
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 1u);
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, hidden_zone_index);
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 21u] = 0u;
        write_be16(slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        alien_runtime_init(&worry_alien_runtime);
        alien_runtime_begin_level(&worry_alien_runtime);
        worry_player.zone_index = viewer_zone_index;
        if (!object_worry_update_single_player(
                &worry_objects, &game.dynamic_level.runtime, &worry_player,
                &worry_alien_runtime, error, sizeof(error)) ||
            slot_bytes[62u] != UINT8_MAX ||
            slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 62u] != 0u) {
            fprintf(stderr, "source PVST worry activation is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        worry_alien_runtime.team_workspace[0u][4u] = 0;
        if (!object_worry_update_single_player(
                &worry_objects, &game.dynamic_level.runtime, &worry_player,
                &worry_alien_runtime, error, sizeof(error)) ||
            slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 62u] != 0x7fu) {
            fprintf(stderr, "source team worry activation is inconsistent: %s\n", error);
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
        alien_runtime_init(&lock_alien_runtime);
        alien_runtime_begin_single_player(&lock_alien_runtime);
        lock_runtime.door_and_lift_locks = 0x0002u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &lock_alien_runtime,
                &game.game_link_catalog,
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
                &lock_objects, &game.dynamic_level, &lock_runtime, &lock_alien_runtime,
                &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            lock_runtime.door_and_lift_locks != 0u) {
            fprintf(stderr, "ObjectHandler dead alien lock suppression is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be16(slot_bytes + 12u, 0u);
        write_be16(slot_bytes + 26u, 0u);
        slot_bytes[18u] = UINT8_MAX;
        lock_alien_runtime.no_enemies = 0u;
        lock_runtime.door_and_lift_locks = 0u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &lock_alien_runtime,
                &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            read_be16(slot_bytes + 12u) != UINT16_MAX ||
            read_be16(slot_bytes + 26u) != 0u ||
            lock_runtime.door_and_lift_locks != 0x0005u) {
            fprintf(stderr, "ItsAnAlien no-enemies gate is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        alien_runtime_begin_single_player(&lock_alien_runtime);
        write_be16(slot_bytes + 12u, UINT16_MAX);
        slot_bytes[18u] = UINT8_MAX;
        lock_runtime.door_and_lift_locks = 0u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &lock_alien_runtime,
                &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            read_be16(slot_bytes + 26u) != UINT16_MAX ||
            lock_runtime.door_and_lift_locks != 0u) {
            fprintf(stderr, "ObjectHandler negative alien-zone gate is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_CheckInFront reads the source point high words and Tmp snapshot. */
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime decision_objects = {0};
        PlayerRuntime decision_player = game.player;
        uint16_t positive_sine_angle = 0u;
        uint16_t negative_sine_angle = 0u;
        int16_t sine_value;
        uint8_t in_front;

        decision_objects.slot_bytes = slot_bytes;
        decision_objects.slot_count = 1u;
        decision_objects.active_slot_count = 1u;
        decision_objects.point_bytes = point_bytes;
        decision_objects.point_count = 1u;
        write_be16(slot_bytes + 0u, 0u);
        write_be32(point_bytes + 0u, 0u);
        write_be32(point_bytes + 4u, 0u);
        for (uint16_t angle = 0u; angle < GAME_MATH_SINE_CYCLE_BYTES; angle += 2u) {
            if (!game_math_sine(&game.math, angle, &sine_value, error, sizeof(error))) {
                fprintf(stderr, "could not read ai_CheckInFront source sine: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (sine_value > 0 && positive_sine_angle == 0u) {
                positive_sine_angle = angle;
            }
            if (sine_value < 0 && negative_sine_angle == 0u) {
                negative_sine_angle = angle;
            }
        }
        if (positive_sine_angle == 0u || negative_sine_angle == 0u) {
            fprintf(stderr, "source bigsine does not provide ai_CheckInFront test angles\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        decision_player.tmp_x = 1;
        decision_player.tmp_z = 0;
        decision_player.x = -500;
        decision_player.z = 500;
        write_be16(slot_bytes + 30u, positive_sine_angle);
        if (!alien_decision_check_in_front(&decision_objects, 0u, &decision_player,
                                           &game.math, &in_front,
                                           error, sizeof(error)) ||
            in_front != UINT8_MAX) {
            fprintf(stderr, "ai_CheckInFront positive source projection is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be16(slot_bytes + 30u, negative_sine_angle);
        if (!alien_decision_check_in_front(&decision_objects, 0u, &decision_player,
                                           &game.math, &in_front,
                                           error, sizeof(error)) ||
            in_front != 0u) {
            fprintf(stderr, "ai_CheckInFront negative source projection is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_CheckAttackOnGround uses only GetNextCPt's walk link. */
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t walk_links[LEVEL_NAVIGATION_MAP_BYTES] = {0};
        uint8_t fly_links[LEVEL_NAVIGATION_MAP_BYTES] = {0};
        ObjectRuntime decision_objects = {0};
        LevelNavigation decision_navigation = {0};
        PlayerRuntime decision_player = game.player;
        LevelZone decision_zone;
        uint8_t lower_control_point;
        uint8_t upper_control_point;
        uint8_t different_control_point;
        uint8_t current_control_point;
        uint8_t can_attack;
        size_t navigation_offset;

        if (!level_runtime_get_zone(&game.dynamic_level.runtime, decision_player.zone_index,
                                    &decision_zone, error, sizeof(error))) {
            fprintf(stderr, "could not read ai_CheckAttackOnGround source zone: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        lower_control_point = (uint8_t)(decision_zone.control_point >> 8u);
        upper_control_point = (uint8_t)decision_zone.control_point;
        if (lower_control_point >= LEVEL_NAVIGATION_CONTROL_POINT_LIMIT ||
            upper_control_point >= LEVEL_NAVIGATION_CONTROL_POINT_LIMIT) {
            fprintf(stderr, "source player control point exceeds GetNextCPt map\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        decision_objects.slot_bytes = slot_bytes;
        decision_objects.slot_count = 1u;
        decision_objects.active_slot_count = 1u;
        decision_navigation.walk_links = walk_links;
        decision_navigation.walk_links_size = sizeof(walk_links);
        decision_navigation.fly_links = fly_links;
        decision_navigation.fly_links_size = sizeof(fly_links);
        decision_player.stood_in_top = 0u;
        write_be16(slot_bytes + 28u, lower_control_point);
        if (!alien_decision_check_attack_on_ground(
                &decision_objects, 0u, &game.dynamic_level.runtime,
                &decision_navigation, &decision_player, &can_attack,
                error, sizeof(error)) ||
            can_attack != UINT8_MAX) {
            fprintf(stderr, "ai_CheckAttackOnGround lower control-point match is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        decision_player.stood_in_top = UINT8_MAX;
        write_be16(slot_bytes + 28u, upper_control_point);
        if (!alien_decision_check_attack_on_ground(
                &decision_objects, 0u, &game.dynamic_level.runtime,
                &decision_navigation, &decision_player, &can_attack,
                error, sizeof(error)) ||
            can_attack != UINT8_MAX) {
            fprintf(stderr, "ai_CheckAttackOnGround upper control-point match is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        decision_player.stood_in_top = 0u;
        current_control_point = lower_control_point == 0u ? 1u : 0u;
        different_control_point = lower_control_point == 0u ? 1u : 0u;
        navigation_offset = (size_t)current_control_point *
            LEVEL_NAVIGATION_CONTROL_POINT_LIMIT + lower_control_point;
        walk_links[navigation_offset] = (uint8_t)(lower_control_point | 0x80u);
        fly_links[navigation_offset] = different_control_point;
        write_be16(slot_bytes + 28u, current_control_point);
        if (!alien_decision_check_attack_on_ground(
                &decision_objects, 0u, &game.dynamic_level.runtime,
                &decision_navigation, &decision_player, &can_attack,
                error, sizeof(error)) ||
            can_attack != UINT8_MAX) {
            fprintf(stderr, "ai_CheckAttackOnGround walk/ONLYSEE source path is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        walk_links[navigation_offset] = different_control_point;
        if (!alien_decision_check_attack_on_ground(
                &decision_objects, 0u, &game.dynamic_level.runtime,
                &decision_navigation, &decision_player, &can_attack,
                error, sizeof(error)) ||
            can_attack != 0u) {
            fprintf(stderr, "ai_CheckAttackOnGround non-arrival source path is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_GetRoomStats[Still]/ai_GetRoomCPT retain alien spatial state. */
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime spatial_objects = {0};
        LevelZone spatial_zone;
        int32_t thing_height = 513;
        int32_t expected_lower_height;
        int32_t expected_upper_height;

        if (!level_runtime_get_zone(&game.dynamic_level.runtime, game.player.zone_index,
                                    &spatial_zone, error, sizeof(error))) {
            fprintf(stderr, "could not read ai_GetRoomStats source zone: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        spatial_objects.slot_bytes = slot_bytes;
        spatial_objects.slot_count = 1u;
        spatial_objects.active_slot_count = 1u;
        spatial_objects.point_bytes = point_bytes;
        spatial_objects.point_count = 1u;
        write_be16(slot_bytes + 0u, 0u);
        write_be32(point_bytes + 0u, 0xaaaabbbbu);
        write_be32(point_bytes + 4u, 0xccccddddu);
        write_be16(slot_bytes + 12u, UINT16_MAX);
        write_be16(slot_bytes + 26u, UINT16_MAX);
        expected_lower_height = source_asr32_7(
            (int32_t)((uint32_t)spatial_zone.floor -
                      (uint32_t)source_asr32_count(thing_height, 1u)));
        expected_upper_height = source_asr32_7(
            (int32_t)((uint32_t)spatial_zone.upper_floor -
                      (uint32_t)source_asr32_count(thing_height, 1u)));
        if (!alien_spatial_store_room_stats(
                &spatial_objects, 0u, &game.dynamic_level.runtime, game.player.zone_index,
                0x1234, -2, thing_height, error, sizeof(error)) ||
            read_be32(point_bytes + 0u) != 0x1234bbbbu ||
            read_be32(point_bytes + 4u) != 0xfffeddddu ||
            read_be16(slot_bytes + 12u) != spatial_zone.id ||
            read_be16(slot_bytes + 26u) != spatial_zone.id ||
            read_be16(slot_bytes + 4u) != (uint16_t)expected_lower_height) {
            fprintf(stderr, "ai_GetRoomStats lower source state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if (!alien_spatial_store_current_control_point(
                &spatial_objects, 0u, &game.dynamic_level.runtime, game.player.zone_index,
                error, sizeof(error)) ||
            read_be16(slot_bytes + 28u) != (spatial_zone.control_point >> 8u)) {
            fprintf(stderr, "ai_GetRoomCPT lower source state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        slot_bytes[63u] = UINT8_MAX;
        if (!alien_spatial_store_room_stats_still(
                &spatial_objects, 0u, &game.dynamic_level.runtime, game.player.zone_index,
                thing_height, error, sizeof(error)) ||
            read_be32(point_bytes + 0u) != 0x1234bbbbu ||
            read_be32(point_bytes + 4u) != 0xfffeddddu ||
            read_be16(slot_bytes + 4u) != (uint16_t)expected_upper_height ||
            !alien_spatial_store_current_control_point(
                &spatial_objects, 0u, &game.dynamic_level.runtime, game.player.zone_index,
                error, sizeof(error)) ||
            read_be16(slot_bytes + 28u) != (spatial_zone.control_point & 0x00ffu)) {
            fprintf(stderr, "ai_GetRoomStats/ai_GetRoomCPT upper source state is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* newaliencontrol.s:ItsAnAlien assembles a source-owned per-alien context. */
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime setup_objects = {0};
        LevelZone setup_zone;
        AlienSetup setup;
        GameAlienDefinition definition;
        GameShootDefinition alien_shoot;
        int16_t alien_brightness;

        if (!level_runtime_get_zone(&game.dynamic_level.runtime, game.player.zone_index,
                                    &setup_zone, error, sizeof(error))) {
            fprintf(stderr, "could not read ItsAnAlien source zone: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        setup_objects.slot_bytes = slot_bytes;
        setup_objects.slot_count = 1u;
        setup_objects.active_slot_count = 1u;
        write_be16(slot_bytes + 0u, 17u);
        write_be16(slot_bytes + 12u, setup_zone.id);
        for (uint16_t alien_index = 0u; alien_index < GAME_LINK_ALIEN_COUNT; ++alien_index) {
            slot_bytes[54u] = (uint8_t)alien_index;
            if (!game_link_get_alien_definition(&game.game_link_catalog, alien_index,
                                                &definition, error, sizeof(error)) ||
                !game_link_get_alien_brightness(&game.game_link_catalog, alien_index,
                                                &alien_brightness, error, sizeof(error)) ||
                !game_link_get_alien_shoot_definition(&game.game_link_catalog, alien_index,
                                                      &alien_shoot, error, sizeof(error)) ||
                !alien_setup_from_slot(&setup_objects, 0u, &game.dynamic_level.runtime,
                                       &game.game_link_catalog, &setup,
                                       error, sizeof(error)) ||
                definition.girth > 2u ||
                setup.alien_type != alien_index || setup.zone_id != setup_zone.id ||
                setup.object_point_index != 17u || setup.zone_echo != setup_zone.echo ||
                setup.brightness != (int16_t)(0u - (uint16_t)alien_brightness) ||
                memcmp(&setup.shoot_definition, &alien_shoot, sizeof(alien_shoot)) != 0 ||
                setup.shot_y_offset != (int32_t)(((uint32_t)alien_shoot.bullet_type << 23u) |
                                                  ((uint32_t)alien_shoot.delay << 7u)) ||
                setup.shot_offset_multiplier !=
                    (int16_t)((uint16_t)(0u - alien_shoot.sound_effect) << 2u) ||
                setup.thing_height != (int32_t)(int16_t)definition.height * 128 ||
                setup.auxiliary_object_type != (int16_t)definition.auxiliary_type ||
                setup.vector_object_flag != (uint8_t)definition.graphics_type ||
                setup.default_mode != (int16_t)definition.default_behaviour ||
                setup.response_mode != (int16_t)definition.response_behaviour ||
                setup.retreat_mode != (int16_t)definition.retreat_behaviour ||
                setup.followup_mode != (int16_t)definition.followup_behaviour ||
                setup.prowl_speed != (int16_t)definition.default_speed ||
                setup.response_speed != (int16_t)definition.response_speed ||
                setup.retreat_speed != (int16_t)definition.retreat_speed ||
                setup.followup_speed != (int16_t)definition.followup_speed ||
                setup.followup_timer != (int16_t)definition.followup_timeout ||
                setup.away_from_wall != (int8_t)definition.girth ||
                setup.extended_wall_length != (int16_t)(40u << definition.girth)) {
                fprintf(stderr, "ItsAnAlien setup %u is inconsistent: %s\n", alien_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
    }
    {
        /* modules/ai.s flying helpers accelerate and clamp around source room bounds. */
        uint8_t slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime flight_objects = {0};
        PlayerRuntime flight_player = game.player;
        LevelZone flight_zone = {0};
        uint16_t flight_zone_index = UINT16_MAX;
        int16_t lower_floor;
        int16_t lower_roof;
        int16_t safe_height;
        int32_t thing_height = 2560;

        for (uint16_t zone_index = 0u;
             zone_index < game.dynamic_level.runtime.zone_count; ++zone_index) {
            LevelZone candidate;

            if (!level_runtime_get_zone(&game.dynamic_level.runtime, zone_index,
                                        &candidate, error, sizeof(error))) {
                fprintf(stderr, "could not scan ai flight source zones: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            lower_floor = (int16_t)source_asr32_count(candidate.floor, 7u);
            lower_roof = (int16_t)source_asr32_count(candidate.roof, 7u);
            if (lower_floor > lower_roof + 128) {
                flight_zone = candidate;
                flight_zone_index = zone_index;
                break;
            }
        }
        if (flight_zone_index == UINT16_MAX) {
            fprintf(stderr, "could not establish ai flight source state: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        lower_floor = (int16_t)source_asr32_count(flight_zone.floor, 7u);
        lower_roof = (int16_t)source_asr32_count(flight_zone.roof, 7u);
        safe_height = (int16_t)(lower_roof + (lower_floor - lower_roof) / 2);
        flight_objects.slot_bytes = slot_bytes;
        flight_objects.slot_count = 2u;
        flight_objects.active_slot_count = 2u;
        write_be16(slot_bytes + 4u, safe_height);
        write_be16(slot_bytes + 48u, 31u);
        if (!alien_flight_move_toward_height(
                &flight_objects, 0u, &game.dynamic_level.runtime, flight_zone_index,
                (int16_t)(safe_height + 1), thing_height, error, sizeof(error)) ||
            read_be16(slot_bytes + 48u) != 32u ||
            read_be16(slot_bytes + 4u) != (uint16_t)(safe_height + 32)) {
            fprintf(stderr, "ai_FlyToHeightCommon downward clamp is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be16(slot_bytes + 4u, safe_height);
        write_be16(slot_bytes + 48u, (uint16_t)-31);
        if (!alien_flight_move_toward_height(
                &flight_objects, 0u, &game.dynamic_level.runtime, flight_zone_index,
                (int16_t)(safe_height - 1), thing_height, error, sizeof(error)) ||
            read_be16(slot_bytes + 48u) != (uint16_t)-32 ||
            read_be16(slot_bytes + 4u) != (uint16_t)(safe_height - 32)) {
            fprintf(stderr, "ai_FlyToHeightCommon upward clamp is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be16(slot_bytes + 4u, lower_floor);
        write_be16(slot_bytes + 48u, 0u);
        if (!alien_flight_check_floor_ceiling(
                &flight_objects, 0u, &game.dynamic_level.runtime, flight_zone_index,
                thing_height, error, sizeof(error)) ||
            read_be16(slot_bytes + 4u) != (uint16_t)(lower_floor - 10)) {
            fprintf(stderr, "ai_CheckFloorCeiling lower source clamp is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be16(slot_bytes + 4u, safe_height);
        write_be16(slot_bytes + 48u, 0u);
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u, safe_height);
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 48u, 0u);
        flight_player.y = (int32_t)(safe_height + 20) * 128;
        if (!alien_flight_move_toward_player_height(
                &flight_objects, 0u, &game.dynamic_level.runtime, flight_zone_index,
                &flight_player, thing_height, error, sizeof(error)) ||
            !alien_flight_move_toward_height(
                &flight_objects, 1u, &game.dynamic_level.runtime, flight_zone_index,
                (int16_t)(safe_height + 20), thing_height, error, sizeof(error)) ||
            memcmp(slot_bytes, slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT,
                   OBJECT_RUNTIME_SLOT_BYTE_COUNT) != 0) {
            fprintf(stderr, "ai_FlyToPlayerHeight source handoff is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_StorePlayerPosition keeps per-entity and team memory separate. */
        uint8_t slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime memory_objects = {0};
        AlienRuntime memory_alien_runtime;
        PlayerRuntime memory_player = game.player;
        LevelZone memory_zone;
        uint16_t expected_lower_control_point;
        uint16_t expected_upper_control_point;

        if (!level_runtime_get_zone(&game.dynamic_level.runtime, memory_player.zone_index,
                                    &memory_zone, error, sizeof(error))) {
            fprintf(stderr, "could not read ai_StorePlayerPosition source zone: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        expected_lower_control_point = (uint16_t)(memory_zone.control_point >> 8u);
        expected_upper_control_point = (uint16_t)(memory_zone.control_point & 0x00ffu);
        memory_objects.slot_bytes = slot_bytes;
        memory_objects.slot_count = 2u;
        memory_objects.active_slot_count = 1u;
        write_be16(slot_bytes + 0u, 17u);
        slot_bytes[21u] = 2u;
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        memory_player.x = 0x1234;
        memory_player.z = -2;
        memory_player.stood_in_top = 0u;
        alien_runtime_init(&memory_alien_runtime);
        alien_runtime_begin_level(&memory_alien_runtime);
        if (!alien_memory_store_player_position(
                &memory_alien_runtime, &memory_objects, 0u, &game.dynamic_level.runtime,
                &memory_player, error, sizeof(error)) ||
            memory_alien_runtime.entity_workspace[17u][0u] != 0x1234 ||
            memory_alien_runtime.entity_workspace[17u][1u] != -2 ||
            memory_alien_runtime.entity_workspace[17u][2u] != (int16_t)memory_zone.id ||
            memory_alien_runtime.entity_workspace[17u][3u] !=
                (int16_t)expected_lower_control_point ||
            memory_alien_runtime.entity_workspace[17u][4u] != -1 ||
            memory_alien_runtime.team_workspace[2u][0u] != 0x1234 ||
            memory_alien_runtime.team_workspace[2u][1u] != -2 ||
            memory_alien_runtime.team_workspace[2u][2u] != (int16_t)memory_zone.id ||
            memory_alien_runtime.team_workspace[2u][3u] !=
                (int16_t)expected_lower_control_point ||
            memory_alien_runtime.team_workspace[2u][4u] != 17) {
            fprintf(stderr, "ai_StorePlayerPosition lower-zone state is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        memory_player.stood_in_top = UINT8_MAX;
        if (!alien_memory_store_player_position(
                &memory_alien_runtime, &memory_objects, 0u, &game.dynamic_level.runtime,
                &memory_player, error, sizeof(error)) ||
            memory_alien_runtime.entity_workspace[17u][3u] !=
                (int16_t)expected_upper_control_point ||
            memory_alien_runtime.team_workspace[2u][3u] !=
                (int16_t)expected_upper_control_point ||
            memory_alien_runtime.team_workspace[2u][4u] != 17) {
            fprintf(stderr, "ai_StorePlayerPosition upper-zone state is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:AI_LookForPlayer1 clears then writes a literal one on sight. */
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime perception_objects = {0};
        PlayerRuntime perception_player = game.player;

        perception_objects.slot_bytes = slot_bytes;
        perception_objects.slot_count = 1u;
        perception_objects.active_slot_count = 1u;
        perception_player.x = 100;
        perception_player.z = 200;
        perception_player.y = 3 * 128;
        perception_player.stood_in_top = 0u;
        slot_bytes[17u] = UINT8_MAX;
        write_be16(slot_bytes + 4u, 3u);
        if (!alien_perception_look_for_player_one(
                &perception_objects, 0u, &game.dynamic_level.runtime, &game.level_clips,
                &perception_player, perception_player.zone_index, 90, 190,
                error, sizeof(error)) ||
            slot_bytes[17u] != 1u) {
            fprintf(stderr, "AI_LookForPlayer1 visible source state is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        perception_player.stood_in_top = UINT8_MAX;
        slot_bytes[17u] = UINT8_MAX;
        if (!alien_perception_look_for_player_one(
                &perception_objects, 0u, &game.dynamic_level.runtime, &game.level_clips,
                &perception_player, perception_player.zone_index, 90, 190,
                error, sizeof(error)) ||
            slot_bytes[17u] != 0u) {
            fprintf(stderr, "AI_LookForPlayer1 upper-zone rejection is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* hires.s:DOALLANIMS uses alien 0's authored walk frame special bytes. */
        uint8_t slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime animation_objects = {0};
        ObjectAnimationRuntime animation_runtime;
        GameRandom animation_random;
        GameRandom expected_animation_random;
        uint8_t expected_random_value;

        animation_objects.slot_bytes = slot_bytes;
        animation_objects.slot_count = 2u;
        animation_objects.active_slot_count = 2u;
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 12u, 0u);
        slot_bytes[16u] = 0u;
        slot_bytes[54u] = 0u;
        slot_bytes[55u] = 0u;
        slot_bytes[62u] = UINT8_MAX;
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        object_animation_runtime_init(&animation_runtime);
        game_random_init(&animation_random);
        expected_animation_random = animation_random;
        if (!object_animation_update_single_player(
                &animation_runtime, &animation_objects, &game.game_link_catalog,
                &animation_random, error, sizeof(error)) ||
            animation_runtime.thistime != 5u ||
            animation_runtime.workspace[0u][0u] != 1u ||
            animation_runtime.workspace[0u][1u] != 0u ||
            animation_runtime.workspace[0u][2u] != 0u ||
            animation_runtime.workspace[0u][3u] != 0u ||
            animation_runtime.workspace[0u][4u] != 5u ||
            read_be16(slot_bytes + 40u) != 1u ||
            animation_random.state != expected_animation_random.state) {
            fprintf(stderr, "DOALLANIMS initial source frame is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint16_t tick = 0u; tick < 4u; ++tick) {
            if (!object_animation_update_single_player(
                    &animation_runtime, &animation_objects, &game.game_link_catalog,
                    &animation_random, error, sizeof(error))) {
                fprintf(stderr, "DOALLANIMS cadence update failed: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        expected_random_value = (uint8_t)(game_random_next(&expected_animation_random) % 10u);
        if (!object_animation_update_single_player(
                &animation_runtime, &animation_objects, &game.game_link_catalog,
                &animation_random, error, sizeof(error)) ||
            animation_runtime.thistime != 5u ||
            animation_runtime.workspace[0u][0u] != 2u ||
            animation_runtime.workspace[0u][1u] != 1u ||
            animation_runtime.workspace[0u][2u] != 0u ||
            animation_runtime.workspace[0u][4u] != expected_random_value ||
            read_be16(slot_bytes + 40u) != 2u ||
            animation_random.state != expected_animation_random.state) {
            fprintf(stderr, "DOALLANIMS random special frame is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        animation_runtime.thistime = 0u;
        animation_runtime.workspace[0u][4u] = 1u;
        write_be16(slot_bytes + 40u, 7u);
        if (!object_animation_update_single_player(
                &animation_runtime, &animation_objects, &game.game_link_catalog,
                &animation_random, error, sizeof(error)) ||
            animation_runtime.workspace[0u][0u] != 3u ||
            animation_runtime.workspace[0u][1u] != 7u ||
            animation_runtime.workspace[0u][3u] != UINT8_MAX ||
            animation_runtime.workspace[0u][4u] != 0u ||
            read_be16(slot_bytes + 40u) != 0u ||
            animation_random.state != expected_animation_random.state) {
            fprintf(stderr, "DOALLANIMS end-frame special is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_CheckForDark preserves its same-zone and random gates. */
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime dark_objects = {0};
        GameRandom dark_random;
        GameRandom expected_dark_random;
        int16_t dark_result;
        int16_t threshold;

        dark_objects.slot_bytes = slot_bytes;
        dark_objects.slot_count = 1u;
        dark_objects.active_slot_count = 1u;
        write_be16(slot_bytes + 0u, 7u);
        game_random_init(&dark_random);
        expected_dark_random = dark_random;
        if (!alien_dark_check(&dark_objects, 0u, 7u, 31, &dark_random, &dark_result,
                              error, sizeof(error)) ||
            dark_result != -1 || dark_random.state != expected_dark_random.state) {
            fprintf(stderr, "ai_CheckForDark same-zone source gate is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be16(slot_bytes + 0u, 8u);
        game_random_init(&dark_random);
        expected_dark_random = dark_random;
        threshold = (int16_t)(game_random_next(&expected_dark_random) & 31u);
        if (!alien_dark_check(&dark_objects, 0u, 7u, (int16_t)(threshold + 1),
                              &dark_random, &dark_result, error, sizeof(error)) ||
            dark_result != 0 || dark_random.state != expected_dark_random.state) {
            fprintf(stderr, "ai_CheckForDark dark source gate is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        game_random_init(&dark_random);
        expected_dark_random = dark_random;
        threshold = (int16_t)(game_random_next(&expected_dark_random) & 31u);
        if (!alien_dark_check(&dark_objects, 0u, 7u, threshold,
                              &dark_random, &dark_result, error, sizeof(error)) ||
            dark_result != -1 || dark_random.state != expected_dark_random.state) {
            fprintf(stderr, "ai_CheckForDark bright source gate is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* hires.s/newanims.s own player room brightness and its seven animation heads. */
        static const int16_t first_animation_values[] = {1, 9, 17, 16, 8, 20, -10};
        LightingRuntime lighting;
        PlayerRuntime lighting_player = game.player;
        uint16_t marker_count = 0u;
        int16_t expected_room_brightness = 0;
        int16_t brightness_sum = 0;

        lighting_runtime_init(&lighting);
        lighting_runtime_vblank(&lighting);
        for (uint16_t point_index = 0u;
             point_index < LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT; ++point_index) {
            int16_t source_brightness;
            size_t source_offset = (size_t)game.dynamic_level.runtime.point_brightness_offset +
                ((size_t)lighting_player.zone_index * LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT +
                 point_index) * sizeof(uint16_t);

            if (!level_runtime_get_point_brightness(
                    &game.dynamic_level.runtime, lighting_player.zone_index, point_index,
                    &source_brightness, error, sizeof(error)) ||
                source_brightness != (int16_t)read_be16(
                    game.dynamic_level.runtime.level_bytes + source_offset)) {
                fprintf(stderr, "Game_Begin point-brightness source table is inconsistent: %s\n",
                        error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        for (uint16_t marker_index = 0u;
             marker_index < LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT; ++marker_index) {
            int16_t marker;
            size_t source_offset = (size_t)game.dynamic_level.runtime.zone_border_points_offset +
                ((size_t)lighting_player.zone_index * LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT +
                 marker_index) * sizeof(uint16_t);

            if (!level_runtime_get_zone_border_point(
                    &game.dynamic_level.runtime, lighting_player.zone_index, marker_index,
                    &marker, error, sizeof(error)) ||
                marker != (int16_t)read_be16(
                    game.dynamic_level.runtime.level_bytes + source_offset)) {
                fprintf(stderr, "Game_Begin zone-brightness marker table is inconsistent: %s\n",
                        error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        if (lighting.animation_timer != -1 ||
            !lighting_runtime_refresh_single_player(
                &lighting, &game.dynamic_level.runtime, &lighting_player,
                error, sizeof(error))) {
            fprintf(stderr, "source initial room-brightness refresh is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        lighting_runtime_advance_animation(&lighting);
        if (lighting.animation_timer != 5 ||
            memcmp(lighting.animation_values, first_animation_values,
                   sizeof(first_animation_values)) != 0) {
            fprintf(stderr, "newanims.s:brightanim first source values are inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint16_t marker_index = 0u;
             marker_index < LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT; ++marker_index) {
            int16_t marker;
            int16_t brightness;

            if (!level_runtime_get_zone_border_point(
                    &game.dynamic_level.runtime, lighting_player.zone_index, marker_index,
                    &marker, error, sizeof(error))) {
                fprintf(stderr, "source room-brightness marker update is invalid: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (marker < 0) {
                break;
            }
            brightness = lighting.current_point_brightness[lighting_player.zone_index][marker_index];
            if (brightness < 0) {
                brightness = (int16_t)(UINT16_C(0) - (uint16_t)brightness);
            }
            brightness_sum = source_add16(brightness_sum, brightness);
            ++marker_count;
        }
        if (marker_count == 0u) {
            fprintf(stderr, "hires.s:Plr1_RoomBright_w source fixture has no markers\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        expected_room_brightness = source_add16(
            (int16_t)((int32_t)brightness_sum / (int32_t)marker_count), -300);
        if (lighting_player.room_brightness != expected_room_brightness) {
            fprintf(stderr, "hires.s:Plr1_RoomBright_w source average is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        {
            int16_t point_index;
            int16_t *point_words = &lighting.current_point_brightness[0u][0u];
            int16_t before_lower;
            int16_t before_upper;
            int16_t before_point_lower;
            int16_t before_point_upper;
            uint16_t source_zone_pvs_entries = 0u;

            if (!level_runtime_get_zone_point_index(
                    &game.dynamic_level.runtime, lighting_player.zone_index, 0u, &point_index,
                    error, sizeof(error)) ||
                point_index < 0 ||
                (uint32_t)point_index >= game.dynamic_level.runtime.world_point_count) {
                fprintf(stderr, "Flash source point-list fixture is invalid: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            before_lower = lighting.zone_brightness[lighting_player.zone_index][0u];
            before_upper = lighting.zone_brightness[lighting_player.zone_index][1u];
            before_point_lower = point_words[(size_t)(uint16_t)point_index * 4u];
            before_point_upper = point_words[(size_t)(uint16_t)point_index * 4u + 1u];
            for (uint32_t visible_index = 0u;
                 visible_index <= game.dynamic_level.runtime.zone_count; ++visible_index) {
                LevelPotentialVisibility visible_zone;

                if (!level_runtime_get_zone_potential_visibility(
                        &game.dynamic_level.runtime, lighting_player.zone_index, visible_index,
                        &visible_zone, error, sizeof(error))) {
                    fprintf(stderr, "Flash PVST fixture is invalid: %s\n", error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                if (visible_zone.zone_index < 0) {
                    break;
                }
                if ((uint16_t)visible_zone.zone_index == lighting_player.zone_index) {
                    ++source_zone_pvs_entries;
                }
            }
            if (!lighting_runtime_flash(&lighting, &game.dynamic_level.runtime,
                                        lighting_player.zone_index, -50,
                                        error, sizeof(error)) ||
                point_words[(size_t)(uint16_t)point_index * 4u] !=
                    source_add16(before_point_lower, -20) ||
                point_words[(size_t)(uint16_t)point_index * 4u + 1u] !=
                    source_add16(before_point_upper, -20) ||
                lighting.zone_brightness[lighting_player.zone_index][0u] !=
                    source_add16(before_lower, (int16_t)(-20 *
                        (int16_t)(source_zone_pvs_entries + 1u))) ||
                lighting.zone_brightness[lighting_player.zone_index][1u] !=
                    source_add16(before_upper, (int16_t)(-20 *
                        (int16_t)(source_zone_pvs_entries + 1u)))) {
                fprintf(stderr, "newanims.s:Flash source state is inconsistent: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        {
            /* newanims.s:anim_BrightenPoints' positive and negative source paths. */
            LightingRuntime dynamic_lighting;
            int16_t point_index;
            LevelWorldPoint point;
            int16_t *point_words;
            int16_t expected_darken = 0;
            uint16_t matching_point_entries = 0u;
            uint16_t bright_zone_index = UINT16_MAX;
            uint16_t bright_marker_index = UINT16_MAX;
            LevelWorldPoint bright_point;
            LevelZone bright_zone;
            int16_t before_disabled;

            lighting_runtime_init(&dynamic_lighting);
            if (!level_runtime_get_zone_point_index(
                    &game.dynamic_level.runtime, lighting_player.zone_index, 0u, &point_index,
                    error, sizeof(error)) ||
                point_index < 0 ||
                !level_runtime_get_world_point(
                    &game.dynamic_level.runtime, (uint16_t)point_index, &point,
                    error, sizeof(error))) {
                fprintf(stderr, "anim_BrightenPoints darken fixture is invalid: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            point_words = &dynamic_lighting.current_point_brightness[0u][0u];
            for (uint32_t list_index = 0u;
                 list_index <= game.dynamic_level.runtime.world_point_count; ++list_index) {
                int16_t source_point_index;

                if (!level_runtime_get_zone_point_index(
                        &game.dynamic_level.runtime, lighting_player.zone_index, list_index,
                        &source_point_index, error, sizeof(error))) {
                    fprintf(stderr, "anim_BrightenPoints darken point list is invalid: %s\n", error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                if (source_point_index < 0) {
                    break;
                }
                if (source_point_index == point_index) {
                    expected_darken = source_add16(expected_darken, 50);
                    ++matching_point_entries;
                }
            }
            if (matching_point_entries == 0u ||
                !lighting_runtime_brighten_points(
                    &dynamic_lighting, &game.dynamic_level.runtime, 50, point.x, point.z, 0,
                    lighting_player.zone_index, error, sizeof(error)) ||
                point_words[(size_t)(uint16_t)point_index * 4u] != expected_darken ||
                point_words[(size_t)(uint16_t)point_index * 4u + 1u] != expected_darken ||
                point_words[(size_t)(uint16_t)point_index * 4u + 2u] != 0 ||
                point_words[(size_t)(uint16_t)point_index * 4u + 3u] != 0) {
                fprintf(stderr, "newanims.s:darken_points source state is inconsistent: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            for (uint32_t visible_index = 0u;
                 visible_index <= game.dynamic_level.runtime.zone_count &&
                 bright_zone_index == UINT16_MAX; ++visible_index) {
                LevelPotentialVisibility visible_zone;

                if (!level_runtime_get_zone_potential_visibility(
                        &game.dynamic_level.runtime, lighting_player.zone_index, visible_index,
                        &visible_zone, error, sizeof(error))) {
                    fprintf(stderr, "anim_BrightenPoints PVST fixture is invalid: %s\n", error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                if (visible_zone.zone_index < 0) {
                    break;
                }
                for (uint16_t marker_index = 0u;
                     marker_index < LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT; ++marker_index) {
                    int16_t marker;

                    if (!level_runtime_get_zone_border_point(
                            &game.dynamic_level.runtime, (uint16_t)visible_zone.zone_index,
                            marker_index, &marker, error, sizeof(error))) {
                        fprintf(stderr, "anim_BrightenPoints border fixture is invalid: %s\n", error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    if (marker < 0) {
                        break;
                    }
                    if (!level_runtime_get_world_point(
                            &game.dynamic_level.runtime, (uint16_t)marker, &bright_point,
                            error, sizeof(error))) {
                        fprintf(stderr, "anim_BrightenPoints border point is invalid: %s\n", error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    bright_zone_index = (uint16_t)visible_zone.zone_index;
                    bright_marker_index = marker_index;
                    break;
                }
            }
            if (bright_zone_index == UINT16_MAX ||
                !level_runtime_get_zone(&game.dynamic_level.runtime, bright_zone_index,
                                        &bright_zone, error, sizeof(error))) {
                fprintf(stderr, "anim_BrightenPoints bright fixture is invalid: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            dynamic_lighting.current_point_brightness[bright_zone_index]
                                                    [(size_t)bright_marker_index * 4u] = -400;
            if (!lighting_runtime_brighten_points(
                    &dynamic_lighting, &game.dynamic_level.runtime, -1000,
                    bright_point.x, bright_point.z, bright_zone.floor,
                    lighting_player.zone_index, error, sizeof(error)) ||
                dynamic_lighting.current_point_brightness[bright_zone_index]
                                                        [(size_t)bright_marker_index * 4u] != 300) {
                fprintf(stderr, "newanims.s:bright_points floor state is inconsistent: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            {
                /* newanims.s:Anim_BrightenPointsAngle's front-only torch path. */
                LightingRuntime directional_lighting;
                int16_t angle_sine;
                int16_t angle_cosine;
                int16_t expected_directional_brightness = 1000;
                uint16_t matching_visible_zones = 0u;

                lighting_runtime_init(&directional_lighting);
                if (!game_math_sine(&game.math, 0u, &angle_sine, error, sizeof(error)) ||
                    !game_math_cosine(&game.math, 0u, &angle_cosine, error, sizeof(error)) ||
                    angle_sine != 0 || angle_cosine != 32767) {
                    fprintf(stderr, "Anim_BrightenPointsAngle source angle fixture is invalid: %s\n",
                            error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                for (uint32_t visible_index = 0u;
                     visible_index <= game.dynamic_level.runtime.zone_count; ++visible_index) {
                    LevelPotentialVisibility visible_zone;

                    if (!level_runtime_get_zone_potential_visibility(
                            &game.dynamic_level.runtime, lighting_player.zone_index, visible_index,
                            &visible_zone, error, sizeof(error))) {
                        fprintf(stderr, "Anim_BrightenPointsAngle PVST fixture is invalid: %s\n",
                                error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    if (visible_zone.zone_index < 0) {
                        break;
                    }
                    if ((uint16_t)visible_zone.zone_index == bright_zone_index) {
                        ++matching_visible_zones;
                    }
                }
                directional_lighting.current_point_brightness[bright_zone_index]
                    [(size_t)bright_marker_index * 4u] = 1000;
                for (uint16_t matching_index = 0u;
                     matching_index < matching_visible_zones; ++matching_index) {
                    /* angle 0, dz 1: ((30*65536 - 32767) << 2) high word is 118. */
                    expected_directional_brightness = source_add16(
                        expected_directional_brightness, -197);
                    if (expected_directional_brightness < 300) {
                        expected_directional_brightness = 300;
                    }
                }
                if (matching_visible_zones == 0u ||
                    !lighting_runtime_brighten_points_angle(
                        &directional_lighting, &game.dynamic_level.runtime, &game.math, -200,
                        bright_point.x, source_add16(bright_point.z, -1), bright_zone.floor,
                        lighting_player.zone_index, 0u, error, sizeof(error)) ||
                    directional_lighting.current_point_brightness[bright_zone_index]
                        [(size_t)bright_marker_index * 4u] != expected_directional_brightness) {
                    fprintf(stderr,
                            "newanims.s:Anim_BrightenPointsAngle front-light state is inconsistent: %s\n",
                            error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                directional_lighting.current_point_brightness[bright_zone_index]
                    [(size_t)bright_marker_index * 4u] = 1000;
                if (!lighting_runtime_brighten_points_angle(
                        &directional_lighting, &game.dynamic_level.runtime, &game.math, -200,
                        bright_point.x, source_add16(bright_point.z, 1), bright_zone.floor,
                        lighting_player.zone_index, 0u, error, sizeof(error)) ||
                    directional_lighting.current_point_brightness[bright_zone_index]
                        [(size_t)bright_marker_index * 4u] != 1000) {
                    fprintf(stderr,
                            "newanims.s:Anim_BrightenPointsAngle behind-point gate is inconsistent: %s\n",
                            error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                directional_lighting.lighting_enabled = 0u;
                if (!lighting_runtime_brighten_points_angle(
                        &directional_lighting, &game.dynamic_level.runtime, &game.math, -200,
                        bright_point.x, source_add16(bright_point.z, -1), bright_zone.floor,
                        lighting_player.zone_index, 0u, error, sizeof(error)) ||
                    directional_lighting.current_point_brightness[bright_zone_index]
                        [(size_t)bright_marker_index * 4u] != 1000) {
                    fprintf(stderr,
                            "Anim_BrightenPointsAngle lighting-enable gate is inconsistent: %s\n", error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                {
                    /* modules/ai.s:ai_DoTorch's ALIENBRIGHT and slot-register handoff. */
                    LightingRuntime torch_lighting;
                    ObjectRuntime torch_objects = {0};
                    AlienSetup torch_setup = {0};
                    uint8_t torch_slot[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};

                    if (bright_zone.floor % 128 != 0) {
                        fprintf(stderr, "ai_DoTorch vertical source fixture is not word-scaled\n");
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    torch_objects.slot_bytes = torch_slot;
                    torch_objects.slot_count = 1u;
                    torch_objects.active_slot_count = 1u;
                    write_be16(torch_slot + 4u, (uint16_t)(bright_zone.floor / 128));
                    write_be16(torch_slot + 12u, bright_zone_index);
                    write_be16(torch_slot + 30u, 0u);
                    torch_setup.brightness = -200;
                    lighting_runtime_init(&torch_lighting);
                    torch_lighting.current_point_brightness[bright_zone_index]
                        [(size_t)bright_marker_index * 4u] = 1000;
                    if (!alien_torch_apply(
                            &torch_lighting, &game.dynamic_level.runtime, &game.math,
                            &torch_objects, 0u, &torch_setup,
                            bright_point.x, source_add16(bright_point.z, -1),
                            error, sizeof(error)) ||
                        torch_lighting.current_point_brightness[bright_zone_index]
                            [(size_t)bright_marker_index * 4u] !=
                                expected_directional_brightness) {
                        fprintf(stderr, "modules/ai.s:ai_DoTorch source state is inconsistent: %s\n",
                                error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    torch_setup.brightness = 0;
                    torch_lighting.current_point_brightness[bright_zone_index]
                        [(size_t)bright_marker_index * 4u] = 1000;
                    if (!alien_torch_apply(
                            &torch_lighting, &game.dynamic_level.runtime, &game.math,
                            &torch_objects, 0u, &torch_setup,
                            bright_point.x, source_add16(bright_point.z, -1),
                            error, sizeof(error)) ||
                        torch_lighting.current_point_brightness[bright_zone_index]
                            [(size_t)bright_marker_index * 4u] != 1000) {
                        fprintf(stderr, "ai_DoTorch ALIENBRIGHT gate is inconsistent: %s\n", error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                }
            }
            before_disabled = dynamic_lighting.current_point_brightness[bright_zone_index]
                                                                        [(size_t)bright_marker_index * 4u];
            dynamic_lighting.lighting_enabled = 0u;
            if (!lighting_runtime_brighten_points(
                    &dynamic_lighting, &game.dynamic_level.runtime, -1000,
                    bright_point.x, bright_point.z, bright_zone.floor,
                    lighting_player.zone_index, error, sizeof(error)) ||
                dynamic_lighting.current_point_brightness[bright_zone_index]
                                                        [(size_t)bright_marker_index * 4u] != before_disabled) {
                fprintf(stderr, "anim_BrightenPoints lighting-enable gate is inconsistent: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
    }
    {
        /* modules/ai.s:ai_TakeDamage's two nonfatal reactions and death route. */
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime damage_objects = {0};
        AlienRuntime damage_runtime;
        ObjectAnimationRuntime damage_animation_runtime;
        PlayerRuntime damage_player = {0};
        GameRandom damage_random;
        GameRandom expected_random;
        ObjectHeading expected_heading = {0};
        AlienDamageState damage_state;
        uint16_t nonzero_random_state = 0u;
        uint16_t zero_random_state = 0u;
        int found_nonzero_random_state = 0;
        int found_zero_random_state = 0;

        for (uint32_t candidate = 0u; candidate <= UINT16_MAX; ++candidate) {
            GameRandom nonzero_candidate = {(uint16_t)candidate};
            GameRandom zero_candidate = {(uint16_t)candidate};

            if ((game_random_next(&nonzero_candidate) & 3u) != 0u &&
                !found_nonzero_random_state) {
                nonzero_random_state = (uint16_t)candidate;
                found_nonzero_random_state = 1;
            }
            if ((game_random_next(&zero_candidate) & 3u) == 0u && !found_zero_random_state) {
                zero_random_state = (uint16_t)candidate;
                found_zero_random_state = 1;
            }
            if (found_nonzero_random_state && found_zero_random_state) {
                break;
            }
        }
        if (!found_nonzero_random_state || !found_zero_random_state) {
            fprintf(stderr, "ai_TakeDamage source random fixtures are unavailable\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        damage_objects.slot_bytes = slot_bytes;
        damage_objects.slot_count = 1u;
        damage_objects.active_slot_count = 1u;
        damage_objects.point_bytes = point_bytes;
        damage_objects.point_count = 1u;
        write_be16(slot_bytes + 0u, 0u);
        write_be32(point_bytes + 0u, 0u);
        write_be32(point_bytes + 4u, 0u);
        damage_player.x = 100;
        damage_player.z = 0;
        expected_heading.old_x = 0;
        expected_heading.old_z = 0;
        expected_heading.new_x = 100;
        expected_heading.new_z = 0;
        expected_heading.range = -20;
        expected_heading.speed = 100;
        if (!object_heading_towards_angle(&game.math, &expected_heading, error, sizeof(error))) {
            fprintf(stderr, "ai_TakeDamage source heading fixture is invalid: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        alien_runtime_init(&damage_runtime);
        object_animation_runtime_init(&damage_animation_runtime);
        slot_bytes[18u] = 10u;
        slot_bytes[19u] = 5u;
        slot_bytes[20u] = 7u;
        slot_bytes[55u] = 6u;
        write_be16(slot_bytes + 30u, 0x1234u);
        write_be16(slot_bytes + 34u, 33u);
        write_be16(slot_bytes + 40u, 44u);
        damage_random.state = nonzero_random_state;
        expected_random = damage_random;
        (void)game_random_next(&expected_random);
        if (!alien_damage_take(
                &damage_objects, 0u, &damage_runtime, &damage_animation_runtime,
                &game.math, &damage_random, &damage_player, &damage_state,
                error, sizeof(error)) ||
            damage_state.route != ALIEN_DAMAGE_ROUTE_NONFATAL ||
            damage_state.got_out != UINT8_MAX || damage_runtime.damage[0u] != 5 ||
            slot_bytes[19u] != 0u || slot_bytes[20u] != 1u || slot_bytes[55u] != 1u ||
            read_be16(slot_bytes + 30u) != expected_heading.angle ||
            read_be16(slot_bytes + 34u) != 0u || read_be16(slot_bytes + 40u) != 0u ||
            damage_animation_runtime.workspace[0u][1u] != UINT8_MAX ||
            damage_random.state != expected_random.state) {
            fprintf(stderr, "ai_TakeDamage pursuit reaction is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        alien_runtime_init(&damage_runtime);
        object_animation_runtime_init(&damage_animation_runtime);
        slot_bytes[18u] = 10u;
        slot_bytes[19u] = 4u;
        slot_bytes[20u] = 7u;
        slot_bytes[55u] = 6u;
        write_be16(slot_bytes + 30u, 0x1234u);
        write_be16(slot_bytes + 34u, 33u);
        write_be16(slot_bytes + 40u, 44u);
        damage_random.state = zero_random_state;
        if (!alien_damage_take(
                &damage_objects, 0u, &damage_runtime, &damage_animation_runtime,
                &game.math, &damage_random, &damage_player, &damage_state,
                error, sizeof(error)) ||
            damage_state.route != ALIEN_DAMAGE_ROUTE_NONFATAL ||
            damage_state.got_out != UINT8_MAX || damage_runtime.damage[0u] != 4 ||
            slot_bytes[19u] != 0u || slot_bytes[20u] != 4u || slot_bytes[55u] != 2u ||
            read_be16(slot_bytes + 30u) != 0x1234u || read_be16(slot_bytes + 34u) != 0u ||
            read_be16(slot_bytes + 40u) != 0u ||
            damage_animation_runtime.workspace[0u][1u] != UINT8_MAX) {
            fprintf(stderr, "ai_TakeDamage hit-animation reaction is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        alien_runtime_init(&damage_runtime);
        object_animation_runtime_init(&damage_animation_runtime);
        slot_bytes[18u] = 1u;
        slot_bytes[19u] = 4u;
        slot_bytes[20u] = 7u;
        slot_bytes[55u] = 6u;
        write_be16(slot_bytes + 34u, 33u);
        write_be16(slot_bytes + 40u, 44u);
        if (!alien_damage_take(
                &damage_objects, 0u, &damage_runtime, &damage_animation_runtime,
                &game.math, &damage_random, &damage_player, &damage_state,
                error, sizeof(error)) ||
            damage_state.route != ALIEN_DAMAGE_ROUTE_JUST_DIED || damage_state.got_out != 0u ||
            damage_runtime.damage[0u] != 4 || slot_bytes[19u] != 0u ||
            slot_bytes[18u] != 1u || slot_bytes[20u] != 7u || slot_bytes[55u] != 6u ||
            read_be16(slot_bytes + 34u) != 33u || read_be16(slot_bytes + 40u) != 44u) {
            fprintf(stderr, "ai_TakeDamage ai_JustDied handoff is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_CalcSqrt's zero and three-refinement source paths. */
        int16_t square_root;

        if (!alien_math_calc_sqrt(0, &square_root, error, sizeof(error)) || square_root != 0 ||
            !alien_math_calc_sqrt(1, &square_root, error, sizeof(error)) || square_root != 1 ||
            !alien_math_calc_sqrt(10000, &square_root, error, sizeof(error)) ||
            square_root != 101 ||
            !alien_math_calc_sqrt(40000, &square_root, error, sizeof(error)) ||
            square_root != 201) {
            fprintf(stderr, "ai_CalcSqrt source approximation is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* AI_MainRoutine's signed CurrentMode comparisons select six source paths. */
        static const uint8_t source_modes[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, UINT8_MAX};
        static const AlienMainRoute expected_routes[] = {
            ALIEN_MAIN_ROUTE_DEFAULT,
            ALIEN_MAIN_ROUTE_RESPONSE,
            ALIEN_MAIN_ROUTE_FOLLOWUP,
            ALIEN_MAIN_ROUTE_RETREAT,
            ALIEN_MAIN_ROUTE_TAKE_DAMAGE,
            ALIEN_MAIN_ROUTE_DIE,
            ALIEN_MAIN_ROUTE_TAKE_DAMAGE,
            ALIEN_MAIN_ROUTE_DEFAULT
        };
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime main_objects = {0};
        AlienSetup selection_setup = {0};

        main_objects.slot_bytes = slot_bytes;
        main_objects.slot_count = 1u;
        main_objects.active_slot_count = 1u;
        for (uint32_t mode_index = 0u; mode_index < sizeof(source_modes); ++mode_index) {
            AlienMainRoute route;

            slot_bytes[20u] = source_modes[mode_index];
            write_be16(slot_bytes + 2u, 0x4321u);
            if (!alien_main_route(&main_objects, 0u, &route, error, sizeof(error)) ||
                route != expected_routes[mode_index] ||
                read_be16(slot_bytes + 2u) != UINT16_C(0xffec)) {
                fprintf(stderr, "AI_MainRoutine mode dispatch is inconsistent: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        for (int16_t mode = -1; mode <= 2; ++mode) {
            AlienMainBehavior behavior;
            AlienMainBehavior expected_behavior = mode < 1 ?
                ALIEN_MAIN_BEHAVIOR_PROWL_RANDOM :
                (mode == 1 ? ALIEN_MAIN_BEHAVIOR_PROWL_RANDOM_FLYING :
                             ALIEN_MAIN_BEHAVIOR_NONE);

            selection_setup.default_mode = mode;
            if (!alien_main_select_behavior(ALIEN_MAIN_ROUTE_DEFAULT, &selection_setup,
                                            &behavior, error, sizeof(error)) ||
                behavior != expected_behavior) {
                fprintf(stderr, "ai_DoDefault mode selection is inconsistent: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        {
            static const AlienMainBehavior expected_response[] = {
                ALIEN_MAIN_BEHAVIOR_CHARGE,
                ALIEN_MAIN_BEHAVIOR_CHARGE,
                ALIEN_MAIN_BEHAVIOR_CHARGE_TO_SIDE,
                ALIEN_MAIN_BEHAVIOR_ATTACK_WITH_GUN,
                ALIEN_MAIN_BEHAVIOR_CHARGE_FLYING,
                ALIEN_MAIN_BEHAVIOR_CHARGE_TO_SIDE_FLYING,
                ALIEN_MAIN_BEHAVIOR_ATTACK_WITH_GUN_FLYING,
                ALIEN_MAIN_BEHAVIOR_NONE,
                ALIEN_MAIN_BEHAVIOR_NONE
            };

            for (int16_t mode = -1; mode <= 7; ++mode) {
                AlienMainBehavior behavior;

                selection_setup.response_mode = mode;
                if (!alien_main_select_behavior(ALIEN_MAIN_ROUTE_RESPONSE, &selection_setup,
                                                &behavior, error, sizeof(error)) ||
                    behavior != expected_response[(uint16_t)(mode + 1)]) {
                    fprintf(stderr, "ai_DoResponse mode selection is inconsistent: %s\n", error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
        }
        {
            static const AlienMainBehavior expected_followup[] = {
                ALIEN_MAIN_BEHAVIOR_PAUSE_BRIEFLY,
                ALIEN_MAIN_BEHAVIOR_PAUSE_BRIEFLY,
                ALIEN_MAIN_BEHAVIOR_APPROACH,
                ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE,
                ALIEN_MAIN_BEHAVIOR_APPROACH_FLYING,
                ALIEN_MAIN_BEHAVIOR_APPROACH_TO_SIDE_FLYING,
                ALIEN_MAIN_BEHAVIOR_NONE,
                ALIEN_MAIN_BEHAVIOR_NONE,
                ALIEN_MAIN_BEHAVIOR_NONE
            };

            for (int16_t mode = -1; mode <= 7; ++mode) {
                AlienMainBehavior behavior;

                selection_setup.followup_mode = mode;
                if (!alien_main_select_behavior(ALIEN_MAIN_ROUTE_FOLLOWUP, &selection_setup,
                                                &behavior, error, sizeof(error)) ||
                    behavior != expected_followup[(uint16_t)(mode + 1)]) {
                    fprintf(stderr, "ai_DoFollowup mode selection is inconsistent: %s\n", error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
        }
        {
            AlienMainBehavior behavior;

            if (!alien_main_select_behavior(ALIEN_MAIN_ROUTE_RETREAT, &selection_setup,
                                            &behavior, error, sizeof(error)) ||
                behavior != ALIEN_MAIN_BEHAVIOR_NONE ||
                !alien_main_select_behavior(ALIEN_MAIN_ROUTE_DIE, &selection_setup,
                                            &behavior, error, sizeof(error)) ||
                behavior != ALIEN_MAIN_BEHAVIOR_DIE ||
                !alien_main_select_behavior(ALIEN_MAIN_ROUTE_TAKE_DAMAGE, &selection_setup,
                                            &behavior, error, sizeof(error)) ||
                behavior != ALIEN_MAIN_BEHAVIOR_TAKE_DAMAGE) {
                fprintf(stderr, "AI_MainRoutine fixed mode selection is inconsistent: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
    }
    {
        /* modules/ai.s:ai_DoWalkAnim consumes one authored alien/auxiliary frame. */
        uint8_t slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime animation_objects = {0};
        ObjectAnimationRuntime animation_runtime;
        AlienSetup animation_setup = {0};
        AlienAnimationState animation_state;
        GameAlienDefinition alien_definition;
        GameAlienAnimationFrame alien_frame;
        GameObjectDefinition auxiliary_definition;
        GameObjectAnimationFrame auxiliary_frame;
        uint16_t selected_alien = UINT16_MAX;
        uint16_t selected_option = UINT16_MAX;
        uint16_t selected_frame = UINT16_MAX;

        for (uint16_t alien_index = 0u;
             alien_index < GAME_LINK_ALIEN_COUNT && selected_alien == UINT16_MAX;
             ++alien_index) {
            if (!game_link_get_alien_definition(&game.game_link_catalog, alien_index,
                                                &alien_definition, error, sizeof(error)) ||
                (int16_t)alien_definition.auxiliary_type < 0 ||
                (int16_t)alien_definition.auxiliary_type >= GAME_LINK_OBJECT_COUNT) {
                continue;
            }
            for (uint16_t option_index = 1u;
                 option_index < GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT &&
                 selected_alien == UINT16_MAX;
                 ++option_index) {
                for (uint16_t frame_index = 0u;
                     frame_index < GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT;
                     ++frame_index) {
                    if (!game_link_get_alien_animation_frame(
                            &game.game_link_catalog, alien_index, option_index, frame_index,
                            &alien_frame, error, sizeof(error))) {
                        fprintf(stderr, "could not read ai_DoWalkAnim source frame: %s\n", error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    if ((int8_t)alien_frame.bytes[8u] < 0 || alien_frame.bytes[8u] >=
                        GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT ||
                        !game_link_get_object_definition(
                            &game.game_link_catalog, (uint16_t)alien_definition.auxiliary_type,
                            &auxiliary_definition, error, sizeof(error)) ||
                        !game_link_get_object_animation_frame(
                            &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                            (uint16_t)alien_definition.auxiliary_type, alien_frame.bytes[8u],
                            &auxiliary_frame, error, sizeof(error))) {
                        continue;
                    }
                    selected_alien = alien_index;
                    selected_option = option_index;
                    selected_frame = frame_index;
                    break;
                }
            }
        }
        if (selected_alien == UINT16_MAX ||
            !game_link_get_alien_definition(&game.game_link_catalog, selected_alien,
                                            &alien_definition, error, sizeof(error)) ||
            !game_link_get_alien_animation_frame(
                &game.game_link_catalog, selected_alien, selected_option, selected_frame,
                &alien_frame, error, sizeof(error)) ||
            !game_link_get_object_definition(
                &game.game_link_catalog, (uint16_t)alien_definition.auxiliary_type,
                &auxiliary_definition, error, sizeof(error)) ||
            !game_link_get_object_animation_frame(
                &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                (uint16_t)alien_definition.auxiliary_type, alien_frame.bytes[8u],
                &auxiliary_frame, error, sizeof(error))) {
            fprintf(stderr, "could not establish ai_DoWalkAnim auxiliary source fixture: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        animation_objects.slot_bytes = slot_bytes;
        animation_objects.slot_count = 2u;
        animation_objects.active_slot_count = 2u;
        write_be16(slot_bytes + 0u, 3u);
        write_be16(slot_bytes + 12u, UINT16_MAX);
        write_be16(slot_bytes + 64u + 0u, 4u);
        write_be16(slot_bytes + 64u + 4u, 0x1234u);
        write_be16(slot_bytes + 64u + 12u, 7u);
        write_be16(slot_bytes + 64u + 26u, 7u);
        write_be16(slot_bytes + 64u + 30u, 2048u);
        write_be16(slot_bytes + 64u + 40u, selected_frame);
        slot_bytes[64u + 63u] = UINT8_MAX;
        object_animation_runtime_init(&animation_runtime);
        animation_runtime.workspace[1u][0u] = 3u;
        animation_runtime.workspace[1u][1u] = (uint8_t)selected_frame;
        animation_runtime.workspace[1u][2u] = (uint8_t)selected_option;
        animation_runtime.workspace[1u][3u] = UINT8_MAX;
        animation_setup.alien_type = selected_alien;
        animation_setup.auxiliary_object_type = (int16_t)alien_definition.auxiliary_type;
        animation_setup.vector_object_flag = (uint8_t)alien_definition.graphics_type;
        if (!alien_animation_update_walk_or_attack(
                &animation_objects, 1u, &animation_runtime, &game.game_link_catalog,
                &game.math, &animation_setup, 0u, &animation_state, error, sizeof(error)) ||
            animation_state.action != 3u || animation_state.finished != UINT8_MAX ||
            animation_runtime.workspace[1u][0u] != 0u ||
            animation_runtime.workspace[1u][1u] != UINT8_MAX ||
            animation_runtime.workspace[1u][3u] != 0u ||
            slot_bytes[64u + 9u] != alien_frame.bytes[0u] ||
            slot_bytes[64u + 11u] !=
                (uint8_t)((int16_t)((int8_t)alien_frame.bytes[1u] > 0 ?
                    (int8_t)alien_frame.bytes[1u] : -(int16_t)(int8_t)alien_frame.bytes[1u]) -
                          1) ||
            read_be16(slot_bytes + 12u) != 7u ||
            read_be16(slot_bytes + 26u) != 7u ||
            read_be16(slot_bytes + 64u + 12u) != 7u ||
            read_be16(slot_bytes + 64u + 26u) != 7u ||
            read_be16(slot_bytes + 64u + 4u) != 0x1234u ||
            slot_bytes[64u + 63u] != UINT8_MAX ||
            read_be16(slot_bytes + 44u) !=
                (uint16_t)((int16_t)(int8_t)alien_frame.bytes[9u] * 2) ||
            read_be16(slot_bytes + 46u) !=
                (uint16_t)((int16_t)(int8_t)alien_frame.bytes[10u] * 2)) {
            fprintf(stderr, "ai_DoWalkAnim source state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if ((int8_t)alien_frame.bytes[1u] <= 0) {
            if (slot_bytes[64u + 10u] != 128u) {
                fprintf(stderr, "ai_DoWalkAnim flip descriptor is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        } else if (slot_bytes[64u + 10u] !=
                   ((int8_t)animation_setup.vector_object_flag > 1 ?
                    animation_setup.vector_object_flag : 0u)) {
            fprintf(stderr, "ai_DoWalkAnim source effect descriptor is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        if (animation_setup.vector_object_flag == 1u) {
            if (read_be16(slot_bytes + 64u + 6u) != UINT16_MAX ||
                animation_state.facing != read_be16(alien_frame.bytes + 2u)) {
                fprintf(stderr, "ai_DoWalkAnim vector source descriptor is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        } else if (read_be16(slot_bytes + 64u + 6u) !=
                   read_be16(alien_frame.bytes + 2u)) {
            fprintf(stderr, "ai_DoWalkAnim source size descriptor is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        if ((int16_t)auxiliary_definition.graphics_type < 1) {
            if (slot_bytes[9u] != auxiliary_frame.byte_0 ||
                slot_bytes[11u] != auxiliary_frame.byte_1 ||
                read_be16(slot_bytes + 6u) != auxiliary_frame.word_2) {
                fprintf(stderr, "ai_DoWalkAnim bitmap auxiliary is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        } else if ((int16_t)auxiliary_definition.graphics_type == 1) {
            if (slot_bytes[9u] != auxiliary_frame.byte_0 ||
                slot_bytes[11u] != auxiliary_frame.byte_1 ||
                read_be16(slot_bytes + 6u) != UINT16_MAX) {
                fprintf(stderr, "ai_DoWalkAnim vector auxiliary is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        } else if (read_be16(slot_bytes + 8u) !=
                   (uint16_t)(int16_t)-(int16_t)(int8_t)auxiliary_frame.byte_0 ||
                   slot_bytes[11u] != auxiliary_frame.byte_1 ||
                   read_be16(slot_bytes + 6u) != auxiliary_frame.word_2) {
            fprintf(stderr, "ai_DoWalkAnim glare auxiliary is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_DoDie frees only after the authored end frame. */
        uint8_t slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime death_objects = {0};
        ObjectAnimationRuntime death_animation_runtime;
        AlienSetup death_setup = {0};
        AlienDeathState death_state;
        GameAlienDefinition death_definition;
        GameAlienAnimationFrame death_frame;
        GameObjectAnimationFrame death_auxiliary_frame;
        LevelZone death_zone;
        uint16_t death_alien = UINT16_MAX;
        uint16_t death_option = UINT16_MAX;
        uint16_t death_frame_index = UINT16_MAX;

        for (uint16_t alien_index = 0u; alien_index < GAME_LINK_ALIEN_COUNT; ++alien_index) {
            if (!game_link_get_alien_definition(&game.game_link_catalog, alien_index,
                                                &death_definition, error, sizeof(error))) {
                fprintf(stderr, "could not read ai_DoDie source alien: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if ((int16_t)death_definition.auxiliary_type < 0 ||
                (int16_t)death_definition.auxiliary_type >= GAME_LINK_OBJECT_COUNT) {
                continue;
            }
            for (uint16_t option_index = 1u;
                 option_index < GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT &&
                 death_alien == UINT16_MAX;
                 ++option_index) {
                for (uint16_t frame_index = 0u;
                     frame_index < GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT;
                     ++frame_index) {
                    if (!game_link_get_alien_animation_frame(
                            &game.game_link_catalog, alien_index, option_index, frame_index,
                            &death_frame, error, sizeof(error))) {
                        fprintf(stderr, "could not read ai_DoDie source frame: %s\n", error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    if ((int8_t)death_frame.bytes[8u] < 0 || death_frame.bytes[8u] >=
                        GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT ||
                        !game_link_get_object_animation_frame(
                            &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                            (uint16_t)death_definition.auxiliary_type, death_frame.bytes[8u],
                            &death_auxiliary_frame, error, sizeof(error))) {
                        continue;
                    }
                    death_alien = alien_index;
                    death_option = option_index;
                    death_frame_index = frame_index;
                    break;
                }
            }
        }
        if (death_alien == UINT16_MAX ||
            !game_link_get_alien_definition(&game.game_link_catalog, death_alien,
                                            &death_definition, error, sizeof(error)) ||
            !level_runtime_get_zone(&game.dynamic_level.runtime, game.player.zone_index,
                                    &death_zone, error, sizeof(error))) {
            fprintf(stderr, "could not establish ai_DoDie source fixture: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        death_objects.slot_bytes = slot_bytes;
        death_objects.slot_count = 2u;
        death_objects.active_slot_count = 2u;
        write_be16(slot_bytes + 0u, 1u);
        write_be16(slot_bytes + 12u, 4u);
        write_be16(slot_bytes + 64u + 0u, 2u);
        write_be16(slot_bytes + 64u + 4u, 100u);
        write_be16(slot_bytes + 64u + 12u, game.player.zone_index);
        write_be16(slot_bytes + 64u + 26u, game.player.zone_index);
        write_be16(slot_bytes + 64u + 40u, death_frame_index);
        slot_bytes[64u + 16u] = 6u;
        slot_bytes[64u + 18u] = 11u;
        slot_bytes[64u + 54u] = (uint8_t)death_alien;
        slot_bytes[64u + 62u] = UINT8_MAX;
        object_animation_runtime_init(&death_animation_runtime);
        death_animation_runtime.workspace[1u][1u] = UINT8_MAX;
        death_animation_runtime.workspace[1u][2u] = (uint8_t)death_option;
        death_animation_runtime.workspace[1u][3u] = UINT8_MAX;
        death_setup.alien_type = death_alien;
        death_setup.zone_id = game.player.zone_index;
        death_setup.thing_height = (int32_t)(int16_t)death_definition.height * 128;
        death_setup.auxiliary_object_type = (int16_t)death_definition.auxiliary_type;
        death_setup.vector_object_flag = (uint8_t)death_definition.graphics_type;
        if (!alien_death_update(
                &death_objects, 1u, &death_animation_runtime, &game.game_link_catalog,
                &game.math, &game.dynamic_level.runtime, &death_setup, game.player.yaw,
                &death_state, error, sizeof(error)) ||
            death_state.got_out != UINT8_MAX ||
            death_state.animation.finished != UINT8_MAX ||
            (int16_t)read_be16(slot_bytes + 64u + 12u) != -1 ||
            (int16_t)read_be16(slot_bytes + 64u + 26u) != -1 ||
            slot_bytes[64u + 16u] != 0u || slot_bytes[64u + 62u] != 0u ||
            (int16_t)read_be16(slot_bytes + 12u) != -1 ||
            (int16_t)read_be16(slot_bytes + 26u) != -1) {
            fprintf(stderr, "ai_DoDie completion state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be16(slot_bytes + 64u + 12u, game.player.zone_index);
        write_be16(slot_bytes + 64u + 26u, game.player.zone_index);
        slot_bytes[64u + 18u] = 11u;
        slot_bytes[64u + 62u] = UINT8_MAX;
        death_animation_runtime.workspace[1u][1u] = UINT8_MAX;
        death_animation_runtime.workspace[1u][2u] = (uint8_t)death_option;
        death_animation_runtime.workspace[1u][3u] = 0u;
        if (!alien_death_update(
                &death_objects, 1u, &death_animation_runtime, &game.game_link_catalog,
                &game.math, &game.dynamic_level.runtime, &death_setup, game.player.yaw,
                &death_state, error, sizeof(error)) ||
            death_state.got_out != 0u || death_state.animation.finished != 0u ||
            slot_bytes[64u + 18u] != 0u ||
            read_be16(slot_bytes + 64u + 12u) != death_zone.id ||
            read_be16(slot_bytes + 64u + 26u) != death_zone.id ||
            read_be16(slot_bytes + 12u) != death_zone.id ||
            read_be16(slot_bytes + 26u) != death_zone.id) {
            fprintf(stderr, "ai_DoDie active-frame state is inconsistent: %s\n", error);
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
        /* newanims.s:Anim_ExplodeIntoBits' bounded alien-shot allocation. */
        uint8_t slot_bytes[21u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[24u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime explosion_objects = {0};
        ObjectExplosionRuntime explosion_runtime;
        GameRandom explosion_random;
        GameRandom expected_random;
        uint32_t spawned_count = 0u;

        explosion_objects.slot_bytes = slot_bytes;
        explosion_objects.slot_count = 21u;
        explosion_objects.active_slot_count = 21u;
        explosion_objects.alien_shot_first_slot = 1u;
        explosion_objects.point_bytes = point_bytes;
        explosion_objects.point_count = 24u;
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 4u, 10u);
        write_be16(slot_bytes + 12u, 3u);
        write_be16(slot_bytes + 42u, 100u);
        write_be16(slot_bytes + 44u, 200u);
        slot_bytes[63u] = UINT8_MAX;
        for (uint32_t fragment_index = 0u;
             fragment_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT; ++fragment_index) {
            uint8_t *fragment_slot = slot_bytes +
                (size_t)(fragment_index + 1u) * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
            uint8_t *fragment_point = point_bytes +
                (size_t)(fragment_index + 1u) * OBJECT_RUNTIME_POINT_BYTE_COUNT;

            write_be16(fragment_slot + 0u, (uint16_t)(fragment_index + 1u));
            write_be16(fragment_slot + 12u, UINT16_MAX);
            fragment_slot[16u] = 2u;
            fragment_slot[29u] = 0xa5u;
            write_be32(fragment_point + 0u, UINT32_C(0xaaaa1111));
            write_be32(fragment_point + 4u, UINT32_C(0xbbbb2222));
        }
        object_explosion_runtime_init(&explosion_runtime);
        game_random_init(&explosion_random);
        expected_random = explosion_random;
        if (!object_explosion_into_bits(
                &explosion_runtime, &explosion_objects, 0u, &game.math, &explosion_random,
                300, -400, 5u, 10, 37, &spawned_count, error, sizeof(error)) ||
            spawned_count != 8u || explosion_runtime.radius != 37) {
            fprintf(stderr, "Anim_ExplodeIntoBits source allocation setup is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint32_t fragment_index = 0u; fragment_index < 8u; ++fragment_index) {
            uint8_t *fragment_slot = slot_bytes +
                (size_t)(fragment_index + 1u) * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
            uint8_t *fragment_point = point_bytes +
                (size_t)(fragment_index + 1u) * OBJECT_RUNTIME_POINT_BYTE_COUNT;
            uint16_t angle_address = game_math_wrap_angle_address(game_random_next(&expected_random));
            int16_t sine;
            int16_t cosine;
            uint16_t shift_count = (uint16_t)((game_random_next(&expected_random) & 3u) + 1u);
            int16_t expected_velocity_y = (int16_t)(UINT16_C(0) -
                (uint16_t)((game_random_next(&expected_random) & 1023u) + 256u));
            int16_t expected_velocity_x;
            int16_t expected_velocity_z;

            if (!game_math_sine(&game.math, angle_address, &sine, error, sizeof(error)) ||
                !game_math_cosine(&game.math, angle_address, &cosine, error, sizeof(error))) {
                fprintf(stderr, "Anim_ExplodeIntoBits source sine fixture is unavailable: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            expected_velocity_x = source_add16(
                (int16_t)(uint16_t)((uint32_t)(int32_t)sine << shift_count >> 16u), 50);
            expected_velocity_z = source_add16(
                (int16_t)(uint16_t)((uint32_t)(int32_t)cosine << shift_count >> 16u), 100);
            if (fragment_slot[16u] != 2u || fragment_slot[28u] != 0u ||
                fragment_slot[29u] != 0xa5u || read_be16(fragment_slot + 12u) != 3u ||
                read_be16(fragment_slot + 18u) != (uint16_t)expected_velocity_x ||
                read_be16(fragment_slot + 22u) != (uint16_t)expected_velocity_z ||
                read_be16(fragment_slot + 42u) != (uint16_t)expected_velocity_y ||
                read_be32(fragment_slot + 36u) != 0u || read_be16(fragment_slot + 4u) != 10u ||
                read_be32(fragment_slot + 44u) != 2048u || fragment_slot[31u] != 5u ||
                read_be16(fragment_slot + 58u) != 0u || read_be16(fragment_slot + 60u) != 0u ||
                fragment_slot[30u] != 0u || fragment_slot[62u] != UINT8_MAX ||
                fragment_slot[63u] != UINT8_MAX || read_be32(fragment_point + 0u) !=
                    UINT32_C(0x012c1111) || read_be32(fragment_point + 4u) !=
                    UINT32_C(0xfe702222)) {
                fprintf(stderr, "Anim_ExplodeIntoBits fragment state is inconsistent: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        if (explosion_random.state != expected_random.state || point_bytes[9u * 8u] != 2u ||
            point_bytes[10u * 8u] != 2u ||
            (int16_t)read_be16(slot_bytes + 9u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) != -1) {
            fprintf(stderr, "Anim_ExplodeIntoBits source pool/count state is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_JustDied's three-child spawned-alien branch. */
        uint8_t slot_bytes[42u * OBJECT_RUNTIME_SLOT_BYTE_COUNT];
        uint8_t point_bytes[42u * OBJECT_RUNTIME_POINT_BYTE_COUNT];
        ObjectRuntime spawn_objects = {0};
        GameAlienDefinition child_definition = {0};
        uint32_t spawned_count = 0u;
        uint8_t *parent_slot;

        memset(slot_bytes, 0xa5, sizeof(slot_bytes));
        memset(point_bytes, 0x5a, sizeof(point_bytes));
        spawn_objects.slot_bytes = slot_bytes;
        spawn_objects.slot_count = 42u;
        spawn_objects.active_slot_count = 1u;
        spawn_objects.alien_shot_first_slot = 1u;
        spawn_objects.point_bytes = point_bytes;
        spawn_objects.point_count = 42u;
        parent_slot = slot_bytes;
        write_be16(parent_slot + 0u, 1u);
        write_be16(parent_slot + 4u, 0x1234u);
        write_be16(parent_slot + 12u, 7u);
        write_be16(parent_slot + 28u, 0x2468u);
        write_be16(parent_slot + 30u, 0x1357u);
        write_be32(parent_slot + 50u, UINT32_C(0x89abcdef));
        parent_slot[63u] = UINT8_MAX;
        write_be32(point_bytes + 1u * OBJECT_RUNTIME_POINT_BYTE_COUNT, UINT32_C(0x11223344));
        write_be32(point_bytes + 1u * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   UINT32_C(0x55667788));
        for (uint32_t child_slot_index = 22u; child_slot_index <= 26u;
             child_slot_index += 2u) {
            uint8_t *child_slot = slot_bytes +
                (size_t)child_slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

            write_be16(child_slot + 0u, (uint16_t)child_slot_index);
            write_be16(child_slot + 12u, UINT16_MAX);
        }
        /* The third candidate is free solely because EntT_HitPoints_b is zero. */
        write_be16(slot_bytes + 26u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 9u);
        slot_bytes[26u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 0u;
        child_definition.hit_points = 0x1234u;
        if (!alien_spawn_smaller(&spawn_objects, 0u, &child_definition, 6u, &spawned_count,
                                 error, sizeof(error)) ||
            spawned_count != 3u) {
            fprintf(stderr, "ai_JustDied spawned-alien allocation is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint32_t child_slot_index = 22u; child_slot_index <= 26u;
             child_slot_index += 2u) {
            uint8_t *child_slot = slot_bytes +
                (size_t)child_slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
            uint8_t *preceding_slot = child_slot - OBJECT_RUNTIME_SLOT_BYTE_COUNT;
            uint8_t *child_point = point_bytes +
                (size_t)child_slot_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;

            if (child_slot[16u] != 0u || child_slot[17u] != 0xa5u ||
                child_slot[18u] != 0x34u || child_slot[19u] != 0u ||
                child_slot[20u] != 0u || child_slot[21u] != UINT8_MAX ||
                child_slot[24u] != UINT8_MAX || child_slot[25u] != 0xa5u ||
                read_be16(child_slot + 4u) != 0x1234u ||
                read_be16(child_slot + 12u) != 7u || read_be16(child_slot + 26u) != 7u ||
                read_be16(child_slot + 28u) != 0x2468u ||
                read_be16(child_slot + 30u) != 0x1357u ||
                read_be16(child_slot + 32u) != 0x2468u ||
                read_be16(child_slot + 34u) != 0u || read_be16(child_slot + 40u) != 0u ||
                read_be16(child_slot + 42u) != 0u || read_be16(child_slot + 44u) != 0u ||
                read_be16(child_slot + 46u) != 0u ||
                read_be32(child_slot + 50u) != UINT32_C(0x89abcdef) ||
                child_slot[54u] != 6u || child_slot[55u] != 0u ||
                child_slot[63u] != UINT8_MAX || preceding_slot[16u] != 3u ||
                read_be16(preceding_slot + 12u) != UINT16_MAX ||
                memcmp(child_point, point_bytes + 1u * OBJECT_RUNTIME_POINT_BYTE_COUNT,
                       OBJECT_RUNTIME_POINT_BYTE_COUNT) != 0) {
                fprintf(stderr, "ai_JustDied spawned-alien state is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
    }
    {
        /* macros.i:STATS_KILL's word counter and overwritten event signal. */
        GameProgression progression;

        game_progression_init(&progression);
        progression.alien_kills[6u] = UINT16_MAX;
        progression.signal = UINT32_C(0xfeedface);
        if (!game_progression_record_alien_kill(&progression, 6u, error, sizeof(error)) ||
            progression.alien_kills[6u] != 0u || progression.signal != 1u ||
            game_progression_record_alien_kill(&progression, GAME_LINK_ALIEN_COUNT,
                                               error, sizeof(error)) ||
            progression.alien_kills[GAME_LINK_ALIEN_COUNT - 1u] != 0u) {
            fprintf(stderr, "STATS_KILL source progression state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_JustDied's bullet-splat and child-splat handoffs. */
        uint8_t slot_bytes[42u * OBJECT_RUNTIME_SLOT_BYTE_COUNT];
        uint8_t point_bytes[42u * OBJECT_RUNTIME_POINT_BYTE_COUNT];
        uint8_t narrative_bytes[AB3D2_LEVEL_MESSAGE_LENGTH] = {0};
        ObjectRuntime death_objects = {0};
        ObjectAnimationRuntime death_animation;
        ObjectExplosionRuntime death_explosion;
        GameProgression death_progression;
        GameRandom death_random;
        LevelRuntime death_level = {0};
        AlienJustDiedState death_state;
        AssetBlob death_link_blob = {0};
        GameLink death_link = {0};
        const uint8_t *alien_definition_table;
        size_t alien_definition_table_size;
        uint8_t *mutable_alien_definition_table;
        uint8_t bullet_parent_type = 0u;
        uint8_t child_parent_type = 1u;
        uint8_t bullet_splat_type = 5u;
        uint8_t child_splat_type = 20u;

        death_link_blob.bytes = malloc(game.game_link.size);
        death_link_blob.size = game.game_link.size;
        if (!death_link_blob.bytes) {
            fprintf(stderr, "ai_JustDied GLFT branch fixture allocation failed\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        memcpy(death_link_blob.bytes, game.game_link.bytes, death_link_blob.size);
        if (!game_link_init(&death_link_blob, &death_link, error, sizeof(error)) ||
            !game_link_table(&death_link, GAME_LINK_TABLE_ALIEN_DEFINITIONS,
                             &alien_definition_table, &alien_definition_table_size) ||
            alien_definition_table_size !=
                (size_t)GAME_LINK_ALIEN_COUNT * GAME_LINK_ALIEN_DEFINITION_SIZE) {
            fprintf(stderr, "ai_JustDied GLFT branch fixture is unavailable: %s\n", error);
            free(death_link_blob.bytes);
            game_bootstrap_destroy(&game);
            return 1;
        }
        mutable_alien_definition_table = (uint8_t *)alien_definition_table;
        /* AlienT_SplatType_w is word 19; ai_JustDied reads its low byte. */
        write_be16(mutable_alien_definition_table + 38u, bullet_splat_type);
        write_be16(mutable_alien_definition_table + GAME_LINK_ALIEN_DEFINITION_SIZE + 38u,
                   child_splat_type);

        death_objects.slot_bytes = slot_bytes;
        death_objects.slot_count = 42u;
        death_objects.active_slot_count = 1u;
        death_objects.alien_shot_first_slot = 1u;
        death_objects.point_bytes = point_bytes;
        death_objects.point_count = 42u;
        death_level.level_bytes = narrative_bytes;
        death_level.level_size = sizeof(narrative_bytes);
        memcpy(narrative_bytes, "SOURCE NARRATIVE", sizeof("SOURCE NARRATIVE") - 1u);

        memset(slot_bytes, 0, sizeof(slot_bytes));
        memset(point_bytes, 0, sizeof(point_bytes));
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 4u, 11u);
        write_be16(slot_bytes + 12u, 3u);
        write_be16(slot_bytes + 24u, UINT16_MAX);
        write_be16(slot_bytes + 42u, 50u);
        write_be16(slot_bytes + 44u, 70u);
        slot_bytes[54u] = bullet_parent_type;
        slot_bytes[63u] = UINT8_MAX;
        write_be32(point_bytes, UINT32_C(0x012caaaa));
        write_be32(point_bytes + 4u, UINT32_C(0xfe70bbbb));
        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            uint8_t *shot_slot = slot_bytes +
                (size_t)(shot_index + 1u) * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

            write_be16(shot_slot + 0u, (uint16_t)(shot_index + 1u));
            write_be16(shot_slot + 12u, UINT16_MAX);
        }
        object_animation_runtime_init(&death_animation);
        object_explosion_runtime_init(&death_explosion);
        game_progression_init(&death_progression);
        game_random_init(&death_random);
        if (!alien_death_just_died(
                &death_objects, 0u, &death_level, &death_link, &death_progression,
                &death_animation, &death_explosion, &game.math, &death_random, &death_state,
                error, sizeof(error)) ||
            death_state.narrative.bytes != NULL || death_state.splat_type != bullet_splat_type ||
            death_state.fragment_count != 8u || death_state.child_count != 0u ||
            death_state.got_out != UINT8_MAX || death_explosion.radius != 0 ||
            slot_bytes[18u] != 0u || slot_bytes[20u] != 5u || slot_bytes[55u] != 3u ||
            read_be16(slot_bytes + 40u) != 0u || death_animation.workspace[0u][1u] != UINT8_MAX ||
            death_progression.alien_kills[bullet_parent_type] != 1u ||
            death_progression.signal != 1u) {
            fprintf(stderr, "ai_JustDied bullet-splat state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }

        memset(slot_bytes, 0, sizeof(slot_bytes));
        memset(point_bytes, 0, sizeof(point_bytes));
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 4u, 12u);
        write_be16(slot_bytes + 12u, 4u);
        write_be16(slot_bytes + 24u, 0u);
        write_be16(slot_bytes + 28u, 9u);
        write_be16(slot_bytes + 30u, 0x1234u);
        write_be32(slot_bytes + 50u, UINT32_C(0x10203040));
        slot_bytes[54u] = child_parent_type;
        slot_bytes[63u] = 1u;
        write_be32(point_bytes, UINT32_C(0x00112233));
        write_be32(point_bytes + 4u, UINT32_C(0x44556677));
        for (uint32_t child_slot_index = 22u; child_slot_index <= 26u;
             child_slot_index += 2u) {
            uint8_t *child_slot = slot_bytes +
                (size_t)child_slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

            write_be16(child_slot + 0u, (uint16_t)child_slot_index);
            write_be16(child_slot + 12u, UINT16_MAX);
        }
        object_animation_runtime_init(&death_animation);
        object_explosion_runtime_init(&death_explosion);
        game_progression_init(&death_progression);
        game_random_init(&death_random);
        if (!alien_death_just_died(
                &death_objects, 0u, &death_level, &death_link, &death_progression,
                &death_animation, &death_explosion, &game.math, &death_random, &death_state,
                error, sizeof(error)) ||
            death_state.narrative.bytes != narrative_bytes ||
            death_state.narrative.byte_count != AB3D2_LEVEL_MESSAGE_LENGTH ||
            death_state.narrative.length_and_tag != AB3D2_LEVEL_MESSAGE_LENGTH ||
            death_state.splat_type != child_splat_type || death_state.fragment_count != 0u ||
            death_state.child_count != 3u || death_state.got_out != UINT8_MAX ||
            slot_bytes[18u] != 0u || slot_bytes[20u] != 5u || slot_bytes[55u] != 3u ||
            read_be16(slot_bytes + 40u) != 0u || death_animation.workspace[0u][1u] != UINT8_MAX ||
            death_progression.alien_kills[child_parent_type] != 1u ||
            death_progression.signal != 1u) {
            fprintf(stderr, "ai_JustDied child-splat state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        free(death_link_blob.bytes);
    }
    {
        /* modules/ai.s:ai_PauseBriefly retains its timer, sight, and facing order. */
        uint8_t slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime pause_objects = {0};
        ObjectAnimationRuntime pause_animation;
        ObjectExplosionRuntime pause_explosion;
        AlienRuntime pause_runtime;
        LightingRuntime pause_lighting;
        GameProgression pause_progression;
        GameRandom pause_random;
        PlayerRuntime pause_player = game.player;
        AlienSetup pause_setup;
        AlienPauseState pause_state;
        GameAlienDefinition pause_definition;
        GameAlienAnimationFrame pause_frame;
        GameObjectDefinition pause_auxiliary_definition;
        GameObjectAnimationFrame pause_auxiliary_frame;
        uint16_t pause_alien = UINT16_MAX;
        uint16_t pause_option = UINT16_MAX;
        uint16_t pause_frame_index = UINT16_MAX;

        for (uint16_t alien_index = 0u;
             alien_index < GAME_LINK_ALIEN_COUNT && pause_alien == UINT16_MAX;
             ++alien_index) {
            if (!game_link_get_alien_definition(&game.game_link_catalog, alien_index,
                                                &pause_definition, error, sizeof(error)) ||
                (int16_t)pause_definition.auxiliary_type < 0 ||
                (int16_t)pause_definition.auxiliary_type >= GAME_LINK_OBJECT_COUNT) {
                continue;
            }
            for (uint16_t option_index = 1u;
                 option_index < GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT &&
                 pause_alien == UINT16_MAX;
                 ++option_index) {
                for (uint16_t frame_index = 0u;
                     frame_index < GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT;
                     ++frame_index) {
                    if (!game_link_get_alien_animation_frame(
                            &game.game_link_catalog, alien_index, option_index, frame_index,
                            &pause_frame, error, sizeof(error)) ||
                        (int8_t)pause_frame.bytes[8u] < 0 ||
                        pause_frame.bytes[8u] >= GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT ||
                        !game_link_get_object_definition(
                            &game.game_link_catalog, (uint16_t)pause_definition.auxiliary_type,
                            &pause_auxiliary_definition, error, sizeof(error)) ||
                        !game_link_get_object_animation_frame(
                            &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                            (uint16_t)pause_definition.auxiliary_type, pause_frame.bytes[8u],
                            &pause_auxiliary_frame, error, sizeof(error))) {
                        continue;
                    }
                    pause_alien = alien_index;
                    pause_option = option_index;
                    pause_frame_index = frame_index;
                    break;
                }
            }
        }
        if (pause_alien == UINT16_MAX) {
            fprintf(stderr, "ai_PauseBriefly animation source fixture is unavailable: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        pause_objects.slot_bytes = slot_bytes;
        pause_objects.slot_count = 2u;
        pause_objects.active_slot_count = 2u;
        pause_objects.point_bytes = point_bytes;
        pause_objects.point_count = 1u;
        pause_player.x = 100;
        pause_player.z = 200;
        pause_player.tmp_x = 100;
        pause_player.tmp_z = 200;
        pause_player.y = 3 * 128;
        pause_player.stood_in_top = 0u;

        write_be16(slot_bytes + 0u, UINT16_MAX);
        write_be16(slot_bytes + 12u, UINT16_MAX);
        write_be16(slot_bytes + 64u + 0u, 0u);
        write_be16(slot_bytes + 64u + 4u, 3u);
        write_be16(slot_bytes + 64u + 12u, pause_player.zone_index);
        write_be16(slot_bytes + 64u + 26u, pause_player.zone_index);
        write_be16(slot_bytes + 64u + 30u, 0x1000u);
        write_be16(slot_bytes + 64u + 34u, 2u);
        slot_bytes[64u + 20u] = 4u;
        slot_bytes[64u + 54u] = (uint8_t)pause_alien;
        slot_bytes[64u + 55u] = 7u;
        slot_bytes[64u + 63u] = pause_player.stood_in_top;
        write_be16(point_bytes + 0u, (uint16_t)pause_player.x);
        write_be16(point_bytes + 4u, (uint16_t)pause_player.z);
        if (!alien_setup_from_slot(&pause_objects, 1u, &game.dynamic_level.runtime,
                                   &game.game_link_catalog, &pause_setup,
                                   error, sizeof(error))) {
            fprintf(stderr, "ai_PauseBriefly setup fixture is invalid: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        object_animation_runtime_init(&pause_animation);
        pause_animation.workspace[1u][1u] = (uint8_t)pause_frame_index;
        pause_animation.workspace[1u][2u] = (uint8_t)pause_option;
        alien_runtime_init(&pause_runtime);
        lighting_runtime_init(&pause_lighting);
        object_explosion_runtime_init(&pause_explosion);
        game_progression_init(&pause_progression);
        game_random_init(&pause_random);
        if (!alien_pause_briefly_update(
                &pause_objects, 1u, &pause_runtime, &pause_animation, &pause_lighting,
                &game.dynamic_level.runtime, &game.level_clips, &game.game_link_catalog,
                &pause_progression, &pause_explosion, &game.math, &pause_random,
                &pause_player, &pause_setup, 1u, &pause_state, error, sizeof(error)) ||
            pause_state.damage_taken != 0u || pause_state.got_out != 0u ||
            read_be16(slot_bytes + 64u + 34u) != 1u ||
            read_be16(slot_bytes + 64u + 40u) != 0u || slot_bytes[64u + 20u] != 4u ||
            slot_bytes[64u + 55u] != 7u ||
            read_be16(slot_bytes + 64u + 30u) !=
                (uint16_t)(0x1000u + pause_state.animation.facing)) {
            fprintf(stderr, "ai_PauseBriefly waiting source state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }

        memset(slot_bytes, 0, sizeof(slot_bytes));
        object_animation_runtime_init(&pause_animation);
        alien_runtime_init(&pause_runtime);
        lighting_runtime_init(&pause_lighting);
        object_explosion_runtime_init(&pause_explosion);
        game_progression_init(&pause_progression);
        game_random_init(&pause_random);
        write_be16(slot_bytes + 0u, UINT16_MAX);
        write_be16(slot_bytes + 12u, UINT16_MAX);
        write_be16(slot_bytes + 64u + 0u, 0u);
        write_be16(slot_bytes + 64u + 4u, 3u);
        write_be16(slot_bytes + 64u + 12u, pause_player.zone_index);
        write_be16(slot_bytes + 64u + 26u, pause_player.zone_index);
        write_be16(slot_bytes + 64u + 30u, 0x1000u);
        write_be16(slot_bytes + 64u + 34u, 0u);
        slot_bytes[64u + 20u] = 4u;
        slot_bytes[64u + 54u] = (uint8_t)pause_alien;
        slot_bytes[64u + 55u] = 7u;
        slot_bytes[64u + 63u] = pause_player.stood_in_top;
        write_be16(point_bytes + 0u, (uint16_t)pause_player.x);
        write_be16(point_bytes + 4u, (uint16_t)pause_player.z);
        pause_animation.workspace[1u][1u] = (uint8_t)pause_frame_index;
        pause_animation.workspace[1u][2u] = (uint8_t)pause_option;
        if (!alien_pause_briefly_update(
                &pause_objects, 1u, &pause_runtime, &pause_animation, &pause_lighting,
                &game.dynamic_level.runtime, &game.level_clips, &game.game_link_catalog,
                &pause_progression, &pause_explosion, &game.math, &pause_random,
                &pause_player, &pause_setup, 1u, &pause_state, error, sizeof(error)) ||
            pause_state.damage_taken != 0u || pause_state.got_out != 0u ||
            slot_bytes[64u + 17u] != 1u || slot_bytes[64u + 20u] != 0u ||
            slot_bytes[64u + 55u] != 0u ||
            read_be16(slot_bytes + 64u + 30u) !=
                (uint16_t)(0x1000u + pause_state.animation.facing)) {
            fprintf(stderr, "ai_PauseBriefly expired no-front source state is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_Widget retains team memory and its raw a2 collision view. */
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[2u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime prowl_objects = {0};
        AlienRuntime prowl_runtime;
        PlayerRuntime prowl_player = game.player;
        GameRandom prowl_random;
        AlienProwlWidgetState prowl_state;
        LevelZone prowl_player_zone;
        uint16_t player_control_point;

        prowl_objects.slot_bytes = slot_bytes;
        prowl_objects.slot_count = 1u;
        prowl_objects.active_slot_count = 1u;
        prowl_objects.point_bytes = point_bytes;
        prowl_objects.point_count = 2u;
        for (uint16_t word_index = 0u; word_index < ALIEN_RUNTIME_WORKSPACE_WORD_COUNT;
             ++word_index) {
            write_be16(point_bytes + (size_t)word_index * sizeof(uint16_t),
                       (uint16_t)(0x1000u + word_index));
        }
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 28u, 0u);
        write_be16(slot_bytes + 32u, 0u);
        slot_bytes[21u] = UINT8_MAX;
        alien_runtime_init(&prowl_runtime);
        alien_runtime_begin_level(&prowl_runtime);
        prowl_runtime.entity_workspace[0u][5u] = 123;
        prowl_runtime.entity_workspace[0u][6u] = 456;
        game_random_init(&prowl_random);
        if (!alien_prowl_widget(
                &prowl_runtime, &prowl_objects, 0u, &game.dynamic_level.runtime,
                &game.level_navigation, &prowl_player, 0, 0u, &prowl_random,
                &prowl_state, error, sizeof(error)) ||
            read_be16(slot_bytes + 32u) != 0u || prowl_state.middle_control_point != 0u ||
            prowl_state.only_see != 0u || prowl_runtime.entity_workspace[0u][5u] != 0 ||
            prowl_runtime.entity_workspace[0u][6u] != 0) {
            fprintf(stderr, "ai_Widget no-team source state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint16_t word_index = 0u; word_index < ALIEN_RUNTIME_WORKSPACE_WORD_COUNT;
             ++word_index) {
            if (prowl_state.words[word_index] != (int16_t)(0x1000u + word_index)) {
                fprintf(stderr, "ai_Widget object-point a2 source view is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        }

        {
            uint8_t navigation_links[LEVEL_NAVIGATION_MAP_BYTES] = {0};
            LevelNavigation prowl_navigation = {0};
            LevelRuntime prowl_level = {0};
            GameRandom expected_random;

            /* Ground `ONLYSEE` reroutes through ai_Widget's one-random eight-try loop. */
            navigation_links[1u] = UINT8_C(0x81);
            prowl_navigation.walk_links = navigation_links;
            prowl_navigation.walk_links_size = sizeof(navigation_links);
            prowl_navigation.fly_links = navigation_links;
            prowl_navigation.fly_links_size = sizeof(navigation_links);
            prowl_level.control_point_count = 2u;
            memset(slot_bytes, 0, sizeof(slot_bytes));
            write_be16(slot_bytes + 0u, 0u);
            write_be16(slot_bytes + 28u, 0u);
            write_be16(slot_bytes + 32u, 1u);
            slot_bytes[21u] = UINT8_MAX;
            alien_runtime_init(&prowl_runtime);
            alien_runtime_begin_level(&prowl_runtime);
            game_random_init(&prowl_random);
            expected_random = prowl_random;
            (void)game_random_next(&expected_random);
            if (!alien_prowl_widget(
                    &prowl_runtime, &prowl_objects, 0u, &prowl_level,
                    &prowl_navigation, &prowl_player, 0, 0u, &prowl_random,
                    &prowl_state, error, sizeof(error)) ||
                read_be16(slot_bytes + 32u) != 1u ||
                prowl_state.middle_control_point != 1u ||
                prowl_state.only_see != UINT8_MAX ||
                prowl_random.state != expected_random.state) {
                fprintf(stderr, "ai_Widget ground ONLYSEE reroute is inconsistent: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }

        memset(slot_bytes, 0, sizeof(slot_bytes));
        write_be16(slot_bytes + 0u, 1u);
        write_be16(slot_bytes + 28u, 0u);
        write_be16(slot_bytes + 32u, 7u);
        slot_bytes[21u] = 2u;
        alien_runtime_init(&prowl_runtime);
        alien_runtime_begin_level(&prowl_runtime);
        for (uint16_t word_index = 0u; word_index < ALIEN_RUNTIME_WORKSPACE_WORD_COUNT;
             ++word_index) {
            prowl_runtime.team_workspace[2u][word_index] = (int16_t)(200u + word_index);
        }
        prowl_runtime.team_workspace[2u][3u] = 0;
        prowl_runtime.team_workspace[2u][4u] = 7;
        game_random_init(&prowl_random);
        if (!alien_prowl_widget(
                &prowl_runtime, &prowl_objects, 0u, &game.dynamic_level.runtime,
                &game.level_navigation, &prowl_player, 0, UINT8_MAX, &prowl_random,
                &prowl_state, error, sizeof(error)) ||
            read_be16(slot_bytes + 32u) != 0u || prowl_state.middle_control_point != 0u ||
            prowl_runtime.entity_workspace[1u][0u] != 200 ||
            prowl_runtime.entity_workspace[1u][2u] != -1 ||
            prowl_runtime.entity_workspace[1u][5u] != 205 ||
            prowl_runtime.entity_workspace[1u][6u] != 206) {
            fprintf(stderr, "ai_Widget team-memory source state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint16_t word_index = 0u; word_index < ALIEN_RUNTIME_WORKSPACE_WORD_COUNT;
             ++word_index) {
            if (prowl_state.words[word_index] !=
                (word_index == 3u ? 0 : (word_index == 4u ? 7 :
                                          (int16_t)(200u + word_index)))) {
                fprintf(stderr, "ai_Widget team a2 source view is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        }

        if (!level_runtime_get_zone(&game.dynamic_level.runtime, prowl_player.zone_index,
                                    &prowl_player_zone, error, sizeof(error))) {
            fprintf(stderr, "ai_Widget player-noise zone fixture is unavailable: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        prowl_player.stood_in_top = 0u;
        player_control_point = (uint8_t)(prowl_player_zone.control_point >> 8u);
        memset(slot_bytes, 0, sizeof(slot_bytes));
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 28u, player_control_point);
        write_be16(slot_bytes + 32u, 0u);
        slot_bytes[21u] = UINT8_MAX;
        alien_runtime_init(&prowl_runtime);
        alien_runtime_begin_level(&prowl_runtime);
        game_random_init(&prowl_random);
        if (!alien_prowl_widget(
                &prowl_runtime, &prowl_objects, 0u, &game.dynamic_level.runtime,
                &game.level_navigation, &prowl_player, 100, 0u, &prowl_random,
                &prowl_state, error, sizeof(error)) ||
            read_be16(slot_bytes + 32u) != player_control_point ||
            prowl_state.middle_control_point != player_control_point ||
            prowl_state.only_see != 0u) {
            fprintf(stderr, "ai_Widget player-noise source state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* objectmove.s:HeadTowardsAng's zero-distance, range, and speed paths. */
        ObjectHeading heading = {0};
        int16_t heading_sine;
        int16_t heading_cosine;

        heading.old_x = 12;
        heading.old_z = -7;
        heading.new_x = 12;
        heading.new_z = -7;
        heading.angle = 0x1234u;
        if (!object_heading_towards_angle(&game.math, &heading, error, sizeof(error)) ||
            heading.got_there != UINT8_MAX || heading.new_x != 12 || heading.new_z != -7 ||
            heading.angle != 0x1234u) {
            fprintf(stderr, "HeadTowardsAng zero-distance path is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        memset(&heading, 0, sizeof(heading));
        heading.new_x = 100;
        heading.speed = 20;
        if (!object_heading_towards_angle(&game.math, &heading, error, sizeof(error)) ||
            heading.got_there != 0u || heading.new_x != 19 || heading.new_z != 0 ||
            heading.angle != 2560u ||
            !game_math_sine(&game.math, heading.angle, &heading_sine, error, sizeof(error)) ||
            heading_sine <= 0) {
            fprintf(stderr, "HeadTowardsAng positive-X speed path is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        memset(&heading, 0, sizeof(heading));
        heading.new_z = 10;
        heading.range = 20;
        heading.speed = 20;
        if (!object_heading_towards_angle(&game.math, &heading, error, sizeof(error)) ||
            heading.got_there != UINT8_MAX || heading.new_x != 0 || heading.new_z != 0 ||
            heading.angle != 7680u ||
            !game_math_cosine(&game.math, heading.angle, &heading_cosine, error, sizeof(error)) ||
            heading_cosine <= 0) {
            fprintf(stderr, "HeadTowardsAng range path is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* newaliencontrol.s:ViewpointToDraw's four cardinal frame branches. */
        uint8_t viewpoint_frame;

        if (!object_viewpoint_select_frame(&game.math, 0u, 0u, &viewpoint_frame,
                                           error, sizeof(error)) ||
            viewpoint_frame != 2u ||
            !object_viewpoint_select_frame(&game.math, 2048u, 0u, &viewpoint_frame,
                                           error, sizeof(error)) ||
            viewpoint_frame != 1u ||
            !object_viewpoint_select_frame(&game.math, 4096u, 0u, &viewpoint_frame,
                                           error, sizeof(error)) ||
            viewpoint_frame != 0u ||
            !object_viewpoint_select_frame(&game.math, 6144u, 0u, &viewpoint_frame,
                                           error, sizeof(error)) ||
            viewpoint_frame != 3u) {
            fprintf(stderr, "ViewpointToDraw cardinal frame selection is inconsistent: %s\n",
                    error);
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
        /*
         * objectmove.s:checkotherwalls is reached only when Obj_ExtLen_w is
         * non-zero. Give zone 0 an empty primary list followed by its
         * extended edge and use the first authored girth extension (40).
         */
        write_be16(movement_state.level_bytes + 64u, UINT16_MAX);
        write_be16(movement_state.level_bytes + 66u, 0u);
        write_be16(movement_state.level_bytes + 68u, UINT16_C(0xfffe));
        write_be16(movement_state.level_bytes + 208u, UINT16_MAX);
        if (!level_dynamic_state_set_edge_flags(&movement_state, 0u, 0u)) {
            fprintf(stderr, "could not prepare MoveObject extended-edge fixture\n");
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
        movement_trace.extension_length = 40;
        movement_trace.wall_flags = 0x0200u;
        movement_trace.away_from_wall = -1;
        movement_trace.exit_first = UINT8_MAX;
        if (!object_movement_trace(&movement_state, &movement_trace, error, sizeof(error)) ||
            movement_trace.hit_wall != UINT8_MAX || movement_trace.new_x != 18 ||
            movement_trace.new_z != 10 ||
            !level_dynamic_state_get_edge_flags(&movement_state, 0u, &edge_flags) ||
            edge_flags != 0x0200u) {
            fprintf(stderr, "MoveObject extended-edge trace is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        /* checkotherwalls requires source `newy >= ZoneT_Roof_l` to pass. */
        write_be16(movement_state.level_bytes + 208u, 1u);
        if (!level_dynamic_state_set_edge_flags(&movement_state, 0u, 0u)) {
            fprintf(stderr, "could not reset MoveObject extended-opening fixture\n");
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
        movement_trace.new_y = -1;
        movement_trace.thing_height = 100;
        movement_trace.step_up = 10000;
        movement_trace.step_down = 10000;
        movement_trace.extension_length = 40;
        movement_trace.wall_flags = 0x0200u;
        movement_trace.away_from_wall = -1;
        movement_trace.exit_first = UINT8_MAX;
        if (!object_movement_trace(&movement_state, &movement_trace, error, sizeof(error)) ||
            movement_trace.hit_wall != UINT8_MAX || movement_trace.new_x != 18 ||
            !level_dynamic_state_get_edge_flags(&movement_state, 0u, &edge_flags) ||
            edge_flags != 0x0200u) {
            fprintf(stderr, "MoveObject extended roof opening is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if (!level_dynamic_state_set_edge_flags(&movement_state, 0u, 0u)) {
            fprintf(stderr, "could not reset passable extended-opening fixture\n");
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        movement_trace.new_x = 20;
        movement_trace.new_z = 10;
        movement_trace.new_y = 0;
        if (!object_movement_trace(&movement_state, &movement_trace, error, sizeof(error)) ||
            movement_trace.hit_wall != 0u || movement_trace.new_x != 20 ||
            movement_trace.new_z != 10 ||
            !level_dynamic_state_get_edge_flags(&movement_state, 0u, &edge_flags) ||
            edge_flags != 0u) {
            fprintf(stderr, "MoveObject extended passable opening is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        level_dynamic_state_destroy(&movement_state);
    }
    {
        /* objectmove.s:Obj_DoCollision's raw a2 vertical extents and X/Z tests. */
        uint8_t collision_slot_bytes[3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t collision_point_bytes[2u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        int16_t collision_a2_words[8u] = {0, 20, 40, 0, 0, 20, 40, 0};
        ObjectRuntime collision_objects = {0};
        ObjectCollisionTrace collision_trace = {0};
        uint8_t hit_wall = 0u;

        collision_objects.slot_bytes = collision_slot_bytes;
        collision_objects.slot_count = 3u;
        collision_objects.active_slot_count = 2u;
        collision_objects.point_bytes = collision_point_bytes;
        collision_objects.point_count = 2u;
        /* The active source list is point 0, point 1, then its -1 terminator. */
        write_be16(collision_slot_bytes + 0u, 0u);
        write_be16(collision_slot_bytes + 4u, 100u);
        write_be16(collision_slot_bytes + 12u, 5u);
        collision_slot_bytes[16u] = 0u;
        collision_slot_bytes[18u] = 1u;
        write_be16(collision_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT, 1u);
        write_be16(collision_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u, 100u);
        write_be16(collision_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 5u);
        collision_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
        collision_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
        write_be16(collision_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        write_be32(collision_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT,
                   UINT32_C(1000) << 16u);
        write_be32(collision_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   UINT32_C(1000) << 16u);
        collision_trace.collision_id = 0u;
        collision_trace.old_x = 1200;
        collision_trace.old_z = 1000;
        collision_trace.new_x = 850;
        collision_trace.new_z = 1000;
        collision_trace.new_y = 90 * 128;
        collision_trace.thing_height = 20 * 128;
        if (!object_collision_check(
                &collision_objects, &game.game_link_catalog, collision_a2_words,
                sizeof(collision_a2_words) / sizeof(collision_a2_words[0]),
                &collision_trace, &hit_wall, error, sizeof(error)) ||
            hit_wall != UINT8_MAX) {
            fprintf(stderr, "Obj_DoCollision source approaching-X collision is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        collision_trace.old_x = 950;
        if (!object_collision_check(
                &collision_objects, &game.game_link_catalog, collision_a2_words,
                sizeof(collision_a2_words) / sizeof(collision_a2_words[0]),
                &collision_trace, &hit_wall, error, sizeof(error)) ||
            hit_wall != 0u) {
            fprintf(stderr, "Obj_DoCollision source receding-X gate is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        collision_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 2u;
        collision_trace.old_x = 1200;
        if (!object_collision_check(
                &collision_objects, &game.game_link_catalog, collision_a2_words,
                sizeof(collision_a2_words) / sizeof(collision_a2_words[0]),
                &collision_trace, &hit_wall, error, sizeof(error)) ||
            hit_wall != 0u) {
            fprintf(stderr, "Obj_DoCollision source type gate is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
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
