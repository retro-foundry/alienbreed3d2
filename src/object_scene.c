#include "object_scene.h"

#include <limits.h>
#include <stdio.h>

enum {
    /* ObjT/EntT/ShotT source offsets consumed by objdrawhires.s. */
    OBJECT_SCENE_POINT_INDEX = 0u,
    OBJECT_SCENE_BRIGHTNESS = 2u,
    OBJECT_SCENE_VERTICAL_POSITION = 4u,
    OBJECT_SCENE_WIDTH_HEIGHT = 6u,
    OBJECT_SCENE_GRAPHICS_TYPE = 8u,
    OBJECT_SCENE_EFFECT = 10u,
    OBJECT_SCENE_FRAME = 11u,
    OBJECT_SCENE_ZONE_ID = 12u,
    OBJECT_SCENE_CURRENT_ANGLE = 30u,
    OBJECT_SCENE_AUX_OFFSET_X = 44u,
    OBJECT_SCENE_AUX_OFFSET_Y = 46u,
    OBJECT_SCENE_IN_UPPER_ZONE = 63u,
    OBJECT_SCENE_BITMAP_LIGHT_FIRST = 2u,
    OBJECT_SCENE_BITMAP_LIGHT_COUNT = 4u
};

static void object_scene_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_scene_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t object_scene_read_be16s(const uint8_t *source)
{
    return (int16_t)object_scene_read_be16(source);
}

static int object_scene_validate_state(const ObjectRuntime *objects, char *error,
                                       size_t error_size)
{
    if (!objects || !objects->slot_bytes || !objects->point_bytes ||
        objects->active_slot_count > objects->slot_count ||
        objects->slot_count == 0u) {
        object_scene_set_error(error, error_size,
                               "Draw_Objects received invalid owned source state");
        return 0;
    }
    return 1;
}

