#include "alien_setup.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    /* defs.i: ObjT/EntT fields used by newaliencontrol.s:ItsAnAlien. */
    ALIEN_SETUP_SLOT_POINT_INDEX = 0u,
    ALIEN_SETUP_SLOT_ZONE_ID = 12u,
    ALIEN_SETUP_SLOT_ENTITY_TYPE = 54u
};

static void alien_setup_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_setup_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static int16_t alien_setup_word_from_u16(uint16_t value)
{
    if (value <= INT16_MAX) {
        return (int16_t)value;
    }
    return (int16_t)((int32_t)value - 65536);
}

static int16_t alien_setup_negate_shift_left_2(int16_t value)
{
    uint16_t word = (uint16_t)(0u - (uint16_t)value);

    word = (uint16_t)(word << 2u);
    return alien_setup_word_from_u16(word);
}

int alien_setup_from_slot(const ObjectRuntime *objects, uint32_t slot_index,
                          const LevelRuntime *level, const GameLink *game_link,
                          AlienSetup *out_setup, char *error, size_t error_size)
{
    uint8_t *slot;
    int16_t zone_index;
    LevelZone zone;
    uint16_t alien_type;
    GameAlienDefinition definition;
    int16_t brightness;
    AlienSetup setup;

    if (!objects || !level || !game_link || !out_setup ||
        slot_index >= objects->active_slot_count ||
        !object_runtime_get_slot_bytes((ObjectRuntime *)objects, slot_index, &slot)) {
        alien_setup_set_error(error, error_size, "ItsAnAlien received invalid source state");
        return 0;
    }
    zone_index = alien_setup_word_from_u16(alien_setup_read_be16(
        slot + ALIEN_SETUP_SLOT_ZONE_ID));
    if (zone_index < 0 || (uint16_t)zone_index >= level->zone_count ||
        !level_runtime_get_zone(level, (uint16_t)zone_index, &zone, error, error_size)) {
        alien_setup_set_error(error, error_size, "ItsAnAlien object zone is outside the source level");
        return 0;
    }
    alien_type = slot[ALIEN_SETUP_SLOT_ENTITY_TYPE];
    if (alien_type >= GAME_LINK_ALIEN_COUNT ||
        !game_link_get_alien_definition(game_link, alien_type, &definition, error, error_size) ||
        !game_link_get_alien_brightness(game_link, alien_type, &brightness,
                                        error, error_size)) {
        alien_setup_set_error(error, error_size, "ItsAnAlien type is outside the GLFT alien catalog");
        return 0;
    }
    if (definition.girth > 2u) {
        alien_setup_set_error(error, error_size,
                              "ItsAnAlien girth is outside diststowall's source table");
        return 0;
    }
    memset(&setup, 0, sizeof(setup));
    setup.alien_type = alien_type;
    setup.zone_id = zone.id;
    setup.object_point_index = alien_setup_read_be16(slot + ALIEN_SETUP_SLOT_POINT_INDEX);
    setup.zone_echo = zone.echo;
    setup.brightness = alien_setup_word_from_u16((uint16_t)(0u - (uint16_t)brightness));
    if (!game_link_get_alien_shoot_definition(game_link, alien_type,
                                              &setup.shoot_definition,
                                              error, error_size)) {
        return 0;
    }
    setup.shot_y_offset = (int32_t)((uint32_t)setup.shoot_definition.bullet_type << 23u |
                                    (uint32_t)setup.shoot_definition.delay << 7u);
    setup.shot_offset_multiplier = alien_setup_negate_shift_left_2(
        alien_setup_word_from_u16(setup.shoot_definition.sound_effect));
    setup.thing_height = (int32_t)alien_setup_word_from_u16(definition.height) * 128;
    setup.auxiliary_object_type = alien_setup_word_from_u16(definition.auxiliary_type);
    setup.vector_object_flag = (uint8_t)definition.graphics_type;
    setup.default_mode = alien_setup_word_from_u16(definition.default_behaviour);
    setup.response_mode = alien_setup_word_from_u16(definition.response_behaviour);
    setup.retreat_mode = alien_setup_word_from_u16(definition.retreat_behaviour);
    setup.followup_mode = alien_setup_word_from_u16(definition.followup_behaviour);
    setup.prowl_speed = alien_setup_word_from_u16(definition.default_speed);
    setup.response_speed = alien_setup_word_from_u16(definition.response_speed);
    setup.retreat_speed = alien_setup_word_from_u16(definition.retreat_speed);
    setup.followup_speed = alien_setup_word_from_u16(definition.followup_speed);
    setup.followup_timer = alien_setup_word_from_u16(definition.followup_timeout);
    setup.away_from_wall = (int8_t)definition.girth;
    setup.extended_wall_length = (int16_t)(40u << definition.girth);
    *out_setup = setup;
    return 1;
}
