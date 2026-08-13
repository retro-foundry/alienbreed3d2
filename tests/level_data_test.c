#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alien_runtime.h"
#include "alien_attack.h"
#include "alien_animation.h"
#include "alien_charge.h"
#include "alien_damage.h"
#include "alien_decision.h"
#include "alien_death.h"
#include "alien_dispatch.h"
#include "alien_dark.h"
#include "alien_flight.h"
#include "alien_math.h"
#include "alien_main.h"
#include "alien_memory.h"
#include "alien_pause.h"
#include "alien_perception.h"
#include "alien_prowl.h"
#include "alien_run_around.h"
#include "alien_setup.h"
#include "alien_spatial.h"
#include "alien_spawn.h"
#include "alien_torch.h"
#include "asset_io.h"
#include "bitmap_source_decode.h"
#include "game_bootstrap.h"
#include "game_vblank_clock.h"
#include "game_link.h"
#include "game_inventory.h"
#include "game_menu.h"
#include "game_progression.h"
#include "game_random.h"
#include "game_save.h"
#include "level_bootstrap.h"
#include "level_draw_graph.h"
#include "level_mechanisms.h"
#include "lighting_runtime.h"
#include "message_runtime.h"
#include "object_collectables.h"
#include "object_collision.h"
#include "object_blast.h"
#include "object_explosion.h"
#include "object_animation.h"
#include "object_handler.h"
#include "object_heading.h"
#include "object_movement.h"
#include "object_passives.h"
#include "object_projectiles.h"
#include "object_scene.h"
#include "object_teleport.h"
#include "object_viewpoint.h"
#include "object_visibility.h"
#include "object_worry.h"
#include "player_entity.h"
#include "player_shoot.h"
#include "render_view.h"
#include "scene_frame.h"
#include "source_vector_model_transform.h"
#include "source_vector_projection.h"

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

static int32_t source_shift_flat_texture_coordinate(int16_t coordinate, int32_t scale)
{
    if (scale >= 0) {
        return (int32_t)((uint32_t)(int32_t)coordinate << (uint32_t)scale);
    }
    return source_asr32_count(coordinate, (unsigned int)-scale);
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

static int source_liftable_audio_position(const GameMath *math,
                                          const PlayerRuntime *player,
                                          const LevelLiftable *liftable,
                                          int16_t *out_x, int16_t *out_z,
                                          char *error, size_t error_size)
{
    int16_t sine;
    int16_t cosine;
    int16_t delta_x;
    int16_t delta_z;
    int32_t product;

    if (!math || !player || !liftable || !out_x || !out_z ||
        !game_math_sine(math, player->yaw, &sine, error, error_size) ||
        !game_math_cosine(math, player->yaw, &cosine, error, error_size)) {
        return 0;
    }
    delta_x = (int16_t)((uint16_t)liftable->word9 -
                        (uint16_t)player_runtime_position_to_world(player->tmp_x));
    delta_z = (int16_t)((uint16_t)liftable->word10 -
                        (uint16_t)player_runtime_position_to_world(player->tmp_z));
    product = (int32_t)((uint32_t)((int32_t)cosine * delta_x) -
                        (uint32_t)((int32_t)sine * delta_z));
    product = (int32_t)((uint32_t)product << 1u);
    *out_x = (int16_t)(uint16_t)((uint32_t)product >> 16u);
    product = (int32_t)((uint32_t)((int32_t)sine * delta_x) -
                        (uint32_t)((int32_t)cosine * delta_z));
    product = (int32_t)((uint32_t)product << 1u);
    *out_z = (int16_t)(uint16_t)((uint32_t)product >> 16u);
    return 1;
}

static uint32_t read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

/* 68000 ASR.W with a register count: only its low six bits participate. */
static int16_t source_asr16_count(int16_t value, uint16_t count)
{
    unsigned int effective_count = count & 63u;

    if (effective_count >= 16u) {
        return value < 0 ? -1 : 0;
    }
    if (value >= 0) {
        return (int16_t)(value >> effective_count);
    }
    return (int16_t)-(((-(int32_t)value) + ((1 << effective_count) - 1)) >>
                     effective_count);
}

static int32_t source_asl32_count(int32_t value, uint16_t count)
{
    unsigned int effective_count = count & 63u;

    if (effective_count >= 32u) {
        return 0;
    }
    return (int32_t)((uint32_t)value << effective_count);
}

/*
 * newanims.s calls DoorRoutine before LiftRoutine.  Each list entry writes
 * its graphics pointer directly, so retain the final exact source writer;
 * this deliberately does not use the renderer's same-EdgeT solid-face rule.
 */
static int find_direct_mechanism_wall_target(const LevelMechanisms *mechanisms,
                                             uint32_t source_record_offset,
                                             uint8_t *out_kind, uint16_t *out_index,
                                             char *error, size_t error_size)
{
    if (!mechanisms || !out_kind || !out_index) {
        return 0;
    }
    *out_kind = LEVEL_STATIC_WALL_MECHANISM_NONE;
    *out_index = 0u;
    for (uint16_t mechanism_index = 0u; mechanism_index < mechanisms->door_count;
         ++mechanism_index) {
        LevelLiftable door;

        if (!level_mechanisms_get_door(mechanisms, mechanism_index, &door,
                                       error, error_size)) {
            return 0;
        }
        for (uint16_t target_index = 0u; target_index < door.wall_count; ++target_index) {
            LevelLiftableWall target;

            if (!level_mechanisms_get_door_wall(mechanisms, mechanism_index, target_index,
                                                &target, error, error_size)) {
                return 0;
            }
            if (target.graphics_offset == source_record_offset) {
                *out_kind = LEVEL_STATIC_WALL_MECHANISM_DOOR;
                *out_index = mechanism_index;
            }
        }
    }
    for (uint16_t mechanism_index = 0u; mechanism_index < mechanisms->lift_count;
         ++mechanism_index) {
        LevelLiftable lift;

        if (!level_mechanisms_get_lift(mechanisms, mechanism_index, &lift,
                                       error, error_size)) {
            return 0;
        }
        for (uint16_t target_index = 0u; target_index < lift.wall_count; ++target_index) {
            LevelLiftableWall target;

            if (!level_mechanisms_get_lift_wall(mechanisms, mechanism_index, target_index,
                                                &target, error, error_size)) {
                return 0;
            }
            if (target.graphics_offset == source_record_offset) {
                *out_kind = LEVEL_STATIC_WALL_MECHANISM_LIFT;
                *out_index = mechanism_index;
            }
        }
    }
    return 1;
}

/* DoorRoutine, LiftRoutine, then DoWaterAnims overwrite direct flat records. */
static int find_direct_dynamic_flat_target(const LevelMechanisms *mechanisms,
                                           uint32_t source_record_offset,
                                           uint8_t *out_kind, uint16_t *out_index,
                                           char *error, size_t error_size)
{
    if (!mechanisms || !out_kind || !out_index) {
        return 0;
    }
    *out_kind = LEVEL_STATIC_DYNAMIC_SURFACE_NONE;
    *out_index = 0u;
    for (uint16_t mechanism_index = 0u; mechanism_index < mechanisms->door_count;
         ++mechanism_index) {
        LevelLiftable door;

        if (!level_mechanisms_get_door(mechanisms, mechanism_index, &door,
                                       error, error_size)) {
            return 0;
        }
        if (door.graphics_offset == source_record_offset) {
            *out_kind = LEVEL_STATIC_DYNAMIC_SURFACE_DOOR;
            *out_index = mechanism_index;
        }
    }
    for (uint16_t mechanism_index = 0u; mechanism_index < mechanisms->lift_count;
         ++mechanism_index) {
        LevelLiftable lift;

        if (!level_mechanisms_get_lift(mechanisms, mechanism_index, &lift,
                                       error, error_size)) {
            return 0;
        }
        if (lift.graphics_offset == source_record_offset) {
            *out_kind = LEVEL_STATIC_DYNAMIC_SURFACE_LIFT;
            *out_index = mechanism_index;
        }
    }
    for (uint16_t animation_index = 0u;
         animation_index < mechanisms->water_animation_count; ++animation_index) {
        LevelWaterAnimation animation;

        if (!level_mechanisms_get_water_animation(mechanisms, animation_index, &animation,
                                                  error, error_size)) {
            return 0;
        }
        for (uint16_t target_index = 0u; target_index < animation.target_count;
             ++target_index) {
            LevelWaterAnimationTarget target;

            if (!level_mechanisms_get_water_animation_target(
                    mechanisms, animation_index, target_index, &target,
                    error, error_size)) {
                return 0;
            }
            if (target.graphics_offset == source_record_offset) {
                *out_kind = LEVEL_STATIC_DYNAMIC_SURFACE_WATER;
                *out_index = animation_index;
            }
        }
    }
    return 1;
}

/* Full-scene depth ownership: co-oriented opaque walls must not overlap. */
static int scene_walls_have_same_facing_overlap(const LevelStaticWallScene *left,
                                                const LevelStaticWallScene *right)
{
    int32_t left_low;
    int32_t left_high;
    int32_t right_low;
    int32_t right_high;

    if (!left || !right ||
        left->vertices[0].position.x != right->vertices[0].position.x ||
        left->vertices[0].position.z != right->vertices[0].position.z ||
        left->vertices[1].position.x != right->vertices[1].position.x ||
        left->vertices[1].position.z != right->vertices[1].position.z) {
        return 0;
    }
    left_low = left->vertices[0].position.y < left->vertices[2].position.y ?
        left->vertices[0].position.y : left->vertices[2].position.y;
    left_high = left->vertices[0].position.y > left->vertices[2].position.y ?
        left->vertices[0].position.y : left->vertices[2].position.y;
    right_low = right->vertices[0].position.y < right->vertices[2].position.y ?
        right->vertices[0].position.y : right->vertices[2].position.y;
    right_high = right->vertices[0].position.y > right->vertices[2].position.y ?
        right->vertices[0].position.y : right->vertices[2].position.y;
    return left_low < right_high && right_low < left_high;
}

/* objdrawhires.s:draw_CalcBrightsInZone's live point-light samples. */
static int scene_sprite_expected_point_light(const GameBootstrap *game,
                                             uint16_t zone_index, uint8_t upper_zone,
                                             int16_t *out_light, char *error,
                                             size_t error_size)
{
    int32_t total = 0;
    uint32_t sample_count = 0u;
    uint32_t component_offset = upper_zone != 0u ? 2u : 0u;

    if (!game || !out_light || zone_index >= game->level_runtime.zone_count ||
        zone_index >= LIGHTING_RUNTIME_POINT_ZONE_CAPACITY) {
        return 0;
    }
    for (uint16_t marker_index = 0u;
         marker_index < LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT; ++marker_index) {
        int16_t marker;
        uint32_t point_offset = (uint32_t)marker_index * 4u + component_offset;

        if (!level_runtime_get_zone_border_point(&game->level_runtime, zone_index,
                                                 marker_index, &marker, error, error_size)) {
            return 0;
        }
        if (marker < 0) {
            break;
        }
        total += game->lighting_runtime.current_point_brightness[zone_index][point_offset];
        total += game->lighting_runtime.current_point_brightness[zone_index][point_offset + 1u];
        sample_count += 2u;
    }
    if (sample_count == 0u) {
        return 0;
    }
    *out_light = (int16_t)(total / (int32_t)sample_count);
    return 1;
}

/* draw_zone_graph.s:itsafloor and hires.s:goursides source-flat light selection. */
static int scene_flat_expected_light(const GameBootstrap *game,
                                     const LevelStaticFlatScene *flat,
                                     uint32_t vertex_index, int16_t *out_light)
{
    int16_t source_light;
    uint32_t component_index;

    if (!game || !flat || !out_light || vertex_index >= flat->vertex_count ||
        flat->source_zone_index >= game->dynamic_level.runtime.zone_count ||
        flat->source_zone_index >= LIGHTING_RUNTIME_POINT_ZONE_CAPACITY) {
        return 0;
    }
    if (flat->primitive == SCENE_GEOMETRY_PRIMITIVE_WATER) {
        int32_t water_light = 300 +
            game->lighting_runtime.zone_brightness[flat->source_zone_index]
                                                   [flat->source_upper_zone != 0u ? 1u : 0u] +
            flat->brightness_offset;

        *out_light = water_light > INT16_MAX ? INT16_MAX :
            (water_light < INT16_MIN ? INT16_MIN : (int16_t)water_light);
        return 1;
    }
    if (!flat->point_brightness_selectors ||
        flat->point_brightness_selectors[vertex_index] >=
            LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT) {
        return 0;
    }
    component_index = (flat->source_upper_zone != 0u ? 2u : 0u) +
        (flat->primitive == SCENE_GEOMETRY_PRIMITIVE_CEILING ? 1u : 0u);
    source_light = game->lighting_runtime.current_point_brightness[flat->source_zone_index]
                                                                    [flat->point_brightness_selectors
                                                                         [vertex_index] * 4u +
                                                                     component_index];
    /* hires.s:goursides applies NEG.W to a negative source point brightness. */
    *out_light = source_light < 0 ?
        (int16_t)(UINT16_C(0) - (uint16_t)source_light) : source_light;
    return 1;
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
        LevelZone source_zone;
        int16_t point_index = (int16_t)read_be16(slot + 0u);
        int16_t zone_id = (int16_t)read_be16(slot + 12u);
        int16_t graphics_type = (int16_t)read_be16(slot + 8u);
        int16_t expected_light;
        uint16_t asset_index;
        uint8_t expected_flags = slot[63u] != 0u ? SCENE_SPRITE_FLAG_UPPER_ZONE : 0u;
        SceneSpriteSurfaceAttachment expected_surface_attachment =
            SCENE_SPRITE_SURFACE_FREE;

        if (point_index < 0) {
            break;
        }
        if (zone_id < 0 || slot_index == game->object_runtime.player1_slot) {
            continue;
        }
        if ((uint16_t)point_index >= game->object_runtime.point_count ||
            command_count >= expected_count ||
            first_command + command_count >= frame->count ||
            frame->commands[first_command + command_count].type !=
                SCENE_COMMAND_SPRITE_INSTANCE) {
            return 0;
        }
        if (slot[16u] == 2u) {
            expected_flags |= SCENE_SPRITE_FLAG_PROJECTILE;
            if (slot[30u] != 0u) {
                expected_flags |= SCENE_SPRITE_FLAG_PROJECTILE_CONTACT;
            }
        }
        point = game->object_runtime.point_bytes +
            (size_t)(uint16_t)point_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;
        if (slot[16u] == 1u) {
            GameObjectDefinition definition;

            if (slot[54u] >= GAME_LINK_OBJECT_COUNT ||
                !game_link_get_object_definition(&game->game_link_catalog, slot[54u],
                                                 &definition, error, error_size)) {
                return 0;
            }
            expected_surface_attachment = definition.floor_ceiling == 0u ?
                SCENE_SPRITE_SURFACE_FLOOR : SCENE_SPRITE_SURFACE_CEILING;
        }
        sprite = &frame->commands[first_command + command_count].data.sprite_instance.sprite;
        if (!level_runtime_get_zone(&game->dynamic_level.runtime, sprite->source_zone_index,
                                    &source_zone, error, error_size) ||
            sprite->source_record_id != slot_index ||
            sprite->position.x != (int16_t)read_be16(point + 0u) ||
            sprite->position.y != (int32_t)(int16_t)read_be16(slot + 4u) * 128 ||
            sprite->position.z != (int16_t)read_be16(point + 4u) ||
            sprite->source_brightness != read_be16(slot + 2u) ||
            sprite->yaw != read_be16(slot + 30u) ||
            sprite->surface_attachment != expected_surface_attachment ||
            sprite->source_aux_offset_x != (slot[16u] == 3u ?
                (int16_t)read_be16(slot + 44u) : 0) ||
            sprite->source_aux_offset_y != (slot[16u] == 3u ?
                (int16_t)read_be16(slot + 46u) : 0) ||
            sprite->source_clip_top_y != ((expected_flags & SCENE_SPRITE_FLAG_UPPER_ZONE) != 0u ?
                                           source_zone.upper_roof : source_zone.roof) ||
            sprite->source_clip_bottom_y != ((expected_flags & SCENE_SPRITE_FLAG_UPPER_ZONE) != 0u ?
                                              source_zone.upper_floor : source_zone.floor)) {
            return 0;
        }
        if (!scene_sprite_expected_point_light(
                game, (uint16_t)zone_id, (expected_flags & SCENE_SPRITE_FLAG_UPPER_ZONE) != 0u,
                &expected_light, error, error_size) ||
            sprite->source_light_level != expected_light) {
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
                sprite->source_palette_bytes != game->shared_resources.texture_maps.bytes ||
                sprite->source_palette_byte_count != game->shared_resources.texture_maps.size ||
                sprite->source_light_palette_bytes !=
                    game->shared_resources.texture_palette.bytes ||
                sprite->source_light_palette_byte_count !=
                    game->shared_resources.texture_palette.size ||
                sprite->source_display_palette_bytes != game->shared_resources.main_palette.bytes ||
                sprite->source_display_palette_byte_count != game->shared_resources.main_palette.size ||
                sprite->presentation != (slot_index == game->object_runtime.player1_slot + 2u ?
                    SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON :
                    SCENE_SPRITE_PRESENTATION_WORLD_OBJECT) ||
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
                sprite->source_display_palette_bytes != game->shared_resources.main_palette.bytes ||
                sprite->source_display_palette_byte_count != game->shared_resources.main_palette.size ||
                sprite->presentation != SCENE_SPRITE_PRESENTATION_WORLD_OBJECT ||
                sprite->source_width != slot[6u] || sprite->source_height != slot[7u] ||
                sprite->frame_metrics.pointer_table_index != source_frame.pointer_table_index ||
                sprite->frame_metrics.down_strip != source_frame.down_strip ||
                sprite->frame_metrics.strip_count != source_frame.strip_count ||
                sprite->frame_metrics.line_count != source_frame.line_count ||
                source_frame.strip_count > UINT16_MAX / 2u ||
                source_frame.line_count > UINT16_MAX / 2u ||
                (size_t)source_frame.pointer_table_index * 4u >
                    game->shared_resources.object_ptrs[asset_index].size ||
                (size_t)source_frame.strip_count * 2u >
                    (game->shared_resources.object_ptrs[asset_index].size -
                     (size_t)source_frame.pointer_table_index * 4u) / 4u) {
                return 0;
            }
            if (glare != 0) {
                if (sprite->source_palette_bytes != game->shared_resources.texture_palette.bytes ||
                    sprite->source_palette_byte_count != game->shared_resources.texture_palette.size ||
                    sprite->source_light_palette_bytes != NULL ||
                    sprite->source_light_palette_byte_count != 0u ||
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
                    sprite->source_light_palette_bytes !=
                        ((expected_flags & SCENE_SPRITE_FLAG_LIGHT_PALETTE) != 0u ?
                            game->shared_resources.bitmap_light_curve.bytes : NULL) ||
                    sprite->source_light_palette_byte_count !=
                        ((expected_flags & SCENE_SPRITE_FLAG_LIGHT_PALETTE) != 0u ?
                            game->shared_resources.bitmap_light_curve.size : 0u) ||
                    sprite->source_effect != effect || sprite->flags != expected_flags) {
                    return 0;
                }
            }
        }
        ++command_count;
    }
    return command_count == expected_count;
}

static int scene_frame_find_instance_layout(const SceneFrame *frame,
                                            size_t *out_geometry_instance_count,
                                            size_t *out_first_sprite_command)
{
    size_t geometry_instance_count = 0u;
    size_t first_sprite_command = SIZE_MAX;

    if (!frame || !out_geometry_instance_count || !out_first_sprite_command) {
        return 0;
    }
    for (size_t command_index = 0u; command_index < frame->count; ++command_index) {
        if (frame->commands[command_index].type == SCENE_COMMAND_GEOMETRY_INSTANCE) {
            ++geometry_instance_count;
        } else if (frame->commands[command_index].type == SCENE_COMMAND_SPRITE_INSTANCE &&
                   first_sprite_command == SIZE_MAX) {
            first_sprite_command = command_index;
        }
    }
    *out_geometry_instance_count = geometry_instance_count;
    *out_first_sprite_command = first_sprite_command;
    return 1;
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
        int16_t expected_rotated_x = 0;
        int16_t expected_rotated_z = 0;
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
                             player_runtime_position_to_world(player->x));
        offset_z = (int16_t)((int32_t)(int16_t)read_be16(point + 4u) -
                             player_runtime_position_to_world(player->z));
        expected_in_line = 0u;
        expected_distance = 0u;
        if ((int16_t)read_be16(slot + 12u) >= 0) {
            int32_t horizontal = (int32_t)((uint32_t)((int32_t)offset_x * cosine) -
                                           (uint32_t)((int32_t)offset_z * sine));
            int32_t depth = (int32_t)((uint32_t)((int32_t)offset_x * sine) +
                                      (uint32_t)((int32_t)offset_z * cosine));
            int16_t horizontal_word;
            int16_t depth_word;

            expected_rotated_x =
                (int16_t)(uint16_t)(((uint32_t)horizontal << 1u) >> 16u);
            expected_rotated_z =
                (int16_t)(uint16_t)(((uint32_t)depth << 1u) >> 16u);
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
        if (observation->rotated_x[output_index] != expected_rotated_x ||
            observation->rotated_z[output_index] != expected_rotated_z ||
            observation->in_line[output_index] != expected_in_line ||
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

enum {
    /* modules/player.s: RAWKEY_1 selects ShootT/GLFT gun entry zero. */
    LEVEL_DATA_SHOTGUN_GUN_INDEX = 0u,
    LEVEL_DATA_SHOTGUN_RAW_KEY = 1u,
    /* GLFT_GunNames_l entry three is "Assault Rifle"; RAWKEY_4 selects it. */
    LEVEL_DATA_ASSAULT_RIFLE_GUN_INDEX = 3u,
    LEVEL_DATA_ASSAULT_RIFLE_RAW_KEY = 4u,
    /* newaliencontrol.s:Collectable:GUNHELD draws Plr1_Use's ENT_NEXT_2. */
    LEVEL_DATA_VIEW_WEAPON_TIMER1_OFFSET = 34u
};

static const SceneSprite *level_data_find_player1_view_weapon(const SceneFrame *frame,
                                                               uint32_t player1_slot)
{
    uint32_t source_record_id = player1_slot + 2u;

    if (!frame) {
        return NULL;
    }
    for (size_t command_index = 0u; command_index < frame->count; ++command_index) {
        const SceneCommand *command = &frame->commands[command_index];

        if (command->type == SCENE_COMMAND_SPRITE_INSTANCE &&
            command->data.sprite_instance.sprite.source_record_id == source_record_id) {
            return &command->data.sprite_instance.sprite;
        }
    }
    return NULL;
}

static uint64_t level_data_hash_view_weapon_source_tick(uint64_t hash,
                                                        const SceneSprite *weapon,
                                                        uint16_t timer1)
{
    /* Keep the source art/frame/timer trace separate from presentation rate. */
    hash ^= weapon->source_asset_id;
    hash *= UINT64_C(1099511628211);
    hash ^= weapon->frame_index;
    hash *= UINT64_C(1099511628211);
    hash ^= timer1;
    hash *= UINT64_C(1099511628211);
    return hash;
}

/*
 * hires.s:VBlankInterrupt calls dosomething at PAL 50 Hz. The host loop must
 * only interpolate completed source snapshots: increasing present rate must
 * neither run Plr1_Shot/ACTANIMOBJ more often nor synthesize a weapon frame.
 */
static int level_data_run_shotgun_animation_present_rate(const char *data_root,
                                                         uint32_t host_present_rate,
                                                         uint64_t *out_source_trace)
{
    const uint64_t host_counter_frequency = UINT64_C(720000);
    GameBootstrap game = {0};
    GameVBlankClock vblank_clock = {0};
    SceneFrame previous_source = {0};
    SceneFrame source = {0};
    SceneFrame presentation = {0};
    GameShootDefinition shotgun_shoot;
    GameShootDefinition assault_rifle_shoot;
    uint64_t source_trace = UINT64_C(1469598103934665603);
    uint32_t source_vblanks = 0u;
    uint32_t baseline_asset_id = 0u;
    uint16_t baseline_frame_index = 0u;
    uint16_t baseline_timer1 = 0u;
    uint32_t fired_pose_asset_id = 0u;
    uint16_t fired_pose_frame_index = 0u;
    uint16_t fired_pose_timer1 = 0u;
    uint32_t assault_rifle_asset_id = 0u;
    uint16_t assault_rifle_frame_index = 0u;
    uint16_t assault_rifle_timer1 = 0u;
    uint8_t baseline_captured = 0u;
    uint8_t fired_pose_captured = 0u;
    uint8_t saw_four_tick_pose_advance = 0u;
    uint8_t saw_shotgun_action = 0u;
    uint8_t saw_shotgun_frame_blend = 0u;
    uint8_t assault_rifle_pose_captured = 0u;
    uint8_t saw_assault_rifle_per_tick_advance = 0u;
    uint8_t succeeded = 0u;
    char error[256] = {0};

    if (!data_root || host_present_rate == 0u || !out_source_trace) {
        fprintf(stderr, "Shotgun present-rate regression received invalid arguments\n");
        return 0;
    }
    if (!game_bootstrap_init(&game, data_root, error, sizeof(error)) ||
        !game_session_default(&game.session, &game.game_link_catalog, error, sizeof(error)) ||
        !game_session_select_level(&game.session, 0u, error, sizeof(error)) ||
        !game_bootstrap_start_selected_single_player(&game, data_root, error, sizeof(error)) ||
        !game_link_get_shoot_definition(
            &game.game_link_catalog, LEVEL_DATA_SHOTGUN_GUN_INDEX, &shotgun_shoot,
            error, sizeof(error)) ||
        !game_link_get_shoot_definition(
            &game.game_link_catalog, LEVEL_DATA_ASSAULT_RIFLE_GUN_INDEX,
            &assault_rifle_shoot, error, sizeof(error)) ||
        shotgun_shoot.bullet_type >= GAME_INVENTORY_AMMUNITION_COUNT ||
        shotgun_shoot.bullet_count == 0u || shotgun_shoot.bullet_count > 1000u ||
        assault_rifle_shoot.bullet_type >= GAME_INVENTORY_AMMUNITION_COUNT ||
        assault_rifle_shoot.bullet_type == shotgun_shoot.bullet_type ||
        assault_rifle_shoot.delay != 2u || assault_rifle_shoot.bullet_count == 0u ||
        assault_rifle_shoot.bullet_count > 500u ||
        !scene_frame_init(&previous_source, 8u) || !scene_frame_init(&source, 8u) ||
        !scene_frame_init(&presentation, 8u)) {
        fprintf(stderr, "could not initialize Shotgun present-rate regression: %s\n", error);
        goto cleanup;
    }
    /* Keep the source ammo comparison positive while exercising one real shot. */
    game.session.player1_inventory.weapons[LEVEL_DATA_SHOTGUN_GUN_INDEX] = UINT8_MAX;
    game.session.player1_inventory.weapons[LEVEL_DATA_ASSAULT_RIFLE_GUN_INDEX] = UINT8_MAX;
    for (uint16_t ammunition_index = 0u;
         ammunition_index < GAME_INVENTORY_AMMUNITION_COUNT; ++ammunition_index) {
        game.session.player1_inventory.ammunition[ammunition_index] = 1000u;
    }
    scene_frame_begin(&source);
    if (!game_bootstrap_submit_scene_frame(&game, &source) ||
        !scene_frame_clone(&previous_source, &source)) {
        fprintf(stderr, "could not capture initial Shotgun source frame\n");
        goto cleanup;
    }
    game_vblank_clock_reset(&vblank_clock, 0u, host_counter_frequency);
    for (uint64_t present_index = 1u;
         present_index <= (uint64_t)host_present_rate * 2u; ++present_index) {
        uint64_t host_counter =
            present_index * host_counter_frequency / (uint64_t)host_present_rate;
        uint32_t elapsed_source_vblanks =
            game_vblank_clock_advance(&vblank_clock, host_counter);

        for (uint32_t vblank_index = 0u; vblank_index < elapsed_source_vblanks;
             ++vblank_index) {
            const SceneSprite *source_weapon;
            uint8_t *weapon_slot;
            uint16_t timer1;

            ++source_vblanks;
            if (source_vblanks == 1u) {
                if (!game_input_set_raw_key(&game.input, LEVEL_DATA_SHOTGUN_RAW_KEY,
                                            1, error, sizeof(error))) {
                    fprintf(stderr, "could not press Shotgun selection key: %s\n", error);
                    goto cleanup;
                }
            } else if (source_vblanks == 2u) {
                if (!game_input_set_raw_key(&game.input, LEVEL_DATA_SHOTGUN_RAW_KEY,
                                            0, error, sizeof(error)) ||
                    !game_input_set_raw_key(
                        &game.input, game.controls.assigned_raw_keys[GAME_CONTROL_FIRE], 1,
                        error, sizeof(error))) {
                    fprintf(stderr, "could not fire Shotgun in present-rate regression: %s\n",
                            error);
                    goto cleanup;
                }
            } else if (source_vblanks == 3u &&
                       !game_input_set_raw_key(
                           &game.input, game.controls.assigned_raw_keys[GAME_CONTROL_FIRE], 0,
                           error, sizeof(error))) {
                fprintf(stderr, "could not release Shotgun fire key: %s\n", error);
                goto cleanup;
            } else if (source_vblanks == 60u) {
                if (!game_input_set_raw_key(
                        &game.input, LEVEL_DATA_ASSAULT_RIFLE_RAW_KEY,
                        1, error, sizeof(error))) {
                    fprintf(stderr, "could not press Assault Rifle selection key: %s\n", error);
                    goto cleanup;
                }
            } else if (source_vblanks == 61u) {
                if (!game_input_set_raw_key(
                        &game.input, LEVEL_DATA_ASSAULT_RIFLE_RAW_KEY,
                        0, error, sizeof(error)) ||
                    !game_input_set_raw_key(
                        &game.input, game.controls.assigned_raw_keys[GAME_CONTROL_FIRE], 1,
                        error, sizeof(error))) {
                    fprintf(stderr,
                            "could not fire Assault Rifle in present-rate regression: %s\n",
                            error);
                    goto cleanup;
                }
            } else if (source_vblanks == 65u &&
                       !game_input_set_raw_key(
                           &game.input, game.controls.assigned_raw_keys[GAME_CONTROL_FIRE], 0,
                           error, sizeof(error))) {
                fprintf(stderr, "could not release Assault Rifle fire key: %s\n", error);
                goto cleanup;
            }
            if (!scene_frame_clone(&previous_source, &source) ||
                !game_bootstrap_update_single_player_at_time(
                    &game, (uint64_t)source_vblanks * 20u, error, sizeof(error))) {
                fprintf(stderr, "Shotgun source VBlank update failed: %s\n", error);
                goto cleanup;
            }
            scene_frame_begin(&source);
            if (!game_bootstrap_submit_scene_frame(&game, &source) ||
                !object_runtime_get_slot_bytes(
                    &game.object_runtime, game.object_runtime.player1_slot + 2u,
                    &weapon_slot) ||
                !(source_weapon = level_data_find_player1_view_weapon(
                    &source, game.object_runtime.player1_slot))) {
                fprintf(stderr, "Shotgun companion is missing from source scene\n");
                goto cleanup;
            }
            timer1 = read_be16(weapon_slot + LEVEL_DATA_VIEW_WEAPON_TIMER1_OFFSET);
            if (source_weapon->presentation != SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON ||
                source_weapon->source != SCENE_SPRITE_SOURCE_VECTOR_MODEL) {
                fprintf(stderr, "Shotgun companion lost its source view-weapon identity\n");
                goto cleanup;
            }
            /*
             * The fire write is an immediate animation restart at VBlank 2.
             * Its selected display pose must then survive three more source
             * ticks and advance on the fourth, independently of present rate.
             */
            if (source_vblanks == 2u) {
                fired_pose_asset_id = source_weapon->source_asset_id;
                fired_pose_frame_index = source_weapon->frame_index;
                fired_pose_timer1 = timer1;
                fired_pose_captured = UINT8_MAX;
                if (game.view_weapon_animation_runtime.held_ticks != 0u) {
                    fprintf(stderr, "Shotgun 4x animation clock did not restart on fire\n");
                    goto cleanup;
                }
            } else if (source_vblanks >= 3u && source_vblanks <= 5u) {
                if (fired_pose_captured == 0u ||
                    source_weapon->source_asset_id != fired_pose_asset_id ||
                    source_weapon->frame_index != fired_pose_frame_index ||
                    timer1 != fired_pose_timer1 ||
                    game.view_weapon_animation_runtime.held_ticks !=
                        (uint8_t)(source_vblanks - 2u)) {
                    fprintf(stderr,
                            "Shotgun authored pose was not held for four source ticks\n");
                    goto cleanup;
                }
            } else if (source_vblanks == 6u) {
                if (fired_pose_captured == 0u ||
                    game.view_weapon_animation_runtime.held_ticks != 0u ||
                    timer1 == fired_pose_timer1) {
                    fprintf(stderr,
                            "Shotgun authored pose did not advance at 4x duration\n");
                    goto cleanup;
                }
                saw_four_tick_pose_advance = UINT8_MAX;
            } else if (source_vblanks == 61u) {
                assault_rifle_asset_id = source_weapon->source_asset_id;
                assault_rifle_frame_index = source_weapon->frame_index;
                assault_rifle_timer1 = timer1;
                assault_rifle_pose_captured = UINT8_MAX;
                if (game.player.tmp_gun_selected != LEVEL_DATA_ASSAULT_RIFLE_GUN_INDEX ||
                    game.view_weapon_animation_runtime.initialized != 0u) {
                    fprintf(stderr,
                            "Assault Rifle incorrectly retained the 4x animation clock\n");
                    goto cleanup;
                }
            } else if (source_vblanks == 62u) {
                if (assault_rifle_pose_captured == 0u ||
                    game.player.tmp_gun_selected != LEVEL_DATA_ASSAULT_RIFLE_GUN_INDEX ||
                    game.view_weapon_animation_runtime.initialized != 0u ||
                    source_weapon->source_asset_id != assault_rifle_asset_id ||
                    source_weapon->frame_index == assault_rifle_frame_index ||
                    timer1 == assault_rifle_timer1) {
                    fprintf(stderr,
                            "Assault Rifle action pose did not advance on the next source tick\n");
                    goto cleanup;
                }
                saw_assault_rifle_per_tick_advance = UINT8_MAX;
            }
            if (baseline_captured == 0u) {
                baseline_asset_id = source_weapon->source_asset_id;
                baseline_frame_index = source_weapon->frame_index;
                baseline_timer1 = timer1;
                baseline_captured = UINT8_MAX;
            } else if (source_weapon->source_asset_id != baseline_asset_id ||
                       source_weapon->frame_index != baseline_frame_index ||
                       timer1 != baseline_timer1) {
                saw_shotgun_action = UINT8_MAX;
            }
            source_trace = level_data_hash_view_weapon_source_tick(
                source_trace, source_weapon, timer1);
        }
        if (source_vblanks != 0u) {
            const SceneSprite *previous_weapon;
            const SceneSprite *source_weapon;
            const SceneSprite *presentation_weapon;

            if (!scene_frame_interpolate(
                    &presentation, &previous_source, &source,
                    game_vblank_clock_interpolation_alpha(&vblank_clock)) ||
                !(source_weapon = level_data_find_player1_view_weapon(
                    &source, game.object_runtime.player1_slot)) ||
                !(presentation_weapon = level_data_find_player1_view_weapon(
                    &presentation, game.object_runtime.player1_slot)) ||
                presentation_weapon->source_asset_id != source_weapon->source_asset_id ||
                presentation_weapon->frame_index != source_weapon->frame_index) {
                fprintf(stderr,
                        "Shotgun interpolation changed a source animation endpoint "
                        "(source asset=%u frame=%u, presentation asset=%u frame=%u)\n",
                        source_weapon ? source_weapon->source_asset_id : 0u,
                        source_weapon ? source_weapon->frame_index : 0u,
                        presentation_weapon ? presentation_weapon->source_asset_id : 0u,
                        presentation_weapon ? presentation_weapon->frame_index : 0u);
                goto cleanup;
            }
            previous_weapon = level_data_find_player1_view_weapon(
                &previous_source, game.object_runtime.player1_slot);
            if (previous_weapon &&
                previous_weapon->source == SCENE_SPRITE_SOURCE_VECTOR_MODEL &&
                source_weapon->source == SCENE_SPRITE_SOURCE_VECTOR_MODEL &&
                previous_weapon->source_asset_id == source_weapon->source_asset_id &&
                previous_weapon->source_bytes == source_weapon->source_bytes &&
                previous_weapon->source_byte_count == source_weapon->source_byte_count &&
                previous_weapon->frame_index != source_weapon->frame_index) {
                if (presentation_weapon->presentation_interpolate_vector_frame == 0u ||
                    presentation_weapon->presentation_previous_frame_index !=
                        previous_weapon->frame_index ||
                    presentation_weapon->presentation_frame_interpolation_alpha !=
                        game_vblank_clock_interpolation_alpha(&vblank_clock)) {
                    fprintf(stderr,
                            "Shotgun presentation did not retain its two source animation frames\n");
                    goto cleanup;
                }
                saw_shotgun_frame_blend = UINT8_MAX;
            }
        }
    }
    if (source_vblanks != 100u || vblank_clock.remainder_counter_units != 0u ||
        baseline_captured == 0u || saw_shotgun_action == 0u ||
        saw_shotgun_frame_blend == 0u || saw_four_tick_pose_advance == 0u ||
        saw_assault_rifle_per_tick_advance == 0u) {
        fprintf(stderr,
                "Shotgun animation did not complete its fixed 50 Hz source trace\n");
        goto cleanup;
    }
    if (game.session.player1_inventory.ammunition[shotgun_shoot.bullet_type] !=
        (uint16_t)(1000u - shotgun_shoot.bullet_count)) {
        fprintf(stderr, "Shotgun present-rate regression did not fire exactly once\n");
        goto cleanup;
    }
    if (game.session.player1_inventory.ammunition[assault_rifle_shoot.bullet_type] !=
        (uint16_t)(1000u - assault_rifle_shoot.bullet_count * 2u)) {
        fprintf(stderr, "Assault Rifle present-rate regression did not fire exactly twice\n");
        goto cleanup;
    }
    *out_source_trace = source_trace;
    succeeded = UINT8_MAX;

cleanup:
    scene_frame_destroy(&presentation);
    scene_frame_destroy(&source);
    scene_frame_destroy(&previous_source);
    game_bootstrap_destroy(&game);
    return succeeded != 0u;
}

static int level_data_verify_shotgun_animation_present_rates(const char *data_root)
{
    static const uint32_t host_present_rates[] = {60u, 120u, 144u, 240u};
    uint64_t expected_source_trace = 0u;

    for (size_t rate_index = 0u;
         rate_index < sizeof(host_present_rates) / sizeof(host_present_rates[0u]);
         ++rate_index) {
        uint64_t source_trace = 0u;

        if (!level_data_run_shotgun_animation_present_rate(
                data_root, host_present_rates[rate_index], &source_trace)) {
            return 0;
        }
        if (rate_index == 0u) {
            expected_source_trace = source_trace;
        } else if (source_trace != expected_source_trace) {
            fprintf(stderr,
                    "Shotgun animation trace differs at %u Hz presentation\n",
                    host_present_rates[rate_index]);
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
    ObjectHandlerAlienContext object_handler_context;
    GameLink game_link;
    AssetBlob game_link_blob = {0};
    const uint8_t *table_bytes;
    const uint8_t *shoot_definition_bytes;
    const uint8_t *floor_data_bytes;
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
    size_t floor_data_size;
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
    uint16_t floor_data_index;
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
    GameFloorData floor_data;
    uint16_t ambient_sample_index;
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
    uint32_t mechanism_surface_fixture_count = 0u;
    uint32_t dynamic_wall_v_scale_fixture_count = 0u;
    uint32_t lift_wall_motion_fixture_count = 0u;
    uint8_t mechanism_audio_fixture_mask = 0u;
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

    /* objdrawhires.s's three packed WAD-column extraction paths. */
    {
        uint16_t full_span = 0u;

        if (!bitmap_source_expand_half_span(32u, &full_span) || full_span != 64u ||
            !bitmap_source_expand_half_span(UINT16_MAX / 2u, &full_span) ||
            full_span != UINT16_MAX - 1u ||
            bitmap_source_expand_half_span(0u, &full_span) ||
            bitmap_source_expand_half_span(UINT16_MAX / 2u + 1u, &full_span) ||
            bitmap_source_expand_half_span(1u, NULL)) {
            fprintf(stderr, "source bitmap half-span expansion is inconsistent\n");
            return 1;
        }
    }
    if (bitmap_source_decode_packed_texel(UINT16_C(0x53c7), 0u) != 7u ||
        bitmap_source_decode_packed_texel(UINT16_C(0x53c7), 1u) != 30u ||
        bitmap_source_decode_packed_texel(UINT16_C(0x53c7), 2u) != 20u) {
        fprintf(stderr, "source packed bitmap-column decoding is inconsistent\n");
        return 1;
    }

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
    {
        GameVBlankClock vblank_clock = {0};
        uint32_t source_vblanks = 0u;

        const uint64_t host_counter_frequency = UINT64_C(1000000);

        /* A 144 Hz presenter must not advance hires.s logic at 144 Hz. */
        game_vblank_clock_reset(&vblank_clock, 0u, host_counter_frequency);
        for (uint64_t present_index = 1u; present_index <= 144u; ++present_index) {
            uint64_t host_counter =
                present_index * host_counter_frequency / UINT64_C(144);

            source_vblanks += game_vblank_clock_advance(&vblank_clock, host_counter);
        }
        if (source_vblanks != 50u || vblank_clock.remainder_counter_units != 0u ||
            game_vblank_clock_advance(&vblank_clock, host_counter_frequency - 1u) != 0u ||
            vblank_clock.remainder_counter_units != 0u) {
            fprintf(stderr, "hires.s VBlank desktop timing boundary is inconsistent\n");
            return 1;
        }
        game_vblank_clock_reset(&vblank_clock, 0u, host_counter_frequency);
        if (game_vblank_clock_advance(&vblank_clock, host_counter_frequency) != 10u ||
            vblank_clock.remainder_counter_units != 0u) {
            fprintf(stderr, "desktop VBlank catch-up cap is inconsistent\n");
            return 1;
        }
        game_vblank_clock_reset(&vblank_clock, 0u, host_counter_frequency);
        if (game_vblank_clock_advance(&vblank_clock, 7000u) != 0u ||
            game_vblank_clock_interpolation_alpha(&vblank_clock) < 0.349f ||
            game_vblank_clock_interpolation_alpha(&vblank_clock) > 0.351f) {
            fprintf(stderr, "desktop VBlank presentation interpolation alpha is inconsistent\n");
            return 1;
        }
    }
    if (!level_data_verify_shotgun_animation_present_rates(argv[1])) {
        return 1;
    }
    {
        SceneFrame previous = {0};
        SceneFrame current = {0};
        SceneFrame presentation = {0};
        SceneVertex previous_vertices[3] = {
            {{0, 0, 0}, 0, 0, 100},
            {{10, 0, 0}, 64, 0, 120},
            {{0, 0, 10}, 0, 64, 140}
        };
        SceneVertex current_vertices[3] = {
            {{20, 20, 20}, 20, 10, 300},
            {{30, 20, 20}, 84, 10, 320},
            {{20, 20, 30}, 20, 74, 340}
        };
        SceneCommand previous_camera = {0};
        SceneCommand current_camera = {0};
        SceneCommand previous_geometry = {0};
        SceneCommand current_geometry = {0};
        SceneCommand previous_sprite = {0};
        SceneCommand current_sprite = {0};
        SceneMeshSurface previous_surface[1] = {{0}};
        SceneMeshSurface current_surface[1] = {{0}};

        previous_camera.type = SCENE_COMMAND_CAMERA;
        previous_camera.data.camera.position = (SceneWorldPoint){0, 0, 0};
        previous_camera.data.camera.source_position_x_16_16 = 16384;
        previous_camera.data.camera.source_position_z_16_16 = 32768;
        previous_camera.data.camera.yaw = 8180u;
        previous_camera.data.camera.look_offset = -20;
        previous_camera.data.camera.has_source_position_16_16 = UINT8_MAX;
        current_camera.type = SCENE_COMMAND_CAMERA;
        current_camera.data.camera.position = (SceneWorldPoint){20, 40, 60};
        current_camera.data.camera.source_position_x_16_16 = 20 * 65536 + 49152;
        current_camera.data.camera.source_position_z_16_16 = 60 * 65536 + 32768;
        current_camera.data.camera.yaw = 8u;
        current_camera.data.camera.look_offset = 20;
        current_camera.data.camera.has_source_position_16_16 = UINT8_MAX;
        previous_surface[0].geometry.vertices = previous_vertices;
        previous_surface[0].geometry.vertex_count = 3u;
        previous_surface[0].geometry.topology = SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST;
        previous_surface[0].geometry.primitive = SCENE_GEOMETRY_PRIMITIVE_FLOOR;
        previous_surface[0].geometry.source_record_id = 42u;
        previous_geometry.type = SCENE_COMMAND_GEOMETRY_INSTANCE;
        previous_geometry.data.geometry_instance.source_instance_id = 42u;
        previous_geometry.data.geometry_instance.mesh.source_mesh_id = 42u;
        previous_geometry.data.geometry_instance.mesh.acceleration_class =
            SCENE_ACCELERATION_CLASS_DYNAMIC;
        previous_geometry.data.geometry_instance.mesh.surfaces = previous_surface;
        previous_geometry.data.geometry_instance.mesh.surface_count = 1u;
        current_geometry = previous_geometry;
        current_surface[0] = previous_surface[0];
        current_surface[0].geometry.vertices = current_vertices;
        current_geometry.data.geometry_instance.mesh.surfaces = current_surface;
        previous_sprite.type = SCENE_COMMAND_SPRITE_INSTANCE;
        previous_sprite.data.sprite_instance.source_mesh_id = 7u;
        previous_sprite.data.sprite_instance.acceleration_class = SCENE_ACCELERATION_CLASS_DYNAMIC;
        previous_sprite.data.sprite_instance.sprite.source_record_id = 7u;
        previous_sprite.data.sprite_instance.sprite.presentation =
            SCENE_SPRITE_PRESENTATION_WORLD_OBJECT;
        previous_sprite.data.sprite_instance.sprite.position = (SceneWorldPoint){0, 0, 0};
        previous_sprite.data.sprite_instance.sprite.yaw = 8180u;
        previous_sprite.data.sprite_instance.sprite.source_brightness = 100u;
        previous_sprite.data.sprite_instance.sprite.source_light_level = 100;
        previous_sprite.data.sprite_instance.sprite.source_bitmap_angle_brightness[0u] = 20;
        previous_sprite.data.sprite_instance.sprite.source_point_and_polygon_brightness[0u] = -20;
        current_sprite = previous_sprite;
        current_sprite.data.sprite_instance.sprite.position = (SceneWorldPoint){20, 40, 60};
        current_sprite.data.sprite_instance.sprite.yaw = 8u;
        current_sprite.data.sprite_instance.sprite.source_brightness = 300u;
        current_sprite.data.sprite_instance.sprite.source_light_level = 300;
        current_sprite.data.sprite_instance.sprite.source_bitmap_angle_brightness[0u] = 60;
        current_sprite.data.sprite_instance.sprite.source_point_and_polygon_brightness[0u] = 20;

        if (!scene_frame_init(&previous, 3u) || !scene_frame_init(&current, 3u) ||
            !scene_frame_init(&presentation, 3u) ||
            !scene_frame_submit(&previous, &previous_camera) ||
            !scene_frame_submit(&previous, &previous_geometry) ||
            !scene_frame_submit(&previous, &previous_sprite) ||
            !scene_frame_submit(&current, &current_camera) ||
            !scene_frame_submit(&current, &current_geometry) ||
            !scene_frame_submit(&current, &current_sprite) ||
            !scene_frame_interpolate(&presentation, &previous, &current, 0.5f) ||
            presentation.count != 3u ||
            presentation.commands[0u].data.camera.position.x != 10 ||
            presentation.commands[0u].data.camera.position.y != 20 ||
            presentation.commands[0u].data.camera.position.z != 30 ||
            presentation.commands[0u].data.camera.source_position_x_16_16 !=
                10 * 65536 + 32768 ||
            presentation.commands[0u].data.camera.source_position_z_16_16 !=
                30 * 65536 + 32768 ||
            presentation.commands[0u].data.camera.has_source_position_16_16 == 0u ||
            presentation.commands[0u].data.camera.yaw != 8190u ||
            presentation.commands[0u].data.camera.look_offset != 0 ||
            presentation.commands[1u].data.geometry_instance.mesh.surfaces[0u].geometry.vertices ==
                current_vertices ||
            presentation.commands[1u].data.geometry_instance.mesh.surfaces[0u].geometry.vertices[0u].position.x != 10 ||
            presentation.commands[1u].data.geometry_instance.mesh.surfaces[0u].geometry.vertices[0u].position.y != 10 ||
            presentation.commands[1u].data.geometry_instance.mesh.surfaces[0u].geometry.vertices[0u].source_light_level != 200 ||
            presentation.commands[2u].data.sprite_instance.sprite.position.x != 10 ||
            presentation.commands[2u].data.sprite_instance.sprite.position.y != 20 ||
            presentation.commands[2u].data.sprite_instance.sprite.yaw != 8190u ||
            presentation.commands[2u].data.sprite_instance.sprite.source_brightness != 200u ||
            presentation.commands[2u].data.sprite_instance.sprite.source_light_level != 200 ||
            presentation.commands[2u].data.sprite_instance.sprite.source_bitmap_angle_brightness[0u] != 40 ||
            presentation.commands[2u].data.sprite_instance.sprite.source_point_and_polygon_brightness[0u] != 0) {
            fprintf(stderr, "source scene presentation interpolation is inconsistent\n");
            scene_frame_destroy(&presentation);
            scene_frame_destroy(&current);
            scene_frame_destroy(&previous);
            return 1;
        }
        current_vertices[0u].position.x = 999;
        if (presentation.commands[1u].data.geometry_instance.mesh.surfaces[0u].geometry.vertices[0u].position.x != 10) {
            fprintf(stderr, "source scene interpolation did not retain its geometry endpoint\n");
            scene_frame_destroy(&presentation);
            scene_frame_destroy(&current);
            scene_frame_destroy(&previous);
            return 1;
        }
        /*
         * modules/ai.s:ai_DoWalkAnim changes an alien's selected ObjT
         * graphics resource as its animation/facing changes. Its source slot
         * remains the entity identity: retain the current resource while
         * blending the slot transform, rather than snapping an animated enemy
         * to every new source-art endpoint.
         */
        current_sprite.data.sprite_instance.source_mesh_id = 99u;
        current_sprite.data.sprite_instance.sprite.source_asset_id = 99u;
        current_sprite.data.sprite_instance.sprite.frame_index = 3u;
        current_sprite.data.sprite_instance.sprite.position = (SceneWorldPoint){40, 60, 80};
        current.commands[2u] = current_sprite;
        if (!scene_frame_interpolate(&presentation, &previous, &current, 0.5f) ||
            presentation.commands[2u].data.sprite_instance.source_mesh_id != 99u ||
            presentation.commands[2u].data.sprite_instance.sprite.source_asset_id != 99u ||
            presentation.commands[2u].data.sprite_instance.sprite.frame_index != 3u ||
            presentation.commands[2u].data.sprite_instance.sprite.position.x != 20 ||
            presentation.commands[2u].data.sprite_instance.sprite.position.y != 30 ||
            presentation.commands[2u].data.sprite_instance.sprite.position.z != 40) {
            fprintf(stderr,
                    "animated source sprite did not retain its slot interpolation identity\n");
            scene_frame_destroy(&presentation);
            scene_frame_destroy(&current);
            scene_frame_destroy(&previous);
            return 1;
        }
        {
            static const uint8_t shotgun_vector_model[] = {0u};
            const SceneSprite *presentation_weapon;

            /*
             * newaliencontrol.s:ACTANIMOBJ changes the PLAYER1 companion's
             * authored frame on a source VBlank.  Presentation must retain
             * both matching endpoints for the renderer to blend its vertices,
             * without replacing the current discrete source frame.
             */
            previous_sprite.data.sprite_instance.source_mesh_id = 4u;
            previous_sprite.data.sprite_instance.sprite.source_record_id = 7u;
            previous_sprite.data.sprite_instance.sprite.source =
                SCENE_SPRITE_SOURCE_VECTOR_MODEL;
            previous_sprite.data.sprite_instance.sprite.presentation =
                SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON;
            previous_sprite.data.sprite_instance.sprite.source_asset_id = 4u;
            previous_sprite.data.sprite_instance.sprite.source_bytes = shotgun_vector_model;
            previous_sprite.data.sprite_instance.sprite.source_byte_count =
                sizeof(shotgun_vector_model);
            previous_sprite.data.sprite_instance.sprite.frame_index = 1u;
            current_sprite = previous_sprite;
            current_sprite.data.sprite_instance.sprite.frame_index = 2u;
            previous.commands[2u] = previous_sprite;
            current.commands[2u] = current_sprite;
            if (!scene_frame_interpolate(&presentation, &previous, &current, 0.25f) ||
                !(presentation_weapon = &presentation.commands[2u].data.sprite_instance.sprite) ||
                presentation_weapon->frame_index != 2u ||
                presentation_weapon->presentation_previous_frame_index != 1u ||
                presentation_weapon->presentation_frame_interpolation_alpha != 0.25f ||
                presentation_weapon->presentation_interpolate_vector_frame == 0u) {
                fprintf(stderr,
                        "view-weapon source-frame presentation interpolation is inconsistent\n");
                scene_frame_destroy(&presentation);
                scene_frame_destroy(&current);
                scene_frame_destroy(&previous);
                return 1;
            }
            current_sprite.data.sprite_instance.sprite.source_asset_id = 5u;
            current.commands[2u] = current_sprite;
            if (!scene_frame_interpolate(&presentation, &previous, &current, 0.25f) ||
                presentation.commands[2u].data.sprite_instance.sprite
                    .presentation_interpolate_vector_frame != 0u) {
                fprintf(stderr,
                        "view-weapon interpolation crossed a source-model boundary\n");
                scene_frame_destroy(&presentation);
                scene_frame_destroy(&current);
                scene_frame_destroy(&previous);
                return 1;
            }
        }
        /* `firefive` owns a player projectile's first movement vector. The
         * scene must retain that raw source data while leaving its completed
         * source position available to ordinary frame interpolation. */
        current_sprite = previous_sprite;
        current_sprite.data.sprite_instance.sprite.source_record_id = 99u;
        current_sprite.data.sprite_instance.sprite.presentation =
            SCENE_SPRITE_PRESENTATION_WORLD_OBJECT;
        current_sprite.data.sprite_instance.sprite.flags = SCENE_SPRITE_FLAG_PROJECTILE;
        current_sprite.data.sprite_instance.sprite.presentation_anchor_to_player_weapon = UINT8_MAX;
        current_sprite.data.sprite_instance.sprite.presentation_projectile_velocity_x_16_16 =
            INT32_C(0x00100000);
        current_sprite.data.sprite_instance.sprite.presentation_projectile_velocity_z_16_16 =
            INT32_C(0x00200000);
        current_sprite.data.sprite_instance.sprite.presentation_projectile_velocity_y = -256;
        current_sprite.data.sprite_instance.sprite.position = (SceneWorldPoint){40, 60, 80};
        current.commands[2u] = current_sprite;
        if (!scene_frame_interpolate(&presentation, &previous, &current, 0.25f) ||
            presentation.commands[2u].data.sprite_instance.sprite.position.x != 40 ||
            presentation.commands[2u].data.sprite_instance.sprite.position.y != 60 ||
            presentation.commands[2u].data.sprite_instance.sprite.position.z != 80 ||
            presentation.commands[2u].data.sprite_instance.sprite
                .presentation_anchor_to_player_weapon == 0u ||
            presentation.commands[2u].data.sprite_instance.sprite
                .presentation_projectile_velocity_x_16_16 != INT32_C(0x00100000) ||
            presentation.commands[2u].data.sprite_instance.sprite
                .presentation_projectile_velocity_z_16_16 != INT32_C(0x00200000) ||
            presentation.commands[2u].data.sprite_instance.sprite
                .presentation_projectile_velocity_y != -256) {
            fprintf(stderr, "player projectile muzzle presentation state is inconsistent\n");
            scene_frame_destroy(&presentation);
            scene_frame_destroy(&current);
            scene_frame_destroy(&previous);
            return 1;
        }
        current_sprite.data.sprite_instance.sprite.flags |= SCENE_SPRITE_FLAG_PROJECTILE_CONTACT;
        current_sprite.data.sprite_instance.sprite.presentation_anchor_to_player_weapon = 0u;
        current.commands[2u] = current_sprite;
        if (!scene_frame_interpolate(&presentation, &previous, &current, 0.25f) ||
            presentation.commands[2u].data.sprite_instance.sprite
                .presentation_anchor_to_player_weapon != 0u) {
            fprintf(stderr, "projectile contact incorrectly retained a weapon muzzle handoff\n");
            scene_frame_destroy(&presentation);
            scene_frame_destroy(&current);
            scene_frame_destroy(&previous);
            return 1;
        }
        scene_frame_destroy(&presentation);
        scene_frame_destroy(&current);
        scene_frame_destroy(&previous);
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
    {
        /* c/message.c:Msg_Init/Msg_PushLine in the source small-screen mode. */
        uint8_t level_messages[MESSAGE_RUNTIME_LEVEL_MESSAGE_COUNT *
                               MESSAGE_RUNTIME_LEVEL_MESSAGE_LENGTH] = {0};
        uint8_t glyph_spacing[MESSAGE_RUNTIME_GLYPH_SPACING_BYTE_COUNT];
        static const uint8_t text_line[] = {'H', 'U', 'D', '!', 0u, 'X'};
        MessageRuntime messages;
        SceneFrame message_frame;

        error[0] = '\0';
        memset(glyph_spacing, 0x10, sizeof(glyph_spacing));
        memcpy(level_messages + MESSAGE_RUNTIME_LEVEL_MESSAGE_LENGTH,
               "ONE  SPACE", sizeof("ONE  SPACE"));
        if (!message_runtime_init(&messages, level_messages, sizeof(level_messages),
                                  glyph_spacing, sizeof(glyph_spacing), error, sizeof(error)) ||
            level_messages[0u] != 0u ||
            memcmp(level_messages + MESSAGE_RUNTIME_LEVEL_MESSAGE_LENGTH,
                   "ONE SPACE", sizeof("ONE SPACE")) != 0 ||
            !message_runtime_push_line(
                &messages, text_line,
                (uint16_t)(sizeof(text_line) |
                           (MESSAGE_RUNTIME_TAG_OPTIONS << MESSAGE_RUNTIME_TAG_SHIFT)),
                0u, error, sizeof(error)) ||
            message_runtime_visible_line_count(&messages) != 0u ||
            !message_runtime_push_line(
                &messages, text_line,
                (uint16_t)(sizeof(text_line) |
                           (MESSAGE_RUNTIME_TAG_OPTIONS << MESSAGE_RUNTIME_TAG_SHIFT)),
                UINT8_MAX, error, sizeof(error)) ||
            message_runtime_visible_line_count(&messages) != 1u ||
            !scene_frame_init(&message_frame, 1u) ||
            !message_runtime_submit_hud(&messages, &message_frame) ||
            message_frame.count != 1u ||
            message_frame.commands[0u].type != SCENE_COMMAND_HUD_TEXT ||
            (const void *)message_frame.commands[0u].data.hud_text.text ==
                (const void *)text_line ||
            memcmp(message_frame.commands[0u].data.hud_text.text,
                   text_line, 4u) != 0 ||
            message_frame.commands[0u].data.hud_text.text_byte_count != 4u ||
            message_frame.commands[0u].data.hud_text.x != 0 ||
            message_frame.commands[0u].data.hud_text.y != 0 ||
            message_frame.commands[0u].data.hud_text.reference_width != 320u ||
            message_frame.commands[0u].data.hud_text.reference_height != 256u ||
            message_frame.commands[0u].data.hud_text.font !=
                SCENE_HUD_FONT_FIRST_PORT_ASCII ||
            message_frame.commands[0u].data.hud_text.layout !=
                SCENE_HUD_LAYOUT_TOP_CENTER ||
            message_frame.commands[0u].data.hud_text.style_id != MESSAGE_RUNTIME_TAG_OPTIONS) {
            fprintf(stderr, "c/message.c source line-ring handoff is inconsistent: %s\n", error);
            return 1;
        }
        scene_frame_destroy(&message_frame);
        if (!message_runtime_push_line_dedup_last(
                &messages, text_line,
                (uint16_t)(sizeof(text_line) |
                           (MESSAGE_RUNTIME_TAG_OPTIONS << MESSAGE_RUNTIME_TAG_SHIFT)),
                UINT8_MAX, 100u, error, sizeof(error)) ||
            messages.line_number != 1u || messages.last_message != text_line ||
            messages.next_duplicate_time_milliseconds !=
                100u + MESSAGE_RUNTIME_DEDUPLICATION_PERIOD_MILLISECONDS ||
            !message_runtime_push_line_dedup_last(
                &messages, text_line,
                (uint16_t)(sizeof(text_line) |
                           (MESSAGE_RUNTIME_TAG_OPTIONS << MESSAGE_RUNTIME_TAG_SHIFT)),
                UINT8_MAX, 101u, error, sizeof(error)) ||
            messages.line_number != 1u ||
            !message_runtime_push_line_dedup_last(
                &messages, text_line,
                (uint16_t)(sizeof(text_line) |
                           (MESSAGE_RUNTIME_TAG_OPTIONS << MESSAGE_RUNTIME_TAG_SHIFT)),
                UINT8_MAX,
                100u + MESSAGE_RUNTIME_DEDUPLICATION_PERIOD_MILLISECONDS,
                error, sizeof(error)) ||
            messages.line_number != 2u) {
            fprintf(stderr, "c/message.c source deduplication timing is inconsistent: %s\n",
                    error);
            return 1;
        }
        if (!message_runtime_init(&messages, level_messages, sizeof(level_messages),
                                  glyph_spacing, sizeof(glyph_spacing), error, sizeof(error)) ||
            !message_runtime_tick(&messages, 0u, 0u, error, sizeof(error)) ||
            messages.line_number != MESSAGE_RUNTIME_LINE_COUNT - 1u ||
            messages.next_scroll_time_milliseconds != 0u ||
            !message_runtime_tick(&messages, UINT8_MAX, 0u, error, sizeof(error)) ||
            messages.line_number != 0u ||
            messages.next_scroll_time_milliseconds !=
                MESSAGE_RUNTIME_SCROLL_PERIOD_MILLISECONDS ||
            !message_runtime_push_line(
                &messages, text_line,
                (uint16_t)(sizeof(text_line) |
                           (MESSAGE_RUNTIME_TAG_NARRATIVE << MESSAGE_RUNTIME_TAG_SHIFT)),
                UINT8_MAX, error, sizeof(error)) ||
            messages.line_number != 1u ||
            message_runtime_visible_line_count(&messages) != 1u ||
            !message_runtime_tick(
                &messages, UINT8_MAX,
                MESSAGE_RUNTIME_SCROLL_PERIOD_MILLISECONDS - 1u,
                error, sizeof(error)) ||
            messages.line_number != 1u) {
            fprintf(stderr, "c/message.c disabled/initial Msg_Tick timing is inconsistent: %s\n",
                    error);
            return 1;
        }
        for (uint64_t tick_time = MESSAGE_RUNTIME_SCROLL_PERIOD_MILLISECONDS;
             tick_time <= MESSAGE_RUNTIME_SCROLL_PERIOD_MILLISECONDS * 5u;
             tick_time += MESSAGE_RUNTIME_SCROLL_PERIOD_MILLISECONDS) {
            if (!message_runtime_tick(&messages, UINT8_MAX, tick_time,
                                      error, sizeof(error)) ||
                (tick_time < MESSAGE_RUNTIME_SCROLL_PERIOD_MILLISECONDS * 5u &&
                 message_runtime_visible_line_count(&messages) != 1u)) {
                fprintf(stderr, "c/message.c Msg_Tick scroll progression is inconsistent: %s\n",
                        error);
                return 1;
            }
        }
        if (messages.line_number != 1u ||
            message_runtime_visible_line_count(&messages) != 0u ||
            messages.next_scroll_time_milliseconds !=
                MESSAGE_RUNTIME_SCROLL_PERIOD_MILLISECONDS * 6u) {
            fprintf(stderr, "c/message.c Msg_Tick did not expire its oldest line\n");
            return 1;
        }
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
    /* demolevels/level_a is the authored bundle staged at levels/level_a. */
    if (level.zone_count == 0 || level.point_count == 0 ||
        level.player1_start_x != -808 || level.player1_start_z != 184 ||
        level.player1_start_zone != 3u || level.player1_start_zone >= level.zone_count) {
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
    memset(&command, 0, sizeof(command));
    command.type = SCENE_COMMAND_HUD_TEXT;
    if (!scene_hud_text_set(&command.data.hud_text, "test", 4u)) {
        fprintf(stderr, "retained scene HUD text initialization failed\n");
        scene_frame_destroy(&frame);
        asset_blob_release(&graphics_data);
        asset_blob_release(&level_data);
        return 1;
    }
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
        !game_link_table(&game_link, GAME_LINK_TABLE_FLOOR_DATA,
                         &floor_data_bytes, &floor_data_size) ||
        floor_data_size != (size_t)GAME_LINK_FLOOR_DATA_COUNT * 4u ||
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
    {
        static const uint8_t csfx_sample[] = {
            'C', 'S', 'F', 'X', 0u, 0u, 0u, 5u, 0u, 0xc3u, 0x0fu
        };
        static const uint8_t clipped_csfx_sample[] = {
            'C', 'S', 'F', 'X', 0u, 0u, 0u, 3u, 120u, 0xffu
        };
        static const uint8_t expected_csfx_sample[] = {0u, 5u, 253u, 219u, 240u};
        static const uint8_t expected_clipped_sample[] = {63u, 192u, 192u};
        AssetBlob decoded_sample;

        if (!asset_io_decode_csfx(csfx_sample, sizeof(csfx_sample), &decoded_sample,
                                  error, sizeof(error)) ||
            decoded_sample.size != sizeof(expected_csfx_sample) ||
            memcmp(decoded_sample.bytes, expected_csfx_sample,
                   sizeof(expected_csfx_sample)) != 0) {
            fprintf(stderr, "source CSFX Fibonacci decode is inconsistent: %s\n", error);
            asset_blob_release(&decoded_sample);
            asset_blob_release(&game_link_blob);
            return 1;
        }
        asset_blob_release(&decoded_sample);
        if (!asset_io_decode_csfx(clipped_csfx_sample, sizeof(clipped_csfx_sample),
                                  &decoded_sample, error, sizeof(error)) ||
            decoded_sample.size != sizeof(expected_clipped_sample) ||
            memcmp(decoded_sample.bytes, expected_clipped_sample,
                   sizeof(expected_clipped_sample)) != 0) {
            fprintf(stderr, "source CSFX clipping or bounds are inconsistent: %s\n", error);
            asset_blob_release(&decoded_sample);
            asset_blob_release(&game_link_blob);
            return 1;
        }
        asset_blob_release(&decoded_sample);
        if (asset_io_decode_csfx(csfx_sample, sizeof(csfx_sample) - 1u,
                                 &decoded_sample, error, sizeof(error))) {
            fprintf(stderr, "truncated source CSFX sample was accepted\n");
            asset_blob_release(&decoded_sample);
            asset_blob_release(&game_link_blob);
            return 1;
        }
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
    for (floor_data_index = 0u;
         floor_data_index < GAME_LINK_FLOOR_DATA_COUNT;
         ++floor_data_index) {
        const uint8_t *source = floor_data_bytes + (size_t)floor_data_index * 4u;

        if (!game_link_get_floor_data(&game_link, floor_data_index, &floor_data,
                                      error, sizeof(error)) ||
            floor_data.damage != read_be16(source) ||
            floor_data.sound_effect != read_be16(source + 2u)) {
            fprintf(stderr, "GLFT floor data %u is inconsistent: %s\n",
                    floor_data_index, error);
            asset_blob_release(&game_link_blob);
            return 1;
        }
    }
    {
        PlayerHazardRuntime hazard_runtime;
        PlayerRuntime hazard_player = {0};
        LevelZone hazard_zone = {0};
        GameFloorData damaging_floor = {0};
        uint16_t damaging_floor_index = GAME_LINK_FLOOR_DATA_COUNT;
        uint8_t entity_damage = 250u;

        for (floor_data_index = 0u;
             floor_data_index < GAME_LINK_FLOOR_DATA_COUNT;
             ++floor_data_index) {
            if (!game_link_get_floor_data(&game_link, floor_data_index, &floor_data,
                                          error, sizeof(error))) {
                fprintf(stderr, "could not find source hazardous floor data: %s\n", error);
                asset_blob_release(&game_link_blob);
                return 1;
            }
            if ((uint8_t)floor_data.damage != 0u) {
                damaging_floor = floor_data;
                damaging_floor_index = floor_data_index;
                break;
            }
        }
        if (damaging_floor_index == GAME_LINK_FLOOR_DATA_COUNT) {
            fprintf(stderr, "GLFT has no hazardous floor fixture\n");
            asset_blob_release(&game_link_blob);
            return 1;
        }
        hazard_zone.floor_noise = damaging_floor_index;
        hazard_zone.water = 2000;
        hazard_player.snap_y = 1000;
        hazard_player.snap_target_y = 2000;
        player_hazard_runtime_init(&hazard_runtime);
        if (hazard_runtime.time_to_damage != 100) {
            fprintf(stderr, "hires.s hazardous-floor initial cadence is inconsistent\n");
            asset_blob_release(&game_link_blob);
            return 1;
        }
        /* Airborne above both liquid and floor: timer runs, damage does not. */
        if (!player_hazard_runtime_update(
                &hazard_runtime, 1u, &hazard_player, &hazard_zone, &game_link,
                &entity_damage, error, sizeof(error)) ||
            hazard_runtime.time_to_damage != 99 || entity_damage != 250u) {
            fprintf(stderr, "hires.s hazardous-floor airborne gate is inconsistent: %s\n",
                    error);
            asset_blob_release(&game_link_blob);
            return 1;
        }
        /* The source timer must not test contact again until its 100th VBlank. */
        hazard_player.snap_target_y = hazard_player.snap_y;
        if (!player_hazard_runtime_update(
                &hazard_runtime, 98u, &hazard_player, &hazard_zone, &game_link,
                &entity_damage, error, sizeof(error)) ||
            hazard_runtime.time_to_damage != 1 || entity_damage != 250u ||
            !player_hazard_runtime_update(
                &hazard_runtime, 1u, &hazard_player, &hazard_zone, &game_link,
                &entity_damage, error, sizeof(error)) ||
            hazard_runtime.time_to_damage != 100 ||
            entity_damage !=
                (uint8_t)(250u + (uint8_t)damaging_floor.damage)) {
            fprintf(stderr, "hires.s hazardous-floor contact damage is inconsistent: %s\n",
                    error);
            asset_blob_release(&game_link_blob);
            return 1;
        }
        /* Water below source Y is the maintained toxic-liquid contact branch. */
        player_hazard_runtime_init(&hazard_runtime);
        hazard_zone.water = 500;
        hazard_player.snap_target_y = 2000;
        entity_damage = 0u;
        if (!player_hazard_runtime_update(
                &hazard_runtime, 100u, &hazard_player, &hazard_zone, &game_link,
                &entity_damage, error, sizeof(error)) ||
            entity_damage != (uint8_t)damaging_floor.damage) {
            fprintf(stderr, "hires.s hazardous-liquid damage is inconsistent: %s\n", error);
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
        game_link_get_floor_data(&game_link, GAME_LINK_FLOOR_DATA_COUNT,
                                 &floor_data, error, sizeof(error)) ||
        game_link_get_ambient_sfx(&game_link, GAME_LINK_AMBIENT_SFX_COUNT,
                                  &ambient_sample_index, error, sizeof(error)) ||
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
    object_handler_context.animation_runtime = &game.object_animation_runtime;
    object_handler_context.lighting_runtime = &game.lighting_runtime;
    object_handler_context.navigation = &game.level_navigation;
    object_handler_context.clips = &game.level_clips;
    object_handler_context.progression = &game.progression;
    object_handler_context.explosion_runtime = &game.object_explosion_runtime;
    object_handler_context.math = &game.math;
    object_handler_context.random = &game.random;
    object_handler_context.observation = &game.object_observation;
    object_handler_context.dispatch_workspace = &game.alien_dispatch_workspace;
    object_handler_context.messages = &game.message_runtime;
    object_handler_context.preferences = &game.preferences;
    object_handler_context.audio_events = &game.audio_events;
    object_handler_context.message_time_milliseconds = 0u;
    if (game.random.state != 234u) {
        fprintf(stderr, "Game_Start source random seed is inconsistent\n");
        game_bootstrap_destroy(&game);
        return 1;
    }
    {
        GameBackgroundAudioRuntime background_runtime;
        GameRandom background_random;
        GameAudioEvents background_events;
        LevelZone background_zone = {0};
        uint16_t first_sample;
        uint16_t second_sample;

        background_zone.background_sfx_mask = UINT16_C(1) << 5u;
        /* BACKSFX's alternating +2-byte mask reads DrawBackdrop/Echo. */
        background_zone.draw_backdrop = 0u;
        background_zone.echo = 4u;
        game_background_audio_runtime_init(&background_runtime);
        game_random_init(&background_random);
        game_audio_events_init(&background_events);
        if (!game_link_get_ambient_sfx(&game.game_link_catalog, 5u, &first_sample,
                                       error, sizeof(error)) ||
            !game_link_get_ambient_sfx(&game.game_link_catalog, 2u, &second_sample,
                                       error, sizeof(error)) ||
            !game_background_audio_update(
                &background_runtime, 1u, &background_zone, &game.game_link_catalog,
                &background_random, &background_events, error, sizeof(error)) ||
            background_runtime.time_to_noise != 182 || background_runtime.odd_even != 2u ||
            background_random.state != UINT16_C(0xe226) || background_events.count != 1u ||
            background_events.events[0u].sample_index != first_sample ||
            background_events.events[0u].volume != 38u ||
            background_events.events[0u].world_x != 0 ||
            background_events.events[0u].world_z != 0 ||
            background_events.events[0u].listener_relative == 0u ||
            background_events.events[0u].source_id != UINT16_C(0xfff0) ||
            background_events.events[0u].suppress_if_playing != UINT8_MAX ||
            background_events.events[0u].echo != 0u) {
            fprintf(stderr, "newanims.s BACKSFX first-mask event is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        game_audio_events_begin(&background_events);
        if (!game_background_audio_update(
                &background_runtime, 181u, &background_zone, &game.game_link_catalog,
                &background_random, &background_events, error, sizeof(error)) ||
            background_runtime.time_to_noise != 1 || background_events.count != 0u ||
            background_random.state != UINT16_C(0xe226)) {
            fprintf(stderr, "newanims.s BACKSFX countdown is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if (!game_background_audio_update(
                &background_runtime, 1u, &background_zone, &game.game_link_catalog,
                &background_random, &background_events, error, sizeof(error)) ||
            background_runtime.time_to_noise != 115 || background_runtime.odd_even != 0u ||
            background_random.state != UINT16_C(0x5be9) || background_events.count != 1u ||
            background_events.events[0u].sample_index != second_sample ||
            background_events.events[0u].volume != 41u) {
            fprintf(stderr, "newanims.s BACKSFX alternate-mask event is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    if (game.shared_resources.main_palette.size != 1536u ||
        game.shared_resources.floor_texture.size != 65536u ||
        game.shared_resources.texture_maps.size != 131072u ||
        game.shared_resources.texture_palette.size != 16384u ||
        game.shared_resources.bitmap_light_curve.size != 16u * 7u * 16u ||
        game.shared_resources.object_count != 14u ||
        game.shared_resources.vector_count != 22u ||
        game.shared_resources.wall_texture_count != 13u ||
        /* Res_LoadSoundFx scans 59 slots and skips 13 empty entries. */
        game.shared_resources.sound_effect_count != 46u ||
        game.shared_resources.sound_effects[0u].size < 4u ||
        memcmp(game.shared_resources.sound_effects[0u].bytes, "CSFX", 4u) == 0 ||
        game.shared_resources.backdrop_image.size != 648u * 240u ||
        game.shared_resources.water_frames.size != 256u * 256u) {
        fprintf(stderr,
                "source-defined shared resources are inconsistent "
                "(mainpal=%zu floor=%zu maps=%zu palette=%zu objects=%u vectors=%u walls=%u sfx=%u backdrop=%zu water=%zu)\n",
                game.shared_resources.main_palette.size,
                game.shared_resources.floor_texture.size,
                game.shared_resources.texture_maps.size,
                game.shared_resources.texture_palette.size,
                game.shared_resources.object_count, game.shared_resources.vector_count,
                game.shared_resources.wall_texture_count, game.shared_resources.sound_effect_count,
                game.shared_resources.backdrop_image.size,
                game.shared_resources.water_frames.size);
        game_bootstrap_destroy(&game);
        return 1;
    }
    {
        int32_t mantis_projectile_adjustment;

        /*
         * Level P's type-16 Mantis uses vector asset 14 and the authored
         * ItsAnAlien SHOTYOFF of -300 level units.  Its frame-zero top is
         * y=-559 model units, so the quarter-scale GPU presentation closes
         * the 160.25-unit visual gap without changing the ShotT trajectory.
         */
        if (game.shared_resources.vector_count <= 14u ||
            !source_vector_model_projectile_y_adjustment(
                game.shared_resources.vector_models[14u].bytes,
                game.shared_resources.vector_models[14u].size,
                0u, -300 * 128, &mantis_projectile_adjustment) ||
            mantis_projectile_adjustment != 20512) {
            fprintf(stderr, "Mantis rocket/model launch attachment is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
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
        uint32_t dynamic_door_fixture_wall_offset = UINT32_MAX;
        uint32_t dynamic_door_fixture_plane_offset = UINT32_MAX;
        int32_t dynamic_door_fixture_boundary = 0;
        uint32_t dynamic_lift_fixture_wall_offset = UINT32_MAX;
        uint32_t dynamic_lift_fixture_plane_offset = UINT32_MAX;
        int32_t dynamic_lift_fixture_boundary = 0;

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
            game.player_hazard_runtime.time_to_damage != 100 ||
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
        if (level_index == 2u) {
            /*
             * LEVEL_C Zone 70 is the first authored two-storey bridge: its
             * upper floor at zero joins the ordinary zero-height room in
             * Zone 69. Exercise Plr1_Control's complete MoveObject handoff at
             * that real edge so the layer flag and upper-floor snap target
             * cannot regress independently of the synthetic wall fixtures.
             */
            enum {
                LEVEL_C_BRIDGE_ENTRY_ZONE = 69u,
                LEVEL_C_BRIDGE_ZONE = 70u,
                LEVEL_C_PLAYER_HEIGHT = 12 * 1024
            };
            LevelDynamicState bridge_state = {0};
            LevelZone bridge_zone;
            PlayerRuntime bridge_player = {0};
            GameInput bridge_input;
            uint16_t bridge_step;

            game_input_init(&bridge_input);
            bridge_player.x = player_runtime_world_to_position(3376);
            bridge_player.z = player_runtime_world_to_position(-2288);
            bridge_player.snap_x = player_runtime_world_to_position(3472);
            bridge_player.snap_z = player_runtime_world_to_position(-2384);
            bridge_player.y = -LEVEL_C_PLAYER_HEIGHT;
            bridge_player.snap_y = bridge_player.y;
            bridge_player.snap_target_y = bridge_player.y;
            bridge_player.height = LEVEL_C_PLAYER_HEIGHT;
            bridge_player.snap_height = LEVEL_C_PLAYER_HEIGHT;
            bridge_player.snap_target_height = LEVEL_C_PLAYER_HEIGHT;
            bridge_player.snap_squished_height = LEVEL_C_PLAYER_HEIGHT;
            bridge_player.zone_index = LEVEL_C_BRIDGE_ENTRY_ZONE;
            bridge_player.health = 100u;
            if (!level_dynamic_state_init(&bridge_state, &game.dynamic_level.runtime,
                                          error, sizeof(error)) ||
                !level_runtime_get_zone(&bridge_state.runtime, LEVEL_C_BRIDGE_ZONE,
                                        &bridge_zone, error, sizeof(error)) ||
                bridge_zone.floor != 36864 || bridge_zone.roof != 4096 ||
                bridge_zone.upper_floor != 0 || bridge_zone.upper_roof != -32768 ||
                !player_runtime_update_spatial(
                    &bridge_player, &bridge_input, &control_defaults,
                    &game.preferences, &game.math, &bridge_state.runtime,
                    &bridge_state, error, sizeof(error)) ||
                bridge_player.zone_index != LEVEL_C_BRIDGE_ZONE ||
                bridge_player.stood_in_top == 0u ||
                bridge_player.snap_target_y !=
                    bridge_zone.upper_floor - LEVEL_C_PLAYER_HEIGHT ||
                player_runtime_position_to_world(bridge_player.x) != 3472 ||
                player_runtime_position_to_world(bridge_player.z) != -2384) {
                fprintf(stderr,
                        "LEVEL_C player bridge/upper-room collision is inconsistent: "
                        "zone=%u top=%u target_y=%d x=%d z=%d: %s\n",
                        bridge_player.zone_index, bridge_player.stood_in_top,
                        bridge_player.snap_target_y,
                        player_runtime_position_to_world(bridge_player.x),
                        player_runtime_position_to_world(bridge_player.z), error);
                level_dynamic_state_destroy(&bridge_state);
                game_bootstrap_destroy(&game);
                return 1;
            }
            for (bridge_step = 1u; bridge_step <= 31u; ++bridge_step) {
                int16_t expected_x = (int16_t)(3472 + (int16_t)bridge_step * 16);
                int16_t expected_z = (int16_t)(-2384 - (int16_t)bridge_step * 16);

                bridge_player.snap_x = player_runtime_world_to_position(expected_x);
                bridge_player.snap_z = player_runtime_world_to_position(expected_z);
                if (!player_runtime_update_spatial(
                        &bridge_player, &bridge_input, &control_defaults,
                        &game.preferences, &game.math, &bridge_state.runtime,
                        &bridge_state, error, sizeof(error)) ||
                    bridge_player.zone_index !=
                        (bridge_step <= 29u ? LEVEL_C_BRIDGE_ZONE : 163u) ||
                    (bridge_player.stood_in_top != 0u) != (bridge_step <= 29u) ||
                    bridge_player.snap_y != -LEVEL_C_PLAYER_HEIGHT ||
                    bridge_player.snap_target_y != -LEVEL_C_PLAYER_HEIGHT) {
                    fprintf(stderr,
                            "LEVEL_C player lost bridge support on step %u: "
                            "zone=%u top=%u y=%d target=%d x=%d z=%d: %s\n",
                            bridge_step, bridge_player.zone_index,
                            bridge_player.stood_in_top, bridge_player.snap_y,
                            bridge_player.snap_target_y,
                            player_runtime_position_to_world(bridge_player.x),
                            player_runtime_position_to_world(bridge_player.z), error);
                    level_dynamic_state_destroy(&bridge_state);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
            level_dynamic_state_destroy(&bridge_state);
        }
        {
        size_t geometry_instance_count = 0u;
        size_t first_sprite_command = SIZE_MAX;

        if (!lighting_runtime_refresh_single_player(
                &game.lighting_runtime, &game.dynamic_level.runtime, &game.player,
                error, sizeof(error)) ||
            !lighting_runtime_refresh_all_zones(
                &game.lighting_runtime, &game.dynamic_level.runtime, error, sizeof(error)) ||
            !scene_frame_init(&frame, 1u) ||
            !object_scene_count_active(&game.object_runtime, &active_sprite_count,
                                       error, sizeof(error)) ||
            !game_bootstrap_submit_scene_frame(&game, &frame) ||
            !scene_frame_find_instance_layout(&frame, &geometry_instance_count,
                                              &first_sprite_command) ||
            frame.count != 5u + geometry_instance_count + active_sprite_count ||
            first_sprite_command == SIZE_MAX ||
            !scene_sprite_commands_match_source(
                &frame, first_sprite_command, &game, active_sprite_count, error, sizeof(error)) ||
            frame.commands[0u].type != SCENE_COMMAND_CAMERA ||
            frame.commands[1u].type != SCENE_COMMAND_LIGHTING ||
            frame.commands[2u].type != SCENE_COMMAND_ENVIRONMENT ||
            frame.commands[frame.count - 2u].type != SCENE_COMMAND_HUD_TEXT ||
            frame.commands[frame.count - 2u].data.hud_text.font !=
                SCENE_HUD_FONT_FIRST_PORT_HEALTH_DIGITS ||
            frame.commands[frame.count - 2u].data.hud_text.layout !=
                SCENE_HUD_LAYOUT_FIRST_PORT_HEALTH ||
            frame.commands[frame.count - 1u].type != SCENE_COMMAND_HUD_TEXT ||
            frame.commands[frame.count - 1u].data.hud_text.font !=
                SCENE_HUD_FONT_FIRST_PORT_AMMO_DIGITS ||
            frame.commands[frame.count - 1u].data.hud_text.layout !=
                SCENE_HUD_LAYOUT_FIRST_PORT_AMMUNITION) {
            fprintf(stderr, "campaign level %u source-object scene handoff is invalid: %s\n",
                    level_index, error);
            scene_frame_destroy(&frame);
            game_bootstrap_destroy(&game);
            return 1;
        }
        {
            int16_t minimum_source_light = INT16_MAX;
            int16_t maximum_source_light = INT16_MIN;
            uint32_t light_vertex_count = 0u;
            uint32_t floor_ceiling_vertex_count = 0u;

            for (uint16_t zone_index = 0u;
                 zone_index < game.dynamic_level.runtime.zone_count; ++zone_index) {
                for (uint16_t point_index = 0u;
                     point_index < LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT; ++point_index) {
                    if (game.lighting_runtime.current_point_brightness[zone_index][point_index] == 0) {
                        fprintf(stderr,
                                "campaign level %u complete-scene lighting left zone %u point %u stale\n",
                                level_index, zone_index, point_index);
                        scene_frame_destroy(&frame);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                }
            }

            for (uint32_t flat_index = 0u; flat_index < game.static_scene.flat_count;
                 ++flat_index) {
                const LevelStaticFlatScene *flat = &game.static_scene.flats[flat_index];

                for (uint32_t vertex_index = 0u; vertex_index < flat->vertex_count;
                     ++vertex_index) {
                    int16_t expected_light;

                    if (!scene_flat_expected_light(&game, flat, vertex_index, &expected_light) ||
                        flat->vertices[vertex_index].source_light_level != expected_light) {
                        fprintf(stderr,
                                "campaign level %u flat %u vertex %u lighting handoff is inconsistent\n",
                                level_index, flat_index, vertex_index);
                        scene_frame_destroy(&frame);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    if (flat->primitive != SCENE_GEOMETRY_PRIMITIVE_WATER) {
                        ++floor_ceiling_vertex_count;
                    }
                }
            }
            if (floor_ceiling_vertex_count == 0u) {
                fprintf(stderr, "campaign level %u has no source-lit floors or ceilings\n",
                        level_index);
                scene_frame_destroy(&frame);
                game_bootstrap_destroy(&game);
                return 1;
            }

            for (size_t command_index = 0u; command_index < frame.count; ++command_index) {
                const SceneCommand *command = &frame.commands[command_index];

                if (command->type != SCENE_COMMAND_GEOMETRY_INSTANCE) {
                    continue;
                }
                for (uint32_t surface_index = 0u;
                     surface_index < command->data.geometry_instance.mesh.surface_count;
                     ++surface_index) {
                    const SceneGeometry *geometry =
                        &command->data.geometry_instance.mesh.surfaces[surface_index].geometry;

                    for (uint32_t vertex_index = 0u; vertex_index < geometry->vertex_count;
                         ++vertex_index) {
                        int16_t source_light = geometry->vertices[vertex_index].source_light_level;

                        if (source_light < minimum_source_light) {
                            minimum_source_light = source_light;
                        }
                        if (source_light > maximum_source_light) {
                            maximum_source_light = source_light;
                        }
                        ++light_vertex_count;
                    }
                }
            }
            if (light_vertex_count == 0u || minimum_source_light == maximum_source_light) {
                fprintf(stderr,
                        "campaign level %u scene lighting has no source-driven variation\n",
                        level_index);
                scene_frame_destroy(&frame);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        {
            LightingRuntime prior_lighting = game.lighting_runtime;
            LevelStaticFlatScene *flat_to_light = NULL;
            uint32_t vertex_to_light = 0u;
            uint32_t component_index;
            uint32_t point_index;

            /*
             * `lighting_runtime_advance_animation` is separately verified
             * against newanims.s:brightanim. This scene handoff check supplies
             * the live CurrentPointBrights word that hires.s:goursides reads
             * through draw_zone_graph.s:itsafloor, including NEG.W handling.
             */
            for (uint32_t flat_index = 0u; flat_index < game.static_scene.flat_count;
                 ++flat_index) {
                LevelStaticFlatScene *flat = &game.static_scene.flats[flat_index];

                if (flat->primitive != SCENE_GEOMETRY_PRIMITIVE_WATER &&
                    flat->vertex_count != 0u) {
                    flat_to_light = flat;
                    break;
                }
            }
            if (!flat_to_light) {
                fprintf(stderr, "campaign level %u has no flat-light scene candidate\n", level_index);
                scene_frame_destroy(&frame);
                game_bootstrap_destroy(&game);
                return 1;
            }
            component_index = (flat_to_light->source_upper_zone != 0u ? 2u : 0u) +
                (flat_to_light->primitive == SCENE_GEOMETRY_PRIMITIVE_CEILING ? 1u : 0u);
            point_index = (uint32_t)flat_to_light->point_brightness_selectors[vertex_to_light] *
                4u + component_index;
            game.lighting_runtime.current_point_brightness[flat_to_light->source_zone_index]
                                                        [point_index] = -345;
            scene_frame_begin(&frame);
            if (!game_bootstrap_submit_scene_frame(&game, &frame) ||
                flat_to_light->vertices[vertex_to_light].source_light_level != 345) {
                fprintf(stderr,
                        "campaign level %u animated flat lighting did not reach the scene\n",
                        level_index);
                scene_frame_destroy(&frame);
                game_bootstrap_destroy(&game);
                return 1;
            }
            game.lighting_runtime = prior_lighting;
            scene_frame_begin(&frame);
            if (!game_bootstrap_submit_scene_frame(&game, &frame)) {
                fprintf(stderr, "campaign level %u source light restoration failed\n", level_index);
                scene_frame_destroy(&frame);
                game_bootstrap_destroy(&game);
                return 1;
            }
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
            uint16_t weapon_source_asset_id;
            int32_t weapon_height;
            int32_t weapon_bobble;
            GameObjectDefinition gun_definition;
            GameObjectAnimationFrame gun_frame;
            SceneFrame weapon_scene = {0};
            uint8_t saw_weapon = 0u;
            uint8_t weapon_lighting_changed = 0u;
            uint8_t weapon_scene_source = UINT8_MAX;
            uint8_t weapon_scene_presentation = UINT8_MAX;
            uint32_t weapon_scene_asset_id = UINT32_MAX;
            uint16_t weapon_scene_yaw = 0u;
            int32_t weapon_scene_y = 0;
            SceneViewWeaponProjection weapon_projection = {0};
            int8_t weapon_source_light[16u * 16u];
            int16_t saved_weapon_zone_lights[LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT];

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

            game.player.reset_weapon_animation = UINT8_MAX;
            if (!player_entity_sync_single_player(&game.object_runtime,
                                                  &game.dynamic_level.runtime,
                                                  &game.game_link_catalog, &game.player,
                                                  &game.session.player1_inventory, &game.random,
                                                  &game.audio_events,
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
                read_be16(weapon_slot + 34u) != 0u || game.player.reset_weapon_animation != 0u ||
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
            /*
             * hires.s:Plr1_Use republishes the ENT_NEXT_2 companion after
             * startup.  ItsAnObject then supplies the selected model fields,
             * so verify the frame produced by a complete direct-play tick.
             */
            if (!game_link_get_object_definition(&game.game_link_catalog, gun_object_type,
                                                 &gun_definition, error, sizeof(error)) ||
                !game_link_get_object_animation_frame(
                    &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_ACTION,
                    gun_object_type, read_be16(weapon_slot + 34u), &gun_frame, error,
                    sizeof(error)) ||
                gun_definition.graphics_type != 1u ||
                !object_handler_apply_active_object_animation_slot(
                    &game.object_runtime, game.object_runtime.player1_slot + 2u,
                    &game.game_link_catalog, error, sizeof(error)) ||
                !scene_frame_init(&weapon_scene, 1u) ||
                !game_bootstrap_submit_scene_frame(&game, &weapon_scene)) {
                fprintf(stderr, "campaign level %u weapon scene submission failed\n",
                        level_index);
                scene_frame_destroy(&weapon_scene);
                game_bootstrap_destroy(&game);
                return 1;
            }
            weapon_source_asset_id = gun_frame.byte_0;
            for (size_t command_index = 0u; command_index < weapon_scene.count;
                 ++command_index) {
                const SceneCommand *weapon_command = &weapon_scene.commands[command_index];

                if (weapon_command->type == SCENE_COMMAND_SPRITE_INSTANCE &&
                    weapon_command->data.sprite_instance.sprite.source_record_id ==
                        game.object_runtime.player1_slot + 2u) {
                    weapon_scene_source = weapon_command->data.sprite_instance.sprite.source;
                    weapon_scene_presentation = weapon_command->data.sprite_instance.sprite.presentation;
                    weapon_scene_asset_id = weapon_command->data.sprite_instance.sprite.source_asset_id;
                    weapon_scene_yaw = weapon_command->data.sprite_instance.sprite.yaw;
                    weapon_scene_y = weapon_command->data.sprite_instance.sprite.position.y;
                    weapon_projection =
                        weapon_command->data.sprite_instance.sprite.view_weapon_projection;
                    saw_weapon = weapon_command->data.sprite_instance.sprite.presentation ==
                        SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON &&
                        weapon_command->data.sprite_instance.sprite.source ==
                            SCENE_SPRITE_SOURCE_VECTOR_MODEL &&
                        weapon_command->data.sprite_instance.sprite.source_asset_id == weapon_source_asset_id;
                    memcpy(weapon_source_light,
                           weapon_command->data.sprite_instance.sprite.source_point_and_polygon_brightness,
                           sizeof(weapon_source_light));
                    break;
                }
            }
            if (saw_weapon == 0u) {
                fprintf(stderr,
                        "campaign level %u live companion weapon is missing from scene "
                        "(source=%u presentation=%u asset=%u expected vector asset=%u)\n",
                        level_index, weapon_scene_source, weapon_scene_presentation,
                        weapon_scene_asset_id, weapon_source_asset_id);
                scene_frame_destroy(&weapon_scene);
                game_bootstrap_destroy(&game);
                return 1;
            }
            {
                uint16_t relative_yaw = game_math_wrap_angle_address(
                    (uint16_t)(weapon_scene_yaw - UINT16_C(2048) - game.player.yaw));
                int16_t expected_sine;
                int16_t expected_cosine;
                int32_t relative_y = (int32_t)((uint32_t)weapon_scene_y -
                                                (uint32_t)game.player.y);
                int32_t expected_y_offset = (int32_t)((uint32_t)relative_y +
                                                       (uint32_t)relative_y);

                if (!game_math_sine(&game.math, relative_yaw, &expected_sine,
                                    error, sizeof(error)) ||
                    !game_math_cosine(&game.math, relative_yaw, &expected_cosine,
                                      error, sizeof(error)) ||
                    weapon_projection.sine != expected_sine ||
                    weapon_projection.cosine != expected_cosine ||
                    weapon_projection.y_offset != expected_y_offset ||
                    weapon_projection.depth_bias != 3 ||
                    weapon_projection.centre_x != 160u ||
                    weapon_projection.centre_y != 120u ||
                    weapon_projection.scale_numerator != 5u ||
                    weapon_projection.scale_denominator != 3u) {
                    fprintf(stderr,
                            "campaign level %u source view-weapon projection is inconsistent: %s\n",
                            level_index, error);
                    scene_frame_destroy(&weapon_scene);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
            /*
             * objdrawhires.s:draw_CalcBrightRings must reach the camera-space
             * ENT_NEXT_2 companion: changing its live source-zone samples
             * changes the unprojected vector-light field submitted to the GPU.
             */
            memcpy(saved_weapon_zone_lights,
                   game.lighting_runtime.current_point_brightness[game.player.zone_index],
                   sizeof(saved_weapon_zone_lights));
            for (uint16_t point_index = 0u;
                 point_index < LEVEL_RUNTIME_POINT_BRIGHTNESS_COUNT; ++point_index) {
                game.lighting_runtime.current_point_brightness[game.player.zone_index]
                                                               [point_index] = -345;
            }
            scene_frame_begin(&weapon_scene);
            if (!game_bootstrap_submit_scene_frame(&game, &weapon_scene)) {
                fprintf(stderr, "campaign level %u companion-light scene submission failed\n",
                        level_index);
                scene_frame_destroy(&weapon_scene);
                game_bootstrap_destroy(&game);
                return 1;
            }
            for (size_t command_index = 0u; command_index < weapon_scene.count;
                 ++command_index) {
                const SceneCommand *weapon_command = &weapon_scene.commands[command_index];

                if (weapon_command->type == SCENE_COMMAND_SPRITE_INSTANCE &&
                    weapon_command->data.sprite_instance.sprite.source_record_id ==
                        game.object_runtime.player1_slot + 2u) {
                    weapon_lighting_changed = memcmp(
                        weapon_source_light,
                        weapon_command->data.sprite_instance.sprite.source_point_and_polygon_brightness,
                        sizeof(weapon_source_light)) != 0;
                    break;
                }
            }
            memcpy(game.lighting_runtime.current_point_brightness[game.player.zone_index],
                   saved_weapon_zone_lights, sizeof(saved_weapon_zone_lights));
            if (weapon_lighting_changed == 0u) {
                fprintf(stderr,
                        "campaign level %u source room lighting did not reach companion weapon\n",
                        level_index);
                scene_frame_destroy(&weapon_scene);
                game_bootstrap_destroy(&game);
                return 1;
            }
            {
                PlayerRuntime dead_player = game.player;
                GameInventory dead_inventory = game.session.player1_inventory;
                GameRandom dead_random = game.random;
                GameAudioEvents dead_audio;

                dead_player.health = 0u;
                dead_inventory.health = 0u;
                game_audio_events_init(&dead_audio);
                if (!player_entity_sync_single_player(
                        &game.object_runtime, &game.dynamic_level.runtime,
                        &game.game_link_catalog, &dead_player, &dead_inventory,
                        &dead_random, &dead_audio, error, sizeof(error)) ||
                    !object_runtime_get_slot_bytes(
                        &game.object_runtime, game.object_runtime.player1_slot + 2u,
                        &weapon_slot) ||
                    read_be16(weapon_slot + 12u) != UINT16_MAX) {
                    fprintf(stderr,
                            "campaign level %u dead Plr1_Use retained its companion weapon\n",
                            level_index);
                    scene_frame_destroy(&weapon_scene);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
            game.preferences.show_weapon = UINT8_MAX;
            scene_frame_begin(&weapon_scene);
            if (!game_bootstrap_submit_scene_frame(&game, &weapon_scene)) {
                fprintf(stderr, "campaign level %u hidden weapon scene submission failed\n",
                        level_index);
                scene_frame_destroy(&weapon_scene);
                game_bootstrap_destroy(&game);
                return 1;
            }
            for (size_t command_index = 0u; command_index < weapon_scene.count;
                 ++command_index) {
                const SceneCommand *weapon_command = &weapon_scene.commands[command_index];

                if (weapon_command->type == SCENE_COMMAND_SPRITE_INSTANCE &&
                    weapon_command->data.sprite_instance.sprite.source_record_id ==
                        game.object_runtime.player1_slot + 2u) {
                    fprintf(stderr,
                            "campaign level %u source show-weapon preference did not hide companion\n",
                            level_index);
                    scene_frame_destroy(&weapon_scene);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
            game.preferences.show_weapon = 0u;
            scene_frame_destroy(&weapon_scene);
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
                GameInventory activatable_expected_inventory;
                GameInventory activatable_grant;
                MessageRuntime activatable_messages = game.message_runtime;
                GameAudioEvents activatable_audio;
                LevelZone activatable_zone;
                uint16_t activatable_point_index = read_be16(activatable_slot + 0u);
                uint8_t activatable_collection_slot[OBJECT_RUNTIME_SLOT_BYTE_COUNT];
                uint8_t activatable_collected = 0u;

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
                memcpy(activatable_collection_slot, activatable_slot,
                       sizeof(activatable_collection_slot));
                /* A held lock makes Plr1_CollectItem take its unconditional success path. */
                write_be32(activatable_collection_slot + 50u, 1u);
                activatable_expected_inventory = activatable_inventory;
                game_audio_events_init(&activatable_audio);
                activatable_audio.source_sample_index = 23;
                activatable_audio.source_id_register = UINT16_C(0x4567);
                activatable_messages.redraw_count = 0u;
                if (!game_link_get_object_inventory_grant(
                        &game.game_link_catalog, activatable_slot[54u],
                        &activatable_grant, error, sizeof(error)) ||
                    !object_collectables_collect_item_single_player(
                        &game.level_runtime, &game.game_link_catalog,
                        &activatable_definition, activatable_collection_slot,
                        activatable_point, activatable_point_index,
                        &game.object_observation,
                        &activatable_inventory, &game.inventory_limits,
                        &activatable_messages, UINT8_MAX, 0u, &activatable_audio,
                        &activatable_collected, error, sizeof(error))) {
                    fprintf(stderr,
                            "campaign level %u activatable Plr1_CollectItem failed: %s\n",
                            level_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                game_inventory_apply_grant(&activatable_expected_inventory,
                                           &activatable_grant,
                                           &game.inventory_limits);
                if (activatable_collected != UINT8_MAX ||
                    memcmp(&activatable_inventory, &activatable_expected_inventory,
                           sizeof(activatable_inventory)) != 0 ||
                    activatable_messages.redraw_count == 0u ||
                    (activatable_definition.sound_effect >= 0 &&
                     (activatable_audio.source_sample_index !=
                          activatable_definition.sound_effect ||
                      activatable_audio.source_id_register != activatable_point_index ||
                      activatable_audio.count != 1u ||
                      activatable_audio.events[0].sample_index !=
                          (uint16_t)activatable_definition.sound_effect ||
                      activatable_audio.events[0].volume != 80u ||
                      activatable_audio.events[0].world_x !=
                          game.object_observation.rotated_x[activatable_point_index] ||
                      activatable_audio.events[0].world_z !=
                          game.object_observation.rotated_z[activatable_point_index] ||
                      activatable_audio.events[0].listener_relative == 0u ||
                      activatable_audio.events[0].source_id !=
                          activatable_point_index)) ||
                    (activatable_definition.sound_effect < 0 &&
                     (activatable_audio.count != 0u ||
                      activatable_audio.source_sample_index != 23 ||
                      activatable_audio.source_id_register != UINT16_C(0x4567)))) {
                    fprintf(stderr,
                            "campaign level %u activatable collection side effects are inconsistent\n",
                            level_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                activatable_inventory = game.session.player1_inventory;
                write_be16(activatable_slot + 12u, activatable_player.zone_index);
                write_be16(activatable_slot + 34u, 0u);
                write_be16(activatable_slot + 40u, 0u);
                activatable_slot[55u] = 0u;
                activatable_slot[62u] = 0x80u;
                activatable_slot[63u] = activatable_player.stood_in_top;
                activatable_player.tmp_x = player_runtime_world_to_position(
                    (int16_t)read_be16(activatable_point + 0u));
                activatable_player.tmp_z = player_runtime_world_to_position(
                    (int16_t)read_be16(activatable_point + 4u));
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
                        &game.game_link_catalog, &object_handler_context, &activatable_player,
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
                            &game.game_link_catalog, &object_handler_context, &game.player,
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
                    uint8_t destruction_message_line;

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
                    write_be16(passive_slot + 24u, 0u);
                    passive_slot[18u] = 1u;
                    passive_slot[19u] = (uint8_t)passive_definition.hit_points;
                    passive_slot[62u] = 0x80u;
                    game.preferences.show_messages = UINT8_MAX;
                    destruction_message_line = game.message_runtime.fullscreen != 0u ?
                        (uint8_t)((game.message_runtime.line_number + 1u) &
                                  (MESSAGE_RUNTIME_LINE_COUNT - 1u)) :
                        (game.message_runtime.line_number < MESSAGE_RUNTIME_MAX_LINES_SMALL ?
                            (uint8_t)(game.message_runtime.line_number + 1u) : 0u);
                    passive_height = passive_definition.floor_ceiling == 0u ?
                        (passive_slot[63u] != 0u ? passive_zone.upper_floor : passive_zone.floor) :
                        (passive_slot[63u] != 0u ? passive_zone.upper_roof : passive_zone.roof);
                    passive_height = source_asr32_7(passive_height) +
                        (int16_t)passive_frame.signed_byte_4 * 2;
                    if (!object_handler_update_single_player(
                            &game.object_runtime, &game.dynamic_level,
                            &game.mechanism_runtime,
                            &game.alien_runtime,
                            &game.game_link_catalog, &object_handler_context, &game.player,
                            &game.session.player1_inventory, &game.inventory_limits, 1u,
                            NULL, error, sizeof(error)) ||
                        passive_slot[18u] != 0u ||
                        read_be16(passive_slot + 4u) != (uint16_t)passive_height ||
                        read_be16(passive_slot + 34u) != passive_frame.next_timer1 ||
                        game.message_runtime.lines[destruction_message_line].text !=
                            game.dynamic_level.runtime.level_bytes ||
                        (game.message_runtime.lines[destruction_message_line].length_and_tag &
                         (uint16_t)~MESSAGE_RUNTIME_LENGTH_MASK) !=
                            (uint16_t)(MESSAGE_RUNTIME_TAG_NARRATIVE <<
                                       MESSAGE_RUNTIME_TAG_SHIFT)) {
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
            LevelEdge dynamic_door_edge;
            uint16_t door_index = 0u;
            GameAudioEvents door_audio;
            int16_t door_audio_x;
            int16_t door_audio_z;
            int16_t door_action_sample;

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
                    !level_runtime_get_edge(&game.dynamic_level.runtime,
                                            (uint16_t)door_wall.edge_index,
                                            &dynamic_door_edge, error, sizeof(error)) ||
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
                game_audio_events_init(&door_audio);
                door_action_sample = expected_velocity < 0 ?
                    door.opening_sound_fx : door.closing_sound_fx;
                if (!level_dynamic_state_set_edge_flags(
                        &game.dynamic_level, (uint16_t)door_wall.edge_index, activation_flags) ||
                    !mechanism_runtime_update_doors_single_player_with_audio(
                        &game.mechanism_runtime, &game.dynamic_level, &game.level_mechanisms,
                        &door_player, &game.math, 1u, &door_audio, error, sizeof(error)) ||
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
                if (door_action_sample > 0 && door_action_sample <= GAME_LINK_SFX_LOAD_COUNT) {
                    const GameAudioEvent *door_event = &door_audio.events[0u];

                    if (!source_liftable_audio_position(
                            &game.math, &door_player, &door, &door_audio_x, &door_audio_z,
                            error, sizeof(error)) ||
                        door_audio.count != 1u ||
                        door_event->sample_index != (uint16_t)(door_action_sample - 1) ||
                        door_event->volume != 50u || door_event->world_x != door_audio_x ||
                        door_event->world_z != door_audio_z ||
                        door_event->source_id != UINT16_C(0xfffd) ||
                        door_event->suppress_if_playing != 0u ||
                        door_event->channel_pick != 1u ||
                        door_event->listener_relative == 0u ||
                        door_event->echo != dynamic_door_zone.echo) {
                        fprintf(stderr,
                                "campaign level %u DoorRoutine audio handoff is inconsistent: %s\n",
                                level_index, error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    mechanism_audio_fixture_mask |= 1u;
                } else if (door_audio.count != 0u) {
                    fprintf(stderr,
                            "campaign level %u silent DoorRoutine emitted audio\n",
                            level_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                {
                    /*
                     * DoorRoutine changes the joined door zone's Roof_l in
                     * the same mutable level that Plr1_Control's MoveObject
                     * trace consumes.  Cross an authored door edge after
                     * writing its fully-open source position, rather than
                     * merely checking the animated graphics record.
                     */
                    LevelDynamicState door_collision_state = {0};
                    MechanismRuntime door_collision_mechanism;
                    PlayerRuntime collision_player = {0};
                    GameInput collision_input;
                    LevelZone collision_owner_zone;
                    LevelZone collision_door_zone;
                    uint8_t *collision_header;
                    uint16_t owner_zone_index = UINT16_MAX;
                    int16_t normal_x;
                    int16_t normal_z;
                    int16_t middle_x;
                    int16_t middle_z;

                    for (uint16_t zone_index = 0u;
                         zone_index < game.level_runtime.zone_count &&
                         owner_zone_index == UINT16_MAX;
                         ++zone_index) {
                        uint32_t edge_count;

                        if (!level_runtime_get_zone_edge_count(&game.level_runtime, zone_index,
                                                               &edge_count, error,
                                                               sizeof(error))) {
                            fprintf(stderr, "campaign level %u door owner lookup failed: %s\n",
                                    level_index, error);
                            game_bootstrap_destroy(&game);
                            return 1;
                        }
                        for (uint32_t edge_list_index = 0u;
                             edge_list_index < edge_count;
                             ++edge_list_index) {
                            uint32_t edge_index;

                            if (!level_runtime_get_zone_edge_index(
                                    &game.level_runtime, zone_index, edge_list_index,
                                    &edge_index, error, sizeof(error))) {
                                fprintf(stderr,
                                        "campaign level %u door owner lookup failed: %s\n",
                                        level_index, error);
                                game_bootstrap_destroy(&game);
                                return 1;
                            }
                            if (edge_index == (uint16_t)door_wall.edge_index) {
                                owner_zone_index = zone_index;
                                break;
                            }
                        }
                    }
                    if (owner_zone_index == UINT16_MAX ||
                        dynamic_door_edge.join_zone_id != door.zone_id ||
                        !level_dynamic_state_init(&door_collision_state, &game.level_runtime,
                                                  error, sizeof(error)) ||
                        !level_dynamic_state_get_graphics_range(
                            &door_collision_state, door.wall_data_offset - 36u, 36u,
                            &collision_header)) {
                        fprintf(stderr,
                                "campaign level %u door has no mutable collision path: %s\n",
                                level_index, error);
                        level_dynamic_state_destroy(&door_collision_state);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    write_be16(collision_header + 22u, (uint16_t)door.top);
                    write_be16(collision_header + 24u, 0u);
                    mechanism_runtime_init(&door_collision_mechanism);
                    if (!mechanism_runtime_update_doors_single_player(
                            &door_collision_mechanism, &door_collision_state,
                            &game.level_mechanisms, &door_player, 1u, error, sizeof(error)) ||
                        !level_runtime_get_zone(&door_collision_state.runtime, owner_zone_index,
                                                &collision_owner_zone, error, sizeof(error)) ||
                        !level_runtime_get_zone(&door_collision_state.runtime,
                                                (uint16_t)door.zone_id,
                                                &collision_door_zone, error, sizeof(error)) ||
                        collision_door_zone.roof !=
                            (int32_t)source_asr16_2(door.top) * 256) {
                        fprintf(stderr,
                                "campaign level %u DoorRoutine did not publish an open "
                                "collision zone: %s\n",
                                level_index, error);
                        level_dynamic_state_destroy(&door_collision_state);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    normal_x = dynamic_door_edge.z_length < 0 ? 16 :
                        dynamic_door_edge.z_length > 0 ? -16 : 0;
                    normal_z = dynamic_door_edge.x_length > 0 ? 16 :
                        dynamic_door_edge.x_length < 0 ? -16 : 0;
                    middle_x = (int16_t)((int32_t)dynamic_door_edge.x +
                                         dynamic_door_edge.x_length / 2);
                    middle_z = (int16_t)((int32_t)dynamic_door_edge.z +
                                         dynamic_door_edge.z_length / 2);
                    collision_player.x = player_runtime_world_to_position(
                        (int16_t)(middle_x - normal_x));
                    collision_player.z = player_runtime_world_to_position(
                        (int16_t)(middle_z - normal_z));
                    collision_player.snap_x = player_runtime_world_to_position(
                        (int16_t)(middle_x + normal_x));
                    collision_player.snap_z = player_runtime_world_to_position(
                        (int16_t)(middle_z + normal_z));
                    collision_player.height = 12 * 1024;
                    collision_player.snap_height = collision_player.height;
                    collision_player.snap_target_height = collision_player.height;
                    collision_player.snap_squished_height = collision_player.height;
                    collision_player.y = collision_owner_zone.floor - collision_player.height;
                    collision_player.snap_y = collision_player.y;
                    collision_player.snap_target_y = collision_player.y;
                    collision_player.zone_index = owner_zone_index;
                    game_input_init(&collision_input);
                    if ((normal_x == 0 && normal_z == 0) ||
                        !player_runtime_update_spatial(
                            &collision_player, &collision_input, &control_defaults,
                            &game.preferences, &game.math, &door_collision_state.runtime,
                            &door_collision_state, error, sizeof(error)) ||
                        collision_player.zone_index != (uint16_t)door.zone_id) {
                        fprintf(stderr,
                                "campaign level %u player could not cross its fully open "
                                "source door (zone=%u expected=%d): %s\n",
                                level_index, collision_player.zone_index, door.zone_id, error);
                        level_dynamic_state_destroy(&door_collision_state);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    level_dynamic_state_destroy(&door_collision_state);
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
            GameAudioEvents lift_audio;
            int16_t lift_audio_x;
            int16_t lift_audio_z;
            int16_t lift_action_sample;

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
                game_audio_events_init(&lift_audio);
                lift_action_sample = expected_velocity < 0 ?
                    lift.opening_sound_fx : lift.closing_sound_fx;
                if (!level_dynamic_state_set_edge_flags(
                        &game.dynamic_level, (uint16_t)lift_wall.edge_index, activation_flags) ||
                    !mechanism_runtime_update_lifts_single_player_with_audio(
                        &game.mechanism_runtime, &game.dynamic_level, &game.level_mechanisms,
                        &lift_player, &game.math, 1u, &lift_audio, error, sizeof(error)) ||
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
                if (lift_action_sample > 0 && lift_action_sample <= GAME_LINK_SFX_LOAD_COUNT) {
                    const GameAudioEvent *lift_event = &lift_audio.events[0u];

                    if (!source_liftable_audio_position(
                            &game.math, &lift_player, &lift, &lift_audio_x, &lift_audio_z,
                            error, sizeof(error)) ||
                        lift_audio.count != 1u ||
                        lift_event->sample_index != (uint16_t)(lift_action_sample - 1) ||
                        lift_event->volume != 50u || lift_event->world_x != lift_audio_x ||
                        lift_event->world_z != lift_audio_z ||
                        lift_event->source_id != UINT16_C(0xfffe) ||
                        lift_event->suppress_if_playing == 0u ||
                        lift_event->channel_pick != 1u ||
                        lift_event->listener_relative == 0u ||
                        lift_event->echo != lift_zone.echo) {
                        fprintf(stderr,
                                "campaign level %u LiftRoutine audio handoff is inconsistent: %s\n",
                                level_index, error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    mechanism_audio_fixture_mask |= 2u;
                } else if (lift_audio.count != 0u) {
                    fprintf(stderr,
                            "campaign level %u silent LiftRoutine emitted audio\n",
                            level_index);
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
                            draw_wall.texture_u_end != read_be16(source + 8u) ||
                            draw_wall.texture_u_tile != read_be16(source + 10u) ||
                            draw_wall.texture_y_offset != read_be16(source + 12u) ||
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
                        draw_flat.texture_scale != (int16_t)read_be16(
                            source + 8u + (size_t)draw_flat.point_count * 2u) ||
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
        /*
         * newanims.s:DoorRoutine/LiftRoutine write +12 in each controlled
         * Draw_Wall.  The native closed mesh must consume that live source
         * V origin rather than retaining an initial UV cache.
         */
        for (static_wall_index = 0u; static_wall_index < game.static_scene.wall_count;
             ++static_wall_index) {
            const LevelStaticWallScene *scene_wall =
                &game.static_scene.walls[static_wall_index];
            uint8_t *dynamic_texture_offset;

            if (scene_wall->is_mechanism_surface == 0u) {
                continue;
            }
            if (!level_dynamic_state_get_graphics_range(
                    &game.dynamic_level, scene_wall->mechanism_wall_source_offset + 12u, 2u,
                    &dynamic_texture_offset)) {
                fprintf(stderr,
                        "campaign level %u mechanism wall has no mutable texture offset\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            write_be16(dynamic_texture_offset,
                       (uint16_t)(read_be16(dynamic_texture_offset) ^ 0x00ffu));
            ++mechanism_surface_fixture_count;
            break;
        }
        /*
         * DoorRoutine changes Draw_Wall +24 as the panel contracts. Exercise
         * that source-sized span independently of the live +12 V origin.
         */
        for (static_wall_index = 0u; static_wall_index < game.static_scene.wall_count;
             ++static_wall_index) {
            const LevelStaticWallScene *scene_wall =
                &game.static_scene.walls[static_wall_index];
            uint8_t *dynamic_door_bounds;
            uint8_t *dynamic_door_plane;
            LevelLiftable door;
            int32_t top;
            int32_t bottom;
            int32_t moved_bottom;
            int16_t moved_plane;

            if (scene_wall->mechanism_kind != LEVEL_STATIC_WALL_MECHANISM_DOOR ||
                scene_wall->source_record_offset != scene_wall->mechanism_wall_source_offset ||
                !level_mechanisms_get_door(&game.level_mechanisms,
                                           scene_wall->mechanism_index, &door,
                                           error, sizeof(error)) ||
                !level_dynamic_state_get_graphics_range(
                    &game.dynamic_level, scene_wall->mechanism_wall_source_offset + 20u, 8u,
                    &dynamic_door_bounds) ||
                !level_dynamic_state_get_graphics_range(
                    &game.dynamic_level, door.graphics_offset + 2u, 2u,
                    &dynamic_door_plane)) {
                continue;
            }
            top = (int32_t)read_be32(dynamic_door_bounds + 0u);
            bottom = (int32_t)read_be32(dynamic_door_bounds + 4u);
            if (bottom > top + 512) {
                moved_bottom = top + (bottom - top) / 2;
            } else if (top > bottom + 512) {
                moved_bottom = top - (top - bottom) / 2;
            } else {
                continue;
            }
            /* DoorRoutine writes the flat from d3 and its wall edge from d3 >> 2. */
            if ((moved_bottom % 64) != 0 || moved_bottom / 64 < INT16_MIN ||
                moved_bottom / 64 > INT16_MAX) {
                fprintf(stderr,
                        "campaign level %u door V-span fixture cannot preserve source plane\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            moved_plane = (int16_t)(moved_bottom / 64);
            /* Exercise DoorRoutine's intermediate ASR.W #2 quantisation, not an endpoint. */
            if ((moved_plane % 4) == 0) {
                moved_plane = moved_plane == INT16_MAX ?
                    (int16_t)(moved_plane - 1) : (int16_t)(moved_plane + 1);
            }
            write_be32(dynamic_door_bounds + 4u,
                       (uint32_t)((int32_t)source_asr16_2(moved_plane) * 256));
            write_be16(dynamic_door_plane, (uint16_t)moved_plane);
            dynamic_door_fixture_wall_offset = scene_wall->mechanism_wall_source_offset;
            dynamic_door_fixture_plane_offset = door.graphics_offset;
            dynamic_door_fixture_boundary =
                (int32_t)source_asr16_2(moved_plane) * 256;
            ++dynamic_wall_v_scale_fixture_count;
            break;
        }
        for (static_wall_index = 0u; static_wall_index < game.static_scene.wall_count;
             ++static_wall_index) {
            const LevelStaticWallScene *scene_wall =
                &game.static_scene.walls[static_wall_index];
            uint8_t *dynamic_lift_top;
            uint8_t *dynamic_lift_plane;
            LevelLiftable lift;
            int32_t moved_boundary;
            int32_t moved_plane;

            if (scene_wall->mechanism_kind != LEVEL_STATIC_WALL_MECHANISM_LIFT ||
                scene_wall->source_record_offset != scene_wall->mechanism_wall_source_offset ||
                !level_mechanisms_get_lift(&game.level_mechanisms,
                                           scene_wall->mechanism_index, &lift,
                                           error, sizeof(error)) ||
                !level_dynamic_state_get_graphics_range(
                    &game.dynamic_level, lift.graphics_offset + 2u, 2u,
                    &dynamic_lift_plane)) {
                continue;
            }
            if (!level_dynamic_state_get_graphics_range(
                    &game.dynamic_level, scene_wall->source_record_offset + 20u, 4u,
                    &dynamic_lift_top)) {
                fprintf(stderr, "campaign level %u lift wall has no mutable Draw_Wall top\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            moved_boundary = (int32_t)read_be32(dynamic_lift_top) + 256;
            if ((moved_boundary % 64) != 0 || moved_boundary / 64 < INT16_MIN ||
                moved_boundary / 64 > INT16_MAX || (moved_boundary / 64) % 4 != 0) {
                fprintf(stderr,
                        "campaign level %u lift movement fixture cannot preserve source plane\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            moved_plane = moved_boundary / 64;
            write_be32(dynamic_lift_top, (uint32_t)moved_boundary);
            write_be16(dynamic_lift_plane, (uint16_t)(int16_t)moved_plane);
            dynamic_lift_fixture_wall_offset = scene_wall->mechanism_wall_source_offset;
            dynamic_lift_fixture_plane_offset = lift.graphics_offset;
            dynamic_lift_fixture_boundary = moved_boundary;
            ++lift_wall_motion_fixture_count;
            break;
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
        if (dynamic_door_fixture_wall_offset != UINT32_MAX) {
            uint32_t fixture_wall_count = 0u;
            uint32_t fixture_plane_count = 0u;

            for (static_wall_index = 0u; static_wall_index < game.static_scene.wall_count;
                 ++static_wall_index) {
                const LevelStaticWallScene *scene_wall =
                    &game.static_scene.walls[static_wall_index];

                if (scene_wall->source_record_offset != dynamic_door_fixture_wall_offset) {
                    continue;
                }
                if (scene_wall->vertices[0].position.y != dynamic_door_fixture_boundary &&
                    scene_wall->vertices[2].position.y != dynamic_door_fixture_boundary) {
                    fprintf(stderr,
                            "campaign level %u moving door wall did not retain its source boundary\n",
                            level_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                ++fixture_wall_count;
            }
            for (static_flat_index = 0u; static_flat_index < game.static_scene.flat_count;
                 ++static_flat_index) {
                const LevelStaticFlatScene *scene_flat =
                    &game.static_scene.flats[static_flat_index];

                if (scene_flat->source_record_offset != dynamic_door_fixture_plane_offset) {
                    continue;
                }
                for (uint16_t point_index = 0u; point_index < scene_flat->vertex_count;
                     ++point_index) {
                    if (scene_flat->vertices[point_index].position.y !=
                        dynamic_door_fixture_boundary) {
                        fprintf(stderr,
                                "campaign level %u moving door plane does not seal its wall\n",
                                level_index);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                }
                ++fixture_plane_count;
            }
            if (fixture_wall_count == 0u || fixture_plane_count == 0u) {
                fprintf(stderr,
                        "campaign level %u moving door fixture has no native closed mesh\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        if (dynamic_lift_fixture_wall_offset != UINT32_MAX) {
            uint32_t fixture_wall_count = 0u;
            uint32_t fixture_plane_count = 0u;

            for (static_wall_index = 0u; static_wall_index < game.static_scene.wall_count;
                 ++static_wall_index) {
                const LevelStaticWallScene *scene_wall =
                    &game.static_scene.walls[static_wall_index];

                if (scene_wall->source_record_offset != dynamic_lift_fixture_wall_offset) {
                    continue;
                }
                if (scene_wall->vertices[0].position.y != dynamic_lift_fixture_boundary &&
                    scene_wall->vertices[2].position.y != dynamic_lift_fixture_boundary) {
                    fprintf(stderr,
                            "campaign level %u moving lift wall did not retain its source boundary\n",
                            level_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                ++fixture_wall_count;
            }
            for (static_flat_index = 0u; static_flat_index < game.static_scene.flat_count;
                 ++static_flat_index) {
                const LevelStaticFlatScene *scene_flat =
                    &game.static_scene.flats[static_flat_index];

                if (scene_flat->source_record_offset != dynamic_lift_fixture_plane_offset) {
                    continue;
                }
                for (uint16_t point_index = 0u; point_index < scene_flat->vertex_count;
                     ++point_index) {
                    if (scene_flat->vertices[point_index].position.y !=
                        dynamic_lift_fixture_boundary) {
                        fprintf(stderr,
                                "campaign level %u moving lift plane does not seal its wall\n",
                                level_index);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                }
                ++fixture_plane_count;
            }
            if (fixture_wall_count == 0u || fixture_plane_count == 0u) {
                fprintf(stderr,
                        "campaign level %u moving lift fixture has no native closed mesh\n",
                        level_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        for (static_wall_index = 0u; static_wall_index < game.static_scene.wall_count;
             ++static_wall_index) {
            const LevelStaticWallScene *scene_wall =
                &game.static_scene.walls[static_wall_index];
            const uint8_t *source = game.dynamic_level.runtime.graphics_bytes +
                scene_wall->source_record_offset;
            const uint8_t *texture_source = source;
            const uint8_t *mechanism_source;
            LevelWorldPoint left_point;
            LevelWorldPoint right_point;
            uint8_t direct_mechanism_kind;
            uint16_t direct_mechanism_index;
            int32_t top;
            int32_t bottom;
            int32_t texture_v_span;
            int32_t texture_v_origin;

            if (scene_wall->source_record_offset > game.dynamic_level.runtime.graphics_size ||
                30u > game.dynamic_level.runtime.graphics_size - scene_wall->source_record_offset ||
                (uint8_t)read_be16(source) != LEVEL_DRAW_GRAPH_TYPE_WALL ||
                !level_runtime_get_world_point(&game.dynamic_level.runtime, read_be16(source + 2u),
                                               &left_point, error, sizeof(error)) ||
                !level_runtime_get_world_point(&game.dynamic_level.runtime, read_be16(source + 4u),
                                               &right_point, error, sizeof(error))) {
                fprintf(stderr, "campaign level %u static wall %u is invalid: %s\n",
                        level_index, static_wall_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (scene_wall->is_mechanism_surface > 1u ||
                scene_wall->mechanism_kind > LEVEL_STATIC_WALL_MECHANISM_LIFT ||
                scene_wall->is_mechanism_surface !=
                    (scene_wall->mechanism_kind != LEVEL_STATIC_WALL_MECHANISM_NONE ? 1u : 0u)) {
                fprintf(stderr, "campaign level %u static wall %u has invalid mechanism metadata\n",
                        level_index, static_wall_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (!find_direct_mechanism_wall_target(
                    &game.level_mechanisms, scene_wall->source_record_offset,
                    &direct_mechanism_kind, &direct_mechanism_index, error, sizeof(error))) {
                fprintf(stderr,
                        "campaign level %u static wall %u has invalid direct mechanism data: %s\n",
                        level_index, static_wall_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            /*
             * DoorRoutine/LiftRoutine mutate only listed graphics pointers.
             * A merely shared EdgeT may be an authored threshold/filler span,
             * so its source geometry must not be replaced by the moving panel.
             */
            if (scene_wall->mechanism_kind != direct_mechanism_kind ||
                (direct_mechanism_kind != LEVEL_STATIC_WALL_MECHANISM_NONE &&
                 (scene_wall->mechanism_index != direct_mechanism_index ||
                  scene_wall->mechanism_wall_source_offset !=
                      scene_wall->source_record_offset))) {
                fprintf(stderr,
                        "campaign level %u static wall %u has a non-source mechanism owner\n",
                        level_index, static_wall_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            switch (scene_wall->mechanism_kind) {
            case LEVEL_STATIC_WALL_MECHANISM_NONE:
                top = (int32_t)read_be32(source + 20u);
                bottom = (int32_t)read_be32(source + 24u);
                break;
            case LEVEL_STATIC_WALL_MECHANISM_DOOR:
                if (scene_wall->mechanism_wall_source_offset >
                        game.dynamic_level.runtime.graphics_size ||
                    30u > game.dynamic_level.runtime.graphics_size -
                               scene_wall->mechanism_wall_source_offset) {
                    fprintf(stderr,
                            "campaign level %u door wall %u has no exact source record\n",
                            level_index, static_wall_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                mechanism_source = game.dynamic_level.runtime.graphics_bytes +
                    scene_wall->mechanism_wall_source_offset;
                if ((uint8_t)read_be16(mechanism_source) != LEVEL_DRAW_GRAPH_TYPE_WALL) {
                    fprintf(stderr,
                            "campaign level %u door wall %u exact source is not Draw_Wall\n",
                            level_index, static_wall_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                top = (int32_t)read_be32(mechanism_source + 20u);
                bottom = (int32_t)read_be32(mechanism_source + 24u);
                texture_source = mechanism_source;
                break;
            case LEVEL_STATIC_WALL_MECHANISM_LIFT:
                if (scene_wall->mechanism_wall_source_offset >
                        game.dynamic_level.runtime.graphics_size ||
                    30u > game.dynamic_level.runtime.graphics_size -
                              scene_wall->mechanism_wall_source_offset) {
                    fprintf(stderr,
                            "campaign level %u lift wall %u has no controlled Draw_Wall\n",
                            level_index, static_wall_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                mechanism_source = game.dynamic_level.runtime.graphics_bytes +
                    scene_wall->mechanism_wall_source_offset;
                texture_source = mechanism_source;
                if ((uint8_t)read_be16(texture_source) != LEVEL_DRAW_GRAPH_TYPE_WALL) {
                    fprintf(stderr,
                            "campaign level %u lift wall %u controlled source is not Draw_Wall\n",
                            level_index, static_wall_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                top = (int32_t)read_be32(texture_source + 20u);
                bottom = (int32_t)read_be32(texture_source + 24u);
                break;
            default:
                fprintf(stderr, "campaign level %u static wall %u has an unknown mechanism type\n",
                        level_index, static_wall_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
            texture_v_origin = read_be16(texture_source + 12u);
            if (scene_wall->presentation_clip_lift_wall_source_offset != UINT32_MAX) {
                const uint8_t *lift_source;
                LevelWorldPoint lift_left_point;
                LevelWorldPoint lift_right_point;
                int32_t static_low;
                int32_t static_high;
                int32_t lift_top;
                int32_t lift_bottom;
                int32_t lift_low;
                int32_t lift_high;

                if (scene_wall->mechanism_kind != LEVEL_STATIC_WALL_MECHANISM_NONE ||
                    scene_wall->presentation_clip_lift_wall_source_offset >=
                        game.dynamic_level.runtime.graphics_size ||
                    30u > game.dynamic_level.runtime.graphics_size -
                              scene_wall->presentation_clip_lift_wall_source_offset) {
                    fprintf(stderr,
                            "campaign level %u static wall %u has invalid lift depth ownership\n",
                            level_index, static_wall_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                lift_source = game.dynamic_level.runtime.graphics_bytes +
                    scene_wall->presentation_clip_lift_wall_source_offset;
                if ((uint8_t)read_be16(lift_source) != LEVEL_DRAW_GRAPH_TYPE_WALL ||
                    !find_direct_mechanism_wall_target(
                        &game.level_mechanisms,
                        scene_wall->presentation_clip_lift_wall_source_offset,
                        &direct_mechanism_kind, &direct_mechanism_index,
                        error, sizeof(error)) ||
                    direct_mechanism_kind != LEVEL_STATIC_WALL_MECHANISM_LIFT ||
                    !level_runtime_get_world_point(&game.dynamic_level.runtime,
                                                   read_be16(lift_source + 2u),
                                                   &lift_left_point, error, sizeof(error)) ||
                    !level_runtime_get_world_point(&game.dynamic_level.runtime,
                                                   read_be16(lift_source + 4u),
                                                   &lift_right_point, error, sizeof(error)) ||
                    lift_left_point.x != left_point.x || lift_left_point.z != left_point.z ||
                    lift_right_point.x != right_point.x || lift_right_point.z != right_point.z) {
                    fprintf(stderr,
                            "campaign level %u static wall %u has non-source lift depth ownership: %s\n",
                            level_index, static_wall_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                static_low = top < bottom ? top : bottom;
                static_high = top > bottom ? top : bottom;
                lift_top = (int32_t)read_be32(lift_source + 20u);
                lift_bottom = (int32_t)read_be32(lift_source + 24u);
                lift_low = lift_top < lift_bottom ? lift_top : lift_bottom;
                lift_high = lift_top > lift_bottom ? lift_top : lift_bottom;
                if (static_low < lift_low && static_high > lift_low &&
                    static_high <= lift_high) {
                    if (top == static_high) {
                        texture_v_origin += (top - lift_low) / 256;
                        top = lift_low;
                    } else {
                        bottom = lift_low;
                    }
                } else if (static_low >= lift_low && static_low < lift_high &&
                           static_high > lift_high) {
                    if (top == static_low) {
                        texture_v_origin += (lift_high - top) / 256;
                        top = lift_high;
                    } else {
                        bottom = lift_high;
                    }
                } else {
                    fprintf(stderr,
                            "campaign level %u static wall %u has invalid lift depth interval\n",
                            level_index, static_wall_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
            {
                int64_t source_y_span = (int64_t)bottom - top;

                if (source_y_span < 0) {
                    source_y_span = -source_y_span;
                }
                texture_v_span = (int32_t)(source_y_span >> 8u);
            }
            if (scene_wall->material_id != read_be16(texture_source + 14u) ||
                scene_wall->point_brightness_selector != texture_source[19u] ||
                scene_wall->left_point_brightness != texture_source[6u] ||
                scene_wall->right_point_brightness != texture_source[7u] ||
                scene_wall->brightness_offset != (int8_t)texture_source[28u] ||
                scene_wall->other_zone != texture_source[29u] ||
                scene_wall->vertices[0].position.x != left_point.x ||
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
                scene_wall->texture_window.u_offset !=
                    (uint16_t)(read_be16(texture_source + 10u) << 4u) ||
                scene_wall->texture_window.u_period != (uint16_t)texture_source[18u] + 1u ||
                scene_wall->texture_window.v_period != (uint16_t)texture_source[16u] + 1u ||
                scene_wall->vertices[0].texture_u != 0 ||
                scene_wall->vertices[0].texture_v != texture_v_origin ||
                scene_wall->vertices[1].texture_u != read_be16(texture_source + 8u) ||
                scene_wall->vertices[1].texture_v != texture_v_origin ||
                scene_wall->vertices[2].texture_u != read_be16(texture_source + 8u) ||
                scene_wall->vertices[2].texture_v !=
                    texture_v_origin + texture_v_span ||
                scene_wall->vertices[5].texture_u != 0 ||
                scene_wall->vertices[5].texture_v !=
                    texture_v_origin + texture_v_span) {
                fprintf(stderr, "campaign level %u static wall %u geometry is inconsistent\n",
                        level_index, static_wall_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        /*
         * The desktop path submits the entire graph, unlike the source zone
         * order.  Direct LiftRoutine faces own a co-oriented shaft boundary;
         * a remaining overlap would make the two opaque materials contend in
         * the GPU depth pass.  Oppositely directed source records remain the
         * intentionally distinct two-sided wall materials.
         */
        for (static_wall_index = 0u; static_wall_index < game.static_scene.wall_count;
             ++static_wall_index) {
            const LevelStaticWallScene *lift_wall =
                &game.static_scene.walls[static_wall_index];

            if (lift_wall->mechanism_kind != LEVEL_STATIC_WALL_MECHANISM_LIFT ||
                lift_wall->source_record_offset != lift_wall->mechanism_wall_source_offset) {
                continue;
            }
            for (uint32_t other_wall_index = 0u;
                 other_wall_index < game.static_scene.wall_count; ++other_wall_index) {
                const LevelStaticWallScene *other_wall =
                    &game.static_scene.walls[other_wall_index];

                if (other_wall->mechanism_kind != LEVEL_STATIC_WALL_MECHANISM_NONE ||
                    !scene_walls_have_same_facing_overlap(lift_wall, other_wall)) {
                    continue;
                }
                fprintf(stderr,
                        "campaign level %u lift wall %u still overlaps co-oriented shaft wall %u\n",
                        level_index, static_wall_index, other_wall_index);
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
            uint8_t direct_dynamic_kind;
            uint16_t direct_dynamic_index;
            int32_t expected_height;

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
                scene_flat->point_brightness_selectors == NULL ||
                scene_flat->vertex_count != draw_flat.point_count ||
                scene_flat->material_id != draw_flat.texture_offset ||
                scene_flat->texture_scale != draw_flat.texture_scale ||
                scene_flat->brightness_offset != draw_flat.brightness_offset ||
                !find_direct_dynamic_flat_target(
                    &game.level_mechanisms, scene_flat->source_record_offset,
                    &direct_dynamic_kind, &direct_dynamic_index, error, sizeof(error)) ||
                scene_flat->dynamic_surface_kind != direct_dynamic_kind ||
                scene_flat->dynamic_surface_index != direct_dynamic_index ||
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
            expected_height = (scene_flat->dynamic_surface_kind ==
                    LEVEL_STATIC_DYNAMIC_SURFACE_DOOR ||
                scene_flat->dynamic_surface_kind ==
                    LEVEL_STATIC_DYNAMIC_SURFACE_LIFT) ?
                (int32_t)source_asr16_2(draw_flat.height) * 256 :
                (int32_t)draw_flat.height * 64;
            for (flat_point_index = 0u; flat_point_index < draw_flat.point_count;
                 ++flat_point_index) {
                int32_t source_scale = (int32_t)draw_flat.texture_scale +
                    (flat_record.type == LEVEL_DRAW_GRAPH_TYPE_WATER ? 2 : 1);

                if (!level_draw_graph_get_flat_point(&game.dynamic_level.runtime, &draw_flat,
                                                    flat_point_index, &flat_raw_point_word,
                                                    &flat_world_point_index,
                                                    error, sizeof(error)) ||
                    !level_runtime_get_world_point(&game.dynamic_level.runtime, flat_world_point_index,
                                                   &world_point, error, sizeof(error)) ||
                    scene_flat->point_brightness_selectors[flat_point_index] !=
                        (uint8_t)(flat_raw_point_word >> 12u) ||
                    (flat_raw_point_word >> 12u) >= LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT ||
                    scene_flat->vertices[flat_point_index].position.x != world_point.x ||
                    scene_flat->vertices[flat_point_index].position.y !=
                        expected_height ||
                    scene_flat->vertices[flat_point_index].position.z != world_point.z ||
                    source_scale < -31 || source_scale >= 32 ||
                    scene_flat->vertices[flat_point_index].texture_u !=
                        source_shift_flat_texture_coordinate(world_point.x, source_scale) ||
                    scene_flat->vertices[flat_point_index].texture_v !=
                        source_shift_flat_texture_coordinate(world_point.z, source_scale)) {
                    fprintf(stderr,
                            "campaign level %u static flat %u point %u is invalid: %s\n",
                            level_index, static_flat_index, flat_point_index, error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
        }
        /*
         * DoorRoutine writes the door Draw_Flats +2 plane and each direct
         * ZDoorWall fields under one controller. In the complete 3D scene
         * the authored plane must follow those direct wall faces in the same
         * controller mesh, rather than remaining static and opening a
         * floor/side gap for a door with any number of wall segments.
         */
        for (uint16_t door_index = 0u;
             door_index < game.level_mechanisms.door_count; ++door_index) {
            LevelLiftable door;
            uint32_t matching_flat_count = 0u;
            uint32_t direct_wall_count = 0u;

            if (!level_mechanisms_get_door(&game.level_mechanisms, door_index, &door,
                                           error, sizeof(error))) {
                fprintf(stderr, "campaign level %u door %u has invalid controller data: %s\n",
                        level_index, door_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            for (static_flat_index = 0u; static_flat_index < game.static_scene.flat_count;
                 ++static_flat_index) {
                const LevelStaticFlatScene *scene_flat =
                    &game.static_scene.flats[static_flat_index];

                if (scene_flat->source_record_offset != door.graphics_offset) {
                    continue;
                }
                if (scene_flat->dynamic_surface_kind != LEVEL_STATIC_DYNAMIC_SURFACE_DOOR ||
                    scene_flat->dynamic_surface_index != door_index ||
                    scene_flat->vertex_count == 0u || !scene_flat->vertices) {
                    fprintf(stderr,
                            "campaign level %u door %u has an ungrouped source movement plane\n",
                            level_index, door_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                ++matching_flat_count;
            }
            for (static_wall_index = 0u; static_wall_index < game.static_scene.wall_count;
                 ++static_wall_index) {
                const LevelStaticWallScene *scene_wall =
                    &game.static_scene.walls[static_wall_index];

                if (scene_wall->mechanism_kind != LEVEL_STATIC_WALL_MECHANISM_DOOR ||
                    scene_wall->mechanism_index != door_index ||
                    scene_wall->source_record_offset !=
                        scene_wall->mechanism_wall_source_offset) {
                    continue;
                }
                ++direct_wall_count;
            }
            if (direct_wall_count != 0u && matching_flat_count == 0u) {
                fprintf(stderr,
                        "campaign level %u door %u has sides but no source movement plane\n",
                        level_index, door_index);
                game_bootstrap_destroy(&game);
                return 1;
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
        }
    if (decoration_fixture_count == 0u || destructible_fixture_count == 0u ||
        water_fixture_count == 0u || mechanism_surface_fixture_count == 0u ||
        dynamic_wall_v_scale_fixture_count == 0u ||
        lift_wall_motion_fixture_count == 0u || mechanism_audio_fixture_mask != 3u) {
        fprintf(stderr,
                "campaign data does not contain all passive-object/water/mechanism fixtures "
                "(audio mask=%u)\n", mechanism_audio_fixture_mask);
        game_bootstrap_destroy(&game);
        return 1;
    }
    /* Retired record-pair command assertions; mesh instances are validated below. */
#if 0
    if (!object_scene_count_active(&game.object_runtime, &active_sprite_count,
                                   error, sizeof(error)) ||
        !level_runtime_get_zone(&game.level_runtime, game.level.player1_start_zone,
                                &zone, error, sizeof(error)) ||
        game.player.x != player_runtime_world_to_position(game.level.player1_start_x) ||
        game.player.z != player_runtime_world_to_position(game.level.player1_start_z) ||
        game.player.y != zone.floor - 12 * 1024 ||
        game.player.snap_x != game.player.x || game.player.snap_y != game.player.y ||
        game.player.snap_z != game.player.z || game.player.snap_target_y != game.player.y ||
        game.player.height != 12 * 1024 ||
        game.player.default_enemy_flags != 0x23u || !scene_frame_init(&frame, 2) ||
        !game_bootstrap_submit_scene_frame(&game, &frame) ||
        frame.count != 3u +
            ((size_t)game.static_scene.wall_count + game.static_scene.flat_count) * 2u +
            active_sprite_count ||
        frame.commands[0].type != SCENE_COMMAND_CAMERA ||
        frame.commands[0].data.camera.position.x !=
            player_runtime_position_to_world(game.player.x) ||
        frame.commands[0].data.camera.position.y != game.player.y ||
        game.static_scene.wall_count == 0u || game.static_scene.flat_count == 0u ||
        frame.commands[1].type != SCENE_COMMAND_LIGHTING ||
        frame.commands[1].data.lighting.current_point_brightness !=
            &game.lighting_runtime.current_point_brightness[0][0] ||
        frame.commands[1].data.lighting.zone_brightness != game.lighting_runtime.zone_brightness ||
        frame.commands[1].data.lighting.zone_count != game.dynamic_level.runtime.zone_count ||
        frame.commands[2].type != SCENE_COMMAND_ENVIRONMENT ||
        frame.commands[2].data.environment.backdrop_bytes != game.shared_resources.backdrop_image.bytes ||
        frame.commands[2].data.environment.backdrop_byte_count != 648u * 240u ||
        frame.commands[2].data.environment.water_bytes != game.shared_resources.water_frames.bytes ||
        frame.commands[2].data.environment.water_byte_count != 256u * 256u ||
        frame.commands[3].type != SCENE_COMMAND_MATERIAL ||
        frame.commands[3].data.material.source != SCENE_MATERIAL_SOURCE_SHARED_WALL_TEXTURE ||
        frame.commands[3].data.material.source_asset_id != game.static_scene.walls[0].material_id ||
        frame.commands[3].data.material.source_bytes !=
            game.shared_resources.wall_textures[game.static_scene.walls[0].material_id].bytes ||
        frame.commands[3].data.material.source_byte_count !=
            game.shared_resources.wall_textures[game.static_scene.walls[0].material_id].size ||
        frame.commands[3].data.material.source_palette_bytes !=
            game.shared_resources.wall_textures[game.static_scene.walls[0].material_id].bytes ||
        frame.commands[3].data.material.source_palette_byte_count != 64u * 32u ||
        frame.commands[3].data.material.source_display_palette_bytes !=
            game.shared_resources.main_palette.bytes ||
        frame.commands[3].data.material.source_display_palette_byte_count !=
            game.shared_resources.main_palette.size ||
        frame.commands[4].type != SCENE_COMMAND_GEOMETRY ||
        frame.commands[4].data.geometry.vertices != game.static_scene.walls[0].vertices ||
        frame.commands[4].data.geometry.vertex_count != 6u ||
        frame.commands[4].data.geometry.topology != SCENE_GEOMETRY_TOPOLOGY_TRIANGLE_LIST ||
        frame.commands[4].data.geometry.primitive != SCENE_GEOMETRY_PRIMITIVE_WALL ||
        frame.commands[4].data.geometry.material_id != game.static_scene.walls[0].material_id ||
        frame.commands[4].data.geometry.source_record_id !=
            game.static_scene.walls[0].source_record_offset ||
        frame.commands[4].data.geometry.source_zone_index !=
            game.static_scene.walls[0].source_zone_index ||
        frame.commands[4].data.geometry.source_upper_zone !=
            game.static_scene.walls[0].source_upper_zone ||
        memcmp(&frame.commands[4].data.geometry.texture_window,
               &game.static_scene.walls[0].texture_window,
               sizeof(game.static_scene.walls[0].texture_window)) != 0 ||
        frame.commands[4].data.geometry.flags != 0u ||
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].type !=
            SCENE_COMMAND_MATERIAL ||
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source !=
            SCENE_MATERIAL_SOURCE_SHARED_FLOOR_TEXTURE ||
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_asset_id !=
            game.static_scene.flats[0].material_id ||
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_bytes !=
            game.shared_resources.floor_texture.bytes ||
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_byte_count !=
            game.shared_resources.floor_texture.size ||
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_palette_bytes !=
            game.shared_resources.texture_palette.bytes ||
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_palette_byte_count !=
            game.shared_resources.texture_palette.size ||
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_display_palette_bytes !=
            game.shared_resources.main_palette.bytes ||
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_display_palette_byte_count !=
            game.shared_resources.main_palette.size ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].type !=
            SCENE_COMMAND_GEOMETRY ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.vertices !=
            game.static_scene.flats[0].vertices ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.vertex_count !=
            game.static_scene.flats[0].vertex_count ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.topology !=
            SCENE_GEOMETRY_TOPOLOGY_POLYGON_BOUNDARY ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.primitive !=
            game.static_scene.flats[0].primitive ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.material_id !=
            game.static_scene.flats[0].material_id ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.source_record_id !=
            game.static_scene.flats[0].source_record_offset ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.source_zone_index !=
            game.static_scene.flats[0].source_zone_index ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.source_upper_zone !=
            game.static_scene.flats[0].source_upper_zone ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.texture_window.u_offset != 0u ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.texture_window.u_period != 0u ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.texture_window.v_period != 0u ||
        frame.commands[4u + (size_t)game.static_scene.wall_count * 2u].data.geometry.flags != 0u ||
        !scene_sprite_commands_match_source(
            &frame, 3u + ((size_t)game.static_scene.wall_count + game.static_scene.flat_count) * 2u,
            &game, active_sprite_count, error, sizeof(error)) ||
        frame.commands[frame.count - 1u].type != SCENE_COMMAND_SPRITE) {
        fprintf(stderr, "Plr_Initialise camera state is inconsistent: %s\n", error);
        scene_frame_destroy(&frame);
        game_bootstrap_destroy(&game);
        return 1;
    }
    {
        int32_t source_player_y = game.player.y;
        int32_t source_bobble_y = game.player.bobble_y;

        /*
         * hires.s places Plr1_YOff_l directly in Plr_YOff_l before drawing.
         * newplayershoot.s launches from that same bobbed coordinate, so any
         * presentation-only removal would pull the projectile away from the
         * weapon's source-space bore.
         */
        game.player.y = source_player_y - 4096 + 512;
        game.player.bobble_y = 512;
        scene_frame_begin(&frame);
        if (!game_bootstrap_submit_scene_frame(&game, &frame) ||
            frame.commands[0].data.camera.position.y != game.player.y) {
            fprintf(stderr, "3D camera does not preserve the source eye coordinate\n");
            scene_frame_destroy(&frame);
            game_bootstrap_destroy(&game);
            return 1;
        }

        /* Preserve even the source fall-step overshoot; it shares the shot frame. */
        game.player.y = zone.floor;
        game.player.bobble_y = 0;
        scene_frame_begin(&frame);
        if (!game_bootstrap_submit_scene_frame(&game, &frame) ||
            frame.commands[0].data.camera.position.y != zone.floor) {
            fprintf(stderr, "3D camera altered the source fall coordinate\n");
            scene_frame_destroy(&frame);
            game_bootstrap_destroy(&game);
            return 1;
        }
        game.player.y = source_player_y;
        game.player.bobble_y = source_bobble_y;
    }
    saved_floor_override = game.level_floor_override;
    saved_wall_override = game.level_wall_overrides[game.static_scene.walls[0].material_id];
    memset(override_marker, 0, sizeof(override_marker));
    game.level_floor_override.bytes = override_marker;
    game.level_floor_override.size = sizeof(override_marker);
    game.level_wall_overrides[game.static_scene.walls[0].material_id].bytes = override_marker;
    game.level_wall_overrides[game.static_scene.walls[0].material_id].size = sizeof(override_marker);
    scene_frame_begin(&frame);
    override_sources_ok = game_bootstrap_submit_scene_frame(&game, &frame) &&
        frame.commands[3].data.material.source ==
            SCENE_MATERIAL_SOURCE_LEVEL_WALL_TEXTURE_OVERRIDE &&
        frame.commands[3].data.material.source_bytes == override_marker &&
        frame.commands[3].data.material.source_byte_count == sizeof(override_marker) &&
        frame.commands[3].data.material.source_palette_bytes == override_marker &&
        frame.commands[3].data.material.source_palette_byte_count == sizeof(override_marker) &&
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source ==
            SCENE_MATERIAL_SOURCE_LEVEL_FLOOR_TEXTURE_OVERRIDE &&
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_bytes ==
            override_marker &&
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_byte_count ==
            sizeof(override_marker) &&
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_palette_bytes ==
            game.shared_resources.texture_palette.bytes &&
        frame.commands[3u + (size_t)game.static_scene.wall_count * 2u].data.material.source_palette_byte_count ==
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
#endif
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
        PlayerRuntime rollover_player = game.player;
        GameInput rollover_input;
        GamePreferences rollover_preferences = game.preferences;
        const int32_t expected_x_velocity = 32767 * 3;
        const int32_t expected_z_velocity = -32767 * 3;

        /*
         * Alien-Breed-3D-I's PC controller resolves opposing movement keys
         * deterministically: the later binding wins. Keep that host input
         * policy instead of exposing a shared-register rollover artefact.
         */
        rollover_player.mouse_active = 0u;
        rollover_player.yaw = 0u;
        rollover_player.snap_yaw = 0u;
        rollover_player.snap_yaw_speed = 0;
        rollover_player.snap_x_speed = 0;
        rollover_player.snap_z_speed = 0;
        rollover_player.decelerate = UINT8_MAX;
        rollover_player.snap_target_y = rollover_player.snap_y;
        rollover_preferences.always_run = UINT8_MAX;
        game_input_init(&rollover_input);
        if (!game_input_set_raw_key(
                &rollover_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_SIDESTEP_LEFT], 1,
                error, sizeof(error)) ||
            !game_input_set_raw_key(
                &rollover_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_SIDESTEP_RIGHT], 1,
                error, sizeof(error)) ||
            !game_input_set_raw_key(
                &rollover_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_FORWARDS], 1,
                error, sizeof(error)) ||
            !game_input_set_raw_key(
                &rollover_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_BACKWARDS], 1,
                error, sizeof(error)) ||
            !player_runtime_update_spatial(
                &rollover_player, &rollover_input, &control_defaults,
                &rollover_preferences, &game.math, &game.level_runtime,
                NULL, error, sizeof(error)) ||
            rollover_player.snap_x_speed != expected_x_velocity ||
            rollover_player.snap_z_speed != expected_z_velocity ||
            rollover_player.snap_x !=
                (int32_t)((uint32_t)game.player.snap_x +
                          (uint32_t)expected_x_velocity) ||
            rollover_player.snap_z !=
                (int32_t)((uint32_t)game.player.snap_z +
                          (uint32_t)expected_z_velocity) ||
            player_runtime_position_to_world(rollover_player.x) !=
                player_runtime_position_to_world(rollover_player.snap_x) ||
            player_runtime_position_to_world(rollover_player.z) !=
                player_runtime_position_to_world(rollover_player.snap_z) ||
            rollover_player.presentation_x != rollover_player.x ||
            rollover_player.presentation_z != rollover_player.z) {
            fprintf(stderr, "first-port opposing-key rollover is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        PlayerRuntime right_strafe_player = game.player;
        GameInput right_strafe_input;
        GamePreferences right_strafe_preferences = game.preferences;
        const int32_t expected_right_velocity = 32767 * 3;

        right_strafe_player.mouse_active = 0u;
        right_strafe_player.yaw = 0u;
        right_strafe_player.snap_yaw = 0u;
        right_strafe_player.snap_yaw_speed = 0;
        right_strafe_player.snap_x_speed = 0;
        right_strafe_player.snap_z_speed = 0;
        right_strafe_player.decelerate = UINT8_MAX;
        right_strafe_player.snap_target_y = right_strafe_player.snap_y;
        right_strafe_preferences.always_run = UINT8_MAX;
        game_input_init(&right_strafe_input);
        if (!game_input_set_raw_key(
                &right_strafe_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_SIDESTEP_RIGHT], 1,
                error, sizeof(error)) ||
            !player_runtime_update_spatial(
                &right_strafe_player, &right_strafe_input, &control_defaults,
                &right_strafe_preferences, &game.math, &game.level_runtime,
                NULL, error, sizeof(error)) ||
            right_strafe_player.snap_x_speed != expected_right_velocity ||
            right_strafe_player.snap_z_speed != 0) {
            fprintf(stderr, "first-port right-strafe movement is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        PlayerRuntime forced_strafe_player = game.player;
        GameInput forced_strafe_input;
        GamePreferences forced_strafe_preferences = game.preferences;
        const int32_t expected_right_velocity = 32767 * 3;

        forced_strafe_player.mouse_active = 0u;
        forced_strafe_player.yaw = 0u;
        forced_strafe_player.snap_yaw = 0u;
        forced_strafe_player.snap_yaw_speed = 0;
        forced_strafe_player.snap_x_speed = 0;
        forced_strafe_player.snap_z_speed = 0;
        forced_strafe_player.decelerate = UINT8_MAX;
        forced_strafe_player.snap_target_y = forced_strafe_player.snap_y;
        forced_strafe_preferences.always_run = UINT8_MAX;
        game_input_init(&forced_strafe_input);
        if (!game_input_set_raw_key(
                &forced_strafe_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_FORCE_SIDESTEP], 1,
                error, sizeof(error)) ||
            !game_input_set_raw_key(
                &forced_strafe_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_TURN_RIGHT], 1,
                error, sizeof(error)) ||
            !player_runtime_update_spatial(
                &forced_strafe_player, &forced_strafe_input, &control_defaults,
                &forced_strafe_preferences, &game.math, &game.level_runtime,
                NULL, error, sizeof(error)) ||
            forced_strafe_player.snap_yaw != 0u ||
            forced_strafe_player.snap_yaw_speed != 0 ||
            forced_strafe_player.snap_x_speed != expected_right_velocity ||
            forced_strafe_player.snap_z_speed != 0) {
            fprintf(stderr, "source force-sidestep movement is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
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
        ObjectMotionRuntime use_snapshot_motion;

        use_snapshot_player.gun_selected = 7u;
        use_snapshot_player.fire = UINT8_MAX;
        use_snapshot_player.clicked = UINT8_MAX;
        game_input_init(&use_snapshot_input);
        object_motion_runtime_init(&use_snapshot_motion);
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
            !player_runtime_update_spatial_with_motion(
                &use_snapshot_player, &use_snapshot_input, &control_defaults,
                &game.preferences, &game.math, &game.level_runtime, NULL,
                &use_snapshot_motion, error, sizeof(error)) ||
            use_snapshot_player.tmp_used != UINT8_MAX || use_snapshot_player.used != 0u ||
            use_snapshot_player.tmp_clicked != UINT8_MAX || use_snapshot_player.clicked != 0u ||
            use_snapshot_player.tmp_fire != UINT8_MAX || use_snapshot_player.fire != UINT8_MAX ||
            use_snapshot_player.tmp_gun_selected != 7u ||
            use_snapshot_motion.new_x != player_runtime_position_to_world(use_snapshot_player.x) ||
            use_snapshot_motion.new_z != player_runtime_position_to_world(use_snapshot_player.z)) {
            fprintf(stderr, "source transient player snapshot is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /*
         * hires.s:Plr1_Control calls Obj_DoCollision before MoveObject. On a
         * hit its move.w restores only the committed integer X/Z words, while
         * Plr1_Snap* retains the attempted fixed-point fractions and the
         * shared newx/newz workspace retains the attempted integer position.
         */
        uint8_t player_collision_slots[3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t player_collision_points[2u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        int16_t player_collision_a2[8u] = {0, 20, 80, 0, 0, 20, 80, 0};
        ObjectRuntime player_collision_objects = {0};
        PlayerObjectCollisionContext player_collision_context = {0};
        PlayerRuntime player_collision_player = game.player;
        GameInput player_collision_input;
        ObjectMotionRuntime player_collision_motion;
        LevelZone player_collision_zone;
        const int16_t old_x = 1000;
        const int16_t old_z = 1000;
        const int16_t attempted_x = 1050;
        const int16_t attempted_z = 1000;
        int32_t visual_y;

        if (!level_runtime_get_zone(&game.level_runtime, player_collision_player.zone_index,
                                    &player_collision_zone, error, sizeof(error))) {
            fprintf(stderr, "player Obj_DoCollision fixture zone is unavailable: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        player_collision_player.x = player_runtime_world_to_position(old_x);
        player_collision_player.z = player_runtime_world_to_position(old_z);
        player_collision_player.snap_x =
            (int32_t)((uint32_t)player_runtime_world_to_position(attempted_x) | UINT32_C(0x1234));
        player_collision_player.snap_z =
            (int32_t)((uint32_t)player_runtime_world_to_position(attempted_z) | UINT32_C(0x5678));
        player_collision_player.snap_x_speed = 0;
        player_collision_player.snap_z_speed = 0;
        player_collision_player.snap_target_y = player_collision_player.snap_y;
        player_collision_player.snap_y_velocity = 0;
        player_collision_player.bobble = 0u;
        player_collision_player.ducked = 0u;
        player_collision_player.squished = 0u;
        visual_y = player_collision_player.snap_y + 2048;

        player_collision_objects.slot_bytes = player_collision_slots;
        player_collision_objects.slot_count = 3u;
        player_collision_objects.active_slot_count = 2u;
        player_collision_objects.player1_slot = 1u;
        player_collision_objects.point_bytes = player_collision_points;
        player_collision_objects.point_count = 2u;
        /* Candidate alien, Player 1, then the source negative terminator. */
        write_be16(player_collision_slots + 0u, 0u);
        write_be16(player_collision_slots + 4u,
                   (uint16_t)(source_asr32_7(visual_y) + 20));
        write_be16(player_collision_slots + 12u, player_collision_zone.id);
        player_collision_slots[16u] = 0u;
        player_collision_slots[18u] = 1u;
        player_collision_slots[63u] = player_collision_player.stood_in_top;
        write_be16(player_collision_slots + OBJECT_RUNTIME_SLOT_BYTE_COUNT, 1u);
        write_be16(player_collision_slots + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   player_collision_zone.id);
        player_collision_slots[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 63u] =
            player_collision_player.stood_in_top;
        write_be16(player_collision_slots + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        write_be32(player_collision_points + 0u, (uint32_t)(uint16_t)attempted_x << 16u);
        write_be32(player_collision_points + 4u, (uint32_t)(uint16_t)attempted_z << 16u);
        write_be32(player_collision_points + OBJECT_RUNTIME_POINT_BYTE_COUNT,
                   (uint32_t)(uint16_t)old_x << 16u);
        write_be32(player_collision_points + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   (uint32_t)(uint16_t)old_z << 16u);
        player_collision_context.objects = &player_collision_objects;
        player_collision_context.source_a2_words = player_collision_a2;
        player_collision_context.source_a2_word_count =
            sizeof(player_collision_a2) / sizeof(player_collision_a2[0]);
        game_input_init(&player_collision_input);
        object_motion_runtime_init(&player_collision_motion);
        if (!player_runtime_update_spatial_with_motion_and_audio(
                &player_collision_player, &player_collision_input, &control_defaults,
                &game.preferences, &game.math, &game.level_runtime, NULL,
                &player_collision_motion, &player_collision_context,
                NULL, &game.game_link_catalog, NULL, error, sizeof(error)) ||
            player_runtime_position_to_world(player_collision_player.x) != old_x ||
            player_runtime_position_to_world(player_collision_player.z) != old_z ||
            ((uint32_t)player_collision_player.x & UINT32_C(0xffff)) != UINT32_C(0x1234) ||
            ((uint32_t)player_collision_player.z & UINT32_C(0xffff)) != UINT32_C(0x5678) ||
            player_collision_player.presentation_x != player_collision_player.x ||
            player_collision_player.presentation_z != player_collision_player.z ||
            player_collision_player.source_x_difference != 0 ||
            player_collision_player.source_z_difference != 0 ||
            player_collision_motion.new_x != attempted_x ||
            player_collision_motion.new_z != attempted_z) {
            fprintf(stderr, "Plr1_Control object collision handoff is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /*
         * hires.s:Plr1_Control's player teleport differs from the generic
         * CheckTeleport helper: it probes destination X/Z at the current Y,
         * then preserves the player's floor-relative Y only after success.
         */
        uint8_t teleport_level_bytes[256u] = {0};
        uint8_t teleport_graphics_bytes[8u] = {0};
        uint8_t teleport_slot_bytes[3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t teleport_point_bytes[2u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        int16_t teleport_a2_words[8u] = {0};
        LevelRuntime player_teleport_level = {0};
        ObjectRuntime player_teleport_objects = {0};
        PlayerObjectCollisionContext player_teleport_context = {0};
        PlayerRuntime player_teleport_player = game.player;
        GameInput player_teleport_input;
        ObjectMotionRuntime player_teleport_motion;
        GameAudioEvents player_teleport_audio;
        const int32_t source_floor = 200 * 256;
        const int32_t destination_floor = 300 * 256;
        const int32_t source_snap_y = source_floor - player_teleport_player.snap_height;
        const int32_t expected_visual_y =
            destination_floor - player_teleport_player.snap_height + 2048;

        player_teleport_level.level_bytes = teleport_level_bytes;
        player_teleport_level.level_size = sizeof(teleport_level_bytes);
        player_teleport_level.graphics_bytes = teleport_graphics_bytes;
        player_teleport_level.graphics_size = sizeof(teleport_graphics_bytes);
        player_teleport_level.zone_offsets_table_offset = 0u;
        player_teleport_level.zone_count = 2u;
        write_be32(teleport_graphics_bytes + 0u, 0u);
        write_be32(teleport_graphics_bytes + 4u, 100u);
        write_be16(teleport_level_bytes + 0u, 5u);
        write_be32(teleport_level_bytes + 2u, (uint32_t)source_floor);
        write_be32(teleport_level_bytes + 6u, 0u);
        write_be32(teleport_level_bytes + 18u, (uint32_t)source_floor);
        write_be16(teleport_level_bytes + 32u, 50u);
        write_be16(teleport_level_bytes + 38u, 1u);
        write_be16(teleport_level_bytes + 40u, 300u);
        write_be16(teleport_level_bytes + 42u, 400u);
        write_be16(teleport_level_bytes + 50u, UINT16_C(0xfffe));
        write_be16(teleport_level_bytes + 100u, 9u);
        write_be32(teleport_level_bytes + 102u, (uint32_t)destination_floor);
        write_be32(teleport_level_bytes + 106u, 0u);
        write_be32(teleport_level_bytes + 118u, (uint32_t)destination_floor);
        write_be16(teleport_level_bytes + 132u, 50u);
        teleport_level_bytes[137u] = 7u;
        write_be16(teleport_level_bytes + 138u, UINT16_MAX);
        write_be16(teleport_level_bytes + 150u, UINT16_C(0xfffe));

        player_teleport_objects.slot_bytes = teleport_slot_bytes;
        player_teleport_objects.slot_count = 3u;
        player_teleport_objects.active_slot_count = 1u;
        player_teleport_objects.player1_slot = 0u;
        player_teleport_objects.point_bytes = teleport_point_bytes;
        player_teleport_objects.point_count = 1u;
        write_be16(teleport_slot_bytes + 0u, 0u);
        write_be16(teleport_slot_bytes + 12u, 5u);
        teleport_slot_bytes[63u] = player_teleport_player.stood_in_top;
        write_be16(teleport_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        write_be32(teleport_point_bytes + 0u, UINT32_C(1000) << 16u);
        write_be32(teleport_point_bytes + 4u, UINT32_C(1000) << 16u);
        player_teleport_context.objects = &player_teleport_objects;
        player_teleport_context.source_a2_words = teleport_a2_words;
        player_teleport_context.source_a2_word_count =
            sizeof(teleport_a2_words) / sizeof(teleport_a2_words[0]);

        player_teleport_player.zone_index = 0u;
        player_teleport_player.x = player_runtime_world_to_position(1000);
        player_teleport_player.z = player_runtime_world_to_position(1000);
        player_teleport_player.snap_x =
            (int32_t)((uint32_t)player_runtime_world_to_position(1010) | UINT32_C(0x1234));
        player_teleport_player.snap_z =
            (int32_t)((uint32_t)player_runtime_world_to_position(1020) | UINT32_C(0x5678));
        player_teleport_player.snap_x_speed = 0;
        player_teleport_player.snap_z_speed = 0;
        player_teleport_player.y = source_snap_y;
        player_teleport_player.snap_y = source_snap_y;
        player_teleport_player.snap_target_y = source_snap_y;
        player_teleport_player.snap_y_velocity = 0;
        player_teleport_player.bobble = 0u;
        player_teleport_player.ducked = 0u;
        player_teleport_player.squished = 0u;
        game_input_init(&player_teleport_input);
        object_motion_runtime_init(&player_teleport_motion);
        game_audio_events_init(&player_teleport_audio);
        if (!player_runtime_update_spatial_with_motion_and_audio(
                &player_teleport_player, &player_teleport_input, &control_defaults,
                &game.preferences, &game.math, &player_teleport_level, NULL,
                &player_teleport_motion, &player_teleport_context,
                NULL, &game.game_link_catalog, &player_teleport_audio,
                error, sizeof(error)) ||
            player_teleport_player.zone_index != 1u ||
            player_runtime_position_to_world(player_teleport_player.x) != 300 ||
            player_runtime_position_to_world(player_teleport_player.z) != 400 ||
            ((uint32_t)player_teleport_player.x & UINT32_C(0xffff)) != UINT32_C(0x1234) ||
            ((uint32_t)player_teleport_player.z & UINT32_C(0xffff)) != UINT32_C(0x5678) ||
            player_teleport_player.y != expected_visual_y ||
            player_teleport_player.snap_y != expected_visual_y ||
            player_teleport_player.snap_target_y !=
                destination_floor - player_teleport_player.height ||
            player_teleport_player.source_x_difference != -11200 ||
            player_teleport_player.source_z_difference != -9600 ||
            player_teleport_motion.new_x != 300 || player_teleport_motion.new_z != 400 ||
            player_teleport_audio.count != 1u ||
            player_teleport_audio.events[0u].sample_index != 26u ||
            player_teleport_audio.events[0u].volume != 100u ||
            player_teleport_audio.events[0u].world_x != 0 ||
            player_teleport_audio.events[0u].world_z != 0 ||
            player_teleport_audio.events[0u].source_id != UINT16_C(0xfff9) ||
            player_teleport_audio.events[0u].listener_relative == 0u ||
            player_teleport_audio.events[0u].echo != 0u) {
            fprintf(stderr, "Plr1_Control source teleport is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }

        /* A destination collision restores attempted movement and continues normally. */
        memset(teleport_slot_bytes, 0, sizeof(teleport_slot_bytes));
        memset(teleport_point_bytes, 0, sizeof(teleport_point_bytes));
        teleport_a2_words[1u] = 20;
        teleport_a2_words[2u] = 80;
        player_teleport_objects.active_slot_count = 2u;
        player_teleport_objects.point_count = 2u;
        write_be16(teleport_slot_bytes + 0u, 0u);
        write_be16(teleport_slot_bytes + 12u, 5u);
        teleport_slot_bytes[63u] = game.player.stood_in_top;
        write_be16(teleport_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT, 1u);
        write_be16(teleport_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                   (uint16_t)(source_asr32_7(source_snap_y + 2048) + 20));
        write_be16(teleport_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 5u);
        teleport_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
        teleport_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
        teleport_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 63u] = game.player.stood_in_top;
        write_be16(teleport_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        write_be32(teleport_point_bytes + 0u, UINT32_C(1000) << 16u);
        write_be32(teleport_point_bytes + 4u, UINT32_C(1000) << 16u);
        write_be32(teleport_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT,
                   UINT32_C(300) << 16u);
        write_be32(teleport_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   UINT32_C(400) << 16u);
        player_teleport_player = game.player;
        player_teleport_player.zone_index = 0u;
        player_teleport_player.x = player_runtime_world_to_position(1000);
        player_teleport_player.z = player_runtime_world_to_position(1000);
        player_teleport_player.snap_x =
            (int32_t)((uint32_t)player_runtime_world_to_position(1010) | UINT32_C(0x1234));
        player_teleport_player.snap_z =
            (int32_t)((uint32_t)player_runtime_world_to_position(1020) | UINT32_C(0x5678));
        player_teleport_player.snap_x_speed = 0;
        player_teleport_player.snap_z_speed = 0;
        player_teleport_player.y = source_snap_y;
        player_teleport_player.snap_y = source_snap_y;
        player_teleport_player.snap_target_y = source_snap_y;
        player_teleport_player.snap_y_velocity = 0;
        player_teleport_player.bobble = 0u;
        player_teleport_player.ducked = 0u;
        player_teleport_player.squished = 0u;
        game_input_init(&player_teleport_input);
        object_motion_runtime_init(&player_teleport_motion);
        game_audio_events_begin(&player_teleport_audio);
        if (!player_runtime_update_spatial_with_motion_and_audio(
                &player_teleport_player, &player_teleport_input, &control_defaults,
                &game.preferences, &game.math, &player_teleport_level, NULL,
                &player_teleport_motion, &player_teleport_context,
                NULL, &game.game_link_catalog, &player_teleport_audio,
                error, sizeof(error)) ||
            player_teleport_player.zone_index != 0u ||
            player_runtime_position_to_world(player_teleport_player.x) != 1010 ||
            player_runtime_position_to_world(player_teleport_player.z) != 1020 ||
            ((uint32_t)player_teleport_player.x & UINT32_C(0xffff)) != UINT32_C(0x1234) ||
            ((uint32_t)player_teleport_player.z & UINT32_C(0xffff)) != UINT32_C(0x5678) ||
            player_teleport_player.source_x_difference != 160 ||
            player_teleport_player.source_z_difference != 320 ||
            player_teleport_motion.new_x != 1010 || player_teleport_motion.new_z != 1020 ||
            player_teleport_audio.count != 0u) {
            fprintf(stderr, "Plr1_Control rejected teleport handoff is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* hires.s:Plr1_Use damage, impact, random twist, and hit-noise block. */
        uint8_t damage_slots[3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t damage_points[3u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime damage_objects = {0};
        PlayerRuntime damage_player = game.player;
        GameInventory damage_inventory = game.session.player1_inventory;
        GameRandom damage_random = {234u};
        GameRandom expected_damage_random = damage_random;
        GameAudioEvents damage_audio;
        int32_t expected_twist;

        damage_objects.slot_bytes = damage_slots;
        damage_objects.slot_count = 3u;
        damage_objects.active_slot_count = 3u;
        damage_objects.player1_slot = 0u;
        damage_objects.point_bytes = damage_points;
        damage_objects.point_count = 3u;
        write_be16(damage_slots + 0u, 0u);
        write_be16(damage_slots + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 2u);
        damage_slots[19u] = 5u;
        write_be16(damage_slots + 42u, 3u);
        write_be16(damage_slots + 44u, (uint16_t)-4);
        write_be16(damage_slots + 46u, (uint16_t)-2);
        damage_player.snap_x_speed = (int32_t)UINT32_C(0x12345678);
        damage_player.snap_z_speed = (int32_t)UINT32_C(0xfffe1111);
        damage_player.snap_y_velocity = 100;
        damage_player.snap_yaw_speed = 7;
        damage_inventory.health = 100u;
        damage_player.health = damage_inventory.health;
        expected_twist = (int32_t)(int16_t)game_random_next(&expected_damage_random) * 5;
        expected_twist = source_asr32_count(expected_twist, 8u);
        expected_twist = source_asr32_count(expected_twist, 4u);
        game_audio_events_init(&damage_audio);
        if (!player_entity_sync_single_player(
                &damage_objects, &game.dynamic_level.runtime, &game.game_link_catalog,
                &damage_player, &damage_inventory, &damage_random, &damage_audio,
                error, sizeof(error)) ||
            (uint32_t)damage_player.snap_x_speed != UINT32_C(0x12375678) ||
            (uint32_t)damage_player.snap_z_speed != UINT32_C(0xfffa1111) ||
            damage_player.snap_y_velocity != -412 ||
            damage_player.snap_yaw_speed != (int16_t)(7 + expected_twist) ||
            damage_inventory.health != 95u || damage_player.health != 95u ||
            damage_random.state != expected_damage_random.state ||
            damage_slots[19u] != 0u || read_be16(damage_slots + 42u) != 0u ||
            read_be16(damage_slots + 44u) != 0u || read_be16(damage_slots + 46u) != 0u ||
            damage_slots[18u] != 10u || damage_audio.count != 1u ||
            damage_audio.events[0u].sample_index != 19u ||
            damage_audio.events[0u].volume != 60u ||
            damage_audio.events[0u].listener_relative == 0u ||
            damage_audio.events[0u].source_id != UINT16_C(0xfffa)) {
            fprintf(stderr, "Plr1_Use player damage/impact response is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        PlayerRuntime number_weapon_player = game.player;
        GameInput number_weapon_input;
        GameInventory number_weapon_inventory = {0};

        number_weapon_player.gun_selected = 3u;
        number_weapon_inventory.weapons[3u] = UINT8_MAX;
        number_weapon_inventory.weapons[5u] = UINT8_MAX;
        number_weapon_inventory.weapons[7u] = UINT8_MAX;
        game_input_init(&number_weapon_input);
        /* raw key $06 is RAWKEY_6, which selects the rocket launcher at entry 5. */
        if (!game_input_set_raw_key(&number_weapon_input, 6u, 1, error, sizeof(error)) ||
            !player_runtime_update_discrete_controls(
                &number_weapon_player, &number_weapon_input, &control_defaults,
                &game.level_runtime, &number_weapon_inventory, error, sizeof(error)) ||
            number_weapon_player.gun_selected != 5u ||
            number_weapon_player.reset_weapon_animation != UINT8_MAX ||
            !game_input_set_raw_key(&number_weapon_input, 6u, 0, error, sizeof(error))) {
            fprintf(stderr, "source rocket-launcher key selection is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        /* raw key $08 is RAWKEY_8, which selects source weapon table entry 7. */
        if (!game_input_set_raw_key(&number_weapon_input, 8u, 1, error, sizeof(error)) ||
            !player_runtime_update_discrete_controls(
                &number_weapon_player, &number_weapon_input, &control_defaults,
                &game.level_runtime, &number_weapon_inventory, error, sizeof(error)) ||
            number_weapon_player.gun_selected != 7u ||
            number_weapon_player.reset_weapon_animation != UINT8_MAX ||
            !player_runtime_update_discrete_controls(
                &number_weapon_player, &number_weapon_input, &control_defaults,
                &game.level_runtime, &number_weapon_inventory, error, sizeof(error)) ||
            number_weapon_player.gun_selected != 7u ||
            number_weapon_player.reset_weapon_animation != UINT8_MAX) {
            fprintf(stderr, "source number-key weapon selection is inconsistent: %s\\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/player.s:plr_Fall/plr_DoFootstepFX: one source floor material step. */
        LevelDynamicState footstep_level = {0};
        PlayerRuntime footstep_player = game.player;
        GameInput footstep_input;
        GameAudioEvents footstep_events;
        GameFloorData footstep_floor;
        LevelZone footstep_zone;
        uint8_t *zone_offset_bytes;
        uint8_t *zone_bytes;
        uint32_t zone_offset;
        int16_t expected_sample;

        if (!level_dynamic_state_init(&footstep_level, &game.dynamic_level.runtime,
                                      error, sizeof(error)) ||
            !level_dynamic_state_get_graphics_range(
                &footstep_level,
                footstep_level.runtime.zone_offsets_table_offset +
                    (size_t)footstep_player.zone_index * 4u,
                4u, &zone_offset_bytes)) {
            fprintf(stderr, "footstep source-level fixture could not be prepared: %s\n", error);
            level_dynamic_state_destroy(&footstep_level);
            game_bootstrap_destroy(&game);
            return 1;
        }
        zone_offset = read_be32(zone_offset_bytes);
        if (!level_dynamic_state_get_level_range(&footstep_level, zone_offset, 50u,
                                                 &zone_bytes) ||
            !game_link_get_floor_data(&game.game_link_catalog, 1u, &footstep_floor,
                                      error, sizeof(error)) ||
            footstep_floor.sound_effect == 0u) {
            fprintf(stderr, "footstep source material fixture is invalid: %s\n", error);
            level_dynamic_state_destroy(&footstep_level);
            game_bootstrap_destroy(&game);
            return 1;
        }
        /* ZoneT_FloorNoise_w selects GLFT entry one; no water direct-slot override. */
        write_be16(zone_bytes + 44u, 1u);
        write_be32(zone_bytes + 18u, read_be32(zone_bytes + 2u));
        if (!level_runtime_get_zone(&footstep_level.runtime, footstep_player.zone_index,
                                    &footstep_zone, error, sizeof(error))) {
            fprintf(stderr, "footstep fixture zone could not be read: %s\n", error);
            level_dynamic_state_destroy(&footstep_level);
            game_bootstrap_destroy(&game);
            return 1;
        }
        expected_sample = (int16_t)(uint16_t)(footstep_floor.sound_effect - 1u);
        footstep_player.walk_sfx_time = 0u;
        game_input_init(&footstep_input);
        game_audio_events_init(&footstep_events);
        if (!game_input_set_raw_key(
                &footstep_input,
                control_defaults.assigned_raw_keys[GAME_CONTROL_FORWARDS], 1,
                error, sizeof(error)) ||
            !player_runtime_update_spatial_with_motion_and_audio(
                &footstep_player, &footstep_input, &control_defaults, &game.preferences,
                &game.math, &footstep_level.runtime, &footstep_level, NULL,
                NULL, NULL,
                &game.game_link_catalog, &footstep_events, error, sizeof(error)) ||
            footstep_events.count != 1u ||
            footstep_events.events[0u].sample_index != (uint16_t)expected_sample ||
            footstep_events.events[0u].volume != 80u ||
            footstep_events.events[0u].world_x != 0 ||
            footstep_events.events[0u].world_z != 100 ||
            footstep_events.events[0u].listener_relative == 0u ||
            footstep_events.events[0u].source_id != UINT16_C(0xfff8) ||
            footstep_events.events[0u].channel_pick != 0u ||
            footstep_events.events[0u].echo != footstep_zone.echo ||
            footstep_player.walk_sfx_time >= 4096u) {
            fprintf(stderr, "source floor footstep event is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&footstep_level);
            game_bootstrap_destroy(&game);
            return 1;
        }
        game_audio_events_begin(&footstep_events);
        if (!player_runtime_update_spatial_with_motion_and_audio(
                &footstep_player, &footstep_input, &control_defaults, &game.preferences,
                &game.math, &footstep_level.runtime, &footstep_level, NULL,
                NULL, NULL,
                &game.game_link_catalog, &footstep_events, error, sizeof(error)) ||
            footstep_events.count != 0u) {
            fprintf(stderr, "source floor footstep cadence is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&footstep_level);
            game_bootstrap_destroy(&game);
            return 1;
        }
        level_dynamic_state_destroy(&footstep_level);
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
        controlled_player.snap_z_speed == 0 ||
        ((uint32_t)controlled_player.x & UINT32_C(0x0000ffff)) == 0u ||
        ((int32_t)player_runtime_position_to_world(controlled_player.x) -
             player_runtime_position_to_world(game.player.x)) > 8 ||
        ((int32_t)player_runtime_position_to_world(controlled_player.x) -
             player_runtime_position_to_world(game.player.x)) < -8 ||
        ((int32_t)player_runtime_position_to_world(controlled_player.z) -
             player_runtime_position_to_world(game.player.z)) > 8 ||
        ((int32_t)player_runtime_position_to_world(controlled_player.z) -
             player_runtime_position_to_world(game.player.z)) < -8) {
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
    {
        PlayerRuntime dead_player = game.player;
        GameInventory dead_inventory = game.session.player1_inventory;
        GameInput dead_input;
        int32_t original_x = dead_player.x;
        int32_t original_z = dead_player.z;

        dead_player.health = 0u;
        dead_player.snap_x_speed = 0;
        dead_player.snap_z_speed = 0;
        dead_player.snap_yaw_speed = 0;
        dead_player.used = UINT8_MAX;
        dead_player.fire = UINT8_MAX;
        dead_player.clicked = UINT8_MAX;
        dead_inventory.health = 0u;
        game_input_init(&dead_input);
        if (!game_input_set_raw_key(
                &dead_input, control_defaults.assigned_raw_keys[GAME_CONTROL_FORWARDS], 1,
                error, sizeof(error)) ||
            !game_input_set_raw_key(
                &dead_input, control_defaults.assigned_raw_keys[GAME_CONTROL_FIRE], 1,
                error, sizeof(error)) ||
            !game_input_set_raw_key(
                &dead_input, control_defaults.assigned_raw_keys[GAME_CONTROL_OPERATE], 1,
                error, sizeof(error)) ||
            !player_runtime_update_discrete_controls(
                &dead_player, &dead_input, &control_defaults, &game.level_runtime,
                &dead_inventory, error, sizeof(error)) ||
            !player_runtime_update_spatial_with_motion_and_audio(
                &dead_player, &dead_input, &control_defaults, &game.preferences,
                &game.math, &game.level_runtime, NULL, NULL, NULL,
                &dead_inventory, &game.game_link_catalog, NULL, error, sizeof(error)) ||
            dead_player.x != original_x || dead_player.z != original_z ||
            dead_player.snap_x_speed != 0 || dead_player.snap_z_speed != 0 ||
            dead_player.fire != 0u || dead_player.clicked != 0u || dead_player.used != 0u ||
            dead_player.snap_height != 8 * 1024 || dead_player.look_offset != -80) {
            fprintf(stderr, "dead-player fall/friction branch accepted live controls: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        int32_t mouse_remainder = 0;

        /* First-port desktop-to-reference input scaling retains tiny motion. */
        if (game_input_scale_present_mouse_delta(1, 768, 1920, &mouse_remainder) != 0 ||
            mouse_remainder != 768 ||
            game_input_scale_present_mouse_delta(1, 768, 1920, &mouse_remainder) != 0 ||
            mouse_remainder != 1536 ||
            game_input_scale_present_mouse_delta(1, 768, 1920, &mouse_remainder) != 1 ||
            mouse_remainder != 384 ||
            game_input_scale_present_mouse_delta(-3, 768, 1920, &mouse_remainder) != -1 ||
            mouse_remainder != 0) {
            fprintf(stderr, "desktop mouse reference scaling is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        SceneViewWeaponProjection projection = {0};
        SourceVectorEyePoint point;
        SourceVectorEyePoint interpolated_point;
        float matrix[16];

        /* objdrawhires.s:rotate_object applies one common transform to every model. */
        projection.sine = INT16_MAX;
        projection.cosine = 0;
        projection.y_offset = 128;
        projection.depth_bias = 3;
        projection.centre_x = 160u;
        projection.centre_y = 120u;
        projection.scale_numerator = 5u;
        projection.scale_denominator = 3u;
        if (source_vector_texture_coordinate(0x12u, 0x34u) != INT16_C(0x3412) ||
            source_vector_texture_coordinate(0x56u, 0x80u) != (int16_t)UINT16_C(0x8056) ||
            !source_vector_transform_view_weapon_point(
                &projection, 4, -2, 6, &point) ||
            !source_vector_transform_view_weapon_interpolated_point(
                &projection, 4.5f, -1.5f, 5.5f, &interpolated_point) ||
            !source_vector_make_view_weapon_matrix(
                &projection, 16.0f / 9.0f, matrix) ||
            point.x != 255.0f || point.y != 0.0f || point.z != -5.0f ||
            interpolated_point.x < 287.9f || interpolated_point.x > 288.1f ||
            interpolated_point.y != -32.0f ||
            interpolated_point.z < -5.8f || interpolated_point.z > -5.7f ||
            matrix[0] < 0.00780f || matrix[0] > 0.00782f ||
            matrix[5] < 0.01388f || matrix[5] > 0.01390f) {
            fprintf(stderr, "source vector decoding/projection is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    game.session.player1_inventory.health = 199u;
    game_session_finish_single_player(&game.session, 0);
    if (game.session.level_ended == 0u || game.session.level_finished != 0u ||
        game.session.campaign_inventory.health != 200u) {
        fprintf(stderr, "unfinished level unexpectedly changed campaign inventory\n");
        game_bootstrap_destroy(&game);
        return 1;
    }
    game_session_finish_single_player(&game.session, 1);
    if (game.session.level_ended == 0u || game.session.level_finished == 0u ||
        game.session.campaign_inventory.health != 199u) {
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
        uint8_t collectable_slot_original[OBJECT_RUNTIME_SLOT_BYTE_COUNT];
        const uint8_t *object_names;
        size_t object_names_size;
        LevelZone collectable_zone;
        GameObjectDefinition collectable_definition;
        GameObjectAnimationFrame collectable_frame;
        GameInventory collectable_grant;
        GameInventory original_inventory;
        PlayerRuntime original_player;
        GameInventory expected_inventory;
        GameInventory full_inventory;
        GameObjectDefinition failed_collectable_definition;
        uint32_t collected_count;
        uint8_t collectable_message_line;
        uint8_t object_name_message_line;
        uint8_t failed_collectable_message_line;
        uint8_t deduped_failed_collectable_message_line;
        uint8_t failed_collectable_type;

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
            !game_link_table(&game.game_link_catalog, GAME_LINK_TABLE_OBJECT_NAMES,
                             &object_names, &object_names_size) ||
            object_names_size != GAME_LINK_OBJECT_COUNT * 20u ||
            !level_runtime_get_zone(&game.level_runtime, 146u, &collectable_zone,
                                    error, sizeof(error))) {
            fprintf(stderr, "Level B source collectable fixture is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        original_player = game.player;
        game.player.zone_index = 146u;
        game.player.stood_in_top = 0u;
        game.player.tmp_x = player_runtime_world_to_position(
            (int16_t)read_be16(collectable_point + 0u));
        game.player.tmp_z = player_runtime_world_to_position(
            (int16_t)read_be16(collectable_point + 4u));
        game.player.tmp_height = 12 * 1024;
        game.player.tmp_y = collectable_zone.floor - game.player.tmp_height;
        memcpy(collectable_slot_original, collectable_slot, sizeof(collectable_slot_original));
        original_inventory = game.session.player1_inventory;
        /*
         * newaliencontrol.s:Collectable consumes a PVS worry for an object in
         * a neighbouring zone/layer. Its source vertical placement comes from
         * ShotT_InUpperZone_b, not Plr1's current zone/layer.
         */
        game.player.zone_index = 0u;
        game.player.stood_in_top = UINT8_MAX;
        game.player.tmp_x = player_runtime_world_to_position(
            (int16_t)((int16_t)read_be16(collectable_point + 0u) +
                      (int16_t)collectable_definition.collision_radius));
        collectable_slot[62u] = 1u;
        if (!game_link_get_object_animation_frame(
                &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT, 0u,
                read_be16(collectable_slot + 34u), &collectable_frame,
                error, sizeof(error)) ||
            !object_collectables_update_single_player(
                &game.object_runtime, &game.level_runtime, &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits,
                &game.message_runtime, game.preferences.show_messages,
                0u, &collected_count, error, sizeof(error)) ||
            collected_count != 0u || (int16_t)read_be16(collectable_slot + 12u) != 146 ||
            collectable_slot[62u] != 0u ||
            (int16_t)read_be16(collectable_slot + 4u) !=
                (int16_t)(source_asr32_7(collectable_zone.floor) +
                          (int16_t)collectable_frame.signed_byte_4 * 2) ||
            read_be16(collectable_slot + 34u) != collectable_frame.next_timer1) {
            fprintf(stderr, "Level B cross-zone collectable activation is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        memcpy(collectable_slot, collectable_slot_original, sizeof(collectable_slot_original));
        game.player = original_player;
        game.session.player1_inventory = original_inventory;
        game.player.zone_index = 146u;
        game.player.stood_in_top = 0u;
        game.player.tmp_x = player_runtime_world_to_position(
            (int16_t)read_be16(collectable_point + 0u));
        game.player.tmp_z = player_runtime_world_to_position(
            (int16_t)read_be16(collectable_point + 4u));
        game.player.tmp_height = 12 * 1024;
        game.player.tmp_y = collectable_zone.floor - game.player.tmp_height;
        /* Exercise Plr1_CollectItem's authored display-text message branch. */
        write_be16(collectable_slot + 24u, 0u);
        game.preferences.show_messages = UINT8_MAX;
        collectable_message_line = game.message_runtime.fullscreen != 0u ?
            (uint8_t)((game.message_runtime.line_number + 1u) &
                      (MESSAGE_RUNTIME_LINE_COUNT - 1u)) :
            (game.message_runtime.line_number < MESSAGE_RUNTIME_MAX_LINES_SMALL ?
                (uint8_t)(game.message_runtime.line_number + 1u) : 0u);
        expected_inventory = original_inventory;
        game_inventory_apply_grant(&expected_inventory, &collectable_grant,
                                   &game.inventory_limits);
        /* Collectable only reaches DEFANIMOBJ after objmoveanim has worried its slot. */
        collectable_slot[62u] = 1u;
        if (!game_link_get_object_animation_frame(
                &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT, 0u,
                read_be16(collectable_slot + 34u), &collectable_frame,
                error, sizeof(error)) ||
            !object_collectables_update_single_player(
                &game.object_runtime, &game.level_runtime, &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits,
                &game.message_runtime, game.preferences.show_messages,
                0u,
                &collected_count, error, sizeof(error)) ||
            collected_count != 1u ||
            (int16_t)read_be16(collectable_slot + 12u) != -1 ||
            collectable_slot[62u] != 0u ||
            (int16_t)read_be16(collectable_slot + 4u) !=
                (int16_t)(source_asr32_7(collectable_zone.floor) +
                          (int16_t)collectable_frame.signed_byte_4 * 2) ||
            read_be16(collectable_slot + 34u) != collectable_frame.next_timer1 ||
            game.message_runtime.lines[collectable_message_line].text !=
                game.level_runtime.level_bytes ||
            (game.message_runtime.lines[collectable_message_line].length_and_tag &
             (uint16_t)~MESSAGE_RUNTIME_LENGTH_MASK) !=
                (uint16_t)(MESSAGE_RUNTIME_TAG_NARRATIVE << MESSAGE_RUNTIME_TAG_SHIFT) ||
            memcmp(&game.session.player1_inventory, &expected_inventory,
                   sizeof(expected_inventory)) != 0) {
            fprintf(stderr, "Level B source collectable update is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        /* Restore the source slot and cover Plr1_CollectItem:.notext exactly. */
        memcpy(collectable_slot, collectable_slot_original, sizeof(collectable_slot_original));
        game.session.player1_inventory = original_inventory;
        write_be16(collectable_slot + 24u, UINT16_MAX);
        collectable_slot[62u] = 1u;
        object_name_message_line = game.message_runtime.fullscreen != 0u ?
            (uint8_t)((game.message_runtime.line_number + 1u) &
                      (MESSAGE_RUNTIME_LINE_COUNT - 1u)) :
            (game.message_runtime.line_number < MESSAGE_RUNTIME_MAX_LINES_SMALL ?
                (uint8_t)(game.message_runtime.line_number + 1u) : 0u);
        if (!object_collectables_update_single_player(
                &game.object_runtime, &game.level_runtime, &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits,
                &game.message_runtime, game.preferences.show_messages,
                0u,
                &collected_count, error, sizeof(error)) ||
            collected_count != 1u ||
            game.message_runtime.lines[object_name_message_line].text != object_names ||
            game.message_runtime.lines[object_name_message_line].length_and_tag !=
                (uint16_t)(20u |
                           (MESSAGE_RUNTIME_TAG_DEFAULT << MESSAGE_RUNTIME_TAG_SHIFT)) ||
            memcmp(&game.session.player1_inventory, &expected_inventory,
                   sizeof(expected_inventory)) != 0) {
            fprintf(stderr, "Level B source object-name collectable update is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        /* Plr1_CollectItem's failed single-player Timer2/deduplicated narrative path. */
        memcpy(collectable_slot, collectable_slot_original, sizeof(collectable_slot_original));
        memset(&full_inventory, 0, sizeof(full_inventory));
        full_inventory.health = game.inventory_limits.health;
        full_inventory.jetpack_fuel = game.inventory_limits.jetpack_fuel;
        for (uint16_t ammunition_index = 0u;
             ammunition_index < GAME_INVENTORY_AMMUNITION_COUNT;
             ++ammunition_index) {
            full_inventory.ammunition[ammunition_index] =
                game.inventory_limits.ammunition[ammunition_index];
        }
        failed_collectable_type = UINT8_MAX;
        for (uint8_t object_type = 0u; object_type < GAME_LINK_OBJECT_COUNT; ++object_type) {
            GameObjectDefinition definition;
            GameInventory grant;

            if (!game_link_get_object_definition(&game.game_link_catalog, object_type,
                                                 &definition, error, sizeof(error)) ||
                !game_link_get_object_inventory_grant(&game.game_link_catalog, object_type,
                                                      &grant, error, sizeof(error))) {
                fprintf(stderr, "could not read failed-collection source fixture: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (definition.behaviour == 0u &&
                !game_inventory_can_collect_single_player(&full_inventory, &grant,
                                                          &game.inventory_limits)) {
                failed_collectable_type = object_type;
                failed_collectable_definition = definition;
                break;
            }
        }
        if (failed_collectable_type == UINT8_MAX) {
            fprintf(stderr, "Level B failed-collection source fixture is not inventory-bound\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        game.session.player1_inventory = full_inventory;
        collectable_slot[54u] = failed_collectable_type;
        collectable_slot[62u] = 1u;
        game.player.tmp_y = (failed_collectable_definition.floor_ceiling == 0u ?
                                 collectable_zone.floor : collectable_zone.roof) -
            game.player.tmp_height;
        write_be16(collectable_slot + 40u, 0u);
        write_be32(collectable_slot + 50u, 0u);
        failed_collectable_message_line = game.message_runtime.fullscreen != 0u ?
            (uint8_t)((game.message_runtime.line_number + 1u) &
                      (MESSAGE_RUNTIME_LINE_COUNT - 1u)) :
            (game.message_runtime.line_number < MESSAGE_RUNTIME_MAX_LINES_SMALL ?
                (uint8_t)(game.message_runtime.line_number + 1u) : 0u);
        if (!object_collectables_update_single_player(
                &game.object_runtime, &game.level_runtime, &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits,
                &game.message_runtime, game.preferences.show_messages,
                1000u, &collected_count, error, sizeof(error)) ||
            collected_count != 0u || read_be16(collectable_slot + 40u) != 199u ||
            (int16_t)read_be16(collectable_slot + 12u) != 146 ||
            game.message_runtime.lines[failed_collectable_message_line].text == NULL ||
            memcmp(game.message_runtime.lines[failed_collectable_message_line].text,
                   "I can't carry any more of these just now.",
                   sizeof("I can't carry any more of these just now.") - 1u) != 0 ||
            (game.message_runtime.lines[failed_collectable_message_line].length_and_tag &
             (uint16_t)~MESSAGE_RUNTIME_LENGTH_MASK) !=
                (uint16_t)(MESSAGE_RUNTIME_TAG_NARRATIVE << MESSAGE_RUNTIME_TAG_SHIFT) ||
            memcmp(&game.session.player1_inventory, &full_inventory,
                   sizeof(full_inventory)) != 0) {
            fprintf(stderr, "Level B source failed-collection notification is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        collectable_slot[62u] = 1u;
        if (!object_collectables_update_single_player(
                &game.object_runtime, &game.level_runtime, &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits,
                &game.message_runtime, game.preferences.show_messages,
                1001u, &collected_count, error, sizeof(error)) ||
            collected_count != 0u || read_be16(collectable_slot + 40u) != 198u ||
            game.message_runtime.line_number != failed_collectable_message_line) {
            fprintf(stderr, "Level B source failed-collection Timer2 decay is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be16(collectable_slot + 40u, 0u);
        collectable_slot[62u] = 1u;
        if (!object_collectables_update_single_player(
                &game.object_runtime, &game.level_runtime, &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits,
                &game.message_runtime, game.preferences.show_messages,
                1001u, &collected_count, error, sizeof(error)) ||
            collected_count != 0u || read_be16(collectable_slot + 40u) != 199u ||
            game.message_runtime.line_number != failed_collectable_message_line) {
            fprintf(stderr, "Level B source failed-collection deduplication is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be16(collectable_slot + 40u, 0u);
        collectable_slot[62u] = 1u;
        deduped_failed_collectable_message_line = game.message_runtime.fullscreen != 0u ?
            (uint8_t)((game.message_runtime.line_number + 1u) &
                      (MESSAGE_RUNTIME_LINE_COUNT - 1u)) :
            (game.message_runtime.line_number < MESSAGE_RUNTIME_MAX_LINES_SMALL ?
                (uint8_t)(game.message_runtime.line_number + 1u) : 0u);
        if (!object_collectables_update_single_player(
                &game.object_runtime, &game.level_runtime, &game.game_link_catalog,
                &game.player, &game.session.player1_inventory, &game.inventory_limits,
                &game.message_runtime, game.preferences.show_messages,
                3000u, &collected_count, error, sizeof(error)) ||
            collected_count != 0u || read_be16(collectable_slot + 40u) != 199u ||
            game.message_runtime.line_number != deduped_failed_collectable_message_line ||
            game.message_runtime.lines[deduped_failed_collectable_message_line].text == NULL ||
            memcmp(game.message_runtime.lines[deduped_failed_collectable_message_line].text,
                   "I can't carry any more of these just now.",
                   sizeof("I can't carry any more of these just now.") - 1u) != 0) {
            fprintf(stderr, "Level B source failed-collection dedup expiry is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    /*
     * hires.s:objmoveanim's first VBlank prepares the later worry pass. Its
     * DOALLANIMS counter then waits five VBlanks before the sixth animation
     * pass revisits those worried slots. Run that exact direct-play boundary
     * for every authored level without forcing the separate source exit-zone
     * completion path.
     */
    {
        GameBootstrap direct_game;

        /* This samples the direct campaign path from source process startup, not test-mutated BSS. */
        memset(&direct_game, 0, sizeof(direct_game));
        if (!game_bootstrap_init(&direct_game, argv[1], error, sizeof(error)) ||
            !game_session_default(&direct_game.session, &direct_game.game_link_catalog,
                                  error, sizeof(error))) {
            fprintf(stderr, "could not initialize direct-play campaign smoke test: %s\n", error);
            game_bootstrap_destroy(&direct_game);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (level_index = 0u; level_index < GAME_LINK_LEVEL_COUNT; ++level_index) {
            uint8_t expected_animation_thistime;
            int saw_animation_pass = 0;

            if (!game_session_select_level(&direct_game.session, level_index, error, sizeof(error)) ||
                !game_bootstrap_start_selected_single_player(
                    &direct_game, argv[1], error, sizeof(error))) {
                fprintf(stderr, "campaign level %u could not start direct-play smoke test: %s\n",
                        level_index, error);
                game_bootstrap_destroy(&direct_game);
                game_bootstrap_destroy(&game);
                return 1;
            }
            direct_game.dynamic_level.runtime.exit_zone_id = -1;
            game_input_init(&direct_game.input);
            /* hires.s:thistime is process-lifetime state, not Game_Begin level state. */
            expected_animation_thistime = direct_game.object_animation_runtime.thistime;
            for (uint16_t frame_index = 1u; frame_index <= 6u; ++frame_index) {
                expected_animation_thistime = (uint8_t)(expected_animation_thistime - 1u);
                if ((int8_t)expected_animation_thistime <= 0) {
                    expected_animation_thistime = 5u;
                    saw_animation_pass = 1;
                }
                if (!game_bootstrap_update_single_player_at_time(
                        &direct_game, (uint64_t)frame_index * 20u, error, sizeof(error))) {
                    fprintf(stderr, "campaign level %u direct-play VBlank %u failed: %s\n",
                            level_index, frame_index, error);
                    game_bootstrap_destroy(&direct_game);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
            if (direct_game.session.level_finished != 0u ||
                direct_game.message_time_milliseconds != 120u ||
                direct_game.object_animation_runtime.thistime != expected_animation_thistime ||
                saw_animation_pass == 0 || direct_game.static_scene.wall_count == 0u ||
                (direct_game.object_runtime.active_slot_count >
                     OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT &&
                 (!direct_game.object_animation_runtime.extended_workspace ||
                  direct_game.object_animation_runtime.extended_workspace_slot_count <
                      direct_game.object_runtime.active_slot_count -
                          OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT))) {
                fprintf(stderr,
                        "campaign level %u direct-play six-VBlank smoke is inconsistent: "
                        "finished=%u time=%llu thistime=%u expected=%u pass=%d walls=%u: %s\n",
                        level_index, (unsigned int)direct_game.session.level_finished,
                        (unsigned long long)direct_game.message_time_milliseconds,
                        (unsigned int)direct_game.object_animation_runtime.thistime,
                        (unsigned int)expected_animation_thistime, saw_animation_pass,
                        (unsigned int)direct_game.static_scene.wall_count, error);
                game_bootstrap_destroy(&direct_game);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        game_bootstrap_destroy(&direct_game);
    }
    /*
     * SDL maps physical W/A/S/D into these source AssignableKeys_vb defaults.
     * Keep the authored completion zone out of this input-to-update check: its
     * scope is modules/player.s:plr_KeyboardControl and hires.s:Plr1_Control,
     * not level-completion presentation.
     */
    {
        const SceneMeshSurface *wall_surface = NULL;
        const SceneMeshSurface *flat_surface = NULL;
        size_t world_surface_count = 0u;
        size_t geometry_instance_count = 0u;
        size_t static_instance_count = 0u;
        size_t dynamic_instance_count = 0u;
        size_t first_sprite_command = SIZE_MAX;

        if (!game_session_select_level(&game.session, 0u, error, sizeof(error)) ||
            !game_bootstrap_start_selected_single_player(&game, argv[1], error, sizeof(error)) ||
            !object_scene_count_active(&game.object_runtime, &active_sprite_count,
                                       error, sizeof(error))) {
            fprintf(stderr, "mesh-instance scene submission failed: %s\n", error);
            scene_frame_destroy(&frame);
            game_bootstrap_destroy(&game);
            return 1;
        }
        /*
         * A rejected move preserves these low words. Source rendering must
         * not expose them as a physical camera endpoint.
         */
        game.player.x = (int32_t)((uint32_t)game.player.x | UINT32_C(0x1234));
        game.player.z = (int32_t)((uint32_t)game.player.z | UINT32_C(0x5678));
        game.player.snap_x = game.player.x;
        game.player.snap_z = game.player.z;
        if (!scene_frame_init(&frame, 2u) ||
            !game_bootstrap_submit_scene_frame(&game, &frame)) {
            fprintf(stderr, "mesh-instance scene submission failed\n");
            scene_frame_destroy(&frame);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (size_t command_index = 0u; command_index < frame.count; ++command_index) {
            const SceneCommand *scene_command = &frame.commands[command_index];

            if (scene_command->type == SCENE_COMMAND_GEOMETRY_INSTANCE) {
                const SceneGeometryInstance *instance =
                    &scene_command->data.geometry_instance;

                ++geometry_instance_count;
                if (instance->mesh.acceleration_class == SCENE_ACCELERATION_CLASS_STATIC) {
                    ++static_instance_count;
                } else if (instance->mesh.acceleration_class == SCENE_ACCELERATION_CLASS_DYNAMIC) {
                    ++dynamic_instance_count;
                } else {
                    fprintf(stderr, "scene mesh has an invalid acceleration class\n");
                    scene_frame_destroy(&frame);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                for (uint32_t surface_index = 0u; surface_index < instance->mesh.surface_count;
                     ++surface_index) {
                    const SceneMeshSurface *surface = &instance->mesh.surfaces[surface_index];

                    ++world_surface_count;
                    if (surface->geometry.source_record_id ==
                            game.static_scene.walls[0].source_record_offset &&
                        surface->geometry.primitive == SCENE_GEOMETRY_PRIMITIVE_WALL) {
                        wall_surface = surface;
                    }
                    if (surface->geometry.source_record_id ==
                            game.static_scene.flats[0].source_record_offset &&
                        surface->geometry.primitive == game.static_scene.flats[0].primitive) {
                        flat_surface = surface;
                    }
                }
            } else if (scene_command->type == SCENE_COMMAND_SPRITE_INSTANCE &&
                       first_sprite_command == SIZE_MAX) {
                first_sprite_command = command_index;
            }
        }
        if (!level_runtime_get_zone(&game.level_runtime, game.level.player1_start_zone,
                                    &zone, error, sizeof(error)) ||
            player_runtime_position_to_world(game.player.x) != game.level.player1_start_x ||
            player_runtime_position_to_world(game.player.z) != game.level.player1_start_z ||
            ((uint32_t)game.player.x & UINT32_C(0xffff)) != UINT32_C(0x1234) ||
            ((uint32_t)game.player.z & UINT32_C(0xffff)) != UINT32_C(0x5678) ||
            game.player.y != zone.floor - 12 * 1024 ||
            game.player.snap_x != game.player.x || game.player.snap_y != game.player.y ||
            game.player.snap_z != game.player.z || game.player.snap_target_y != game.player.y ||
            game.player.presentation_x !=
                player_runtime_world_to_position(game.level.player1_start_x) ||
            game.player.presentation_z !=
                player_runtime_world_to_position(game.level.player1_start_z) ||
            game.player.height != 12 * 1024 || game.player.default_enemy_flags != 0x23u ||
            frame.count < 3u + geometry_instance_count + active_sprite_count ||
            frame.commands[0u].type != SCENE_COMMAND_CAMERA ||
            frame.commands[0u].data.camera.position.x !=
                player_runtime_position_to_world(game.player.x) ||
            frame.commands[0u].data.camera.position.y != game.player.y ||
            frame.commands[0u].data.camera.source_position_x_16_16 !=
                game.player.presentation_x ||
            frame.commands[0u].data.camera.source_position_z_16_16 !=
                game.player.presentation_z ||
            frame.commands[0u].data.camera.has_source_position_16_16 == 0u ||
            frame.commands[1u].type != SCENE_COMMAND_LIGHTING ||
            frame.commands[1u].data.lighting.current_point_brightness !=
                &game.lighting_runtime.current_point_brightness[0][0] ||
            frame.commands[2u].type != SCENE_COMMAND_ENVIRONMENT ||
            frame.commands[2u].data.environment.backdrop_byte_count != 648u * 240u ||
            frame.commands[2u].data.environment.water_byte_count != 256u * 256u ||
            geometry_instance_count == 0u || static_instance_count != 1u ||
            dynamic_instance_count == 0u ||
            world_surface_count != (size_t)game.static_scene.wall_count +
                game.static_scene.flat_count ||
            !wall_surface || !flat_surface ||
            wall_surface->geometry.vertices != game.static_scene.walls[0].vertices ||
            wall_surface->geometry.vertex_count != 6u ||
            wall_surface->material.source_asset_id != game.static_scene.walls[0].material_id ||
            wall_surface->material.source_palette_byte_count != 64u * 32u ||
            flat_surface->geometry.vertices != game.static_scene.flats[0].vertices ||
            flat_surface->geometry.vertex_count != game.static_scene.flats[0].vertex_count ||
            flat_surface->material.source_asset_id != game.static_scene.flats[0].material_id ||
            flat_surface->material.source_palette_bytes != game.shared_resources.texture_palette.bytes ||
            first_sprite_command == SIZE_MAX ||
            !scene_sprite_commands_match_source(&frame, first_sprite_command, &game,
                                                active_sprite_count, error, sizeof(error))) {
            fprintf(stderr, "mesh-instance scene handoff is inconsistent: %s\n", error);
            scene_frame_destroy(&frame);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        static const uint16_t movement_bindings[] = {
            GAME_CONTROL_FORWARDS,
            GAME_CONTROL_BACKWARDS,
            GAME_CONTROL_SIDESTEP_LEFT,
            GAME_CONTROL_SIDESTEP_RIGHT
        };
        GameBootstrap movement_game;

        for (size_t binding_index = 0u;
             binding_index < sizeof(movement_bindings) / sizeof(movement_bindings[0]);
             ++binding_index) {
            memset(&movement_game, 0, sizeof(movement_game));
            if (!game_bootstrap_init(&movement_game, argv[1], error, sizeof(error)) ||
                !game_session_default(&movement_game.session,
                                      &movement_game.game_link_catalog,
                                      error, sizeof(error)) ||
                !game_session_select_level(&movement_game.session, 0u, error, sizeof(error)) ||
                !game_bootstrap_start_selected_single_player(
                    &movement_game, argv[1], error, sizeof(error))) {
                fprintf(stderr, "could not initialize movement input regression: %s\n", error);
                game_bootstrap_destroy(&movement_game);
                game_bootstrap_destroy(&game);
                return 1;
            }
            movement_game.dynamic_level.runtime.exit_zone_id = -1;
            if (!game_input_set_raw_key(
                    &movement_game.input,
                    movement_game.controls.assigned_raw_keys[movement_bindings[binding_index]],
                    1, error, sizeof(error))) {
                fprintf(stderr, "could not press source movement binding %u: %s\n",
                        (unsigned int)movement_bindings[binding_index], error);
                game_bootstrap_destroy(&movement_game);
                game_bootstrap_destroy(&game);
                return 1;
            }
            for (uint16_t frame_index = 1u; frame_index <= 120u; ++frame_index) {
                if (!game_bootstrap_update_single_player_at_time(
                        &movement_game, (uint64_t)frame_index * 20u, error, sizeof(error)) ||
                    movement_game.session.level_finished != 0u) {
                    fprintf(stderr,
                            "source movement binding %u ended direct play on VBlank %u: %s\n",
                            (unsigned int)movement_bindings[binding_index],
                            (unsigned int)frame_index, error);
                    game_bootstrap_destroy(&movement_game);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
            game_bootstrap_destroy(&movement_game);
        }
    }
    /*
     * Source integration checkpoint: an actual Plr1_Shot trigger must leave
     * an ItsABullet record which Draw_Objects submits in the same frame.  A
     * renderer-only fixture cannot detect a missing player-shot pool handoff.
     */
    {
        GameBootstrap effects_game;
        SceneFrame effects_frame = {0};
        uint8_t saw_live_shot_slot = 0u;
        uint8_t saw_live_shot_sprite = 0u;

        memset(&effects_game, 0, sizeof(effects_game));
        if (!game_bootstrap_init(&effects_game, argv[1], error, sizeof(error)) ||
            !game_session_default(&effects_game.session, &effects_game.game_link_catalog,
                                  error, sizeof(error)) ||
            !game_session_select_level(&effects_game.session, 0u, error, sizeof(error)) ||
            !game_bootstrap_start_selected_single_player(
                &effects_game, argv[1], error, sizeof(error))) {
            fprintf(stderr, "could not initialize source projectile scene regression: %s\n", error);
            game_bootstrap_destroy(&effects_game);
            game_bootstrap_destroy(&game);
            return 1;
        }
        effects_game.dynamic_level.runtime.exit_zone_id = -1;
        if (!game_input_set_raw_key(
                &effects_game.input,
                effects_game.controls.assigned_raw_keys[GAME_CONTROL_FIRE], 1,
                error, sizeof(error)) ||
            !game_bootstrap_update_single_player_at_time(
                &effects_game, 20u, error, sizeof(error))) {
            fprintf(stderr, "source Plr1_Shot projectile regression did not update: %s\n", error);
            game_bootstrap_destroy(&effects_game);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            uint32_t slot_index = effects_game.object_runtime.player_shot_first_slot + shot_index;
            uint8_t *slot = NULL;

            if (!object_runtime_get_slot_bytes(&effects_game.object_runtime, slot_index, &slot)) {
                fprintf(stderr, "source player-shot pool entry is outside the owned list\n");
                game_bootstrap_destroy(&effects_game);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if ((int16_t)read_be16(slot + 12u) >= 0 && slot[16u] == 2u) {
                saw_live_shot_slot = UINT8_MAX;
                break;
            }
        }
        if (!scene_frame_init(&effects_frame, 1u) ||
            !game_bootstrap_submit_scene_frame(&effects_game, &effects_frame)) {
            fprintf(stderr, "could not submit source projectile scene regression\n");
            scene_frame_destroy(&effects_frame);
            game_bootstrap_destroy(&effects_game);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (size_t command_index = 0u; command_index < effects_frame.count; ++command_index) {
            const SceneCommand *command = &effects_frame.commands[command_index];

            if (command->type == SCENE_COMMAND_SPRITE_INSTANCE &&
                command->data.sprite_instance.sprite.source_record_id >=
                    effects_game.object_runtime.player_shot_first_slot &&
                command->data.sprite_instance.sprite.source_record_id <
                    effects_game.object_runtime.player_shot_first_slot +
                        OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT &&
                (command->data.sprite_instance.sprite.flags & SCENE_SPRITE_FLAG_PROJECTILE) != 0u) {
                if (command->data.sprite_instance.sprite.source_aux_offset_x != 0 ||
                    command->data.sprite_instance.sprite.source_aux_offset_y != 0 ||
                    command->data.sprite_instance.sprite.source_width == 0u ||
                    command->data.sprite_instance.sprite.source_height == 0u) {
                    fprintf(stderr,
                            "Plr1_Shot leaked ShotT physics bytes into its bitmap placement\n");
                    scene_frame_destroy(&effects_frame);
                    game_bootstrap_destroy(&effects_game);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                saw_live_shot_sprite = UINT8_MAX;
                break;
            }
        }
        scene_frame_destroy(&effects_frame);
        game_bootstrap_destroy(&effects_game);
        if (saw_live_shot_slot == 0u || saw_live_shot_sprite == 0u) {
            fprintf(stderr,
                    "Plr1_Shot did not hand a live ItsABullet descriptor to Draw_Objects "
                    "(slot=%u sprite=%u)\n",
                    (unsigned int)saw_live_shot_slot, (unsigned int)saw_live_shot_sprite);
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
        game.player.noise_volume = 99;
        if (!game_bootstrap_update_single_player(&game, error, sizeof(error)) ||
            game.player.noise_volume != 0 ||
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
        /* Level A's authored start resolves its static connected-zone chain first. */
        if (!game_bootstrap_update_single_player(&game, error, sizeof(error)) ||
            !object_observation_matches_source(
                &game.object_observation, &game.object_runtime, &game.player, &game.math,
                error, sizeof(error))) {
            fprintf(stderr, "source exit-zone settle is inconsistent: %s\n", error);
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
        GameInventory campaign_before_death;
        uint64_t ended_time;

        if (!game_session_default(&game.session, &game.game_link_catalog,
                                  error, sizeof(error)) ||
            !game_bootstrap_start_selected_single_player(
                &game, argv[1], error, sizeof(error))) {
            fprintf(stderr, "could not load source death/endlevel fixture: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        game.dynamic_level.runtime.exit_zone_id = -1;
        campaign_before_death = game.session.campaign_inventory;
        game.session.player1_inventory.health = 0u;
        game.player.health = 0u;
        if (!game_bootstrap_update_single_player(&game, error, sizeof(error)) ||
            game.session.level_ended == 0u || game.session.level_finished != 0u ||
            memcmp(&game.session.campaign_inventory, &campaign_before_death,
                   sizeof(campaign_before_death)) != 0) {
            fprintf(stderr, "source player-death endlevel handoff is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        ended_time = game.message_time_milliseconds;
        if (!game_bootstrap_update_single_player(&game, error, sizeof(error)) ||
            game.message_time_milliseconds != ended_time) {
            fprintf(stderr, "ended source level continued simulating: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
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
        /*
         * modules/transform.s:CalcPLR1InLine writes a dense observation
         * stream for non-AUX slots.  newplayershoot.s:Plr1_Shot must consume
         * that stream separately from ObjT_PointID, which indexes distances.
         */
        uint8_t slot_bytes[3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[3u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime shot_objects = {0};
        ObjectObservation shot_observation;
        PlayerRuntime shot_player = {0};
        GameBulletDefinition shot_bullet = {0};
        PlayerShotTarget shot_target;

        shot_objects.slot_bytes = slot_bytes;
        shot_objects.slot_count = 3u;
        shot_objects.active_slot_count = 3u;
        shot_objects.point_bytes = point_bytes;
        shot_objects.point_count = 3u;
        /* First normal record uses point 2 but is not in line. */
        write_be16(slot_bytes + 0u, 2u);
        write_be16(slot_bytes + 4u, 4u);
        write_be16(slot_bytes + 12u, 0u);
        slot_bytes[16u] = 1u;
        slot_bytes[17u] = 1u;
        slot_bytes[18u] = 1u;
        /* AUX consumes neither Plr1_ObsInLine_vb nor its distance entry. */
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 1u);
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 3u;
        /* Second normal record uses point 0 and consumes observation entry 1. */
        write_be16(slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 0u);
        write_be16(slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u, 4u);
        write_be16(slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
        slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 1u;
        slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 17u] = 1u;
        slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
        object_observation_init(&shot_observation);
        shot_observation.in_line[1u] = UINT8_MAX;
        shot_observation.distances[0u] = 50u;
        shot_player.height = 12 * 1024;
        if (!player_shoot_find_target_single_player(
                &shot_objects, &shot_observation, &shot_player, &shot_bullet,
                &shot_target, error, sizeof(error)) || shot_target.found != UINT8_MAX ||
            shot_target.slot_index != 2u || shot_target.point_index != 0u ||
            shot_target.distance != 50u) {
            fprintf(stderr, "Plr1_Shot dense observation indexing is inconsistent: %s\n",
                    error);
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
        GameAudioEvents parent_audio;

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
        parent_observation.rotated_x[PARENT_TARGET_SLOT] = 73;
        parent_observation.rotated_z[PARENT_TARGET_SLOT] = -91;
        parent_player.height = 12 * 1024;
        parent_player.tmp_gun_selected = 0u;
        parent_player.tmp_fire = UINT8_MAX;
        parent_player.zone_index = 0u;
        parent_inventory.ammunition[parent_shoot.bullet_type] = 8u;
        game_random_init(&parent_random);
        game_audio_events_init(&parent_audio);
        /* move.b #$fb,IDNUM replaces the high byte and retains this low byte. */
        parent_audio.source_id_register = UINT16_C(0x12ab);
        expected_parent_random = parent_random;
        for (uint16_t shot_index = 0u; shot_index < parent_shoot.bullet_count; ++shot_index) {
            (void)game_random_next(&expected_parent_random);
        }
        if (!player_shoot_update_single_player_with_motion_and_audio(
                &parent_objects, &game.dynamic_level, &parent_observation, &parent_player,
                NULL, &parent_inventory, &game.game_link_catalog, &game.preferences, &game.math,
                &parent_random, 1u, 0u, &parent_audio, error, sizeof(error)) ||
            parent_player.time_to_shoot != (int16_t)parent_shoot.delay ||
            parent_player.noise_volume != 100 ||
            parent_audio.count != 1u ||
            parent_audio.source_id_register != UINT16_C(0xfbab) ||
            parent_audio.events[0u].source_id != UINT16_C(0xfbab) ||
            parent_audio.events[0u].sample_index != parent_shoot.sound_effect ||
            parent_audio.events[0u].world_x != 73 ||
            parent_audio.events[0u].world_z != -91 ||
            parent_audio.events[0u].listener_relative == 0u ||
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
        parent_player.time_to_shoot = 0;
        parent_player.noise_volume = 0;
        parent_inventory.ammunition[parent_shoot.bullet_type] = 0u;
        if (!player_shoot_update_single_player(
                &parent_objects, &game.dynamic_level, &parent_observation, &parent_player,
                &parent_inventory, &game.game_link_catalog, &game.preferences, &game.math,
                &parent_random, 1u, error, sizeof(error)) ||
            parent_player.noise_volume != 100 ||
            parent_inventory.ammunition[parent_shoot.bullet_type] != 0u) {
            fprintf(stderr, "Plr1_Shot out-of-ammunition noise is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        parent_player.time_to_shoot = 0;
        parent_player.noise_volume = 0;
        if (!player_shoot_update_single_player_with_motion_and_audio(
                &parent_objects, &game.dynamic_level, &parent_observation, &parent_player,
                NULL, &parent_inventory, &game.game_link_catalog, &game.preferences, &game.math,
                &parent_random, 1u, UINT8_MAX, NULL, error, sizeof(error)) ||
            parent_player.time_to_shoot != (int16_t)parent_shoot.delay ||
            parent_player.noise_volume != 100 ||
            parent_inventory.ammunition[parent_shoot.bullet_type] != 0u) {
            fprintf(stderr, "desktop infinite-ammo firing did not retain Plr1_Shot semantics\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        enum {
            ALL_WEAPON_TARGET_SLOT = 0u,
            ALL_WEAPON_SHOT_FIRST_SLOT = 1u,
            ALL_WEAPON_PLAYER_SLOT =
                ALL_WEAPON_SHOT_FIRST_SLOT + OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT,
            ALL_WEAPON_COMPANION_SLOT = ALL_WEAPON_PLAYER_SLOT + 2u,
            ALL_WEAPON_TERMINATOR_SLOT = ALL_WEAPON_COMPANION_SLOT + 1u,
            ALL_WEAPON_SLOT_COUNT = ALL_WEAPON_TERMINATOR_SLOT + 1u
        };
        uint8_t slot_bytes[ALL_WEAPON_SLOT_COUNT * OBJECT_RUNTIME_SLOT_BYTE_COUNT];
        uint8_t point_bytes[ALL_WEAPON_TERMINATOR_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT];
        ObjectRuntime weapon_objects = {0};
        ObjectObservation weapon_observation;

        weapon_objects.slot_bytes = slot_bytes;
        weapon_objects.slot_count = ALL_WEAPON_SLOT_COUNT;
        weapon_objects.active_slot_count = ALL_WEAPON_TERMINATOR_SLOT;
        weapon_objects.player_shot_first_slot = ALL_WEAPON_SHOT_FIRST_SLOT;
        weapon_objects.player1_slot = ALL_WEAPON_PLAYER_SLOT;
        weapon_objects.point_bytes = point_bytes;
        weapon_objects.point_count = ALL_WEAPON_TERMINATOR_SLOT;

        for (uint16_t gun_index = 0u; gun_index < GAME_LINK_GUN_COUNT; ++gun_index) {
            GameShootDefinition shoot;
            GameBulletDefinition bullet;
            PlayerRuntime weapon_player = game.player;
            GameInventory weapon_inventory = {0};
            PlayerShotTarget expected_target;
            GameRandom weapon_random;
            GameRandom expected_random;
            uint16_t weapon_random_seed = 0u;
            uint16_t expected_projectile_count;
            int16_t expected_vertical_speed;
            int found_hit_seed = 0;

            if (!game_link_get_shoot_definition(&game.game_link_catalog, gun_index, &shoot,
                                                error, sizeof(error)) ||
                !game_link_get_bullet_definition(&game.game_link_catalog, shoot.bullet_type,
                                                 &bullet, error, sizeof(error))) {
                fprintf(stderr, "could not load source weapon %u: %s\n", gun_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            memset(slot_bytes, 0, sizeof(slot_bytes));
            memset(point_bytes, 0, sizeof(point_bytes));
            object_observation_init(&weapon_observation);
            weapon_player.x = game.player.x;
            weapon_player.y = 0;
            weapon_player.z = game.player.z;
            weapon_player.height = 12 * 1024;
            weapon_player.aim_speed = 512;
            weapon_player.mouse_active = 0u;
            weapon_player.stood_in_top = UINT8_MAX;
            weapon_player.tmp_gun_selected = (uint8_t)gun_index;
            weapon_player.tmp_fire = UINT8_MAX;
            weapon_player.time_to_shoot = 0;
            write_be16(slot_bytes + ALL_WEAPON_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT,
                       ALL_WEAPON_TARGET_SLOT);
            write_be16(slot_bytes + ALL_WEAPON_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                           12u,
                       weapon_player.zone_index);
            slot_bytes[ALL_WEAPON_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 1u;
            slot_bytes[ALL_WEAPON_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 17u] =
                UINT8_MAX;
            slot_bytes[ALL_WEAPON_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 100u;
            write_be32(point_bytes + 0u,
                       (uint32_t)weapon_player.x | UINT32_C(0x00001111));
            write_be32(point_bytes + 4u,
                       (uint32_t)weapon_player.z | UINT32_C(0x00002222));
            for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
                 ++shot_index) {
                uint32_t slot_index = ALL_WEAPON_SHOT_FIRST_SLOT + shot_index;
                uint8_t *shot_slot = slot_bytes +
                    (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
                uint8_t *shot_point = point_bytes +
                    (size_t)slot_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;

                write_be16(shot_slot + 0u, (uint16_t)slot_index);
                write_be16(shot_slot + 12u, UINT16_MAX);
                write_be32(shot_point + 0u, UINT32_C(0x11112222));
                write_be32(shot_point + 4u, UINT32_C(0x33334444));
            }
            for (uint32_t slot_index = ALL_WEAPON_PLAYER_SLOT;
                 slot_index < ALL_WEAPON_TERMINATOR_SLOT; ++slot_index) {
                write_be16(slot_bytes + (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT,
                           (uint16_t)slot_index);
                write_be16(slot_bytes + (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                               12u,
                           UINT16_MAX);
            }
            write_be16(slot_bytes +
                           (size_t)ALL_WEAPON_TERMINATOR_SLOT *
                               OBJECT_RUNTIME_SLOT_BYTE_COUNT,
                       UINT16_MAX);
            weapon_observation.in_line[0u] = UINT8_MAX;
            weapon_observation.distances[0u] = 50u;
            weapon_inventory.ammunition[shoot.bullet_type] =
                (uint16_t)(shoot.bullet_count + 5u);
            if (!player_shoot_find_target_single_player(
                    &weapon_objects, &weapon_observation, &weapon_player, &bullet,
                    &expected_target, error, sizeof(error)) ||
                expected_target.found != UINT8_MAX) {
                fprintf(stderr, "Plr1_Shot could not acquire source fixture target for gun %u: %s\n",
                        gun_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            expected_projectile_count = shoot.bullet_count == 0u ? 1u : shoot.bullet_count;
            expected_vertical_speed = expected_target.vertical_speed;
            if (bullet.gravity != 0u) {
                expected_vertical_speed = source_asr16_count(
                    (int16_t)weapon_player.aim_speed,
                    (uint16_t)(UINT16_C(8) - (uint16_t)bullet.speed));
            }
            if (expected_vertical_speed > 20 * 128) {
                expected_vertical_speed = 20 * 128;
            }
            if (expected_vertical_speed < -20 * 128) {
                expected_vertical_speed = -20 * 128;
            }
            if ((uint16_t)bullet.is_hitscan != 0u) {
                for (uint32_t candidate = 0u; candidate <= UINT16_MAX; ++candidate) {
                    GameRandom candidate_random = {(uint16_t)candidate};
                    uint16_t shot_index;

                    found_hit_seed = 1;
                    for (shot_index = 0u; shot_index < expected_projectile_count; ++shot_index) {
                        if (((game_random_next(&candidate_random) & UINT16_C(0x7fff)) << 1u) ==
                            0u) {
                            found_hit_seed = 0;
                            break;
                        }
                    }
                    if (found_hit_seed != 0) {
                        weapon_random_seed = (uint16_t)candidate;
                        break;
                    }
                }
                if (found_hit_seed == 0) {
                    fprintf(stderr, "could not prepare source hit-roll fixture for gun %u\n",
                            gun_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
            weapon_random.state = weapon_random_seed;
            expected_random = weapon_random;
            if ((uint16_t)bullet.is_hitscan != 0u) {
                for (uint16_t shot_index = 0u; shot_index < expected_projectile_count;
                     ++shot_index) {
                    (void)game_random_next(&expected_random);
                }
            }
            if (!player_shoot_update_single_player(
                    &weapon_objects, &game.dynamic_level, &weapon_observation, &weapon_player,
                    &weapon_inventory, &game.game_link_catalog, &game.preferences, &game.math,
                    &weapon_random, 1u, error, sizeof(error)) ||
                weapon_player.time_to_shoot != (int16_t)shoot.delay ||
                weapon_player.noise_volume != 100 ||
                weapon_inventory.ammunition[shoot.bullet_type] != 5u ||
                read_be16(slot_bytes + (size_t)ALL_WEAPON_COMPANION_SLOT *
                                      OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                              34u) != 1u || weapon_random.state != expected_random.state) {
                fprintf(stderr, "Plr1_Shot common source state is inconsistent for gun %u: %s\n",
                        gun_index, error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if ((uint16_t)bullet.is_hitscan != 0u) {
                if (slot_bytes[ALL_WEAPON_TARGET_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] !=
                    (uint8_t)((uint8_t)bullet.hit_damage *
                              (uint8_t)expected_projectile_count)) {
                    fprintf(stderr, "Plr1_Shot hitscan damage is inconsistent for gun %u\n",
                            gun_index);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                for (uint16_t shot_index = 0u; shot_index < expected_projectile_count;
                     ++shot_index) {
                    const uint8_t *shot_slot = slot_bytes +
                        (size_t)(ALL_WEAPON_SHOT_FIRST_SLOT + shot_index) *
                            OBJECT_RUNTIME_SLOT_BYTE_COUNT;

                    if (read_be16(shot_slot + 12u) != weapon_player.zone_index ||
                        shot_slot[30u] != 1u || shot_slot[31u] != (uint8_t)shoot.bullet_type ||
                        read_be16(shot_slot + 54u) != 0u || shot_slot[52u] != 0u ||
                        shot_slot[62u] != UINT8_MAX) {
                        fprintf(stderr, "Plr1_Shot hitscan dispatch is inconsistent for gun %u\n",
                                gun_index);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                }
            } else {
                int16_t angle = (int16_t)((uint16_t)weapon_player.yaw -
                    (uint16_t)((uint16_t)(shoot.bullet_count - 1u) * 128u));
                int32_t launch_y = weapon_player.y + 30 * 128;

                angle = (int16_t)game_math_wrap_angle_address((uint16_t)angle);
                for (uint16_t shot_index = 0u; shot_index < expected_projectile_count;
                     ++shot_index) {
                    const uint8_t *shot_slot = slot_bytes +
                        (size_t)(ALL_WEAPON_SHOT_FIRST_SLOT + shot_index) *
                            OBJECT_RUNTIME_SLOT_BYTE_COUNT;
                    const uint8_t *shot_point = point_bytes +
                        (size_t)(ALL_WEAPON_SHOT_FIRST_SLOT + shot_index) *
                            OBJECT_RUNTIME_POINT_BYTE_COUNT;
                    int16_t sine;
                    int16_t cosine;

                    if (!game_math_sine(&game.math, (uint16_t)angle, &sine,
                                        error, sizeof(error)) ||
                        !game_math_cosine(&game.math, (uint16_t)angle, &cosine,
                                          error, sizeof(error)) ||
                        shot_slot[16u] != 2u ||
                        read_be16(shot_slot + 12u) != weapon_player.zone_index ||
                        shot_slot[30u] != 0u || shot_slot[31u] != (uint8_t)shoot.bullet_type ||
                        shot_slot[28u] != (uint8_t)bullet.hit_damage ||
                        read_be16(shot_slot + 54u) != (uint16_t)bullet.gravity ||
                        shot_slot[60u] != (uint8_t)bullet.bounce_horizontal ||
                        shot_slot[61u] != (uint8_t)bullet.bounce_vertical ||
                        read_be32(shot_slot + 18u) !=
                            (uint32_t)source_asl32_count(sine, (uint16_t)bullet.speed) ||
                        read_be32(shot_slot + 22u) !=
                            (uint32_t)source_asl32_count(cosine, (uint16_t)bullet.speed) ||
                        read_be16(shot_slot + 42u) != (uint16_t)expected_vertical_speed ||
                        read_be32(shot_slot + 44u) != (uint32_t)launch_y ||
                        read_be16(shot_slot + 4u) !=
                            (uint16_t)source_asr32_7(launch_y) ||
                        read_be32(shot_point + 0u) !=
                            ((uint32_t)(uint16_t)player_runtime_position_to_world(weapon_player.x)
                             << 16u | UINT32_C(0x2222)) ||
                        read_be32(shot_point + 4u) !=
                            ((uint32_t)(uint16_t)player_runtime_position_to_world(weapon_player.z)
                             << 16u | UINT32_C(0x4444)) ||
                        shot_slot[62u] != UINT8_MAX ||
                        shot_slot[63u] != weapon_player.stood_in_top ||
                        read_be32(shot_slot + 36u) != UINT32_C(0x23)) {
                        fprintf(stderr,
                                "firefive source launch state is inconsistent for gun %u: %s\n",
                                gun_index, error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    angle = (int16_t)game_math_wrap_angle_address(
                        (uint16_t)((uint16_t)angle + 256u));
                }
            }
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
        LightingRuntime projectile_lighting;
        LightingRuntime expected_projectile_lighting;
        ObjectMotionRuntime projectile_motion;
        ObjectProjectileSourceRuntime projectile_source_runtime = {0};
        SceneFrame projectile_scene = {0};
        int16_t projectile_old_x;
        int16_t projectile_old_z;
        int16_t projectile_start_x;
        int16_t projectile_start_z;
        int32_t projectile_launch_y;

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
        /* Use the loaded source start state so ItsABullet's MoveObject and
         * anim_BrightenPoints path has a valid authored zone, not an
         * arbitrary synthetic x/y/z triple. */
        projectile_player = game.player;
        projectile_player.yaw = 0u;
        projectile_start_x = player_runtime_position_to_world(projectile_player.x);
        projectile_start_z = player_runtime_position_to_world(projectile_player.z);
        projectile_launch_y = projectile_player.y + 30 * 128;
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
                (uint32_t)source_asl32_count(sine, (uint16_t)projectile_speed);
            uint32_t expected_velocity_z =
                (uint32_t)source_asl32_count(cosine, (uint16_t)projectile_speed);

            if (slot[16u] != 2u ||
                read_be16(slot + 12u) != projectile_player.zone_index ||
                slot[31u] != (uint8_t)projectile_bullet_index ||
                slot[28u] != (uint8_t)projectile_bullet.hit_damage ||
                read_be16(slot + 54u) != (uint16_t)projectile_bullet.gravity ||
                slot[60u] != (uint8_t)projectile_bullet.bounce_horizontal ||
                slot[61u] != (uint8_t)projectile_bullet.bounce_vertical ||
                read_be32(slot + 18u) != expected_velocity_x ||
                read_be32(slot + 22u) != expected_velocity_z ||
                read_be16(slot + 42u) != 2560u ||
                slot[63u] != projectile_player.stood_in_top ||
                read_be16(slot + 58u) != 0u || read_be32(slot + 36u) != 0x23u ||
                read_be32(slot + 44u) != (uint32_t)projectile_launch_y ||
                read_be16(slot + 4u) !=
                    (uint16_t)source_asr32_7(projectile_launch_y) ||
                slot[62u] != UINT8_MAX ||
                read_be32(point + 0u) !=
                    ((uint32_t)(uint16_t)projectile_start_x << 16u | UINT32_C(0x2222)) ||
                read_be32(point + 4u) !=
                    ((uint32_t)(uint16_t)projectile_start_z << 16u | UINT32_C(0x4444))) {
                fprintf(stderr, "firefive source projectile launch state is inconsistent\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        lighting_runtime_init(&projectile_lighting);
        expected_projectile_lighting = projectile_lighting;
        object_motion_runtime_init(&projectile_motion);
        projectile_source_runtime.motion_runtime = &projectile_motion;
        projectile_old_x = (int16_t)(read_be32(point_bytes) >> 16u);
        projectile_old_z = (int16_t)(read_be32(point_bytes + 4u) >> 16u);
        if (!game_link_get_bullet_animation_frame(
                &game.game_link_catalog, GAME_LINK_BULLET_ANIMATION_FLIGHT,
                projectile_bullet_index, 0u, &projectile_frame, error, sizeof(error)) ||
            !object_projectiles_update_flight_animation_slot_with_source_state(
                &projectile_objects, 0u, &game.dynamic_level, &projectile_lighting,
                &projectile_source_runtime,
                &game.game_link_catalog, 1u,
                error, sizeof(error)) ||
            slot_bytes[30u] != 0u ||
            read_be16(slot_bytes + 6u) != projectile_frame.word_2 ||
            slot_bytes[11u] != projectile_frame.byte_1 ||
            projectile_motion.new_x != (int16_t)(read_be32(point_bytes) >> 16u) ||
            projectile_motion.new_z != (int16_t)(read_be32(point_bytes + 4u) >> 16u) ||
            slot_bytes[52u] != ((int16_t)(uint16_t)projectile_bullet.animation_frames < 1 ?
                                     0u : 1u)) {
            fprintf(stderr, "ItsABullet source flight animation is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if (projectile_old_x == (int16_t)(read_be32(point_bytes) >> 16u) &&
            projectile_old_z == (int16_t)(read_be32(point_bytes + 4u) >> 16u)) {
            fprintf(stderr, "ItsABullet source brightness fixture did not move\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        /*
         * Feed the live ItsABullet descriptor into the scene handoff. A
         * projectile can be exactly coplanar with its impact surface, so the
         * GPU needs its source class as well as the ordinary bitmap fields.
         */
        projectile_objects.player1_slot = OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
        if (!scene_frame_init(&projectile_scene, 2u) ||
            !object_scene_submit_active(
                &projectile_objects, &game.game_link_catalog, &game.shared_resources,
                &game.dynamic_level.runtime, &projectile_lighting, &game.math,
                &game.preferences, 0, 0u, &projectile_scene, error, sizeof(error))) {
            fprintf(stderr, "ItsABullet source scene submission is inconsistent: %s\n", error);
            scene_frame_destroy(&projectile_scene);
            game_bootstrap_destroy(&game);
            return 1;
        }
        {
            const SceneSprite *projectile_sprite = NULL;

            for (size_t command_index = 0u; command_index < projectile_scene.count;
                 ++command_index) {
                if (projectile_scene.commands[command_index].type ==
                        SCENE_COMMAND_SPRITE_INSTANCE &&
                    projectile_scene.commands[command_index].data.sprite_instance.sprite.source_record_id ==
                        0u) {
                    projectile_sprite =
                        &projectile_scene.commands[command_index].data.sprite_instance.sprite;
                    break;
                }
            }
            if (!projectile_sprite ||
                (projectile_sprite->flags & SCENE_SPRITE_FLAG_PROJECTILE) == 0u ||
                (projectile_sprite->flags & SCENE_SPRITE_FLAG_PROJECTILE_CONTACT) != 0u ||
                projectile_sprite->presentation_anchor_to_player_weapon == 0u ||
                projectile_sprite->presentation_projectile_velocity_x_16_16 !=
                    (int32_t)read_be32(slot_bytes + 18u) ||
                projectile_sprite->presentation_projectile_velocity_z_16_16 !=
                    (int32_t)read_be32(slot_bytes + 22u) ||
                projectile_sprite->presentation_projectile_velocity_y !=
                    (int16_t)read_be16(slot_bytes + 42u) ||
                projectile_sprite->source_width != (uint8_t)(projectile_frame.word_2 >> 8u) ||
                projectile_sprite->source_height != (uint8_t)projectile_frame.word_2) {
                fprintf(stderr, "ItsABullet source scene projectile handoff is inconsistent\n");
                scene_frame_destroy(&projectile_scene);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        /* Only ItsABullet's stationary pop receives the GPU draw-order bias. */
        slot_bytes[30u] = 1u;
        scene_frame_begin(&projectile_scene);
        if (!object_scene_submit_active(
                &projectile_objects, &game.game_link_catalog, &game.shared_resources,
                &game.dynamic_level.runtime, &projectile_lighting, &game.math,
                &game.preferences, 0, 0u, &projectile_scene, error, sizeof(error))) {
            fprintf(stderr, "ItsABullet stationary contact handoff is inconsistent: %s\n",
                    error);
            scene_frame_destroy(&projectile_scene);
            game_bootstrap_destroy(&game);
            return 1;
        }
        {
            const SceneSprite *contact_sprite = NULL;

            for (size_t command_index = 0u; command_index < projectile_scene.count;
                 ++command_index) {
                if (projectile_scene.commands[command_index].type ==
                        SCENE_COMMAND_SPRITE_INSTANCE &&
                    projectile_scene.commands[command_index].data.sprite_instance.sprite.source_record_id ==
                        0u) {
                    contact_sprite =
                        &projectile_scene.commands[command_index].data.sprite_instance.sprite;
                    break;
                }
            }
            if (!contact_sprite ||
                (contact_sprite->flags & SCENE_SPRITE_FLAG_PROJECTILE_CONTACT) == 0u ||
                contact_sprite->presentation_anchor_to_player_weapon != 0u) {
                fprintf(stderr, "ItsABullet stationary contact flag is inconsistent\n");
                scene_frame_destroy(&projectile_scene);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        slot_bytes[30u] = 0u;
        scene_frame_destroy(&projectile_scene);
        if (!lighting_runtime_brighten_points(
                &expected_projectile_lighting, &game.dynamic_level.runtime,
                (int16_t)-(int16_t)projectile_frame.byte_5,
                (int16_t)(read_be32(point_bytes) >> 16u),
                (int16_t)(read_be32(point_bytes + 4u) >> 16u),
                (int32_t)read_be32(slot_bytes + 44u) - 5 * 128,
                read_be16(slot_bytes + 12u), error, sizeof(error)) ||
            memcmp(&projectile_lighting, &expected_projectile_lighting,
                   sizeof(projectile_lighting)) != 0) {
            fprintf(stderr, "ItsABullet source flight brightness is inconsistent: %s\n", error);
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
                &game.game_link_catalog, &object_handler_context,
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
            int32_t projectile_start_y = 150000;
            int16_t expected_target_height = source_asr32_7(
                projectile_start_y + (int16_t)(uint16_t)projectile_bullet.gravity);

            flight_level.level_bytes = flight_level_bytes;
            flight_level.level_size = sizeof(flight_level_bytes);
            flight_level.graphics_bytes = flight_graphics_bytes;
            flight_level.graphics_size = sizeof(flight_graphics_bytes);
            flight_level.zone_offsets_table_offset = 0u;
            flight_level.zone_count = 1u;
            write_be32(flight_graphics_bytes, 0u);
            write_be16(flight_level_bytes + 0u, 0u);
            write_be32(flight_level_bytes + 2u, 200000u);
            write_be32(flight_level_bytes + 6u, 100000u);
            write_be32(flight_level_bytes + 10u, 200000u);
            write_be32(flight_level_bytes + 14u, 100000u);
            write_be16(flight_level_bytes + 32u, 56u);
            write_be16(flight_level_bytes + 48u, UINT16_MAX);
            write_be16(flight_level_bytes + 56u, UINT16_MAX);
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
            write_be32(flight_slot_bytes + 44u, (uint32_t)projectile_start_y);
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
                    &flight_objects, 0u, &flight_dynamic, &game.lighting_runtime,
                    &game.game_link_catalog, 1u,
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
            /*
             * newanims.s:ItsABullet refines its segment range exactly three
             * times. For this 2,011-unit segment, that yields 2,012: the
             * target at source range + 80 must be hit, while a fourth
             * refinement incorrectly produces 2,011 and rejects it.
             */
            memset(flight_slot_bytes, 0, sizeof(flight_slot_bytes));
            memset(flight_point_bytes, 0, sizeof(flight_point_bytes));
            write_be16(flight_slot_bytes + 0u, 0u);
            write_be16(flight_slot_bytes + 4u, (uint16_t)expected_target_height);
            write_be16(flight_slot_bytes + 12u, 0u);
            flight_slot_bytes[16u] = 2u;
            flight_slot_bytes[28u] = 7u;
            flight_slot_bytes[31u] = (uint8_t)projectile_bullet_index;
            write_be16(flight_slot_bytes + 58u, UINT16_MAX);
            write_be32(flight_slot_bytes + 18u, UINT32_C(2011) << 16u);
            write_be32(flight_slot_bytes + 44u, (uint32_t)projectile_start_y);
            write_be32(flight_slot_bytes + 36u, 1u);
            write_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 1u);
            write_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                       (uint16_t)expected_target_height);
            write_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
            flight_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
            write_be32(flight_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u,
                       UINT32_C(2092) << 16u);
            write_be16(flight_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                       UINT16_MAX);
            if (!object_projectiles_update_flight_animation_slot(
                    &flight_objects, 0u, &flight_dynamic, &game.lighting_runtime,
                    &game.game_link_catalog, 1u,
                    error, sizeof(error)) || flight_slot_bytes[30u] != 1u ||
                read_be32(flight_point_bytes + 0u) != (UINT32_C(2011) << 16u) ||
                flight_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] != 7u ||
                read_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 42u) != 2011u ||
                read_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 44u) != 0u) {
                fprintf(stderr,
                        "ItsABullet source direct-target range refinement is inconsistent: %s\n",
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
            ObjectMotionRuntime flight_motion;
            ObjectProjectileSourceRuntime flight_source = {0};
            ObjectObservation flight_observation;
            GameAudioEvents flight_audio;

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
            /* This fixture owns collision only; its minimal level has no draw graph for flash. */
            for (uint16_t frame = 0u; frame < GAME_LINK_BULLET_ANIMATION_FRAME_COUNT; ++frame) {
                bullet_definition[60u + (size_t)frame *
                    GAME_LINK_BULLET_ANIMATION_FRAME_SIZE + 5u] = 0u;
            }
            if (!game_link_get_bullet_animation_frame(
                    &flight_link, GAME_LINK_BULLET_ANIMATION_FLIGHT,
                    projectile_bullet_index, 0u, &projectile_frame, error, sizeof(error)) ||
                projectile_frame.byte_5 != 0u) {
                fprintf(stderr, "ItsABullet collision fixture did not disable its flash byte\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
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
            /* ZoneT floor is below its roof in the source down-positive Y domain. */
            write_be32(flight_level_bytes + 2u, 200000u);
            write_be32(flight_level_bytes + 6u, 100000u);
            write_be32(flight_level_bytes + 10u, 200000u);
            write_be32(flight_level_bytes + 14u, 100000u);
            write_be16(flight_level_bytes + 32u, 56u);
            flight_level_bytes[37u] = 7u;
            write_be16(flight_level_bytes + 48u, UINT16_MAX);
            write_be16(flight_level_bytes + 56u, 0u);
            write_be16(flight_level_bytes + 58u, UINT16_MAX);
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
            object_motion_runtime_init(&flight_motion);
            object_observation_init(&flight_observation);
            flight_observation.rotated_x[0u] = 123;
            flight_observation.rotated_z[0u] = -45;
            game_audio_events_init(&flight_audio);
            flight_source.motion_runtime = &flight_motion;
            flight_source.audio_events = &flight_audio;
            flight_source.observation = &flight_observation;
            write_be16(flight_slot_bytes + 0u, 0u);
            write_be16(flight_slot_bytes + 12u, 0u);
            flight_slot_bytes[16u] = 2u;
            flight_slot_bytes[31u] = (uint8_t)projectile_bullet_index;
            write_be16(flight_slot_bytes + 58u, UINT16_MAX);
            write_be16(flight_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, UINT16_MAX);
            write_be32(flight_point_bytes + 0u, 0u);
            write_be32(flight_point_bytes + 4u, UINT32_C(0x000a0000));
            write_be32(flight_slot_bytes + 18u, UINT32_C(0x00140000));
            write_be32(flight_slot_bytes + 44u, 100000u);
            write_be16(flight_slot_bytes + 4u, (uint16_t)(100000u >> 7u));
            if (!object_projectiles_update_flight_animation_slot(
                    &flight_objects, 0u, &flight_dynamic, &game.lighting_runtime,
                    &flight_link, 1u,
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
            write_be32(flight_slot_bytes + 44u, 100000u);
            write_be32(flight_point_bytes + 0u, 0u);
            if (!object_projectiles_update_flight_animation_slot(
                    &flight_objects, 0u, &flight_dynamic, &game.lighting_runtime,
                    &flight_link, 1u, error, sizeof(error)) ||
                flight_slot_bytes[30u] != 1u ||
                read_be32(flight_point_bytes + 0u) != UINT32_C(0x000a0000) ||
                read_be32(flight_slot_bytes + 44u) != 100000u - 5u * 128u / 2u) {
                fprintf(stderr,
                        "ItsABullet source horizontal impact is inconsistent: %s "
                        "(status %u, x %08x, acc-y %08x)\n",
                        error, flight_slot_bytes[30u],
                        read_be32(flight_point_bytes + 0u),
                        read_be32(flight_slot_bytes + 44u));
                level_dynamic_state_destroy(&flight_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
            /*
             * ItsABullet:.hitsomething clears a simultaneous lifetime
             * timeout before emitting its wall impact. The port must retain
             * that single source event rather than playing and blasting the
             * same collision twice.
             */
            write_be32(bullet_definition + 8u, 0u);
            write_be32(bullet_definition + 48u, 5u);
            flight_slot_bytes[30u] = 0u;
            flight_slot_bytes[52u] = 0u;
            write_be16(flight_slot_bytes + 58u, 1u);
            write_be32(flight_slot_bytes + 18u, UINT32_C(0x00140000));
            write_be32(flight_slot_bytes + 22u, 0u);
            write_be32(flight_slot_bytes + 44u, 100000u);
            write_be32(flight_point_bytes + 0u, 0u);
            write_be32(flight_point_bytes + 4u, UINT32_C(0x000a0000));
            game_audio_events_begin(&flight_audio);
            if (!object_projectiles_update_flight_animation_slot_with_source_state(
                    &flight_objects, 0u, &flight_dynamic, &game.lighting_runtime,
                    &flight_source, &flight_link, 1u, error, sizeof(error)) ||
                flight_slot_bytes[30u] != 1u || flight_audio.count != 1u ||
                flight_audio.events[0u].sample_index != 4u ||
                flight_audio.events[0u].world_x != flight_observation.rotated_x[0u] ||
                flight_audio.events[0u].world_z != flight_observation.rotated_z[0u] ||
                flight_audio.events[0u].source_id != 0u ||
                flight_audio.events[0u].listener_relative == 0u ||
                flight_audio.events[0u].echo != 7u) {
                fprintf(stderr,
                        "ItsABullet simultaneous wall/timeout impact is inconsistent: %s\n",
                        error);
                level_dynamic_state_destroy(&flight_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
            /* Floor response uses BulT_BounceVert_l rather than ShotT's roof flag byte. */
            write_be32(bullet_definition + 8u, UINT32_MAX);
            write_be32(bullet_definition + 48u, UINT32_MAX);
            write_be32(bullet_definition + 20u, 1u);
            flight_slot_bytes[30u] = 0u;
            flight_slot_bytes[52u] = 0u;
            write_be32(flight_slot_bytes + 18u, 0u);
            write_be16(flight_slot_bytes + 42u, 256u);
            write_be32(flight_slot_bytes + 44u, 199000u);
            if (!object_projectiles_update_flight_animation_slot(
                    &flight_objects, 0u, &flight_dynamic, &game.lighting_runtime,
                    &flight_link, 1u,
                    error, sizeof(error)) ||
                flight_slot_bytes[30u] != 0u ||
                read_be16(flight_slot_bytes + 42u) != UINT16_C(0xff80) ||
                read_be32(flight_slot_bytes + 44u) != 198592u) {
                fprintf(stderr, "ItsABullet source floor bounce is inconsistent: %s\n", error);
                level_dynamic_state_destroy(&flight_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
            /* A finite source lifetime marks the same impact state after movement. */
            write_be32(bullet_definition + 8u, 0u);
            write_be32(bullet_definition + 48u, UINT32_MAX);
            write_be32(bullet_definition + 20u, 0u);
            flight_slot_bytes[30u] = 0u;
            flight_slot_bytes[52u] = 0u;
            write_be16(flight_slot_bytes + 58u, 1u);
            write_be16(flight_slot_bytes + 42u, 0u);
            write_be32(flight_slot_bytes + 44u, 100000u);
            if (!object_projectiles_update_flight_animation_slot(
                    &flight_objects, 0u, &flight_dynamic, &game.lighting_runtime,
                    &flight_link, 1u,
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
            read_be16(slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) !=
                projectile_player.zone_index) {
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
        uint8_t slot_bytes[3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime lock_objects = {0};
        MechanismRuntime lock_runtime;

        lock_objects.slot_bytes = slot_bytes;
        lock_objects.slot_count = 3u;
        lock_objects.active_slot_count = 3u;
        /* The source's alien postamble reaches the immediately preceding AUX slot. */
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 12u, 0u);
        slot_bytes[16u] = 3u;
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 1u);
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = UINT8_MAX;
        /* High word locks doors; low word independently locks lifts. */
        write_be32(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 50u, 0x00050009u);
        write_be16(slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        mechanism_runtime_init(&lock_runtime);
        alien_runtime_init(&lock_alien_runtime);
        alien_runtime_begin_single_player(&lock_alien_runtime);
        lock_runtime.door_and_lift_locks = 0x0002u;
        lock_runtime.lift_only_locks = 0x0010u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &lock_alien_runtime,
                &game.game_link_catalog, &object_handler_context,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            lock_runtime.door_and_lift_locks != 0x0007u ||
            lock_runtime.lift_only_locks != 0x0019u ||
            read_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u) != 0u) {
            fprintf(stderr, "ObjectHandler alien lock preamble is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 0u;
        lock_runtime.door_and_lift_locks = 0u;
        lock_runtime.lift_only_locks = 0u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &lock_alien_runtime,
                &game.game_link_catalog, &object_handler_context,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            lock_runtime.door_and_lift_locks != 0u ||
            lock_runtime.lift_only_locks != 0u) {
            fprintf(stderr, "ObjectHandler dead alien lock suppression is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u, 0u);
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = UINT8_MAX;
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 62u] = UINT8_MAX;
        lock_alien_runtime.no_enemies = 0u;
        lock_runtime.door_and_lift_locks = 0u;
        lock_runtime.lift_only_locks = 0u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &lock_alien_runtime,
                &game.game_link_catalog, &object_handler_context,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            read_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) != UINT16_MAX ||
            read_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u) != 0u ||
            read_be16(slot_bytes + 12u) != UINT16_MAX ||
            read_be16(slot_bytes + 26u) != 0u ||
            lock_runtime.door_and_lift_locks != 0x0005u ||
            lock_runtime.lift_only_locks != 0x0009u) {
            fprintf(stderr, "ItsAnAlien no-enemies gate is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        alien_runtime_begin_single_player(&lock_alien_runtime);
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, UINT16_MAX);
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = UINT8_MAX;
        lock_runtime.door_and_lift_locks = 0u;
        lock_runtime.lift_only_locks = 0u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &lock_alien_runtime,
                &game.game_link_catalog, &object_handler_context,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            read_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u) != UINT16_MAX ||
            lock_runtime.door_and_lift_locks != 0u ||
            lock_runtime.lift_only_locks != 0u) {
            fprintf(stderr, "ObjectHandler negative alien-zone gate is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /*
         * newanims.s:ObjectHandler enters newaliencontrol.s:ItsAnAlien only
         * for a worried slot, then modules/ai.s:AI_MainRoutine's mode-three
         * ai_DoRetreat returns immediately. This verifies the live dispatch
         * preamble and preceding AUX-zone postamble without inventing a route
         * setup for another behaviour.
         */
        uint8_t slot_bytes[3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[2u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime live_objects = {0};
        MechanismRuntime live_mechanism_runtime;
        AlienRuntime live_alien_runtime;

        live_objects.slot_bytes = slot_bytes;
        live_objects.slot_count = 3u;
        live_objects.active_slot_count = 3u;
        live_objects.point_bytes = point_bytes;
        live_objects.point_count = 2u;
        /* The source scans this AUX slot before its alien and later copies into it. */
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 12u, UINT16_C(0x7fff));
        write_be16(slot_bytes + 26u, UINT16_C(0x7fff));
        slot_bytes[16u] = 3u;
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 1u);
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, game.player.zone_index);
        write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u, UINT16_C(0x7fff));
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = UINT8_MAX;
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 20u] = 3u;
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u] = 0u;
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 62u] = UINT8_MAX;
        write_be16(slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        mechanism_runtime_init(&live_mechanism_runtime);
        alien_runtime_init(&live_alien_runtime);
        alien_runtime_begin_single_player(&live_alien_runtime);
        if (!object_handler_update_single_player(
                &live_objects, &game.dynamic_level, &live_mechanism_runtime,
                &live_alien_runtime, &game.game_link_catalog, &object_handler_context,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            read_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 2u) != UINT16_C(0xffec) ||
            read_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u) !=
                game.player.zone_index ||
            read_be16(slot_bytes + 12u) != game.player.zone_index ||
            read_be16(slot_bytes + 26u) != game.player.zone_index) {
            fprintf(stderr, "ObjectHandler live ItsAnAlien dispatch/AUX ordering is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        uint8_t slot_bytes[4u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime lock_objects = {0};
        MechanismRuntime lock_runtime;
        AlienRuntime object_lock_alien_runtime;
        GameObjectDefinition destructible_definition;
        uint8_t collectable_type = UINT8_MAX;
        uint8_t activatable_type = UINT8_MAX;
        uint8_t destructible_type = UINT8_MAX;
        uint16_t object_zone_index;

        for (uint8_t object_type = 0u; object_type < GAME_LINK_OBJECT_COUNT; ++object_type) {
            GameObjectDefinition definition;

            if (!game_link_get_object_definition(&game.game_link_catalog, object_type,
                                                 &definition, error, sizeof(error))) {
                fprintf(stderr, "could not read source object lock definition: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (definition.behaviour == 0u && collectable_type == UINT8_MAX) {
                collectable_type = object_type;
            } else if (definition.behaviour == 1u && activatable_type == UINT8_MAX) {
                activatable_type = object_type;
            } else if (definition.behaviour == 2u && definition.hit_points > 0u &&
                       definition.hit_points <= UINT8_MAX && destructible_type == UINT8_MAX) {
                destructible_type = object_type;
                destructible_definition = definition;
            }
        }
        if (game.dynamic_level.runtime.zone_count < 2u || collectable_type == UINT8_MAX ||
            activatable_type == UINT8_MAX || destructible_type == UINT8_MAX) {
            fprintf(stderr, "source object lock fixtures are unavailable\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        object_zone_index = game.player.zone_index == 0u ? 1u : 0u;
        lock_objects.slot_bytes = slot_bytes;
        lock_objects.slot_count = 4u;
        lock_objects.active_slot_count = 4u;
        for (uint32_t slot_index = 0u; slot_index < 3u; ++slot_index) {
            uint8_t *slot = slot_bytes + (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

            write_be16(slot + 0u, (uint16_t)slot_index);
            write_be16(slot + 12u, object_zone_index);
            slot[16u] = 1u;
        }
        slot_bytes[54u] = collectable_type;
        write_be32(slot_bytes + 50u, 0x00010008u);
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u] = activatable_type;
        write_be32(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 50u, 0x00020010u);
        slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u] = destructible_type;
        write_be32(slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 50u, 0x00040020u);
        write_be16(slot_bytes + 3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        mechanism_runtime_init(&lock_runtime);
        alien_runtime_init(&object_lock_alien_runtime);
        alien_runtime_begin_single_player(&object_lock_alien_runtime);
        lock_runtime.door_and_lift_locks = 0x0010u;
        lock_runtime.lift_only_locks = 0x0040u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &object_lock_alien_runtime,
                &game.game_link_catalog, &object_handler_context,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            lock_runtime.door_and_lift_locks != 0x0017u ||
            lock_runtime.lift_only_locks != 0x0078u ||
            read_be16(slot_bytes + 26u) != object_zone_index ||
            read_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u) != object_zone_index ||
            read_be16(slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u) !=
                object_zone_index) {
            fprintf(stderr, "ObjectHandler source object lock acquisition is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        object_lock_alien_runtime.no_enemies = 0u;
        lock_runtime.door_and_lift_locks = 0x0010u;
        lock_runtime.lift_only_locks = 0x0040u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &object_lock_alien_runtime,
                &game.game_link_catalog, &object_handler_context,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            lock_runtime.door_and_lift_locks != 0x0010u ||
            lock_runtime.lift_only_locks != 0x0040u) {
            fprintf(stderr, "ObjectHandler source no-enemies object lock gate is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        alien_runtime_begin_single_player(&object_lock_alien_runtime);
        slot_bytes[55u] = UINT8_MAX;
        slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 55u] = UINT8_MAX;
        slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] =
            (uint8_t)destructible_definition.hit_points;
        slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 0u;
        lock_runtime.door_and_lift_locks = 0x0010u;
        lock_runtime.lift_only_locks = 0x0040u;
        if (!object_handler_update_single_player(
                &lock_objects, &game.dynamic_level, &lock_runtime, &object_lock_alien_runtime,
                &game.game_link_catalog, &object_handler_context,
                &game.player, &game.session.player1_inventory, &game.inventory_limits, 1u,
                NULL, error, sizeof(error)) ||
            lock_runtime.door_and_lift_locks != 0x0010u ||
            lock_runtime.lift_only_locks != 0x0040u) {
            fprintf(stderr, "ObjectHandler active/dead object lock suppression is inconsistent: %s\n",
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
        decision_player.tmp_x = player_runtime_world_to_position(1);
        decision_player.tmp_z = player_runtime_world_to_position(0);
        decision_player.x = player_runtime_world_to_position(-500);
        decision_player.z = player_runtime_world_to_position(500);
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
                setup.reaction_time != (int16_t)definition.reaction_time ||
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
        /* modules/ai.s:ai_AttackCommon prepares SHOT* state from every AlienT/BulT pair. */
        uint8_t slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime attack_objects = {0};
        AlienAttackSetup attack_setup;
        GameAlienDefinition attack_alien;
        GameBulletDefinition attack_bullet;

        attack_objects.slot_bytes = slot_bytes;
        attack_objects.slot_count = 1u;
        attack_objects.active_slot_count = 1u;
        for (uint16_t alien_index = 0u; alien_index < GAME_LINK_ALIEN_COUNT; ++alien_index) {
            uint16_t expected_shot_speed;

            slot_bytes[54u] = (uint8_t)alien_index;
            if (!game_link_get_alien_definition(&game.game_link_catalog, alien_index,
                                                &attack_alien, error, sizeof(error)) ||
                !game_link_get_bullet_definition(&game.game_link_catalog,
                                                 attack_alien.bullet_type, &attack_bullet,
                                                 error, sizeof(error)) ||
                !alien_attack_setup_from_slot(&attack_objects, 0u, &game.game_link_catalog,
                                              &attack_setup, error, sizeof(error))) {
                fprintf(stderr, "could not prepare ai_AttackCommon source state: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            expected_shot_speed = (uint16_t)(UINT32_C(1) << (attack_bullet.speed & 31u));
            if (attack_setup.shot_type != (uint8_t)attack_alien.bullet_type ||
                attack_setup.shot_power != (uint8_t)attack_bullet.hit_damage ||
                attack_setup.shot_speed != expected_shot_speed ||
                attack_setup.shot_shift != (uint16_t)(attack_bullet.speed - 1u) ||
                attack_setup.is_hitscan !=
                    (attack_bullet.is_hitscan != 0u ? UINT8_MAX : 0u)) {
                fprintf(stderr, "ai_AttackCommon SHOT setup %u is inconsistent\n", alien_index);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
    }
    {
        /* newaliencontrol.s:FireAtPlayer1's alien-shot allocation and launch writes. */
        enum {
            FIRE_ALIEN_SLOT = 0u,
            FIRE_PLAYER_SLOT = 1u,
            FIRE_SHOT_FIRST_SLOT = 2u,
            FIRE_SLOT_COUNT = FIRE_SHOT_FIRST_SLOT + OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT
        };
        uint8_t slot_bytes[FIRE_SLOT_COUNT * OBJECT_RUNTIME_SLOT_BYTE_COUNT];
        uint8_t point_bytes[FIRE_SLOT_COUNT * OBJECT_RUNTIME_POINT_BYTE_COUNT];
        ObjectRuntime fire_objects = {0};
        ObjectAlienShotPresentation
            fire_shot_presentation[OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT] = {{0}};
        PlayerRuntime fire_player = {0};
        AlienSetup fire_alien_setup = {0};
        AlienAttackSetup fire_attack_setup = {0};
        ObjectObservation fire_observation;
        GameAudioEvents fire_audio;
        ObjectApproach expected_approach = {0};
        uint8_t spawned = 0u;
        int16_t expected_vertical_divisor;
        int16_t expected_vertical_speed;
        int32_t expected_accumulated_y;
        int16_t future_x;
        int16_t future_z;

        memset(slot_bytes, 0xa5, sizeof(slot_bytes));
        memset(point_bytes, 0x5a, sizeof(point_bytes));
        fire_objects.slot_bytes = slot_bytes;
        fire_objects.slot_count = FIRE_SLOT_COUNT;
        fire_objects.active_slot_count = FIRE_SLOT_COUNT;
        fire_objects.alien_shot_first_slot = FIRE_SHOT_FIRST_SLOT;
        fire_objects.alien_shot_presentation = fire_shot_presentation;
        fire_objects.player1_slot = FIRE_PLAYER_SLOT;
        fire_objects.point_bytes = point_bytes;
        fire_objects.point_count = FIRE_SLOT_COUNT;
        write_be16(slot_bytes + FIRE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   FIRE_ALIEN_SLOT);
        write_be16(slot_bytes + FIRE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u, 50u);
        write_be16(slot_bytes + FIRE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 3u);
        slot_bytes[FIRE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 63u] = UINT8_MAX;
        write_be32(point_bytes + FIRE_ALIEN_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u,
                   UINT32_C(0x00641111));
        write_be32(point_bytes + FIRE_ALIEN_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   UINT32_C(0x00c82222));
        write_be16(slot_bytes + FIRE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u, 80u);
        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            uint32_t slot_index = FIRE_SHOT_FIRST_SLOT + shot_index;

            write_be16(slot_bytes + slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                       (uint16_t)slot_index);
            write_be16(slot_bytes + slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                       UINT16_MAX);
        }
        write_be32(point_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u,
                   UINT32_C(0x5555beef));
        write_be32(point_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   UINT32_C(0x6666face));
        fire_player.x = player_runtime_world_to_position(500);
        fire_player.z = player_runtime_world_to_position(-100);
        fire_player.source_x_difference = 16;
        fire_player.source_z_difference = -32;
        fire_alien_setup.shot_y_offset = 1234;
        fire_alien_setup.shot_offset_multiplier = 128;
        fire_alien_setup.vector_object_flag = UINT8_MAX;
        fire_alien_setup.zone_echo = 5u;
        write_be16(slot_bytes + FIRE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 8u, 14u);
        write_be16(slot_bytes + FIRE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 10u, 3u);
        fire_attack_setup.shot_type = 3u;
        fire_attack_setup.shot_power = 7u;
        fire_attack_setup.shot_speed = 16u;
        fire_attack_setup.shot_shift = 4u;
        object_observation_init(&fire_observation);
        fire_observation.rotated_x[FIRE_ALIEN_SLOT] = 37;
        fire_observation.rotated_z[FIRE_ALIEN_SLOT] = -29;
        game_audio_events_init(&fire_audio);
        /* DOALLANIMS would have selected the attack-frame sample first. */
        fire_audio.source_sample_index = 23;
        expected_approach.old_x = 100;
        expected_approach.old_z = 200;
        expected_approach.new_x = player_runtime_position_to_world(fire_player.x);
        expected_approach.new_z = player_runtime_position_to_world(fire_player.z);
        if (!object_heading_calculate_distance(&expected_approach, error, sizeof(error))) {
            fprintf(stderr, "FireAtPlayer1 distance fixture is invalid: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        expected_approach.new_x = source_add16(
            expected_approach.new_x,
            (int16_t)source_asr32_count(
                (int32_t)fire_player.source_x_difference * expected_approach.distance / 16,
                4u));
        expected_approach.new_z = source_add16(
            expected_approach.new_z,
            (int16_t)source_asr32_count(
                (int32_t)fire_player.source_z_difference * expected_approach.distance / 16,
                4u));
        future_x = expected_approach.new_x;
        future_z = expected_approach.new_z;
        expected_approach.range = 0;
        expected_approach.speed = 16;
        if (!object_heading_towards(&expected_approach, error, sizeof(error))) {
            fprintf(stderr, "FireAtPlayer1 heading fixture is invalid: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        {
            int16_t source_x_offset = (int16_t)source_asr32_count(
                (int32_t)fire_alien_setup.shot_offset_multiplier *
                    (int16_t)(expected_approach.new_x - expected_approach.old_x),
                8u);
            int16_t source_z_offset = (int16_t)source_asr32_count(
                (int32_t)fire_alien_setup.shot_offset_multiplier *
                    (int16_t)(expected_approach.new_z - expected_approach.old_z),
                8u);

            expected_approach.old_x = source_add16(expected_approach.old_x,
                                                    source_z_offset);
            expected_approach.old_z = (int16_t)((uint16_t)expected_approach.old_z -
                                                (uint16_t)source_x_offset);
            expected_approach.new_x = future_x;
            expected_approach.new_z = future_z;
            if (!object_heading_towards(&expected_approach, error, sizeof(error))) {
                fprintf(stderr, "FireAtPlayer1 offset heading fixture is invalid: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        expected_accumulated_y = 50 * 128 + fire_alien_setup.shot_y_offset;
        expected_vertical_divisor = (int16_t)source_asr32_count(expected_approach.distance, 4u);
        if (expected_vertical_divisor <= 0) {
            expected_vertical_divisor = 1;
        }
        expected_vertical_speed = (int16_t)(((80 - 20) * 128 - expected_accumulated_y) * 2 /
                                            expected_vertical_divisor);
        if (!alien_attack_fire_at_player_one(
                &fire_objects, FIRE_ALIEN_SLOT, &fire_player, &fire_alien_setup,
                &fire_attack_setup, &fire_observation, &fire_audio,
                &spawned, error, sizeof(error)) ||
            spawned != UINT8_MAX ||
            fire_audio.count != 1u || fire_audio.events[0u].sample_index != 23u ||
            fire_audio.events[0u].volume != 100u ||
            fire_audio.events[0u].world_x != 37 || fire_audio.events[0u].world_z != -29 ||
            fire_audio.events[0u].listener_relative == 0u ||
            fire_audio.events[0u].source_id != FIRE_ALIEN_SLOT ||
            fire_audio.events[0u].suppress_if_playing != GAME_AUDIO_RESTART_SOURCE ||
            fire_audio.events[0u].channel_pick != 1u ||
            fire_audio.events[0u].echo != fire_alien_setup.zone_echo ||
            slot_bytes[FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] != 2u ||
            read_be16(slot_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 58u) !=
                0u ||
            slot_bytes[FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 31u] != 3u ||
            slot_bytes[FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 28u] != 7u ||
            slot_bytes[FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 29u] != 0xa5u ||
            read_be16(point_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u) !=
                (uint16_t)expected_approach.new_x ||
            read_be16(point_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 2u) !=
                UINT16_C(0xbeef) ||
            read_be16(point_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u) !=
                (uint16_t)expected_approach.new_z ||
            read_be16(point_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 6u) !=
                UINT16_C(0xface) ||
            read_be16(slot_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u) !=
                (uint16_t)(expected_approach.new_x - expected_approach.old_x) ||
            read_be16(slot_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 22u) !=
                (uint16_t)(expected_approach.new_z - expected_approach.old_z) ||
            read_be32(slot_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 36u) !=
                UINT32_C(0x32) ||
            read_be16(slot_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) !=
                3u ||
            read_be16(slot_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u) !=
                50u ||
            read_be32(slot_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 44u) !=
                (uint32_t)expected_accumulated_y ||
            slot_bytes[FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 63u] != UINT8_MAX ||
            read_be16(slot_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 42u) !=
                (uint16_t)expected_vertical_speed ||
            slot_bytes[FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 62u] != UINT8_MAX ||
            slot_bytes[FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 30u] != 0xa5u ||
            slot_bytes[FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 52u] != 0xa5u ||
            read_be16(slot_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u) !=
                UINT16_C(0xa5a5) ||
            read_be16(slot_bytes + FIRE_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 60u) !=
                UINT16_C(0xa5a5) ||
            fire_objects.alien_shot_presentation[0u].anchor_to_vector_model == 0u ||
            fire_objects.alien_shot_presentation[0u].source_asset_id != 14u ||
            fire_objects.alien_shot_presentation[0u].frame_index != 3u ||
            fire_objects.alien_shot_presentation[0u].source_y_offset != 1234) {
            fprintf(stderr, "FireAtPlayer1 source projectile state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        game_audio_events_begin(&fire_audio);
        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            write_be16(slot_bytes + (FIRE_SHOT_FIRST_SLOT + shot_index) *
                           OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                       0u);
        }
        spawned = UINT8_MAX;
        if (!alien_attack_fire_at_player_one(
                &fire_objects, FIRE_ALIEN_SLOT, &fire_player, &fire_alien_setup,
                &fire_attack_setup, &fire_observation, &fire_audio,
                &spawned, error, sizeof(error)) ||
            spawned != 0u || fire_audio.count != 0u || fire_audio.source_sample_index != 23) {
            fprintf(stderr, "FireAtPlayer1 exhausted-pool path is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_AttackWithProjectile's action/finished attack branch. */
        enum {
            PROJECTILE_ATTACK_AUXILIARY_SLOT = 0u,
            PROJECTILE_ATTACK_ALIEN_SLOT = 1u,
            PROJECTILE_ATTACK_SHOT_FIRST_SLOT = 2u,
            PROJECTILE_ATTACK_PLAYER_SLOT =
                PROJECTILE_ATTACK_SHOT_FIRST_SLOT + OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT,
            PROJECTILE_ATTACK_SLOT_COUNT = PROJECTILE_ATTACK_PLAYER_SLOT + 1u
        };
        uint8_t slot_bytes[PROJECTILE_ATTACK_SLOT_COUNT * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[PROJECTILE_ATTACK_SLOT_COUNT * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime attack_objects = {0};
        ObjectAnimationRuntime attack_animation;
        LightingRuntime attack_lighting;
        AlienRuntime attack_runtime;
        ObjectExplosionRuntime attack_explosion;
        GameProgression attack_progression;
        GameRandom attack_random;
        PlayerRuntime attack_player = game.player;
        AlienSetup attack_alien_setup;
        AlienProjectileAttackState attack_state;
        GameAlienDefinition attack_definition;
        GameBulletDefinition attack_bullet;
        uint16_t attack_alien = UINT16_MAX;

        for (uint16_t alien_index = 0u; alien_index < GAME_LINK_ALIEN_COUNT; ++alien_index) {
            uint16_t source_shot_speed;

            if (!game_link_get_alien_definition(&game.game_link_catalog, alien_index,
                                                &attack_definition, error, sizeof(error)) ||
                !game_link_get_bullet_definition(&game.game_link_catalog,
                                                 attack_definition.bullet_type,
                                                 &attack_bullet, error, sizeof(error))) {
                fprintf(stderr, "could not scan ai_AttackWithProjectile source data: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            source_shot_speed = (uint16_t)(UINT32_C(1) << (attack_bullet.speed & 31u));
            if (attack_definition.girth <= 2u &&
                (int16_t)attack_definition.auxiliary_type >= 0 &&
                attack_definition.auxiliary_type < GAME_LINK_OBJECT_COUNT &&
                attack_bullet.is_hitscan == 0u && source_shot_speed != 0u) {
                attack_alien = alien_index;
                break;
            }
        }
        if (attack_alien == UINT16_MAX) {
            fprintf(stderr, "no source projectile alien supports ai_AttackWithProjectile\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        attack_objects.slot_bytes = slot_bytes;
        attack_objects.slot_count = PROJECTILE_ATTACK_SLOT_COUNT;
        attack_objects.active_slot_count = PROJECTILE_ATTACK_SLOT_COUNT;
        attack_objects.alien_shot_first_slot = PROJECTILE_ATTACK_SHOT_FIRST_SLOT;
        attack_objects.player1_slot = PROJECTILE_ATTACK_PLAYER_SLOT;
        attack_objects.point_bytes = point_bytes;
        attack_objects.point_count = PROJECTILE_ATTACK_SLOT_COUNT;
        write_be16(slot_bytes + PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   PROJECTILE_ATTACK_ALIEN_SLOT);
        write_be16(slot_bytes + PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                   50u);
        write_be16(slot_bytes + PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   attack_player.zone_index);
        write_be16(slot_bytes + PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 30u,
                   0u);
        slot_bytes[PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u] =
            (uint8_t)attack_alien;
        write_be32(point_bytes + PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT,
                   UINT32_C(0x00640000));
        write_be32(point_bytes + PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_POINT_BYTE_COUNT +
                       4u,
                   UINT32_C(0x00c80000));
        for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++shot_index) {
            uint32_t slot_index = PROJECTILE_ATTACK_SHOT_FIRST_SLOT + shot_index;

            write_be16(slot_bytes + slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                       (uint16_t)slot_index);
            write_be16(slot_bytes + slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                       UINT16_MAX);
        }
        write_be16(slot_bytes + PROJECTILE_ATTACK_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                   80u);
        attack_player.x = player_runtime_world_to_position(500);
        attack_player.z = player_runtime_world_to_position(-100);
        attack_player.tmp_x = attack_player.x;
        attack_player.tmp_z = attack_player.z;
        if (!alien_setup_from_slot(&attack_objects, PROJECTILE_ATTACK_ALIEN_SLOT,
                                   &game.dynamic_level.runtime, &game.game_link_catalog,
                                   &attack_alien_setup, error, sizeof(error))) {
            fprintf(stderr, "ai_AttackWithProjectile setup fixture is invalid: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        object_animation_runtime_init(&attack_animation);
        attack_animation.workspace[PROJECTILE_ATTACK_ALIEN_SLOT][0u] = UINT8_MAX;
        attack_animation.workspace[PROJECTILE_ATTACK_ALIEN_SLOT][1u] = UINT8_MAX;
        attack_animation.workspace[PROJECTILE_ATTACK_ALIEN_SLOT][2u] = 0u;
        attack_animation.workspace[PROJECTILE_ATTACK_ALIEN_SLOT][3u] = UINT8_MAX;
        lighting_runtime_init(&attack_lighting);
        alien_runtime_init(&attack_runtime);
        alien_runtime_begin_level(&attack_runtime);
        object_explosion_runtime_init(&attack_explosion);
        game_progression_init(&attack_progression);
        game_random_init(&attack_random);
        if (!alien_attack_with_projectile_update(
                &attack_objects, PROJECTILE_ATTACK_ALIEN_SLOT, &attack_runtime,
                &attack_animation, &attack_lighting, &game.dynamic_level.runtime,
                &game.level_clips, &game.game_link_catalog, &attack_progression,
                &attack_explosion, &game.math, &attack_random, &attack_player,
                &attack_alien_setup, NULL, NULL, &attack_state, error, sizeof(error)) ||
            attack_state.setup.is_hitscan != 0u || attack_state.animation.action != UINT8_MAX ||
            attack_state.animation.finished != UINT8_MAX ||
            attack_state.projectile_spawned != UINT8_MAX ||
            attack_runtime.heading_angle != attack_state.heading.angle ||
            attack_runtime.motion.new_x != 100 || attack_runtime.motion.new_z != 200 ||
            read_be16(slot_bytes + PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                          30u) !=
                (uint16_t)(attack_state.heading.angle + attack_state.animation.facing) ||
            slot_bytes[PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 20u] != 2u ||
            slot_bytes[PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 55u] != 0u ||
            read_be16(slot_bytes + PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                          34u) != (uint16_t)attack_alien_setup.followup_timer ||
            read_be16(slot_bytes + PROJECTILE_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                          40u) != 0u ||
            attack_runtime.entity_workspace[PROJECTILE_ATTACK_ALIEN_SLOT][0u] !=
                player_runtime_position_to_world(attack_player.x) ||
            attack_runtime.entity_workspace[PROJECTILE_ATTACK_ALIEN_SLOT][1u] !=
                player_runtime_position_to_world(attack_player.z) ||
            slot_bytes[PROJECTILE_ATTACK_SHOT_FIRST_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] !=
                2u) {
            fprintf(stderr, "ai_AttackWithProjectile finished-action state is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* modules/ai.s:ai_AttackWithHitScan's action/finished direct-hit branch. */
        enum {
            HITSCAN_ATTACK_AUXILIARY_SLOT = 0u,
            HITSCAN_ATTACK_ALIEN_SLOT = 1u,
            HITSCAN_ATTACK_PLAYER_SLOT = 2u,
            HITSCAN_ATTACK_SLOT_COUNT = HITSCAN_ATTACK_PLAYER_SLOT + 1u,
            HITSCAN_ATTACK_POINT_COUNT = 2u
        };
        uint8_t slot_bytes[HITSCAN_ATTACK_SLOT_COUNT * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[HITSCAN_ATTACK_POINT_COUNT * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime attack_objects = {0};
        ObjectObservation attack_observation;
        ObjectAnimationRuntime attack_animation;
        LightingRuntime attack_lighting;
        AlienRuntime attack_runtime;
        ObjectExplosionRuntime attack_explosion;
        GameProgression attack_progression;
        GameRandom attack_random;
        GameRandom expected_random;
        PlayerRuntime attack_player = game.player;
        AlienSetup attack_alien_setup;
        AlienHitscanAttackState attack_state;
        GameAlienDefinition attack_definition;
        GameBulletDefinition attack_bullet;
        uint16_t attack_alien = UINT16_MAX;
        uint16_t random_value;
        int32_t squared_view_distance;
        int32_t expected_chance_distance;
        int16_t impact_root;
        int16_t expected_impact_x;
        int16_t expected_impact_z;
        int16_t initial_impact_x = 5;
        int16_t initial_impact_z = 6;

        for (uint16_t alien_index = 0u; alien_index < GAME_LINK_ALIEN_COUNT; ++alien_index) {
            if (!game_link_get_alien_definition(&game.game_link_catalog, alien_index,
                                                &attack_definition, error, sizeof(error)) ||
                !game_link_get_bullet_definition(&game.game_link_catalog,
                                                 attack_definition.bullet_type,
                                                 &attack_bullet, error, sizeof(error))) {
                fprintf(stderr, "could not scan ai_AttackWithHitScan source data: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (attack_definition.girth <= 2u &&
                (int16_t)attack_definition.auxiliary_type >= 0 &&
                attack_definition.auxiliary_type < GAME_LINK_OBJECT_COUNT &&
                attack_bullet.is_hitscan != 0u && (uint8_t)attack_bullet.hit_damage != 0u) {
                attack_alien = alien_index;
                break;
            }
        }
        if (attack_alien == UINT16_MAX) {
            fprintf(stderr, "no source hitscan alien supports ai_AttackWithHitScan\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        attack_objects.slot_bytes = slot_bytes;
        attack_objects.slot_count = HITSCAN_ATTACK_SLOT_COUNT;
        attack_objects.active_slot_count = HITSCAN_ATTACK_SLOT_COUNT;
        attack_objects.player1_slot = HITSCAN_ATTACK_PLAYER_SLOT;
        attack_objects.point_bytes = point_bytes;
        attack_objects.point_count = HITSCAN_ATTACK_POINT_COUNT;
        slot_bytes[HITSCAN_ATTACK_AUXILIARY_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 3u;
        write_be16(slot_bytes + HITSCAN_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   0u);
        write_be16(slot_bytes + HITSCAN_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   attack_player.zone_index);
        slot_bytes[HITSCAN_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u] =
            (uint8_t)attack_alien;
        write_be32(point_bytes + 0u, UINT32_C(0x00640000));
        write_be32(point_bytes + 4u, UINT32_C(0x00c80000));
        attack_player.x = player_runtime_world_to_position(100);
        attack_player.z = player_runtime_world_to_position(600);
        attack_player.tmp_x = attack_player.x;
        attack_player.tmp_y = attack_player.y;
        attack_player.tmp_z = attack_player.z;
        attack_player.yaw = 0u;
        write_be16(slot_bytes + HITSCAN_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                   (uint16_t)source_asr32_7(attack_player.y));
        slot_bytes[HITSCAN_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 63u] =
            attack_player.stood_in_top;
        write_be16(slot_bytes + HITSCAN_ATTACK_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   1u);
        write_be16(slot_bytes + HITSCAN_ATTACK_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                   (uint16_t)source_asr32_7(attack_player.y));
        write_be16(slot_bytes + HITSCAN_ATTACK_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   attack_player.zone_index);
        slot_bytes[HITSCAN_ATTACK_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] = 0x10u;
        write_be16(slot_bytes + HITSCAN_ATTACK_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 42u,
                   (uint16_t)initial_impact_x);
        write_be16(slot_bytes + HITSCAN_ATTACK_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 44u,
                   (uint16_t)initial_impact_z);
        write_be32(point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u, UINT32_C(0x00640000));
        write_be32(point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u, UINT32_C(0x02580000));
        if (!alien_setup_from_slot(&attack_objects, HITSCAN_ATTACK_ALIEN_SLOT,
                                   &game.dynamic_level.runtime, &game.game_link_catalog,
                                   &attack_alien_setup, error, sizeof(error))) {
            fprintf(stderr, "ai_AttackWithHitScan setup fixture is invalid: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        object_observation_init(&attack_observation);
        if (!object_observation_update_single_player(
                &attack_observation, &attack_objects, &attack_player, &game.math,
                error, sizeof(error))) {
            fprintf(stderr, "ai_AttackWithHitScan ObjRotated fixture is invalid: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        object_animation_runtime_init(&attack_animation);
        attack_animation.workspace[HITSCAN_ATTACK_ALIEN_SLOT][0u] = UINT8_MAX;
        attack_animation.workspace[HITSCAN_ATTACK_ALIEN_SLOT][1u] = UINT8_MAX;
        attack_animation.workspace[HITSCAN_ATTACK_ALIEN_SLOT][2u] = 0u;
        attack_animation.workspace[HITSCAN_ATTACK_ALIEN_SLOT][3u] = UINT8_MAX;
        lighting_runtime_init(&attack_lighting);
        alien_runtime_init(&attack_runtime);
        alien_runtime_begin_level(&attack_runtime);
        object_explosion_runtime_init(&attack_explosion);
        game_progression_init(&attack_progression);
        game_random_init(&attack_random);
        expected_random = attack_random;
        random_value = game_random_next(&expected_random);
        squared_view_distance =
            (int32_t)((uint32_t)((int32_t)attack_observation.rotated_x[0u] *
                                 attack_observation.rotated_x[0u]) +
                      (uint32_t)((int32_t)attack_observation.rotated_z[0u] *
                                 attack_observation.rotated_z[0u]));
        expected_chance_distance = source_asr32_count(squared_view_distance, 6u);
        if (!alien_math_calc_sqrt(400 * 400, &impact_root, error, sizeof(error))) {
            fprintf(stderr, "ai_AttackWithHitScan impact root fixture is invalid: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        expected_impact_x = 0;
        expected_impact_z = (int16_t)(-(400 * (int32_t)(uint8_t)attack_bullet.hit_damage) /
                                      (impact_root * 2));
        if (!alien_attack_with_hitscan_update(
                &attack_objects, HITSCAN_ATTACK_ALIEN_SLOT, &attack_runtime,
                &attack_animation, &attack_lighting, &game.dynamic_level, &game.level_clips,
                &game.game_link_catalog, &attack_progression, &attack_explosion, &game.math,
                &attack_random, &attack_player, &attack_alien_setup, &attack_observation,
                &attack_state, error, sizeof(error)) ||
            attack_state.setup.is_hitscan != UINT8_MAX ||
            attack_state.animation.action != UINT8_MAX ||
            attack_state.animation.finished != UINT8_MAX ||
            attack_state.player_hit != UINT8_MAX || attack_state.player_missed != 0u ||
            attack_state.chance_roll != (int32_t)((random_value & UINT16_C(0x7fff)) << 2u) ||
            attack_state.chance_distance != expected_chance_distance ||
            attack_state.chance_roll <= attack_state.chance_distance ||
            attack_state.impact_x != expected_impact_x ||
            attack_state.impact_z != expected_impact_z ||
            attack_random.state != expected_random.state ||
            attack_runtime.heading_angle != attack_state.heading.angle ||
            attack_runtime.motion.new_x != 100 || attack_runtime.motion.new_z != 200 ||
            slot_bytes[HITSCAN_ATTACK_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] !=
                (uint8_t)(0x10u + (uint8_t)attack_bullet.hit_damage) ||
            read_be16(slot_bytes + HITSCAN_ATTACK_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                          42u) != (uint16_t)(initial_impact_x - expected_impact_x) ||
            read_be16(slot_bytes + HITSCAN_ATTACK_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                          44u) != (uint16_t)(initial_impact_z - expected_impact_z) ||
            slot_bytes[HITSCAN_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 20u] != 2u ||
            slot_bytes[HITSCAN_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 55u] != 0u ||
            read_be16(slot_bytes + HITSCAN_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                          34u) != (uint16_t)attack_alien_setup.followup_timer ||
            read_be16(slot_bytes + HITSCAN_ATTACK_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                          40u) != 0u ||
            attack_runtime.entity_workspace[0u][0u] !=
                player_runtime_position_to_world(attack_player.x) ||
            attack_runtime.entity_workspace[0u][1u] !=
                player_runtime_position_to_world(attack_player.z)) {
            fprintf(stderr, "ai_AttackWithHitScan direct-hit state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
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
        memory_player.x = player_runtime_world_to_position(0x1234);
        memory_player.z = player_runtime_world_to_position(-2);
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
        AlienRuntime perception_alien_runtime;
        PlayerRuntime perception_player = game.player;

        alien_runtime_init(&perception_alien_runtime);
        perception_objects.slot_bytes = slot_bytes;
        perception_objects.slot_count = 1u;
        perception_objects.active_slot_count = 1u;
        perception_player.x = player_runtime_world_to_position(100);
        perception_player.z = player_runtime_world_to_position(200);
        perception_player.y = 3 * 128;
        perception_player.stood_in_top = 0u;
        slot_bytes[17u] = UINT8_MAX;
        write_be16(slot_bytes + 4u, 3u);
        if (!alien_perception_look_for_player_one(
                &perception_alien_runtime, &perception_objects, 0u,
                &game.dynamic_level.runtime, &game.level_clips,
                &perception_player, perception_player.zone_index, 90, 190,
                error, sizeof(error)) ||
            slot_bytes[17u] != 1u ||
            perception_alien_runtime.motion.new_x != 90 ||
            perception_alien_runtime.motion.new_z != 190 ||
            perception_alien_runtime.visibility.viewer_x != 90 ||
            perception_alien_runtime.visibility.viewer_z != 190 ||
            perception_alien_runtime.visibility.viewer_y != 3 ||
            perception_alien_runtime.visibility.viewer_in_upper_zone != 0u) {
            fprintf(stderr, "AI_LookForPlayer1 visible source state is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        perception_player.stood_in_top = UINT8_MAX;
        slot_bytes[17u] = UINT8_MAX;
        if (!alien_perception_look_for_player_one(
                &perception_alien_runtime, &perception_objects, 0u,
                &game.dynamic_level.runtime, &game.level_clips,
                &perception_player, perception_player.zone_index, 90, 190,
                error, sizeof(error)) ||
            slot_bytes[17u] != 0u) {
            fprintf(stderr, "AI_LookForPlayer1 upper-zone rejection is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        {
            /* newaliencontrol.s:StillHere supplies the live object's point to the same helper. */
            uint8_t point_bytes[OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
            GameObjectDefinition destructible_definition = {0};
            GameObjectAnimationFrame destructible_frame;
            LevelZone destructible_zone;
            MessageRuntime passive_messages = {0};

            perception_objects.point_bytes = point_bytes;
            perception_objects.point_count = 1u;
            write_be16(slot_bytes + 0u, 0u);
            write_be16(slot_bytes + 12u, perception_player.zone_index);
            slot_bytes[16u] = 1u;
            slot_bytes[17u] = UINT8_MAX;
            slot_bytes[18u] = 0u;
            slot_bytes[19u] = 0u;
            slot_bytes[62u] = UINT8_MAX;
            slot_bytes[63u] = 0u;
            write_be16(point_bytes + 0u, 90u);
            write_be16(point_bytes + 4u, 190u);
            destructible_definition.behaviour = 2u;
            destructible_definition.hit_points = 2u;
            perception_player.stood_in_top = 0u;
            if (!level_runtime_get_zone(
                    &game.dynamic_level.runtime, perception_player.zone_index,
                    &destructible_zone, error, sizeof(error)) ||
                !game_link_get_object_animation_frame(
                    &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                    0u, 0u, &destructible_frame, error, sizeof(error)) ||
                !object_passives_update_slot(
                    &perception_objects, 0u, &perception_alien_runtime,
                    &game.dynamic_level.runtime, &game.game_link_catalog,
                    &game.level_clips, &perception_player, &destructible_definition,
                    &passive_messages, 0u, error, sizeof(error)) ||
                slot_bytes[17u] != 1u || slot_bytes[18u] != 1u ||
                read_be16(slot_bytes + 4u) !=
                    (uint16_t)(source_asr32_7(destructible_zone.floor) +
                               (int16_t)destructible_frame.signed_byte_4 * 2) ||
                read_be16(slot_bytes + 34u) != destructible_frame.next_timer1 ||
                perception_alien_runtime.motion.new_x != 90 ||
                perception_alien_runtime.motion.new_z != 190 ||
                perception_alien_runtime.visibility.viewer_x != 90 ||
                perception_alien_runtime.visibility.viewer_z != 190) {
                fprintf(stderr, "StillHere player visibility state is inconsistent: %s\n",
                        error);
                game_bootstrap_destroy(&game);
                return 1;
            }
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
        {
            static const uint8_t source_options[4u] = {0u, 8u, 9u, 10u};
            ObjectObservation animation_observation;
            GameAudioEvents animation_audio;
            GameAlienAnimationFrame sound_frame;
            uint16_t sound_alien = UINT16_MAX;
            uint16_t sound_frame_index = 0u;
            uint8_t sound_which = 0u;

            for (uint16_t alien = 0u; alien < GAME_LINK_ALIEN_COUNT &&
                 sound_alien == UINT16_MAX; ++alien) {
                for (uint8_t which = 0u; which < 4u &&
                     sound_alien == UINT16_MAX; ++which) {
                    for (uint16_t frame_index = 0u;
                         frame_index + 1u < GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT;
                         ++frame_index) {
                        if (!game_link_get_alien_animation_frame(
                                &game.game_link_catalog, alien, source_options[which],
                                frame_index, &sound_frame, error, sizeof(error))) {
                            fprintf(stderr, "DOALLANIMS sound-frame scan failed: %s\n", error);
                            game_bootstrap_destroy(&game);
                            return 1;
                        }
                        if (sound_frame.bytes[5u] != 0u) {
                            sound_alien = alien;
                            sound_frame_index = frame_index;
                            sound_which = which;
                            break;
                        }
                    }
                }
            }
            if (sound_alien == UINT16_MAX) {
                fprintf(stderr, "DOALLANIMS source data has no sound frame\n");
                game_bootstrap_destroy(&game);
                return 1;
            }
            memset(slot_bytes, 0, sizeof(slot_bytes));
            write_be16(slot_bytes + 0u, 0u);
            write_be16(slot_bytes + 12u, 0u);
            slot_bytes[16u] = 0u;
            slot_bytes[54u] = (uint8_t)sound_alien;
            slot_bytes[55u] = sound_which;
            slot_bytes[62u] = UINT8_MAX;
            write_be16(slot_bytes + 40u, sound_frame_index);
            write_be16(slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
            object_animation_runtime_destroy(&animation_runtime);
            object_animation_runtime_init(&animation_runtime);
            object_observation_init(&animation_observation);
            animation_observation.rotated_x[0u] = 44;
            animation_observation.rotated_z[0u] = -55;
            game_audio_events_init(&animation_audio);
            if (!object_animation_update_single_player_with_audio(
                    &animation_runtime, &animation_objects, &game.game_link_catalog,
                    &animation_random, &animation_observation, &animation_audio,
                    error, sizeof(error)) ||
                animation_audio.count != 1u ||
                animation_audio.events[0u].sample_index !=
                    (uint16_t)(sound_frame.bytes[5u] - 1u) ||
                animation_audio.events[0u].world_x != 44 ||
                animation_audio.events[0u].world_z != -55 ||
                animation_audio.events[0u].source_id != 0u ||
                animation_audio.events[0u].listener_relative == 0u) {
                fprintf(stderr, "DOALLANIMS source sound handoff is inconsistent: %s\n",
                        error);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        object_animation_runtime_destroy(&animation_runtime);
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
        damage_player.x = player_runtime_world_to_position(100);
        damage_player.z = player_runtime_world_to_position(0);
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
            damage_runtime.heading_angle != expected_heading.angle ||
            damage_runtime.motion.new_x != expected_heading.new_x ||
            damage_runtime.motion.new_z != expected_heading.new_z ||
            read_be16(slot_bytes + 34u) != 0u || read_be16(slot_bytes + 40u) != 0u ||
            damage_animation_runtime.workspace[0u][1u] != UINT8_MAX ||
            damage_random.state != expected_random.state) {
            fprintf(stderr, "ai_TakeDamage pursuit reaction is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        alien_runtime_init(&damage_runtime);
        damage_runtime.motion.new_x = 321;
        damage_runtime.motion.new_z = 654;
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
            read_be16(slot_bytes + 30u) != 0x1234u || damage_runtime.heading_angle != 0u ||
            damage_runtime.motion.new_x != 321 || damage_runtime.motion.new_z != 654 ||
            read_be16(slot_bytes + 34u) != 0u ||
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
        GameObjectDefinition collision_a2_definition;
        GameObjectAnimationFrame auxiliary_frame;
        GameShootDefinition first_alien_shoot_definition;
        GameShootDefinition second_alien_shoot_definition;
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
            !game_link_get_object_definition(&game.game_link_catalog, 0u,
                                             &collision_a2_definition, error, sizeof(error)) ||
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
            animation_state.collision_a2_words[1u] !=
                (int16_t)collision_a2_definition.graphics_type ||
            animation_state.collision_a2_words[2u] != collision_a2_definition.active_timeout ||
            animation_state.collision_a2_words[5u] !=
                (int16_t)collision_a2_definition.impassible ||
            animation_state.collision_a2_words[6u] !=
                (int16_t)collision_a2_definition.default_animation_length ||
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
        if (!game_link_get_alien_shoot_definition(
                &game.game_link_catalog, 0u, &first_alien_shoot_definition,
                error, sizeof(error)) ||
            !game_link_get_alien_shoot_definition(
                &game.game_link_catalog, 1u, &second_alien_shoot_definition,
                error, sizeof(error))) {
            fprintf(stderr, "could not establish ai_DoWalkAnim no-auxiliary a2 fixture: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        animation_setup.auxiliary_object_type = -1;
        if (!alien_animation_update_walk_or_attack(
                &animation_objects, 1u, &animation_runtime, &game.game_link_catalog,
                &game.math, &animation_setup, 0u, &animation_state, error, sizeof(error)) ||
            animation_state.collision_a2_words[1u] !=
                (int16_t)first_alien_shoot_definition.delay ||
            animation_state.collision_a2_words[2u] !=
                (int16_t)first_alien_shoot_definition.bullet_count ||
            animation_state.collision_a2_words[5u] !=
                (int16_t)second_alien_shoot_definition.delay ||
            animation_state.collision_a2_words[6u] !=
                (int16_t)second_alien_shoot_definition.bullet_count) {
            fprintf(stderr, "ai_DoWalkAnim no-auxiliary a2 source state is inconsistent: %s\n",
                    error);
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
        uint8_t point_bytes[OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime impact_objects = {0};
        LightingRuntime impact_lighting;
        LightingRuntime expected_impact_lighting;
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
        impact_objects.point_bytes = point_bytes;
        impact_objects.point_count = 1u;
        lighting_runtime_init(&impact_lighting);
        write_be16(slot_bytes + 0u, 0u);
        write_be16(slot_bytes + 12u, 0u);
        slot_bytes[16u] = 2u;
        slot_bytes[30u] = 1u;
        slot_bytes[31u] = (uint8_t)impact_bullet_index;
        expected_impact_lighting = impact_lighting;
        if (!lighting_runtime_brighten_points(
                &expected_impact_lighting, &game.dynamic_level.runtime,
                (int16_t)-(int16_t)impact_frame.byte_5, 0, 0, 0, 0u,
                error, sizeof(error)) ||
            !object_projectiles_update_impact_slot(
                &impact_objects, 0u, &game.dynamic_level, &impact_lighting,
                &game.game_link_catalog, error, sizeof(error)) ||
            memcmp(&impact_lighting, &expected_impact_lighting,
                   sizeof(impact_lighting)) != 0 ||
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
                    &impact_objects, 0u, &game.dynamic_level, &impact_lighting,
                    &game.game_link_catalog, error, sizeof(error))) {
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
        /* newanims.s:ComputeBlast target damage/impulse and player-shot flames. */
        uint8_t blast_level_bytes[96u] = {0};
        uint8_t blast_graphics_bytes[4u] = {0};
        uint8_t blast_slot_bytes[24u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t blast_point_bytes[23u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        LevelRuntime blast_level = {0};
        LevelDynamicState blast_dynamic = {0};
        ObjectRuntime blast_objects = {0};
        ObjectBlastRuntime blast_runtime;
        ObjectMotionRuntime blast_motion;
        ObjectVisibilityRuntime blast_visibility;
        GameRandom blast_random;
        GameRandom expected_blast_random;
        GameBulletDefinition blast_bullet;
        GameBulletDefinition blast_explosive_bullet;
        uint16_t blast_bullet_index = UINT16_MAX;
        uint16_t blast_projectile_bullet_index = UINT16_MAX;
        uint16_t blast_explosive_bullet_index = UINT16_MAX;
        AssetBlob same_zone_clips = {0};

        for (uint16_t bullet_index = 0u; bullet_index < GAME_LINK_BULLET_COUNT;
             ++bullet_index) {
            if (!game_link_get_bullet_definition(&game.game_link_catalog, bullet_index,
                                                 &blast_bullet, error, sizeof(error))) {
                fprintf(stderr, "could not read ComputeBlast source bullet: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (blast_bullet.gravity == 0u) {
                blast_bullet_index = bullet_index;
            } else {
                blast_projectile_bullet_index = bullet_index;
            }
            if (blast_bullet_index != UINT16_MAX &&
                blast_projectile_bullet_index != UINT16_MAX) {
                break;
            }
        }
        if (blast_bullet_index == UINT16_MAX || blast_projectile_bullet_index == UINT16_MAX) {
            fprintf(stderr, "ComputeBlast source gravity variants are unavailable\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint16_t bullet_index = 0u; bullet_index < GAME_LINK_BULLET_COUNT;
             ++bullet_index) {
            if (!game_link_get_bullet_definition(&game.game_link_catalog, bullet_index,
                                                 &blast_explosive_bullet,
                                                 error, sizeof(error))) {
                fprintf(stderr, "could not read explosive ComputeBlast bullet: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (blast_explosive_bullet.explosive_force != 0u &&
                blast_explosive_bullet.explosive_force <= UINT8_MAX &&
                blast_explosive_bullet.bounce_vertical == 0u) {
                blast_explosive_bullet_index = bullet_index;
                break;
            }
        }
        if (blast_explosive_bullet_index == UINT16_MAX) {
            fprintf(stderr, "ComputeBlast source roof-impact bullet is unavailable\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        blast_level.level_bytes = blast_level_bytes;
        blast_level.level_size = sizeof(blast_level_bytes);
        blast_level.graphics_bytes = blast_graphics_bytes;
        blast_level.graphics_size = sizeof(blast_graphics_bytes);
        blast_level.zone_offsets_table_offset = 0u;
        blast_level.zone_count = 1u;
        write_be32(blast_graphics_bytes, 0u);
        write_be16(blast_level_bytes + 0u, 0u);
        write_be32(blast_level_bytes + 2u, 100000u);
        write_be32(blast_level_bytes + 6u, 200000u);
        write_be32(blast_level_bytes + 10u, 100000u);
        write_be32(blast_level_bytes + 14u, 200000u);
        write_be16(blast_level_bytes + 32u, 56u);
        write_be16(blast_level_bytes + 48u, UINT16_MAX);
        write_be16(blast_level_bytes + 56u, UINT16_MAX);
        write_be16(blast_level_bytes + 58u, UINT16_C(0xfffe));
        if (!level_dynamic_state_init(&blast_dynamic, &blast_level, error, sizeof(error))) {
            fprintf(stderr, "could not initialize ComputeBlast source fixture: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        blast_objects.slot_bytes = blast_slot_bytes;
        blast_objects.slot_count = 24u;
        blast_objects.active_slot_count = 24u;
        blast_objects.player_shot_first_slot = 3u;
        blast_objects.point_bytes = blast_point_bytes;
        blast_objects.point_count = 23u;
        write_be16(blast_slot_bytes + 0u, 0u);
        write_be16(blast_slot_bytes + 4u, 10u);
        write_be16(blast_slot_bytes + 12u, 0u);
        blast_slot_bytes[16u] = 2u;
        blast_slot_bytes[31u] = (uint8_t)blast_bullet_index;
        write_be16(blast_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 1u);
        write_be16(blast_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u, 10u);
        write_be16(blast_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
        blast_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
        blast_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
        write_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 2u);
        write_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u, 10u);
        write_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
        blast_slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 2u;
        blast_slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 31u] =
            (uint8_t)blast_projectile_bullet_index;
        write_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u, 10u);
        write_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 22u, 20u);
        write_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 42u, 100u);
        for (uint32_t point_index = 0u; point_index < 23u; ++point_index) {
            uint8_t *point = blast_point_bytes +
                (size_t)point_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;

            write_be32(point, UINT32_C(0x00004a5a));
            write_be32(point + 4u, UINT32_C(0x00004a5a));
        }
        write_be32(blast_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT,
                   UINT32_C(0x00644a5a));
        write_be32(blast_point_bytes + 2u * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   UINT32_C(0x00644a5a));
        for (uint32_t pool_index = 0u; pool_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
             ++pool_index) {
            uint8_t *slot = blast_slot_bytes +
                (size_t)(pool_index + 3u) * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

            write_be16(slot, (uint16_t)(pool_index + 3u));
            write_be16(slot + 12u, UINT16_MAX);
        }
        write_be16(blast_slot_bytes + 23u * OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        object_blast_runtime_init(&blast_runtime);
        object_blast_runtime_note_bullet(&blast_runtime, (uint8_t)blast_bullet_index);
        object_motion_runtime_init(&blast_motion);
        object_visibility_runtime_init(&blast_visibility);
        object_visibility_runtime_set_viewer(&blast_visibility, 0, 0, 10, 0u);
        game_random_init(&blast_random);
        expected_blast_random = blast_random;
        for (uint32_t random_call = 0u; random_call < 18u; ++random_call) {
            (void)game_random_next(&expected_blast_random);
        }
        if (!object_blast_compute(
                &blast_runtime, &blast_objects, 0u, &blast_dynamic, &same_zone_clips,
                &game.game_link_catalog, &blast_random, &blast_motion, &blast_visibility, 64,
                error, sizeof(error)) ||
            blast_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] != 64u ||
            read_be16(blast_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 42u) != 25u ||
            read_be16(blast_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 44u) != 0u ||
            read_be16(blast_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 46u) !=
                (uint16_t)-4 ||
            read_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u) != 74u ||
            read_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 22u) != 45u ||
            read_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 42u) !=
                (uint16_t)-924 ||
            blast_runtime.completed_flame_count != 6u ||
            blast_motion.new_x != (int16_t)(read_be32(
                                      blast_point_bytes + 8u * OBJECT_RUNTIME_POINT_BYTE_COUNT) >>
                                                  16u) ||
            blast_motion.new_z != (int16_t)(read_be32(
                                      blast_point_bytes + 8u * OBJECT_RUNTIME_POINT_BYTE_COUNT +
                                      4u) >> 16u) ||
            blast_random.state != expected_blast_random.state) {
            fprintf(stderr, "ComputeBlast source damage/impulse/flame state is inconsistent: %s\n",
                    error);
            level_dynamic_state_destroy(&blast_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        for (uint32_t pool_index = 0u; pool_index < 6u; ++pool_index) {
            const uint8_t *slot = blast_slot_bytes +
                (size_t)(pool_index + 3u) * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
            const uint8_t *point = blast_point_bytes +
                (size_t)(pool_index + 3u) * OBJECT_RUNTIME_POINT_BYTE_COUNT;

            if (slot[16u] != 2u || read_be16(slot + 12u) != 0u ||
                slot[30u] != UINT8_MAX || slot[31u] != (uint8_t)blast_bullet_index ||
                slot[62u] != UINT8_MAX || (read_be32(point) & UINT32_C(0xffff)) !=
                    UINT32_C(0x4a5a) ||
                (read_be32(point + 4u) & UINT32_C(0xffff)) != UINT32_C(0x4a5a)) {
                fprintf(stderr, "ComputeBlast source flame allocation is inconsistent\n");
                level_dynamic_state_destroy(&blast_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        if ((int16_t)read_be16(blast_slot_bytes +
                               9u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) >= 0) {
            fprintf(stderr, "ComputeBlast source flame cap is inconsistent\n");
            level_dynamic_state_destroy(&blast_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        {
            ObjectProjectileSourceRuntime projectile_source_runtime = {0};
            LightingRuntime projectile_lighting;

            /* ItsABullet roof impact consumes the prior newx/newz before flight motion. */
            write_be32(blast_dynamic.level_bytes + 2u, 200000u);
            write_be32(blast_dynamic.level_bytes + 6u, 100000u);
            write_be32(blast_dynamic.level_bytes + 10u, 200000u);
            write_be32(blast_dynamic.level_bytes + 14u, 100000u);
            write_be32(blast_point_bytes, UINT32_C(0x03e84a5a));
            write_be16(blast_slot_bytes + 4u, 10u);
            write_be16(blast_slot_bytes + 12u, 0u);
            blast_slot_bytes[16u] = 2u;
            blast_slot_bytes[30u] = 0u;
            blast_slot_bytes[31u] = (uint8_t)blast_explosive_bullet_index;
            blast_slot_bytes[52u] = 0u;
            blast_slot_bytes[60u] = 0u;
            blast_slot_bytes[61u] = 0u;
            write_be16(blast_slot_bytes + 58u, 0u);
            write_be32(blast_slot_bytes + 18u, 0u);
            write_be32(blast_slot_bytes + 22u, 0u);
            write_be16(blast_slot_bytes + 42u, 0u);
            /* `cmp.l #10*128,d0 / blt .nohitroof`: equality is a roof impact. */
            write_be32(blast_slot_bytes + 44u, 100000u - 10u * 128u);
            write_be32(blast_slot_bytes + 36u, 0u);
            blast_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] = 0u;
            write_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                       UINT16_MAX);
            for (uint32_t pool_index = 0u;
                 pool_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT; ++pool_index) {
                uint8_t *slot = blast_slot_bytes +
                    (size_t)(pool_index + 3u) * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

                write_be16(slot + 12u, UINT16_MAX);
            }
            object_blast_runtime_init(&blast_runtime);
            object_motion_runtime_init(&blast_motion);
            object_visibility_runtime_init(&blast_visibility);
            object_visibility_runtime_set_viewer(&blast_visibility, -1, -1, -1, UINT8_MAX);
            game_random_init(&blast_random);
            lighting_runtime_init(&projectile_lighting);
            projectile_source_runtime.blast_runtime = &blast_runtime;
            projectile_source_runtime.motion_runtime = &blast_motion;
            projectile_source_runtime.visibility_runtime = &blast_visibility;
            projectile_source_runtime.clips = &same_zone_clips;
            projectile_source_runtime.random = &blast_random;
            if (!object_projectiles_update_flight_animation_slot_with_source_state(
                    &blast_objects, 0u, &blast_dynamic, &projectile_lighting,
                    &projectile_source_runtime, &game.game_link_catalog, 1u,
                    error, sizeof(error)) ||
                blast_slot_bytes[30u] != 1u ||
                blast_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] !=
                    (uint8_t)blast_explosive_bullet.explosive_force ||
                blast_runtime.completed_flame_count != 6u ||
                blast_visibility.viewer_x != 0 || blast_visibility.viewer_z != 0 ||
                blast_visibility.viewer_y != 10 ||
                blast_visibility.viewer_in_upper_zone != 0u ||
                blast_motion.new_x != 1000 || blast_motion.new_z != 0) {
                fprintf(stderr, "ItsABullet source roof blast caller state is inconsistent: %s\n",
                        error);
                level_dynamic_state_destroy(&blast_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        {
            ObjectProjectileSourceRuntime projectile_source_runtime = {0};
            LightingRuntime projectile_lighting;
            int16_t direct_target_y = source_asr32_7(
                150000 + (int16_t)(uint16_t)blast_explosive_bullet.gravity);

            /* Direct target impact writes Viewerx/y/z but deliberately retains ViewerTop. */
            write_be32(blast_dynamic.level_bytes + 2u, 200000u);
            write_be32(blast_dynamic.level_bytes + 6u, 100000u);
            write_be32(blast_dynamic.level_bytes + 10u, 200000u);
            write_be32(blast_dynamic.level_bytes + 14u, 100000u);
            write_be32(blast_point_bytes, UINT32_C(0x00004a5a));
            write_be32(blast_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT,
                       UINT32_C(0x00324a5a));
            write_be32(blast_point_bytes + 2u * OBJECT_RUNTIME_POINT_BYTE_COUNT,
                       UINT32_C(0x00644a5a));
            write_be32(blast_point_bytes + 2u * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                       UINT32_C(0x00004a5a));
            write_be16(blast_slot_bytes + 4u, 0u);
            write_be16(blast_slot_bytes + 12u, 0u);
            blast_slot_bytes[16u] = 2u;
            blast_slot_bytes[28u] = 3u;
            blast_slot_bytes[30u] = 0u;
            blast_slot_bytes[31u] = (uint8_t)blast_explosive_bullet_index;
            blast_slot_bytes[52u] = 0u;
            write_be16(blast_slot_bytes + 58u, 0u);
            write_be32(blast_slot_bytes + 18u, UINT32_C(0x00640000));
            write_be32(blast_slot_bytes + 22u, 0u);
            write_be16(blast_slot_bytes + 42u, 0u);
            write_be32(blast_slot_bytes + 44u, 150000u);
            write_be32(blast_slot_bytes + 36u, 1u);
            write_be16(blast_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                       (uint16_t)direct_target_y);
            write_be16(blast_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
            blast_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
            blast_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
            blast_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] = 0u;
            write_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                       (uint16_t)direct_target_y);
            write_be16(blast_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
            blast_slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
            blast_slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
            blast_slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] = 0u;
            blast_slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 63u] = UINT8_MAX;
            for (uint32_t pool_index = 0u;
                 pool_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT; ++pool_index) {
                uint8_t *slot = blast_slot_bytes +
                    (size_t)(pool_index + 3u) * OBJECT_RUNTIME_SLOT_BYTE_COUNT;

                write_be16(slot + 12u, UINT16_MAX);
            }
            object_blast_runtime_init(&blast_runtime);
            object_motion_runtime_init(&blast_motion);
            object_visibility_runtime_init(&blast_visibility);
            object_visibility_runtime_set_viewer(&blast_visibility, -1, -1, -1, UINT8_MAX);
            game_random_init(&blast_random);
            lighting_runtime_init(&projectile_lighting);
            projectile_source_runtime.blast_runtime = &blast_runtime;
            projectile_source_runtime.motion_runtime = &blast_motion;
            projectile_source_runtime.visibility_runtime = &blast_visibility;
            projectile_source_runtime.clips = &same_zone_clips;
            projectile_source_runtime.random = &blast_random;
            if (!object_projectiles_update_flight_animation_slot_with_source_state(
                    &blast_objects, 0u, &blast_dynamic, &projectile_lighting,
                    &projectile_source_runtime, &game.game_link_catalog, 1u,
                    error, sizeof(error)) ||
                blast_slot_bytes[30u] != 1u ||
                blast_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] != 3u ||
                blast_slot_bytes[2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] !=
                    (uint8_t)blast_explosive_bullet.explosive_force ||
                blast_runtime.completed_flame_count != 6u ||
                blast_visibility.viewer_x != 100 || blast_visibility.viewer_z != 0 ||
                blast_visibility.viewer_y != direct_target_y ||
                blast_visibility.viewer_in_upper_zone != UINT8_MAX ||
                blast_motion.new_x != (int16_t)(read_be32(
                                          blast_point_bytes +
                                          8u * OBJECT_RUNTIME_POINT_BYTE_COUNT) >> 16u) ||
                blast_motion.new_z != (int16_t)(read_be32(
                                          blast_point_bytes +
                                          8u * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u) >>
                                                  16u)) {
                fprintf(stderr,
                        "ItsABullet source direct-target blast caller state is inconsistent: %s\n",
                        error);
                level_dynamic_state_destroy(&blast_dynamic);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        level_dynamic_state_destroy(&blast_dynamic);
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
        AlienRuntime death_runtime;
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
        alien_runtime_init(&death_runtime);
        game_progression_init(&death_progression);
        game_random_init(&death_random);
        if (!alien_death_just_died(
                &death_objects, 0u, &death_runtime, &death_level, &death_link, &death_progression,
                &death_animation, &death_explosion, &game.math, &death_random, &death_state,
                error, sizeof(error)) ||
            death_state.narrative.bytes != NULL || death_state.splat_type != bullet_splat_type ||
            death_state.fragment_count != 8u || death_state.child_count != 0u ||
            death_state.got_out != UINT8_MAX || death_explosion.radius != 0 ||
            slot_bytes[18u] != 0u || slot_bytes[20u] != 5u || slot_bytes[55u] != 3u ||
            read_be16(slot_bytes + 40u) != 0u || death_animation.workspace[0u][1u] != UINT8_MAX ||
            death_progression.alien_kills[bullet_parent_type] != 1u ||
            death_progression.signal != 1u || death_runtime.motion.new_x != 300 ||
            death_runtime.motion.new_z != -400) {
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
        alien_runtime_init(&death_runtime);
        game_progression_init(&death_progression);
        game_random_init(&death_random);
        if (!alien_death_just_died(
                &death_objects, 0u, &death_runtime, &death_level, &death_link, &death_progression,
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
            death_progression.signal != 1u || death_runtime.motion.new_x != 17 ||
            death_runtime.motion.new_z != 17493) {
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
        pause_player.x = player_runtime_world_to_position(100);
        pause_player.z = player_runtime_world_to_position(200);
        pause_player.tmp_x = player_runtime_world_to_position(100);
        pause_player.tmp_z = player_runtime_world_to_position(200);
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
        write_be16(point_bytes + 0u,
                   (uint16_t)player_runtime_position_to_world(pause_player.x));
        write_be16(point_bytes + 4u,
                   (uint16_t)player_runtime_position_to_world(pause_player.z));
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
        write_be16(point_bytes + 0u,
                   (uint16_t)player_runtime_position_to_world(pause_player.x));
        write_be16(point_bytes + 4u,
                   (uint16_t)player_runtime_position_to_world(pause_player.z));
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

        {
            AlienDamageReactionState reaction_state;
            ObjectHeading expected_heading = {0};

            memset(slot_bytes, 0, sizeof(slot_bytes));
            object_animation_runtime_init(&pause_animation);
            lighting_runtime_init(&pause_lighting);
            write_be16(slot_bytes + 0u, UINT16_MAX);
            write_be16(slot_bytes + 12u, pause_player.zone_index);
            write_be16(slot_bytes + 26u, pause_player.zone_index);
            write_be16(slot_bytes + 64u + 0u, 0u);
            write_be16(slot_bytes + 64u + 4u, 22u);
            write_be16(slot_bytes + 64u + 12u, pause_player.zone_index);
            write_be16(slot_bytes + 64u + 26u, pause_player.zone_index);
            write_be16(slot_bytes + 64u + 30u, 0x2222u);
            write_be16(slot_bytes + 64u + 40u, pause_frame_index);
            slot_bytes[64u + 20u] = 4u;
            slot_bytes[64u + 54u] = (uint8_t)pause_alien;
            slot_bytes[64u + 55u] = 7u;
            slot_bytes[64u + 63u] = pause_player.stood_in_top;
            write_be16(point_bytes + 0u, 100u);
            write_be16(point_bytes + 4u, 200u);
            pause_player.x = player_runtime_world_to_position(300);
            pause_player.z = player_runtime_world_to_position(400);
            expected_heading.old_x = 100;
            expected_heading.old_z = 200;
            expected_heading.new_x = 300;
            expected_heading.new_z = 400;
            expected_heading.range = -20;
            expected_heading.speed = 20;
            if (!object_heading_towards_angle(&game.math, &expected_heading,
                                              error, sizeof(error))) {
                fprintf(stderr, "ai_DoTakeDamage heading fixture is invalid: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            pause_animation.workspace[1u][1u] = (uint8_t)pause_frame_index;
            pause_animation.workspace[1u][2u] = (uint8_t)pause_option;
            pause_animation.workspace[1u][3u] = UINT8_MAX;
            if (!alien_damage_update_reaction(
                    &pause_objects, 1u, &pause_animation, &game.game_link_catalog,
                    &game.math, &game.dynamic_level.runtime, &pause_lighting,
                    &pause_player, &pause_setup, 100, 200, &reaction_state,
                    error, sizeof(error)) ||
                reaction_state.got_out != 0u || reaction_state.animation.finished != UINT8_MAX ||
                slot_bytes[64u + 20u] != 0u || slot_bytes[64u + 55u] != 0u ||
                read_be16(slot_bytes + 64u + 40u) != 0u ||
                read_be16(slot_bytes + 64u + 30u) !=
                    (uint16_t)(expected_heading.angle + reaction_state.animation.facing) ||
                read_be16(slot_bytes + 12u) != read_be16(slot_bytes + 64u + 12u) ||
                read_be16(slot_bytes + 26u) != read_be16(slot_bytes + 64u + 26u) ||
                (pause_setup.default_mode >= 1 && read_be16(slot_bytes + 64u + 4u) != 22u)) {
                fprintf(stderr, "ai_DoTakeDamage source state is inconsistent: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }

            {
                AlienDispatchWorkspace damage_workspace = {0};
                AlienDispatchState damage_dispatch;
                ObjectObservation damage_observation;
                LightingRuntime expected_damage_lighting;
                LightingRuntime zero_origin_lighting;
                AlienSetup damage_setup = pause_setup;
                LevelPotentialVisibility damage_visible_zone;
                LevelZone damage_room;
                LevelWorldPoint damage_point;
                int16_t damage_point_index = -1;
                uint16_t damage_marker_index = UINT16_MAX;

                memset(slot_bytes, 0, sizeof(slot_bytes));
                object_animation_runtime_init(&pause_animation);
                alien_runtime_init(&pause_runtime);
                lighting_runtime_init(&pause_lighting);
                lighting_runtime_init(&expected_damage_lighting);
                lighting_runtime_init(&zero_origin_lighting);
                object_explosion_runtime_init(&pause_explosion);
                game_progression_init(&pause_progression);
                game_random_init(&pause_random);
                object_observation_init(&damage_observation);
                write_be16(slot_bytes + 0u, UINT16_MAX);
                write_be16(slot_bytes + 12u, pause_player.zone_index);
                write_be16(slot_bytes + 26u, pause_player.zone_index);
                write_be16(slot_bytes + 64u + 0u, 0u);
                write_be16(slot_bytes + 64u + 4u, 22u);
                write_be16(slot_bytes + 64u + 12u, pause_player.zone_index);
                write_be16(slot_bytes + 64u + 26u, pause_player.zone_index);
                write_be16(slot_bytes + 64u + 30u, 0u);
                write_be16(slot_bytes + 64u + 40u, pause_frame_index);
                slot_bytes[64u + 20u] = 4u;
                slot_bytes[64u + 54u] = (uint8_t)pause_alien;
                slot_bytes[64u + 55u] = 7u;
                slot_bytes[64u + 63u] = pause_player.stood_in_top;
                write_be16(point_bytes + 0u, 100u);
                write_be16(point_bytes + 4u, 200u);
                pause_animation.workspace[1u][1u] = (uint8_t)pause_frame_index;
                pause_animation.workspace[1u][2u] = (uint8_t)pause_option;
                pause_animation.workspace[1u][3u] = UINT8_MAX;
                if (!level_runtime_get_zone_potential_visibility(
                        &game.dynamic_level.runtime, pause_player.zone_index, 0u,
                        &damage_visible_zone, error, sizeof(error)) ||
                    damage_visible_zone.zone_index < 0 ||
                    !level_runtime_get_zone(
                        &game.dynamic_level.runtime,
                        (uint16_t)damage_visible_zone.zone_index, &damage_room,
                        error, sizeof(error))) {
                    fprintf(stderr,
                            "ai_DoTakeDamage shared motion PVST fixture is unavailable: %s\n",
                            error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                write_be16(slot_bytes + 64u + 4u,
                           (uint16_t)(int16_t)(damage_room.floor / 128));
                for (uint16_t marker_index = 0u;
                     marker_index < LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT; ++marker_index) {
                    if (!level_runtime_get_zone_border_point(
                            &game.dynamic_level.runtime,
                            (uint16_t)damage_visible_zone.zone_index, marker_index,
                            &damage_point_index, error, sizeof(error))) {
                        fprintf(stderr,
                                "ai_DoTakeDamage shared motion marker fixture is invalid: %s\n",
                                error);
                        game_bootstrap_destroy(&game);
                        return 1;
                    }
                    if (damage_point_index >= 0) {
                        damage_marker_index = marker_index;
                        break;
                    }
                }
                if (damage_marker_index == UINT16_MAX ||
                    !level_runtime_get_world_point(
                        &game.dynamic_level.runtime, (uint16_t)damage_point_index,
                        &damage_point, error, sizeof(error))) {
                    fprintf(stderr,
                            "ai_DoTakeDamage shared motion point fixture is unavailable: %s\n",
                            error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                pause_runtime.motion.new_x = damage_point.x;
                pause_runtime.motion.new_z = source_add16(damage_point.z, -1);
                expected_damage_lighting.current_point_brightness
                    [(uint16_t)damage_visible_zone.zone_index]
                    [(size_t)damage_marker_index * 4u] = 1000;
                zero_origin_lighting.current_point_brightness
                    [(uint16_t)damage_visible_zone.zone_index]
                    [(size_t)damage_marker_index * 4u] = 1000;
                pause_lighting.current_point_brightness
                    [(uint16_t)damage_visible_zone.zone_index]
                    [(size_t)damage_marker_index * 4u] = 1000;
                damage_setup.brightness = -200;
                damage_setup.default_mode = 1;
                if (!alien_torch_apply(
                        &expected_damage_lighting, &game.dynamic_level.runtime,
                        &game.math, &pause_objects, 1u, &damage_setup,
                        pause_runtime.motion.new_x, pause_runtime.motion.new_z,
                        error, sizeof(error)) ||
                    !alien_torch_apply(
                        &zero_origin_lighting, &game.dynamic_level.runtime,
                        &game.math, &pause_objects, 1u, &damage_setup, 0, 0,
                        error, sizeof(error)) ||
                    memcmp(&expected_damage_lighting, &zero_origin_lighting,
                           sizeof(expected_damage_lighting)) == 0) {
                    fprintf(stderr,
                            "ai_DoTakeDamage shared motion fixture is not discriminating: %s\n",
                            error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                if (!alien_dispatch_update(
                        &pause_objects, 1u, &pause_runtime, &pause_animation,
                        &pause_lighting, &game.dynamic_level, &game.level_navigation,
                        &game.level_clips, &game.game_link_catalog, &pause_progression,
                        &pause_explosion, &game.math, &pause_random, &pause_player,
                        &damage_setup, &damage_observation, NULL, 1u,
                        &damage_workspace, &damage_dispatch, error, sizeof(error)) ||
                    damage_dispatch.behavior != ALIEN_MAIN_BEHAVIOR_TAKE_DAMAGE ||
                    memcmp(&pause_lighting, &expected_damage_lighting,
                           sizeof(pause_lighting)) != 0 ||
                    pause_runtime.motion.new_x != damage_dispatch.damage_reaction.heading.new_x ||
                    pause_runtime.motion.new_z != damage_dispatch.damage_reaction.heading.new_z) {
                    fprintf(stderr,
                            "ai_DoTakeDamage did not consume shared objectmove motion: %s\n",
                            error);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
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
        /* modules/ai.s:ai_ProwlFly moves with its Widget a2 words and no global collision table. */
        enum {
            PROWL_AUXILIARY_SLOT = 0u,
            PROWL_ALIEN_SLOT = 1u,
            PROWL_TERMINATOR_SLOT = 2u,
            PROWL_SLOT_COUNT = 3u,
            PROWL_POINT_INDEX = 1u
        };
        uint8_t slot_bytes[PROWL_SLOT_COUNT * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[PROWL_SLOT_COUNT * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        ObjectRuntime prowl_objects = {0};
        LevelDynamicState prowl_dynamic = {0};
        ObjectAnimationRuntime prowl_animation;
        ObjectExplosionRuntime prowl_explosion;
        AlienRuntime prowl_runtime;
        LightingRuntime prowl_lighting;
        GameProgression prowl_progression;
        GameRandom prowl_random;
        PlayerRuntime prowl_player = game.player;
        AlienSetup prowl_setup;
        AlienProwlState prowl_state;
        GameAlienDefinition prowl_definition;
        GameAlienAnimationFrame prowl_frame;
        LevelZone prowl_zone;
        LevelControlPoint prowl_control_point;
        uint16_t prowl_alien = UINT16_MAX;
        uint16_t prowl_option = UINT16_MAX;
        uint16_t prowl_frame_index = UINT16_MAX;
        uint16_t prowl_phase;
        int16_t prowl_sine;
        int16_t prowl_cosine;
        int16_t prowl_target_x;
        int16_t prowl_target_z;
        int16_t prowl_old_x;
        int16_t prowl_old_z;
        int16_t expected_prowl_y;

        for (uint16_t alien_index = 0u;
             alien_index < GAME_LINK_ALIEN_COUNT && prowl_alien == UINT16_MAX;
             ++alien_index) {
            if (!game_link_get_alien_definition(&game.game_link_catalog, alien_index,
                                                &prowl_definition, error, sizeof(error)) ||
                prowl_definition.girth > 2u) {
                continue;
            }
            for (uint16_t option_index = 0u;
                 option_index < GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT &&
                 prowl_alien == UINT16_MAX;
                 ++option_index) {
                for (uint16_t frame_index = 0u;
                     frame_index < GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT;
                     ++frame_index) {
                    if (!game_link_get_alien_animation_frame(
                            &game.game_link_catalog, alien_index, option_index, frame_index,
                            &prowl_frame, error, sizeof(error)) ||
                        ((int16_t)prowl_definition.auxiliary_type >= 0 &&
                         (int8_t)prowl_frame.bytes[8u] >= 0)) {
                        continue;
                    }
                    prowl_alien = alien_index;
                    prowl_option = option_index;
                    prowl_frame_index = frame_index;
                    break;
                }
            }
        }
        if (prowl_alien == UINT16_MAX ||
            !level_dynamic_state_init(&prowl_dynamic, &game.dynamic_level.runtime,
                                      error, sizeof(error))) {
            fprintf(stderr, "ai_ProwlFly source fixture is unavailable: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        /* A private source-layout clone supplies the one control-point record this mode reads. */
        prowl_dynamic.runtime.control_point_coordinates_offset = 0u;
        prowl_dynamic.runtime.control_point_count = 1u;
        write_be16(prowl_dynamic.level_bytes + 0u, 100u);
        write_be16(prowl_dynamic.level_bytes + 2u, 200u);
        write_be16(prowl_dynamic.level_bytes + 4u, 0u);
        write_be16(prowl_dynamic.level_bytes + 6u, 0u);
        if (!level_runtime_get_zone(&prowl_dynamic.runtime, prowl_player.zone_index,
                                    &prowl_zone, error, sizeof(error)) ||
            !level_runtime_get_control_point(&prowl_dynamic.runtime, 0u,
                                             &prowl_control_point, error, sizeof(error))) {
            fprintf(stderr, "ai_ProwlFly source fixture is unavailable: %s\n", error);
            level_dynamic_state_destroy(&prowl_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        prowl_phase = (uint16_t)((int32_t)(int16_t)PROWL_POINT_INDEX * 0x1347) & 4095u;
        if (!game_math_sine(&game.math, (uint16_t)(prowl_phase << 1u), &prowl_sine,
                            error, sizeof(error)) ||
            !game_math_cosine(&game.math, (uint16_t)(prowl_phase << 1u), &prowl_cosine,
                              error, sizeof(error))) {
            fprintf(stderr, "ai_ProwlFly phase fixture is invalid: %s\n", error);
            level_dynamic_state_destroy(&prowl_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        prowl_target_x = source_add16(
            prowl_control_point.x, (int16_t)source_asr32_count(prowl_sine, 12u));
        prowl_target_z = source_add16(
            prowl_control_point.z, (int16_t)source_asr32_count(prowl_cosine, 12u));
        prowl_old_x = source_add16(prowl_target_x, 20);
        prowl_old_z = prowl_target_z;
        prowl_objects.slot_bytes = slot_bytes;
        prowl_objects.slot_count = PROWL_SLOT_COUNT;
        prowl_objects.active_slot_count = PROWL_TERMINATOR_SLOT;
        prowl_objects.point_bytes = point_bytes;
        prowl_objects.point_count = PROWL_SLOT_COUNT;
        write_be16(slot_bytes + PROWL_AUXILIARY_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   0u);
        write_be16(slot_bytes + PROWL_AUXILIARY_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   UINT16_MAX);
        write_be16(slot_bytes + PROWL_TERMINATOR_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   UINT16_MAX);
        write_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   PROWL_POINT_INDEX);
        write_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u, 3u);
        write_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   prowl_player.zone_index);
        write_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u,
                   prowl_player.zone_index);
        write_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 28u, 0u);
        write_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 32u, 0u);
        write_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 34u, 99u);
        write_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 40u,
                   prowl_frame_index);
        slot_bytes[PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 10u;
        slot_bytes[PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 21u] = UINT8_MAX;
        slot_bytes[PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u] =
            (uint8_t)prowl_alien;
        write_be16(point_bytes + PROWL_POINT_INDEX * OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u,
                   (uint16_t)prowl_old_x);
        write_be16(point_bytes + PROWL_POINT_INDEX * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   (uint16_t)prowl_old_z);
        if (!alien_setup_from_slot(&prowl_objects, PROWL_ALIEN_SLOT,
                                   &prowl_dynamic.runtime, &game.game_link_catalog,
                                   &prowl_setup, error, sizeof(error))) {
            fprintf(stderr, "ai_ProwlFly setup fixture is invalid: %s\n", error);
            level_dynamic_state_destroy(&prowl_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        object_animation_runtime_init(&prowl_animation);
        prowl_animation.workspace[PROWL_ALIEN_SLOT][1u] = (uint8_t)prowl_frame_index;
        prowl_animation.workspace[PROWL_ALIEN_SLOT][2u] = (uint8_t)prowl_option;
        alien_runtime_init(&prowl_runtime);
        alien_runtime_begin_level(&prowl_runtime);
        prowl_runtime.boredom[PROWL_ALIEN_SLOT][1u] = source_add16(prowl_old_x, -100);
        prowl_runtime.boredom[PROWL_ALIEN_SLOT][2u] = prowl_old_z;
        lighting_runtime_init(&prowl_lighting);
        object_explosion_runtime_init(&prowl_explosion);
        game_progression_init(&prowl_progression);
        game_random_init(&prowl_random);
        prowl_player.noise_volume = 0;
        expected_prowl_y = (int16_t)source_asr32_7(
            (int32_t)((uint32_t)prowl_zone.floor -
                      (uint32_t)source_asr32_count(prowl_setup.thing_height, 1u)));
        if (!alien_prowl_random_update(
                &prowl_objects, PROWL_ALIEN_SLOT, &prowl_runtime, &prowl_animation,
                &prowl_lighting, &prowl_dynamic, &game.level_navigation, &game.level_clips,
                &game.game_link_catalog, &prowl_progression, &prowl_explosion, &game.math,
                &prowl_random, &prowl_player, &prowl_setup, 0u, 1u, &prowl_state,
                error, sizeof(error)) ||
            prowl_state.damage_taken != 0u || prowl_state.got_out != 0u ||
            prowl_state.widget.middle_control_point != 0u ||
            prowl_state.heading.got_there != UINT8_MAX ||
            prowl_runtime.heading_angle != prowl_state.heading.angle ||
            prowl_state.hit_object != 0u ||
            prowl_state.movement.new_x != prowl_old_x ||
            prowl_state.movement.new_z != prowl_old_z ||
            read_be16(point_bytes + PROWL_POINT_INDEX * OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u) !=
                (uint16_t)prowl_old_x ||
            read_be16(point_bytes + PROWL_POINT_INDEX * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u) !=
                (uint16_t)prowl_old_z ||
            read_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) !=
                prowl_zone.id ||
            read_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u) !=
                prowl_zone.id ||
            read_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u) !=
                (uint16_t)expected_prowl_y ||
            read_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 28u) !=
                0u ||
            read_be16(slot_bytes + PROWL_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 32u) >=
                prowl_dynamic.runtime.control_point_count ||
            prowl_runtime.boredom[PROWL_ALIEN_SLOT][0u] != 100) {
            fprintf(stderr, "ai_ProwlFly source movement state is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&prowl_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        level_dynamic_state_destroy(&prowl_dynamic);
    }
    {
        /* modules/ai.s:ai_Charge preserves its pre-dispatch workspace and melee path. */
        enum {
            CHARGE_AUXILIARY_SLOT = 0u,
            CHARGE_ALIEN_SLOT = 1u,
            CHARGE_PLAYER_SLOT = 2u,
            CHARGE_TERMINATOR_SLOT = 3u,
            CHARGE_SLOT_COUNT = 4u,
            CHARGE_ALIEN_POINT = 1u
        };
        uint8_t slot_bytes[CHARGE_SLOT_COUNT * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t point_bytes[CHARGE_SLOT_COUNT * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        uint8_t charge_initial_slot_bytes[
            CHARGE_SLOT_COUNT * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t charge_initial_point_bytes[
            CHARGE_SLOT_COUNT * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        uint8_t charge_expected_flying_slot[OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        ObjectRuntime charge_objects = {0};
        ObjectRuntime charge_expected_flying_objects = {0};
        LevelDynamicState charge_dynamic = {0};
        ObjectAnimationRuntime charge_animation;
        ObjectExplosionRuntime charge_explosion;
        AlienRuntime charge_runtime;
        LightingRuntime charge_lighting;
        GameProgression charge_progression;
        GameRandom charge_random;
        PlayerRuntime charge_player = game.player;
        AlienSetup charge_setup;
        AlienChargeWorkspace charge_workspace = {0};
        AlienChargeState charge_state;
        AlienDispatchWorkspace charge_dispatch_workspace = {0};
        AlienDispatchState charge_dispatch_state;
        ObjectObservation charge_dispatch_observation;
        GameAlienDefinition charge_definition;
        GameAlienAnimationFrame charge_frame;
        GameObjectDefinition charge_auxiliary_definition;
        GameObjectAnimationFrame charge_auxiliary_frame;
        LevelZone charge_zone = {0};
        LevelZone charge_teleport_zone = {0};
        LevelZone charge_teleport_destination = {0};
        uint16_t charge_alien = UINT16_MAX;
        uint16_t charge_option = UINT16_MAX;
        uint16_t charge_frame_index = UINT16_MAX;
        uint16_t charge_zone_index = UINT16_MAX;
        uint16_t charge_teleport_zone_index = UINT16_MAX;
        int16_t charge_player_x;
        int16_t charge_player_z;
        int16_t charge_alien_x;
        int16_t expected_charge_y;
        int16_t expected_charge_flying_y;

        for (uint16_t zone_index = 0u; zone_index < game.dynamic_level.runtime.zone_count;
             ++zone_index) {
            if (!level_runtime_get_zone(&game.dynamic_level.runtime, zone_index, &charge_zone,
                                        error, sizeof(error))) {
                fprintf(stderr, "could not read ai_Charge source zone: %s\n", error);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (charge_zone.teleport_zone < 0 && charge_zone_index == UINT16_MAX) {
                charge_zone_index = zone_index;
            }
            if (charge_zone.teleport_zone >= 0 && charge_teleport_zone_index == UINT16_MAX) {
                charge_teleport_zone_index = zone_index;
            }
        }
        for (uint16_t alien_index = 0u;
             alien_index < GAME_LINK_ALIEN_COUNT && charge_alien == UINT16_MAX;
             ++alien_index) {
            if (!game_link_get_alien_definition(&game.game_link_catalog, alien_index,
                                                &charge_definition, error, sizeof(error)) ||
                charge_definition.girth > 2u ||
                (int16_t)charge_definition.auxiliary_type < 0 ||
                (int16_t)charge_definition.auxiliary_type >= GAME_LINK_OBJECT_COUNT) {
                continue;
            }
            for (uint16_t option_index = 1u;
                 option_index < GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT &&
                 charge_alien == UINT16_MAX;
                 ++option_index) {
                for (uint16_t frame_index = 0u;
                     frame_index < GAME_LINK_ALIEN_ANIMATION_FRAME_COUNT; ++frame_index) {
                    if (!game_link_get_alien_animation_frame(
                            &game.game_link_catalog, alien_index, option_index, frame_index,
                            &charge_frame, error, sizeof(error)) ||
                        (int8_t)charge_frame.bytes[8u] < 0 ||
                        charge_frame.bytes[8u] >= GAME_LINK_OBJECT_ANIMATION_FRAME_COUNT ||
                        !game_link_get_object_definition(
                            &game.game_link_catalog,
                            (uint16_t)charge_definition.auxiliary_type,
                            &charge_auxiliary_definition, error, sizeof(error)) ||
                        !game_link_get_object_animation_frame(
                            &game.game_link_catalog, GAME_LINK_OBJECT_ANIMATION_DEFAULT,
                            (uint16_t)charge_definition.auxiliary_type,
                            charge_frame.bytes[8u], &charge_auxiliary_frame,
                            error, sizeof(error))) {
                        continue;
                    }
                    charge_alien = alien_index;
                    charge_option = option_index;
                    charge_frame_index = frame_index;
                    break;
                }
            }
        }
        if (charge_zone_index == UINT16_MAX || charge_alien == UINT16_MAX ||
            !level_dynamic_state_init(&charge_dynamic, &game.dynamic_level.runtime,
                                      error, sizeof(error))) {
            fprintf(stderr, "ai_Charge source fixture is unavailable: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if (!level_runtime_get_zone(&charge_dynamic.runtime, charge_zone_index, &charge_zone,
                                    error, sizeof(error))) {
            fprintf(stderr, "could not read ai_Charge dynamic source zone: %s\n", error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if (charge_teleport_zone_index != UINT16_MAX &&
            (!level_runtime_get_zone(&charge_dynamic.runtime, charge_teleport_zone_index,
                                     &charge_teleport_zone, error, sizeof(error)) ||
             charge_teleport_zone.teleport_zone < 0 ||
             !level_runtime_get_zone(&charge_dynamic.runtime,
                                     (uint16_t)charge_teleport_zone.teleport_zone,
                                     &charge_teleport_destination, error, sizeof(error)))) {
            fprintf(stderr, "could not read ai_Charge teleport source zones: %s\n", error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        charge_player.zone_index = charge_zone_index;
        charge_player.tmp_x = charge_player.x;
        charge_player.tmp_z = charge_player.z;
        charge_player_x = player_runtime_position_to_world(charge_player.x);
        charge_player_z = player_runtime_position_to_world(charge_player.z);
        charge_alien_x = source_add16(charge_player_x, -100);
        charge_objects.slot_bytes = slot_bytes;
        charge_objects.slot_count = CHARGE_SLOT_COUNT;
        charge_objects.active_slot_count = CHARGE_TERMINATOR_SLOT;
        charge_objects.player1_slot = CHARGE_PLAYER_SLOT;
        charge_objects.point_bytes = point_bytes;
        charge_objects.point_count = CHARGE_SLOT_COUNT;
        write_be16(slot_bytes + CHARGE_AUXILIARY_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   0u);
        write_be16(slot_bytes + CHARGE_AUXILIARY_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   UINT16_MAX);
        write_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   CHARGE_ALIEN_POINT);
        write_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                   UINT16_C(0xffec));
        write_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   charge_zone_index);
        write_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u,
                   charge_zone_index);
        write_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 40u,
                   charge_frame_index);
        slot_bytes[CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
        slot_bytes[CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 10u;
        slot_bytes[CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 21u] = UINT8_MAX;
        slot_bytes[CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u] =
            (uint8_t)charge_alien;
        write_be16(slot_bytes + CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   CHARGE_PLAYER_SLOT);
        write_be16(slot_bytes + CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   charge_zone_index);
        slot_bytes[CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 2u;
        slot_bytes[CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
        slot_bytes[CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] = 5u;
        write_be16(slot_bytes + CHARGE_TERMINATOR_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT,
                   UINT16_MAX);
        write_be16(point_bytes + CHARGE_ALIEN_POINT * OBJECT_RUNTIME_POINT_BYTE_COUNT,
                   (uint16_t)charge_alien_x);
        write_be16(point_bytes + CHARGE_ALIEN_POINT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   (uint16_t)charge_player_z);
        memcpy(charge_initial_slot_bytes, slot_bytes, sizeof(slot_bytes));
        memcpy(charge_initial_point_bytes, point_bytes, sizeof(point_bytes));
        if (!alien_setup_from_slot(&charge_objects, CHARGE_ALIEN_SLOT,
                                   &charge_dynamic.runtime, &game.game_link_catalog,
                                   &charge_setup, error, sizeof(error))) {
            fprintf(stderr, "ai_Charge setup fixture is invalid: %s\n", error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        object_animation_runtime_init(&charge_animation);
        charge_animation.workspace[CHARGE_ALIEN_SLOT][0u] = 1u;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][1u] = (uint8_t)charge_frame_index;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][2u] = (uint8_t)charge_option;
        alien_runtime_init(&charge_runtime);
        alien_runtime_begin_level(&charge_runtime);
        lighting_runtime_init(&charge_lighting);
        object_explosion_runtime_init(&charge_explosion);
        game_progression_init(&charge_progression);
        game_random_init(&charge_random);
        expected_charge_y = (int16_t)source_asr32_7(
            (int32_t)((uint32_t)charge_zone.floor -
                      (uint32_t)source_asr32_count(charge_setup.thing_height, 1u)));
        if (!alien_charge_update(
                &charge_objects, CHARGE_ALIEN_SLOT, &charge_runtime, &charge_animation,
                &charge_lighting, &charge_dynamic, &game.level_navigation, &game.level_clips,
                &game.game_link_catalog, &charge_progression, &charge_explosion, &game.math,
                &charge_random, &charge_player, &charge_setup, 0u, 1u, &charge_workspace,
                &charge_state, error, sizeof(error)) ||
            charge_state.damage_taken != 0u || charge_state.got_out != 0u ||
            charge_state.teleport.teleported != 0u ||
            charge_state.heading.got_there != UINT8_MAX ||
            charge_state.damaged_player != UINT8_MAX ||
            slot_bytes[CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] != 7u ||
            read_be16(slot_bytes + CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 42u) !=
                0u ||
            read_be16(slot_bytes + CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 44u) !=
                0u ||
            read_be16(point_bytes + CHARGE_ALIEN_POINT * OBJECT_RUNTIME_POINT_BYTE_COUNT) !=
                (uint16_t)charge_alien_x ||
            read_be16(point_bytes + CHARGE_ALIEN_POINT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u) !=
                (uint16_t)charge_player_z ||
            read_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) !=
                charge_zone.id ||
            read_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u) !=
                charge_zone.id ||
            read_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u) !=
                (uint16_t)expected_charge_y ||
            charge_runtime.heading_angle != charge_state.heading.angle) {
            fprintf(stderr, "ai_Charge source movement state is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }

        /* ai_ChargeFlying keeps its height across ai_GetRoomStats, then flies. */
        memcpy(slot_bytes, charge_initial_slot_bytes, sizeof(slot_bytes));
        memcpy(point_bytes, charge_initial_point_bytes, sizeof(point_bytes));
        object_animation_runtime_init(&charge_animation);
        charge_animation.workspace[CHARGE_ALIEN_SLOT][0u] = 1u;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][1u] = (uint8_t)charge_frame_index;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][2u] = (uint8_t)charge_option;
        alien_runtime_init(&charge_runtime);
        alien_runtime_begin_level(&charge_runtime);
        lighting_runtime_init(&charge_lighting);
        object_explosion_runtime_init(&charge_explosion);
        game_progression_init(&charge_progression);
        game_random_init(&charge_random);
        charge_expected_flying_objects.slot_bytes = charge_expected_flying_slot;
        charge_expected_flying_objects.slot_count = 1u;
        charge_expected_flying_objects.active_slot_count = 1u;
        memcpy(charge_expected_flying_slot,
               charge_initial_slot_bytes +
                   CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT,
               sizeof(charge_expected_flying_slot));
        if (!alien_flight_move_toward_player_height(
                &charge_expected_flying_objects, 0u, &charge_dynamic.runtime,
                charge_zone_index, &charge_player, charge_setup.thing_height,
                error, sizeof(error))) {
            fprintf(stderr, "ai_ChargeFlying expected flight state is invalid: %s\n", error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        expected_charge_flying_y = read_be16(charge_expected_flying_slot + 4u);
        memset(&charge_workspace, 0, sizeof(charge_workspace));
        if (!alien_charge_flying_update(
                &charge_objects, CHARGE_ALIEN_SLOT, &charge_runtime, &charge_animation,
                &charge_lighting, &charge_dynamic, &game.level_clips, &game.game_link_catalog,
                &charge_progression, &charge_explosion, &game.math, &charge_random,
                &charge_player, &charge_setup, 0u, 1u, &charge_workspace, &charge_state,
                error, sizeof(error)) ||
            charge_state.damage_taken != 0u || charge_state.got_out != 0u ||
            charge_state.teleport.teleported != 0u ||
            charge_state.heading.got_there != UINT8_MAX ||
            charge_state.movement.step_down != 1000 * 256 ||
            charge_state.damaged_player != UINT8_MAX ||
            slot_bytes[CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] != 7u ||
            read_be16(slot_bytes + CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 42u) !=
                0u ||
            read_be16(slot_bytes + CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 44u) !=
                0u ||
            read_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u) !=
                (uint16_t)expected_charge_flying_y) {
            fprintf(stderr, "ai_ChargeFlying source movement state is inconsistent: %s\n",
                    error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }

        /* ai_Approach's action-gated follow-up speed has no charge melee path. */
        memcpy(slot_bytes, charge_initial_slot_bytes, sizeof(slot_bytes));
        memcpy(point_bytes, charge_initial_point_bytes, sizeof(point_bytes));
        object_animation_runtime_init(&charge_animation);
        charge_animation.workspace[CHARGE_ALIEN_SLOT][0u] = 1u;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][1u] = (uint8_t)charge_frame_index;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][2u] = (uint8_t)charge_option;
        alien_runtime_init(&charge_runtime);
        alien_runtime_begin_level(&charge_runtime);
        lighting_runtime_init(&charge_lighting);
        object_explosion_runtime_init(&charge_explosion);
        game_progression_init(&charge_progression);
        game_random_init(&charge_random);
        memset(&charge_workspace, 0, sizeof(charge_workspace));
        if (!alien_approach_update(
                &charge_objects, CHARGE_ALIEN_SLOT, &charge_runtime, &charge_animation,
                &charge_lighting, &charge_dynamic, &game.level_navigation, &game.level_clips,
                &game.game_link_catalog, &charge_progression, &charge_explosion, &game.math,
                &charge_random, &charge_player, &charge_setup, 0u, 0u, 1u,
                &charge_workspace, &charge_state, error, sizeof(error)) ||
            charge_state.damage_taken != 0u || charge_state.got_out != 0u ||
            charge_state.teleport.teleported != 0u ||
            charge_state.heading.speed !=
                (int16_t)((int32_t)4 * charge_setup.followup_speed) ||
            charge_state.movement.step_down != 30 * 256 ||
            slot_bytes[CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] != 5u) {
            fprintf(stderr, "ai_Approach source movement state is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }

        /* ai_ApproachFlying flies first, then restores that height after room stats. */
        memcpy(slot_bytes, charge_initial_slot_bytes, sizeof(slot_bytes));
        memcpy(point_bytes, charge_initial_point_bytes, sizeof(point_bytes));
        object_animation_runtime_init(&charge_animation);
        charge_animation.workspace[CHARGE_ALIEN_SLOT][0u] = 1u;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][1u] = (uint8_t)charge_frame_index;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][2u] = (uint8_t)charge_option;
        alien_runtime_init(&charge_runtime);
        alien_runtime_begin_level(&charge_runtime);
        lighting_runtime_init(&charge_lighting);
        object_explosion_runtime_init(&charge_explosion);
        game_progression_init(&charge_progression);
        game_random_init(&charge_random);
        memset(charge_expected_flying_slot, 0, sizeof(charge_expected_flying_slot));
        memcpy(charge_expected_flying_slot,
               charge_initial_slot_bytes +
                   CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT,
               sizeof(charge_expected_flying_slot));
        if (!alien_flight_move_toward_player_height(
                &charge_expected_flying_objects, 0u, &charge_dynamic.runtime,
                charge_zone_index, &charge_player, charge_setup.thing_height,
                error, sizeof(error))) {
            fprintf(stderr, "ai_ApproachFlying expected flight state is invalid: %s\n", error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        expected_charge_flying_y = read_be16(charge_expected_flying_slot + 4u);
        memset(&charge_workspace, 0, sizeof(charge_workspace));
        if (!alien_approach_update(
                &charge_objects, CHARGE_ALIEN_SLOT, &charge_runtime, &charge_animation,
                &charge_lighting, &charge_dynamic, NULL, &game.level_clips,
                &game.game_link_catalog, &charge_progression, &charge_explosion, &game.math,
                &charge_random, &charge_player, &charge_setup, UINT8_MAX, 0u, 1u,
                &charge_workspace, &charge_state, error, sizeof(error)) ||
            charge_state.damage_taken != 0u || charge_state.got_out != 0u ||
            charge_state.teleport.teleported != 0u ||
            charge_state.movement.step_down != 1000 * 256 ||
            slot_bytes[CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] != 5u ||
            read_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u) !=
                (uint16_t)expected_charge_flying_y) {
            fprintf(stderr, "ai_ApproachFlying source movement state is inconsistent: %s\n",
                    error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }

        /* AI_MainRoutine selects the complete source charge owner from response mode. */
        memcpy(slot_bytes, charge_initial_slot_bytes, sizeof(slot_bytes));
        memcpy(point_bytes, charge_initial_point_bytes, sizeof(point_bytes));
        object_animation_runtime_init(&charge_animation);
        charge_animation.workspace[CHARGE_ALIEN_SLOT][0u] = 1u;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][1u] = (uint8_t)charge_frame_index;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][2u] = (uint8_t)charge_option;
        alien_runtime_init(&charge_runtime);
        alien_runtime_begin_level(&charge_runtime);
        lighting_runtime_init(&charge_lighting);
        object_explosion_runtime_init(&charge_explosion);
        game_progression_init(&charge_progression);
        game_random_init(&charge_random);
        object_observation_init(&charge_dispatch_observation);
        memset(&charge_dispatch_workspace, 0, sizeof(charge_dispatch_workspace));
        slot_bytes[CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 20u] = 1u;
        charge_setup.response_mode = 0;
        if (!alien_dispatch_update(
                &charge_objects, CHARGE_ALIEN_SLOT, &charge_runtime, &charge_animation,
                &charge_lighting, &charge_dynamic, &game.level_navigation, &game.level_clips,
                &game.game_link_catalog, &charge_progression, &charge_explosion, &game.math,
                &charge_random, &charge_player, &charge_setup, &charge_dispatch_observation,
                NULL, 1u, &charge_dispatch_workspace, &charge_dispatch_state,
                error, sizeof(error)) ||
            charge_dispatch_state.route != ALIEN_MAIN_ROUTE_RESPONSE ||
            charge_dispatch_state.behavior != ALIEN_MAIN_BEHAVIOR_CHARGE ||
            charge_dispatch_state.charge.damaged_player != UINT8_MAX ||
            charge_runtime.motion.new_x != charge_dispatch_workspace.movement.new_x ||
            charge_runtime.motion.new_z != charge_dispatch_workspace.movement.new_z ||
            slot_bytes[CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 19u] != 7u) {
            fprintf(stderr, "AI_MainRoutine response dispatch is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }

        /* CheckTeleport's success branch skips ai_ChargeCommon's AUX zone copy. */
        if (charge_teleport_zone_index != UINT16_MAX) {
        memset(slot_bytes, 0, sizeof(slot_bytes));
        memset(point_bytes, 0, sizeof(point_bytes));
        object_animation_runtime_init(&charge_animation);
        alien_runtime_init(&charge_runtime);
        alien_runtime_begin_level(&charge_runtime);
        lighting_runtime_init(&charge_lighting);
        object_explosion_runtime_init(&charge_explosion);
        game_progression_init(&charge_progression);
        game_random_init(&charge_random);
        charge_player.zone_index = (uint16_t)charge_teleport_zone.teleport_zone;
        charge_player.x = player_runtime_world_to_position(charge_teleport_zone.teleport_x);
        charge_player.z = player_runtime_world_to_position(charge_teleport_zone.teleport_z);
        charge_player.tmp_x = charge_player.x;
        charge_player.tmp_z = charge_player.z;
        write_be16(slot_bytes + CHARGE_AUXILIARY_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   0u);
        write_be16(slot_bytes + CHARGE_AUXILIARY_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   UINT16_MAX);
        write_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   CHARGE_ALIEN_POINT);
        write_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u,
                   UINT16_C(0xffec));
        write_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   charge_teleport_zone_index);
        write_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u,
                   charge_teleport_zone_index);
        write_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 40u,
                   charge_frame_index);
        slot_bytes[CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
        slot_bytes[CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 10u;
        slot_bytes[CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 21u] = UINT8_MAX;
        slot_bytes[CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u] =
            (uint8_t)charge_alien;
        write_be16(slot_bytes + CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                   CHARGE_PLAYER_SLOT);
        write_be16(slot_bytes + CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                   charge_player.zone_index);
        slot_bytes[CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 2u;
        slot_bytes[CHARGE_PLAYER_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
        write_be16(slot_bytes + CHARGE_TERMINATOR_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT,
                   UINT16_MAX);
        write_be16(point_bytes + CHARGE_ALIEN_POINT * OBJECT_RUNTIME_POINT_BYTE_COUNT,
                   (uint16_t)charge_teleport_zone.teleport_x);
        write_be16(point_bytes + CHARGE_ALIEN_POINT * OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   (uint16_t)charge_teleport_zone.teleport_z);
        if (!alien_setup_from_slot(&charge_objects, CHARGE_ALIEN_SLOT,
                                   &charge_dynamic.runtime, &game.game_link_catalog,
                                   &charge_setup, error, sizeof(error))) {
            fprintf(stderr, "ai_Charge teleport setup fixture is invalid: %s\n", error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        charge_animation.workspace[CHARGE_ALIEN_SLOT][0u] = 1u;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][1u] = (uint8_t)charge_frame_index;
        charge_animation.workspace[CHARGE_ALIEN_SLOT][2u] = (uint8_t)charge_option;
        memset(&charge_workspace, 0, sizeof(charge_workspace));
        if (!alien_charge_update(
                &charge_objects, CHARGE_ALIEN_SLOT, &charge_runtime, &charge_animation,
                &charge_lighting, &charge_dynamic, &game.level_navigation, &game.level_clips,
                &game.game_link_catalog, &charge_progression, &charge_explosion, &game.math,
                &charge_random, &charge_player, &charge_setup, 0u, 1u, &charge_workspace,
                &charge_state, error, sizeof(error)) ||
            charge_state.teleport.teleported != UINT8_MAX ||
            read_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) !=
                charge_teleport_destination.id ||
            read_be16(slot_bytes + CHARGE_ALIEN_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u) !=
                charge_teleport_destination.id ||
            read_be16(slot_bytes + CHARGE_AUXILIARY_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                      12u) != charge_teleport_zone.id ||
            read_be16(slot_bytes + CHARGE_AUXILIARY_SLOT * OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                      26u) != charge_teleport_zone.id) {
            fprintf(stderr, "ai_Charge teleport source state is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&charge_dynamic);
            game_bootstrap_destroy(&game);
            return 1;
        }
        }
        level_dynamic_state_destroy(&charge_dynamic);
    }
    {
        /* newaliencontrol.s:RunAround chooses the source-side lateral target. */
        AlienRunAroundState run_around = {
            100, 50, 20, 10,
            0, 100, 0, 0,
            50, 0
        };

        if (!alien_run_around_apply(&run_around, error, sizeof(error)) ||
            run_around.new_x != 40 || run_around.new_z != -30) {
            fprintf(stderr, "RunAround right-side source target is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        run_around.new_x = 20;
        run_around.new_z = 10;
        run_around.object_x = -50;
        if (!alien_run_around_apply(&run_around, error, sizeof(error)) ||
            run_around.new_x != 0 || run_around.new_z != 50) {
            fprintf(stderr, "RunAround left-side source target is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    {
        /* objectmove.s:CalcDist/HeadTowards and HeadTowardsAng source paths. */
        ObjectApproach approach = {0};
        ObjectHeading heading = {0};
        int16_t heading_sine;
        int16_t heading_cosine;

        approach.new_x = 3;
        approach.new_z = 4;
        if (!object_heading_calculate_distance(&approach, error, sizeof(error)) ||
            approach.x_difference != 3 || approach.z_difference != 4 ||
            approach.distance != 5) {
            fprintf(stderr, "CalcDist source state is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        memset(&approach, 0, sizeof(approach));
        approach.new_x = 100;
        approach.speed = 20;
        if (!object_heading_towards(&approach, error, sizeof(error)) ||
            approach.distance != 101 || approach.got_there != 0u ||
            approach.new_x != 19 || approach.new_z != 0) {
            fprintf(stderr, "HeadTowards speed path is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        memset(&approach, 0, sizeof(approach));
        approach.new_z = 10;
        approach.range = 20;
        if (!object_heading_towards(&approach, error, sizeof(error)) ||
            approach.distance != 10 || approach.got_there != UINT8_MAX ||
            approach.new_x != 0 || approach.new_z != -10) {
            fprintf(stderr, "HeadTowards range path is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }
        memset(&approach, 0, sizeof(approach));
        approach.old_x = 12;
        approach.old_z = -7;
        approach.new_x = 12;
        approach.new_z = -7;
        approach.got_there = 0x5au;
        if (!object_heading_towards(&approach, error, sizeof(error)) ||
            approach.distance != 0 || approach.got_there != 0x5au) {
            fprintf(stderr, "HeadTowards zero-distance path is inconsistent: %s\n", error);
            game_bootstrap_destroy(&game);
            return 1;
        }

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
        GameInput player_collision_input;
        PlayerRuntime player_collision_player = {0};
        uint16_t edge_flags = 0u;
        uint32_t other_edge_count = 0u;
        uint32_t other_edge_index = UINT32_MAX;

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
        write_be16(level_bytes + 68u, UINT16_C(0xfffe));
        write_be16(level_bytes + 164u, UINT16_MAX);
        write_be16(level_bytes + 200u, 10u);
        write_be16(level_bytes + 202u, 20u);
        write_be16(level_bytes + 204u, 0u);
        write_be16(level_bytes + 206u, UINT16_C(0xffec));
        write_be16(level_bytes + 208u, UINT16_MAX);
        write_be16(level_bytes + 210u, 20u);
        if (!level_runtime_get_zone_extended_edge_count(
                &movement_level, 0u, &other_edge_count, error, sizeof(error)) ||
            other_edge_count != 1u ||
            !level_runtime_get_zone_extended_edge_index(
                &movement_level, 0u, 0u, &other_edge_index, error, sizeof(error)) ||
            other_edge_index != 0u) {
            fprintf(stderr,
                    "MoveObject checkotherwalls did not restart its source edge list: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }
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
        /* hires.s:Plr1_Control uses this non-zero-extension MoveObject path. */
        game_input_init(&player_collision_input);
        player_collision_player.x = player_runtime_world_to_position(8);
        player_collision_player.z = player_runtime_world_to_position(10);
        player_collision_player.snap_x = player_runtime_world_to_position(12);
        player_collision_player.snap_z = player_runtime_world_to_position(10);
        player_collision_player.height = 12 * 1024;
        player_collision_player.snap_height = player_collision_player.height;
        player_collision_player.snap_target_height = player_collision_player.height;
        player_collision_player.snap_squished_height = player_collision_player.height;
        player_collision_player.snap_y = 1000;
        player_collision_player.snap_target_y = player_collision_player.snap_y;
        player_collision_player.zone_index = 0u;
        if (!level_dynamic_state_set_edge_flags(&movement_state, 0u, 0u)) {
            fprintf(stderr, "could not reset Plr1_Control MoveObject edge flags\n");
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if (!player_runtime_update_spatial(
                &player_collision_player, &player_collision_input, &control_defaults,
                &game.preferences, &game.math, &movement_state.runtime, &movement_state,
                error, sizeof(error))) {
            fprintf(stderr, "Plr1_Control MoveObject wall update failed: %s\n", error);
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        if (player_runtime_position_to_world(player_collision_player.x) != 11 ||
            player_runtime_position_to_world(player_collision_player.z) != 10 ||
            !level_dynamic_state_get_edge_flags(&movement_state, 0u, &edge_flags) ||
            edge_flags != 0x0100u) {
            fprintf(stderr,
                    "Plr1_Control MoveObject wall collision is inconsistent: x=%d z=%d "
                    "flags=%04x: %s\n",
                    player_runtime_position_to_world(player_collision_player.x),
                    player_runtime_position_to_world(player_collision_player.z), edge_flags,
                    error);
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        {
            /* modules/player.s:plr_Fall source gravity/water/lift handoff. */
            PlayerRuntime fall_player = {0};
            GameInput fall_input;
            const int32_t fall_floor = 20000;
            const int32_t fall_roof = -50000;
            const int32_t fall_target = fall_floor - 12 * 1024;

            write_be32(movement_state.level_bytes + 2u, (uint32_t)fall_floor);
            write_be32(movement_state.level_bytes + 6u, (uint32_t)fall_roof);
            write_be32(movement_state.level_bytes + 18u, (uint32_t)fall_floor);
            game_input_init(&fall_input);
            fall_player.x = player_runtime_world_to_position(8);
            fall_player.z = player_runtime_world_to_position(10);
            fall_player.snap_x = fall_player.x;
            fall_player.snap_z = fall_player.z;
            fall_player.y = fall_target - 2712;
            fall_player.snap_y = fall_player.y;
            fall_player.snap_target_y = fall_target;
            fall_player.snap_y_velocity = 511;
            fall_player.height = 12 * 1024;
            fall_player.snap_height = fall_player.height;
            fall_player.snap_target_height = fall_player.height;
            fall_player.snap_squished_height = fall_player.height;
            fall_player.zone_index = 0u;
            fall_player.health = 200u;
            if (!player_runtime_update_spatial(
                    &fall_player, &fall_input, &control_defaults, &game.preferences,
                    &game.math, &movement_state.runtime, &movement_state, error,
                    sizeof(error)) ||
                fall_player.snap_y != fall_target - 2201 ||
                fall_player.snap_y_velocity != 575 || fall_player.fall_damage != 1 ||
                fall_player.decelerate != 0u) {
                fprintf(stderr, "plr_Fall dry source acceleration is inconsistent: y=%d "
                                "velocity=%d damage=%d decelerate=%u: %s\n",
                        fall_player.snap_y, fall_player.snap_y_velocity,
                        fall_player.fall_damage, fall_player.decelerate, error);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }

            write_be32(movement_state.level_bytes + 18u, 0u);
            fall_player.snap_y = fall_target - 2712;
            fall_player.snap_target_y = fall_target;
            fall_player.snap_y_velocity = 511;
            fall_player.fall_damage = 9;
            fall_player.decelerate = 0u;
            if (!player_runtime_update_spatial(
                    &fall_player, &fall_input, &control_defaults, &game.preferences,
                    &game.math, &movement_state.runtime, &movement_state, error,
                    sizeof(error)) ||
                fall_player.snap_y_velocity != 512 || fall_player.fall_damage != 0 ||
                fall_player.decelerate == 0u) {
                fprintf(stderr, "plr_Fall water terminal velocity is inconsistent: velocity=%d "
                                "damage=%d decelerate=%u: %s\n",
                        fall_player.snap_y_velocity, fall_player.fall_damage,
                        fall_player.decelerate, error);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }

            write_be32(movement_state.level_bytes + 18u, (uint32_t)fall_floor);
            fall_player.snap_y = fall_target - 100;
            fall_player.snap_target_y = fall_target;
            fall_player.snap_y_velocity = 128;
            fall_player.floor_speed = -7;
            fall_player.fall_damage = 23;
            if (!player_runtime_update_spatial(
                    &fall_player, &fall_input, &control_defaults, &game.preferences,
                    &game.math, &movement_state.runtime, &movement_state, error,
                    sizeof(error)) ||
                fall_player.snap_y != fall_target + 28 ||
                fall_player.snap_y_velocity != -7 * 64 || fall_player.fall_damage != 0) {
                fprintf(stderr, "plr_Fall landing FloorSpd handoff is inconsistent: y=%d "
                                "velocity=%d damage=%d: %s\n",
                        fall_player.snap_y, fall_player.snap_y_velocity,
                        fall_player.fall_damage, error);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }
            if (!player_runtime_update_spatial(
                    &fall_player, &fall_input, &control_defaults, &game.preferences,
                    &game.math, &movement_state.runtime, &movement_state, error,
                    sizeof(error)) ||
                fall_player.snap_y != fall_target ||
                fall_player.snap_y_velocity != -7 * 64) {
                fprintf(stderr, "plr_Fall bounded below-floor correction is inconsistent: "
                                "y=%d velocity=%d: %s\n",
                        fall_player.snap_y, fall_player.snap_y_velocity, error);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }

            write_be32(movement_state.level_bytes + 18u, (uint32_t)fall_target);
            game_input_init(&fall_input);
            if (!game_input_set_raw_key(
                    &fall_input,
                    control_defaults.assigned_raw_keys[GAME_CONTROL_JUMP], 1,
                    error, sizeof(error))) {
                fprintf(stderr, "could not prepare plr_Fall water jump fixture: %s\n", error);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }
            fall_player.snap_y = fall_target;
            fall_player.snap_target_y = fall_target;
            fall_player.snap_y_velocity = 0;
            fall_player.floor_speed = 0;
            if (!player_runtime_update_spatial(
                    &fall_player, &fall_input, &control_defaults, &game.preferences,
                    &game.math, &movement_state.runtime, &movement_state, error,
                    sizeof(error)) ||
                fall_player.snap_y != fall_target - 512 ||
                fall_player.snap_y_velocity != -512) {
                fprintf(stderr, "plr_Fall water jump speed is inconsistent: y=%d velocity=%d: %s\n",
                        fall_player.snap_y, fall_player.snap_y_velocity, error);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }

            {
                GameInventory fall_inventory = {0};
                GameAudioEvents fall_audio;
                uint8_t entity_damage = 250u;

                /* plr_Fall applies its >100 accumulator through ADD.B. */
                game_input_init(&fall_input);
                fall_player.snap_y = fall_target;
                fall_player.snap_target_y = fall_target;
                fall_player.snap_y_velocity = 0;
                fall_player.floor_speed = 0;
                fall_player.fall_damage = 125;
                fall_player.add_to_bobble = 0;
                fall_player.walk_sfx_time = 0u;
                if (!player_runtime_update_fall(
                        &fall_player, &fall_input, &control_defaults, &game.math,
                        &movement_state.runtime, &fall_inventory, &entity_damage,
                        &game.game_link_catalog, NULL, error, sizeof(error)) ||
                    entity_damage != 19u || fall_player.fall_damage != 0) {
                    fprintf(stderr, "plr_Fall grounded damage is inconsistent: %s\n", error);
                    level_dynamic_state_destroy(&movement_state);
                    game_bootstrap_destroy(&game);
                    return 1;
                }

                /* TST.W/BLE prevents jump thrust at zero or signed-negative health. */
                fall_player.snap_y = fall_target;
                fall_player.snap_target_y = fall_target;
                fall_player.snap_y_velocity = 0;
                fall_player.health = UINT16_C(0xffff);
                if (!game_input_set_raw_key(
                        &fall_input,
                        control_defaults.assigned_raw_keys[GAME_CONTROL_JUMP], 1,
                        error, sizeof(error)) ||
                    !player_runtime_update_fall(
                        &fall_player, &fall_input, &control_defaults, &game.math,
                        &movement_state.runtime, &fall_inventory, &entity_damage,
                        &game.game_link_catalog, NULL, error, sizeof(error)) ||
                    fall_player.snap_y != fall_target ||
                    fall_player.snap_y_velocity != 0) {
                    fprintf(stderr, "plr_Fall dead-player jump gate is inconsistent: %s\n",
                            error);
                    level_dynamic_state_destroy(&movement_state);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                fall_player.health = game.player.health;
                game_input_init(&fall_input);

                entity_damage = 0u;
                fall_player.snap_y = fall_target - 100;
                fall_player.snap_target_y = fall_target;
                fall_player.snap_y_velocity = 128;
                fall_player.fall_damage = 131;
                if (!player_runtime_update_fall(
                        &fall_player, &fall_input, &control_defaults, &game.math,
                        &movement_state.runtime, &fall_inventory, &entity_damage,
                        &game.game_link_catalog, NULL, error, sizeof(error)) ||
                    fall_player.snap_y != fall_target + 28 ||
                    fall_player.snap_y_velocity != 0 || entity_damage != 31u ||
                    fall_player.fall_damage != 0) {
                    fprintf(stderr, "plr_Fall crossed-floor damage is inconsistent: %s\n",
                            error);
                    level_dynamic_state_destroy(&movement_state);
                    game_bootstrap_destroy(&game);
                    return 1;
                }

                /* Owned jetpack caps fuel, consumes one unit, and applies -128 thrust. */
                game_input_init(&fall_input);
                if (!game_input_set_raw_key(
                        &fall_input,
                        control_defaults.assigned_raw_keys[GAME_CONTROL_JUMP], 1,
                        error, sizeof(error))) {
                    fprintf(stderr, "could not prepare plr_Fall jetpack fixture: %s\n", error);
                    level_dynamic_state_destroy(&movement_state);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
                fall_inventory.jetpack = 1u;
                fall_inventory.jetpack_fuel = 300u;
                fall_player.snap_y = fall_target - 2000;
                fall_player.snap_target_y = fall_target;
                fall_player.snap_y_velocity = 200;
                fall_player.fall_damage = 44;
                fall_player.bobble = 8190u;
                entity_damage = 0u;
                if (!player_runtime_update_fall(
                        &fall_player, &fall_input, &control_defaults, &game.math,
                        &movement_state.runtime, &fall_inventory, &entity_damage,
                        &game.game_link_catalog, NULL, error, sizeof(error)) ||
                    fall_inventory.jetpack_fuel != 249u ||
                    fall_player.snap_y != fall_target - 1928 ||
                    fall_player.snap_y_velocity != 136 || fall_player.fall_damage != 1 ||
                    fall_player.bobble != 38u || fall_player.decelerate == 0u ||
                    entity_damage != 0u) {
                    fprintf(stderr, "plr_Fall jetpack thrust is inconsistent: %s\n", error);
                    level_dynamic_state_destroy(&movement_state);
                    game_bootstrap_destroy(&game);
                    return 1;
                }

                /* Crossing ZoneT_Water emits source sample six and caps descent. */
                write_be32(movement_state.level_bytes + 18u, 0u);
                game_input_init(&fall_input);
                game_audio_events_init(&fall_audio);
                memset(&fall_inventory, 0, sizeof(fall_inventory));
                fall_player.snap_y = 1000;
                fall_player.snap_target_y = 5000;
                fall_player.snap_y_velocity = 511;
                fall_player.fall_damage = 9;
                fall_player.snap_x = player_runtime_world_to_position(8);
                fall_player.snap_z = player_runtime_world_to_position(10);
                fall_player.snap_yaw = 0u;
                if (!player_runtime_update_fall(
                        &fall_player, &fall_input, &control_defaults, &game.math,
                        &movement_state.runtime, &fall_inventory, &entity_damage,
                        &game.game_link_catalog, &fall_audio, error, sizeof(error)) ||
                    fall_player.snap_y != 1511 || fall_player.snap_y_velocity != 512 ||
                    fall_player.fall_damage != 0 || fall_player.decelerate == 0u ||
                    fall_audio.count != 1u || fall_audio.events[0u].sample_index != 6u ||
                    fall_audio.events[0u].volume != 80u ||
                    fall_audio.events[0u].source_id != UINT16_C(0xfff8)) {
                    fprintf(stderr, "plr_Fall water-entry splash is inconsistent: %s\n", error);
                    level_dynamic_state_destroy(&movement_state);
                    game_bootstrap_destroy(&game);
                    return 1;
                }
            }
            write_be32(movement_state.level_bytes + 2u, 0u);
            write_be32(movement_state.level_bytes + 6u, 0u);
            write_be32(movement_state.level_bytes + 18u, 0u);
        }
        {
            uint8_t miss_slot_bytes[(OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT + 2u) *
                                    OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
            uint8_t miss_point_bytes[2u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
            ObjectRuntime miss_objects = {0};
            PlayerRuntime miss_player = {0};
            PlayerShotTarget target_miss = {0};
            GameRandom miss_random;
            GameRandom expected_random;
            ObjectMotionRuntime miss_motion;
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
            miss_objects.slot_count = OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT + 2u;
            miss_objects.active_slot_count = OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT + 1u;
            miss_objects.player_shot_first_slot = 0u;
            miss_objects.point_bytes = miss_point_bytes;
            miss_objects.point_count = 2u;
            write_be16(miss_slot_bytes + 0u, 0u);
            write_be16(miss_slot_bytes + 12u, UINT16_MAX);
            miss_slot_bytes[16u] = 0x7eu;
            write_be16(miss_slot_bytes + 26u, UINT16_C(0x1234));
            write_be32(miss_point_bytes + 0u, UINT32_C(0x12345678));
            write_be32(miss_point_bytes + 4u, UINT32_C(0x9abcdef0));
            miss_player.x = player_runtime_world_to_position(0);
            miss_player.y = 1000;
            miss_player.z = player_runtime_world_to_position(10);
            miss_player.yaw = 0u;
            miss_player.zone_index = 0u;
            object_motion_runtime_init(&miss_motion);
            game_random_init(&miss_random);
            expected_random = miss_random;
            spread_word = game_random_next(&expected_random);
            spread = (int32_t)(spread_word & 0x0fffu) - 0x0800;
            expected_hit_height = miss_player.y + 10 * 128 + spread +
                (spread / ray_z) * (10 - ray_z);
            if (!player_shoot_apply_hitscan_miss_with_motion(
                    &miss_objects, &movement_state, &miss_player, &game.math,
                    &miss_motion,
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
                miss_motion.new_x != 0 || miss_motion.new_z != 20 ||
                !level_dynamic_state_get_edge_flags(&movement_state, 0u, &edge_flags) ||
                edge_flags != 0x0400u) {
                fprintf(stderr, "plr1_HitscanFailed source miss state is inconsistent: %s\n",
                        error);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }
            /*
             * A selected-target miss has already consumed its hit roll.  The
             * source traces halfway to a4's point, using the terminal ObjT
             * record at a0 for its initial height, without another GetRand.
             */
            write_be16(miss_slot_bytes + 12u, UINT16_MAX);
            write_be16(miss_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u, 1u);
            write_be16(miss_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
            write_be32(miss_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u, 0u);
            write_be32(miss_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                       UINT32_C(30) << 16u);
            write_be16(miss_slot_bytes +
                           (OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT + 1u) *
                               OBJECT_RUNTIME_SLOT_BYTE_COUNT +
                           4u,
                       7u);
            target_miss.found = UINT8_MAX;
            target_miss.slot_index = 1u;
            target_miss.point_index = 1u;
            miss_spawned = 0u;
            object_motion_runtime_init(&miss_motion);
            if (!player_shoot_apply_hitscan_target_miss_with_motion(
                    &miss_objects, &movement_state, &miss_player, &target_miss,
                    &game.math, &miss_motion, &miss_random, 7u, &miss_spawned,
                    error, sizeof(error)) ||
                miss_spawned != UINT8_MAX || miss_random.state != expected_random.state ||
                read_be32(miss_slot_bytes + 44u) != 896u ||
                read_be16(miss_slot_bytes + 4u) != 7u ||
                read_be32(miss_point_bytes + 0u) != UINT32_C(0x00005678) ||
                read_be32(miss_point_bytes + 4u) != UINT32_C(0x0014def0) ||
                miss_motion.new_x != 0 || miss_motion.new_z != 20) {
                fprintf(stderr, "Plr1_Shot selected-target miss is inconsistent: %s\n", error);
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }
        }
        {
            /* newaliencontrol.s:SHOOTPLAYER1 uses the alien/player snapshot ray, not Plr1_Shot. */
            enum {
                ALIEN_MISS_SLOT_COUNT = 1u + OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT
            };
            uint8_t miss_slot_bytes[ALIEN_MISS_SLOT_COUNT * OBJECT_RUNTIME_SLOT_BYTE_COUNT];
            uint8_t miss_point_bytes[ALIEN_MISS_SLOT_COUNT * OBJECT_RUNTIME_POINT_BYTE_COUNT];
            ObjectRuntime miss_objects = {0};
            PlayerRuntime miss_player = {0};
            GameRandom miss_random;
            GameRandom expected_random;
            AlienHitscanMissState miss_state;
            uint16_t nonnegative_random_state = 0u;
            int found_nonnegative_random_state = 0;

            for (uint32_t candidate = 0u; candidate <= UINT16_MAX; ++candidate) {
                GameRandom candidate_random = {(uint16_t)candidate};

                if ((int16_t)game_random_next(&candidate_random) >= 0) {
                    nonnegative_random_state = (uint16_t)candidate;
                    found_nonnegative_random_state = 1;
                    break;
                }
            }
            if (!found_nonnegative_random_state ||
                !level_dynamic_state_set_edge_flags(&movement_state, 0u, 0u)) {
                fprintf(stderr, "could not prepare SHOOTPLAYER1 source fixture\n");
                level_dynamic_state_destroy(&movement_state);
                game_bootstrap_destroy(&game);
                return 1;
            }
            memset(miss_slot_bytes, 0xa5, sizeof(miss_slot_bytes));
            memset(miss_point_bytes, 0x5a, sizeof(miss_point_bytes));
            miss_objects.slot_bytes = miss_slot_bytes;
            miss_objects.slot_count = ALIEN_MISS_SLOT_COUNT;
            miss_objects.active_slot_count = ALIEN_MISS_SLOT_COUNT;
            miss_objects.player_shot_first_slot = 1u;
            miss_objects.point_bytes = miss_point_bytes;
            miss_objects.point_count = ALIEN_MISS_SLOT_COUNT;
            write_be16(miss_slot_bytes + 0u, 0u);
            write_be16(miss_slot_bytes + 4u, 15u);
            write_be16(miss_slot_bytes + 12u, 0u);
            write_be32(miss_point_bytes + 0u, UINT32_C(0x00001111));
            write_be32(miss_point_bytes + 4u, UINT32_C(0x000a2222));
            for (uint32_t shot_index = 0u; shot_index < OBJECT_RUNTIME_PROJECTILE_SLOT_COUNT;
                 ++shot_index) {
                uint32_t slot_index = 1u + shot_index;

                write_be16(miss_slot_bytes + slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 0u,
                           (uint16_t)slot_index);
                write_be16(miss_slot_bytes + slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u,
                           UINT16_MAX);
            }
            miss_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0x7eu;
            write_be16(miss_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u,
                       UINT16_C(0x1234));
            write_be32(miss_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u,
                       UINT32_C(0x5555beef));
            write_be32(miss_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                       UINT32_C(0x6666face));
            miss_player.tmp_x = player_runtime_world_to_position(0);
            miss_player.tmp_y = 0;
            miss_player.tmp_z = player_runtime_world_to_position(11);
            miss_random.state = nonnegative_random_state;
            expected_random = miss_random;
            (void)game_random_next(&expected_random);
            if (!alien_attack_shoot_player_one(
                    &miss_objects, 0u, &movement_state, &miss_player, &miss_random,
                    &miss_state, error, sizeof(error)) ||
                miss_state.impact_spawned != UINT8_MAX ||
                miss_state.movement.hit_wall != UINT8_MAX ||
                miss_state.movement.new_x != 0 || miss_state.movement.new_z != 20 ||
                miss_state.movement.wall_hit_height != 15 * 128 ||
                miss_random.state != expected_random.state ||
                miss_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] != 0x7eu ||
                read_be16(miss_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u) != 0u ||
                read_be16(miss_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 26u) !=
                    UINT16_C(0x1234) ||
                miss_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 30u] != 1u ||
                miss_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 31u] != 0u ||
                miss_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 52u] != 0u ||
                read_be16(miss_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 54u) != 0u ||
                miss_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 62u] != UINT8_MAX ||
                read_be32(miss_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 44u) !=
                    15u * 128u ||
                read_be16(miss_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 4u) != 15u ||
                read_be32(miss_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 0u) !=
                    UINT32_C(0x0000beef) ||
                read_be32(miss_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u) !=
                    UINT32_C(0x0014face) ||
                !level_dynamic_state_get_edge_flags(&movement_state, 0u, &edge_flags) ||
                edge_flags != 0x0400u) {
                fprintf(stderr, "SHOOTPLAYER1 source miss state is inconsistent: %s\n", error);
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
         * The same joined edge at the upper opening must select StoodInTop,
         * not reject the two-storey room or snap back to its lower floor.
         */
        write_be32(movement_state.level_bytes + 110u, (uint32_t)-20000);
        write_be32(movement_state.level_bytes + 114u, (uint32_t)-50000);
        memset(&movement_trace, 0, sizeof(movement_trace));
        movement_trace.zone_index = 0u;
        movement_trace.old_x = 0;
        movement_trace.old_z = 10;
        movement_trace.new_x = 20;
        movement_trace.new_z = 10;
        movement_trace.old_y = -30000;
        movement_trace.new_y = -30000;
        movement_trace.thing_height = 12000;
        movement_trace.step_up = 40 * 256;
        if (!object_movement_trace_zero_extension(&movement_state, &movement_trace,
                                                  error, sizeof(error)) ||
            movement_trace.hit_wall != 0u || movement_trace.zone_index != 1u ||
            movement_trace.stood_in_top != UINT8_MAX || movement_trace.new_x != 20 ||
            movement_trace.new_z != 10) {
            fprintf(stderr, "MoveObject upper-zone crossing is inconsistent: %s\n", error);
            level_dynamic_state_destroy(&movement_state);
            game_bootstrap_destroy(&game);
            return 1;
        }
        write_be32(movement_state.level_bytes + 110u, 10000u);
        write_be32(movement_state.level_bytes + 114u, 0u);
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
         * objectmove.s:CheckTeleport keeps the object's ObjT zone until its
         * caller later runs ai_GetRoomStats. Its collision pass therefore
         * still examines the source zone, even while testing teleport X/Z.
         */
        uint8_t teleport_level_bytes[256u] = {0};
        uint8_t teleport_graphics_bytes[16u] = {0};
        uint8_t teleport_slot_bytes[3u * OBJECT_RUNTIME_SLOT_BYTE_COUNT] = {0};
        uint8_t teleport_point_bytes[2u * OBJECT_RUNTIME_POINT_BYTE_COUNT] = {0};
        int16_t teleport_a2_words[8u] = {0, 20, 40, 0, 0, 20, 40, 0};
        LevelRuntime teleport_level = {0};
        ObjectRuntime teleport_objects = {0};
        ObjectCollisionTrace teleport_trace = {0};
        ObjectTeleportState teleport_state = {0};

        teleport_level.level_bytes = teleport_level_bytes;
        teleport_level.level_size = sizeof(teleport_level_bytes);
        teleport_level.graphics_bytes = teleport_graphics_bytes;
        teleport_level.graphics_size = sizeof(teleport_graphics_bytes);
        teleport_level.zone_offsets_table_offset = 0u;
        teleport_level.zone_count = 2u;
        write_be32(teleport_graphics_bytes + 0u, 0u);
        write_be32(teleport_graphics_bytes + 4u, 100u);
        write_be16(teleport_level_bytes + 0u, 0u);
        write_be32(teleport_level_bytes + 2u, 100u);
        write_be16(teleport_level_bytes + 38u, 1u);
        write_be16(teleport_level_bytes + 40u, 300u);
        write_be16(teleport_level_bytes + 42u, 400u);
        write_be16(teleport_level_bytes + 100u, 1u);
        write_be32(teleport_level_bytes + 102u, 600u);
        write_be16(teleport_level_bytes + 138u, UINT16_MAX);

        teleport_objects.slot_bytes = teleport_slot_bytes;
        teleport_objects.slot_count = 3u;
        teleport_objects.active_slot_count = 1u;
        teleport_objects.point_bytes = teleport_point_bytes;
        teleport_objects.point_count = 2u;
        write_be16(teleport_slot_bytes + 0u, 0u);
        write_be16(teleport_slot_bytes + 12u, 0u);
        teleport_slot_bytes[16u] = 0u;
        teleport_slot_bytes[18u] = 1u;
        write_be16(teleport_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        teleport_trace.collision_id = 0u;
        teleport_trace.old_x = 0;
        teleport_trace.old_z = 0;
        teleport_trace.new_x = 111;
        teleport_trace.new_z = 222;
        teleport_trace.new_y = 500;
        teleport_trace.thing_height = 20 * 128;
        if (!object_teleport_check(
                &teleport_level, &teleport_objects, &game.game_link_catalog, 0u,
                teleport_a2_words,
                sizeof(teleport_a2_words) / sizeof(teleport_a2_words[0]),
                &teleport_trace, &teleport_state, error, sizeof(error)) ||
            teleport_state.teleported != UINT8_MAX || teleport_state.zone_index != 1u ||
            teleport_state.floor_delta != 500 || teleport_trace.new_x != 300 ||
            teleport_trace.new_z != 400 || teleport_trace.new_y != 500) {
            fprintf(stderr, "CheckTeleport successful source transition is inconsistent: %s\n",
                    error);
            game_bootstrap_destroy(&game);
            return 1;
        }

        /* The candidate stays in the original zone: this rejects the jump. */
        write_be16(teleport_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT, 1u);
        write_be16(teleport_slot_bytes + OBJECT_RUNTIME_SLOT_BYTE_COUNT + 12u, 0u);
        teleport_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 16u] = 0u;
        teleport_slot_bytes[OBJECT_RUNTIME_SLOT_BYTE_COUNT + 18u] = 1u;
        write_be16(teleport_slot_bytes + 2u * OBJECT_RUNTIME_SLOT_BYTE_COUNT, UINT16_MAX);
        write_be32(teleport_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT,
                   UINT32_C(300) << 16u);
        write_be32(teleport_point_bytes + OBJECT_RUNTIME_POINT_BYTE_COUNT + 4u,
                   UINT32_C(400) << 16u);
        teleport_objects.active_slot_count = 2u;
        teleport_trace.new_x = 111;
        teleport_trace.new_z = 222;
        teleport_trace.new_y = 500;
        if (!object_teleport_check(
                &teleport_level, &teleport_objects, &game.game_link_catalog, 0u,
                teleport_a2_words,
                sizeof(teleport_a2_words) / sizeof(teleport_a2_words[0]),
                &teleport_trace, &teleport_state, error, sizeof(error)) ||
            teleport_state.teleported != 0u || teleport_state.zone_index != 0u ||
            teleport_state.floor_delta != 500 || teleport_trace.new_x != 300 ||
            teleport_trace.new_z != 400 || teleport_trace.new_y != 500) {
            fprintf(stderr, "CheckTeleport collision rejection is inconsistent: %s\n", error);
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
        uint8_t level_bytes[300u] = {0};
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
    {
        RenderView view;
        PlayerRuntime camera_player = game.player;

        render_view_init(&view);
        render_view_set_source_yaw(&view, 8180u);
        render_view_add_mouse_yaw(&view, 5);
        if (render_view_yaw(&view) != 8u) {
            fprintf(stderr, "native host-rate view yaw does not wrap source angles\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        player_runtime_set_camera_yaw(&camera_player, render_view_yaw(&view));
        if (camera_player.yaw != render_view_yaw(&view) ||
            camera_player.snap_yaw != render_view_yaw(&view)) {
            fprintf(stderr, "host-rate camera yaw is not the player heading\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        render_view_add_mouse_yaw(&view, 2);
        if (render_view_yaw(&view) != 16u) {
            fprintf(stderr, "native host-rate view yaw depends on source-tick reconciliation\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        player_runtime_set_camera_yaw(&camera_player, UINT16_MAX);
        if (camera_player.yaw != 8190u || camera_player.snap_yaw != 8190u) {
            fprintf(stderr, "camera-owned player yaw does not wrap source angles\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        render_view_add_mouse_motion(&view, -100, 0u);
        if (view.pitch_degrees < 32.00f || view.pitch_degrees > 32.01f ||
            view.aim_speed != -10240 || view.look_offset != -80) {
            fprintf(stderr, "source projectile-aligned upward pitch is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        render_view_add_mouse_motion(&view, 1000, 0u);
        if (view.pitch_degrees > -32.00f || view.pitch_degrees < -32.01f ||
            view.aim_speed != 10240 || view.look_offset != 80) {
            fprintf(stderr, "source projectile-aligned lower clamp is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        render_view_init(&view);
        render_view_add_mouse_motion(&view, -100, UINT8_MAX);
        if (view.pitch_degrees > -32.00f || view.pitch_degrees < -32.01f) {
            fprintf(stderr, "source projectile-aligned mouse inversion is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
        render_view_set_source_look(&view, -4096, -8);
        if (view.pitch_degrees < 14.03f || view.pitch_degrees > 14.04f ||
            view.aim_speed != -4096 || view.look_offset != -8) {
            fprintf(stderr, "source committed aim reconciliation is inconsistent\n");
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    game_bootstrap_destroy(&game);
    (void)remove(save_path);
    return 0;
}