int object_scene_count_active(const ObjectRuntime *objects, uint32_t *out_count,
                              char *error, size_t error_size)
{
    uint32_t count = 0u;

    if (!out_count || !object_scene_validate_state(objects, error, error_size)) {
        return 0;
    }
    for (uint32_t slot_index = 0u; slot_index < objects->active_slot_count; ++slot_index) {
        const uint8_t *slot = objects->slot_bytes +
            (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
        int16_t point_index = object_scene_read_be16s(slot + OBJECT_SCENE_POINT_INDEX);

        /* Draw_Objects stops at the same negative ObjT point-index sentinel. */
        if (point_index < 0) {
            break;
        }
        if (object_scene_read_be16s(slot + OBJECT_SCENE_ZONE_ID) < 0) {
            continue;
        }
        if ((uint16_t)point_index >= objects->point_count || count == UINT32_MAX) {
            object_scene_set_error(error, error_size,
                                   "live ObjT record has an invalid source point");
            return 0;
        }
        ++count;
    }
    *out_count = count;
    return 1;
}

static int object_scene_select_bitmap_assets(const GameSharedResources *resources,
                                             uint16_t asset_index, SceneSprite *sprite,
                                             char *error, size_t error_size)
{
    if (asset_index >= resources->object_count ||
        !resources->object_wads[asset_index].bytes ||
        !resources->object_ptrs[asset_index].bytes || !resources->main_palette.bytes) {
        object_scene_set_error(error, error_size,
                               "ObjT bitmap graphics index has no loaded source asset");
        return 0;
    }
    sprite->source_bytes = resources->object_wads[asset_index].bytes;
    sprite->source_byte_count = resources->object_wads[asset_index].size;
    sprite->source_aux_bytes = resources->object_ptrs[asset_index].bytes;
    sprite->source_aux_byte_count = resources->object_ptrs[asset_index].size;
    sprite->source_display_palette_bytes = resources->main_palette.bytes;
    sprite->source_display_palette_byte_count = resources->main_palette.size;
    return 1;
}

static int object_scene_apply_frame_metrics(const GameLink *game_link, uint16_t asset_index,
                                            uint16_t frame_index, SceneSprite *sprite,
                                            char *error, size_t error_size)
{
    GameObjectFrameData frame;

    if (!game_link_get_object_frame_data(game_link, asset_index, frame_index, &frame,
                                          error, error_size)) {
        return 0;
    }
    sprite->frame_metrics.pointer_table_index = frame.pointer_table_index;
    sprite->frame_metrics.down_strip = frame.down_strip;
    sprite->frame_metrics.strip_count = frame.strip_count;
    sprite->frame_metrics.line_count = frame.line_count;
    return 1;
}

static int object_scene_find_zone_index(const LevelRuntime *level, int16_t source_zone_id,
                                        uint16_t *out_zone_index, char *error,
                                        size_t error_size)
{
    if (!level || !out_zone_index || source_zone_id < 0) {
        object_scene_set_error(error, error_size, "ObjT sprite has an invalid source zone id");
        return 0;
    }
    for (uint16_t zone_index = 0u; zone_index < level->zone_count; ++zone_index) {
        LevelZone zone;

        if (!level_runtime_get_zone(level, zone_index, &zone, error, error_size)) {
            return 0;
        }
        if (zone.id == (uint16_t)source_zone_id) {
            *out_zone_index = zone_index;
            return 1;
        }
    }
    object_scene_set_error(error, error_size, "ObjT sprite source zone id is not in the level");
    return 0;
}

/*
 * objdrawhires.s:draw_CalcBrightsInZone walks the ten ZoneBorderPoints slots
 * in order and reads the two lower or upper CurrentPointBrights words for
 * each. A billboard has no source polygon face to select one directional
 * sample, so retain the same live samples as their smooth local average.
 */
static int object_scene_average_point_light(const LevelRuntime *level,
                                            const LightingRuntime *lighting,
                                            uint16_t zone_index, uint8_t upper_zone,
                                            int16_t *out_light, char *error,
                                            size_t error_size)
{
    int32_t total = 0;
    uint32_t sample_count = 0u;
    uint32_t component_offset = upper_zone != 0u ? 2u : 0u;

    if (!level || !lighting || !out_light || zone_index >= level->zone_count ||
        zone_index >= LIGHTING_RUNTIME_POINT_ZONE_CAPACITY) {
        object_scene_set_error(error, error_size,
                               "ObjT sprite point lighting is outside source tables");
        return 0;
    }
    for (uint16_t marker_index = 0u;
         marker_index < LEVEL_RUNTIME_ZONE_BORDER_POINT_COUNT; ++marker_index) {
        int16_t marker;
        uint32_t point_offset = (uint32_t)marker_index * 4u + component_offset;

        if (!level_runtime_get_zone_border_point(level, zone_index, marker_index, &marker,
                                                 error, error_size)) {
            return 0;
        }
        if (marker < 0) {
            break;
        }
        total += lighting->current_point_brightness[zone_index][point_offset];
        total += lighting->current_point_brightness[zone_index][point_offset + 1u];
        sample_count += 2u;
    }
    if (sample_count == 0u) {
        object_scene_set_error(error, error_size,
                               "ObjT sprite zone has no source brightness markers");
        return 0;
    }
    if (total / (int32_t)sample_count > INT16_MAX) {
        *out_light = INT16_MAX;
    } else if (total / (int32_t)sample_count < INT16_MIN) {
        *out_light = INT16_MIN;
    } else {
        *out_light = (int16_t)(total / (int32_t)sample_count);
    }
    return 1;
}

static int object_scene_build_sprite(const ObjectRuntime *objects, const GameLink *game_link,
                                     const GameSharedResources *resources,
                                     const LevelRuntime *level,
                                     const LightingRuntime *lighting,
                                     uint32_t slot_index, SceneSprite *out_sprite,
                                     char *error, size_t error_size)
{
    const uint8_t *slot = objects->slot_bytes +
        (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
    const uint8_t *point;
    uint16_t point_index = object_scene_read_be16(slot + OBJECT_SCENE_POINT_INDEX);
    int16_t source_zone_id = object_scene_read_be16s(slot + OBJECT_SCENE_ZONE_ID);
    int16_t graphics_type = object_scene_read_be16s(slot + OBJECT_SCENE_GRAPHICS_TYPE);
    uint16_t asset_index;
    SceneSprite sprite = {0};

    if (point_index >= objects->point_count || !level || !lighting ||
        !object_scene_find_zone_index(level, source_zone_id, &sprite.source_zone_index,
                                      error, error_size)) {
        object_scene_set_error(error, error_size,
                               "live ObjT record has an invalid source point");
        return 0;
    }
    point = objects->point_bytes + (size_t)point_index * OBJECT_RUNTIME_POINT_BYTE_COUNT;
    /* objdrawhires.s takes word-sized x/z coordinates from each Vec2L. */
    sprite.position.x = object_scene_read_be16s(point + 0u);
    sprite.position.y = (int32_t)object_scene_read_be16s(slot + OBJECT_SCENE_VERTICAL_POSITION) *
        128;
    sprite.position.z = object_scene_read_be16s(point + 4u);
    sprite.source_record_id = slot_index;
    sprite.source_brightness = object_scene_read_be16(slot + OBJECT_SCENE_BRIGHTNESS);
    sprite.yaw = object_scene_read_be16(slot + OBJECT_SCENE_CURRENT_ANGLE);
    sprite.source_aux_offset_x = object_scene_read_be16s(slot + OBJECT_SCENE_AUX_OFFSET_X);
    sprite.source_aux_offset_y = object_scene_read_be16s(slot + OBJECT_SCENE_AUX_OFFSET_Y);
    if (slot[OBJECT_SCENE_IN_UPPER_ZONE] != 0u) {
        sprite.flags |= SCENE_SPRITE_FLAG_UPPER_ZONE;
    }
    if (sprite.source_zone_index >= level->zone_count ||
        sprite.source_zone_index >= LIGHTING_RUNTIME_ZONE_BRIGHTNESS_CAPACITY) {
        object_scene_set_error(error, error_size, "ObjT sprite lighting zone is outside source tables");
        return 0;
    }
    if (!object_scene_average_point_light(
            level, lighting, sprite.source_zone_index,
            (sprite.flags & SCENE_SPRITE_FLAG_UPPER_ZONE) != 0u,
            &sprite.source_light_level, error, error_size)) {
        return 0;
    }

    /* draw_Object branches on the first byte of this source display word. */
    if (slot[OBJECT_SCENE_WIDTH_HEIGHT] == UINT8_MAX) {
        asset_index = (uint16_t)graphics_type;
        if (asset_index >= resources->vector_count ||
            !resources->vector_models[asset_index].bytes) {
            object_scene_set_error(error, error_size,
                                   "ObjT vector graphics index has no loaded source asset");
            return 0;
        }
        sprite.source = SCENE_SPRITE_SOURCE_VECTOR_MODEL;
        sprite.source_asset_id = asset_index;
        sprite.frame_index = object_scene_read_be16(slot + OBJECT_SCENE_EFFECT);
        sprite.source_bytes = resources->vector_models[asset_index].bytes;
        sprite.source_byte_count = resources->vector_models[asset_index].size;
        /* objdrawhires.s:doapoly indexes Draw_TextureMapsPtr per vector face. */
        sprite.source_palette_bytes = resources->texture_maps.bytes;
        sprite.source_palette_byte_count = resources->texture_maps.size;
        sprite.source_display_palette_bytes = resources->main_palette.bytes;
        sprite.source_display_palette_byte_count = resources->main_palette.size;
        if (slot_index == objects->player1_slot + 2u) {
            sprite.presentation = SCENE_SPRITE_PRESENTATION_PLAYER1_VIEW_WEAPON;
        }
    } else if (graphics_type < 0) {
        asset_index = (uint16_t)(0u - (uint16_t)graphics_type);
        if (!object_scene_select_bitmap_assets(resources, asset_index, &sprite,
                                               error, error_size) ||
            !resources->texture_palette.bytes ||
            !object_scene_apply_frame_metrics(game_link, asset_index,
                                              object_scene_read_be16(
                                                  slot + OBJECT_SCENE_EFFECT),
                                              &sprite, error, error_size)) {
            if (resources->texture_palette.bytes == NULL) {
                object_scene_set_error(error, error_size,
                                       "glare sprite has no loaded shared texture palette");
            }
            return 0;
        }
        sprite.source = SCENE_SPRITE_SOURCE_GLARE_BITMAP;
        sprite.source_asset_id = asset_index;
        sprite.frame_index = object_scene_read_be16(slot + OBJECT_SCENE_EFFECT);
        sprite.source_width = slot[OBJECT_SCENE_WIDTH_HEIGHT];
        sprite.source_height = slot[OBJECT_SCENE_WIDTH_HEIGHT + 1u];
        sprite.source_palette_bytes = resources->texture_palette.bytes;
        sprite.source_palette_byte_count = resources->texture_palette.size;
    } else {
        uint8_t effect = slot[OBJECT_SCENE_EFFECT];
        uint8_t effect_class = (uint8_t)(effect & 0x7fu);

        asset_index = (uint16_t)graphics_type;
        if (!object_scene_select_bitmap_assets(resources, asset_index, &sprite,
                                               error, error_size) ||
            !resources->object_palettes[asset_index].bytes ||
            !object_scene_apply_frame_metrics(game_link, asset_index,
                                              slot[OBJECT_SCENE_FRAME], &sprite,
                                              error, error_size)) {
            if (resources->object_palettes[asset_index].bytes == NULL) {
                object_scene_set_error(error, error_size,
                                       "ObjT bitmap graphics index has no loaded source palette");
            }
            return 0;
        }
        sprite.source = SCENE_SPRITE_SOURCE_OBJECT_BITMAP;
        sprite.source_asset_id = asset_index;
        sprite.frame_index = slot[OBJECT_SCENE_FRAME];
        sprite.source_width = slot[OBJECT_SCENE_WIDTH_HEIGHT];
        sprite.source_height = slot[OBJECT_SCENE_WIDTH_HEIGHT + 1u];
        sprite.source_effect = effect;
        sprite.source_palette_bytes = resources->object_palettes[asset_index].bytes;
        sprite.source_palette_byte_count = resources->object_palettes[asset_index].size;
        if ((effect & 0x80u) != 0u) {
            sprite.flags |= SCENE_SPRITE_FLAG_FLIP_HORIZONTAL;
        }
        if (effect_class >= OBJECT_SCENE_BITMAP_LIGHT_FIRST) {
            sprite.flags |= effect_class <
                    OBJECT_SCENE_BITMAP_LIGHT_FIRST + OBJECT_SCENE_BITMAP_LIGHT_COUNT ?
                SCENE_SPRITE_FLAG_LIGHT_PALETTE : SCENE_SPRITE_FLAG_ADDITIVE;
        }
    }
    *out_sprite = sprite;
    return 1;
}

int object_scene_submit_active(const ObjectRuntime *objects, const GameLink *game_link,
                               const GameSharedResources *resources,
                               const LevelRuntime *level,
                               const LightingRuntime *lighting,
                               const GamePreferences *preferences, SceneFrame *frame,
                               char *error, size_t error_size)
{
    if (!game_link || !resources || !level || !lighting || !preferences || !frame ||
        !object_scene_validate_state(objects, error, error_size)) {
        object_scene_set_error(error, error_size,
                               "Draw_Objects scene handoff received invalid state");
        return 0;
    }
    for (uint32_t slot_index = 0u; slot_index < objects->active_slot_count; ++slot_index) {
        const uint8_t *slot = objects->slot_bytes +
            (size_t)slot_index * OBJECT_RUNTIME_SLOT_BYTE_COUNT;
        SceneCommand command;

        if (object_scene_read_be16s(slot + OBJECT_SCENE_POINT_INDEX) < 0) {
            break;
        }
        if (object_scene_read_be16s(slot + OBJECT_SCENE_ZONE_ID) < 0 ||
            (preferences->show_weapon != 0u &&
             slot_index == objects->player1_slot + 2u)) {
            continue;
        }
        command.type = SCENE_COMMAND_SPRITE;
        if (!object_scene_build_sprite(objects, game_link, resources, level, lighting, slot_index,
                                       &command.data.sprite, error, error_size) ||
            !scene_frame_submit(frame, &command)) {
            return 0;
        }
    }
    return 1;
}
