#include "object_handler.h"

#include <stdio.h>

#include "object_activatables.h"
#include "object_collectables.h"
#include "object_passives.h"
#include "object_projectiles.h"

enum {
    /* defs.i:ObjT/EntT offsets and ObjectHandler type/behaviour branches. */
    OBJECT_SLOT_POINT_INDEX = 0u,
    OBJECT_SLOT_VERTICAL_POSITION = 4u,
    OBJECT_SLOT_GRAPHICS_WORD = 6u,
    OBJECT_SLOT_GRAPHICS_LONG = 8u,
    OBJECT_SLOT_ZONE_ID = 12u,
    OBJECT_SLOT_TYPE_ID = 16u,
    OBJECT_SLOT_ENTITY_HIT_POINTS = 18u,
    OBJECT_SLOT_ENTITY_DAMAGE_TAKEN = 19u,
    OBJECT_SLOT_ENTITY_ZONE_ID = 26u,
    OBJECT_SLOT_DOORS_AND_LIFTS_HELD = 50u,
    OBJECT_SLOT_ENTITY_TYPE = 54u,
    OBJECT_SLOT_WHICH_ANIMATION = 55u,
    OBJECT_SLOT_CURRENT_ANGLE = 30u,
    OBJECT_SLOT_TIMER1 = 34u,
    OBJECT_SLOT_WORRY = 62u,
    OBJECT_TYPE_OBJECT = 1u,
    OBJECT_TYPE_PROJECTILE = 2u,
    OBJECT_BEHAVIOUR_COLLECTABLE = 0u,
    OBJECT_BEHAVIOUR_ACTIVATABLE = 1u,
    OBJECT_BEHAVIOUR_DESTRUCTIBLE = 2u,
    OBJECT_BEHAVIOUR_DECORATION = 3u
};

static void object_handler_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t object_handler_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static uint32_t object_handler_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

static void object_handler_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void object_handler_or_held_mechanism_locks(MechanismRuntime *runtime,
                                                   const uint8_t *slot)
{
    uint32_t held_locks = object_handler_read_be32(
        slot + OBJECT_SLOT_DOORS_AND_LIFTS_HELD);

    /*
     * bss/anim_bss.s deliberately aliases one longword: the high word is
     * Anim_DoorAndLiftLocks_l and the independently addressed low word is
     * anim_LiftOnlyLocks_w. newanims.s ORs the complete EntT long into it.
     */
    runtime->door_and_lift_locks |= (uint16_t)(held_locks >> 16u);
    runtime->lift_only_locks |= (uint16_t)held_locks;
}

/*
 * newaliencontrol.s:ACTANIMOBJ, reached through Collectable:GUNHELD for
 * Player 1's ENT_NEXT_2 companion.  The display descriptor belongs to the
 * live object slot: rendering must not guess a separate PC weapon state.
 */
static int object_handler_apply_active_object_animation(
    const GameLink *game_link, const GameObjectDefinition *definition, uint8_t *slot,
    char *error, size_t error_size)
{
    GameObjectAnimationFrame frame;
    uint16_t frame_index = object_handler_read_be16(slot + OBJECT_SLOT_TIMER1);
    int16_t vertical_adjustment;

    if (!game_link_get_object_animation_frame(
            game_link, GAME_LINK_OBJECT_ANIMATION_ACTION,
            slot[OBJECT_SLOT_ENTITY_TYPE], frame_index, &frame, error, error_size)) {
        return 0;
    }
    object_handler_write_be16(slot + OBJECT_SLOT_GRAPHICS_LONG, 0u);
    object_handler_write_be16(slot + OBJECT_SLOT_GRAPHICS_LONG + 2u, 0u);
    if (definition->graphics_type == 1u) {
        slot[OBJECT_SLOT_GRAPHICS_LONG + 1u] = frame.byte_0;
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_handler_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, UINT16_MAX);
        object_handler_write_be16(
            slot + OBJECT_SLOT_CURRENT_ANGLE,
            (uint16_t)((uint32_t)object_handler_read_be16(
                slot + OBJECT_SLOT_CURRENT_ANGLE) + frame.word_2));
    } else if (definition->graphics_type > 1u) {
        object_handler_write_be16(
            slot + OBJECT_SLOT_GRAPHICS_LONG,
            (uint16_t)(int16_t)-(int16_t)(int8_t)frame.byte_0);
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_handler_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, frame.word_2);
    } else {
        slot[OBJECT_SLOT_GRAPHICS_LONG + 1u] = frame.byte_0;
        slot[OBJECT_SLOT_GRAPHICS_LONG + 3u] = frame.byte_1;
        object_handler_write_be16(slot + OBJECT_SLOT_GRAPHICS_WORD, frame.word_2);
    }
    vertical_adjustment = (int16_t)((int16_t)frame.signed_byte_4 * 2);
    object_handler_write_be16(
        slot + OBJECT_SLOT_VERTICAL_POSITION,
        (uint16_t)((uint32_t)object_handler_read_be16(
            slot + OBJECT_SLOT_VERTICAL_POSITION) + (uint16_t)vertical_adjustment));
    object_handler_write_be16(slot + OBJECT_SLOT_TIMER1, frame.next_timer1);
    return 1;
}

static int object_handler_copy_alien_auxiliary(ObjectRuntime *objects, uint32_t slot_index,
                                               uint8_t *slot, char *error, size_t error_size)
{
    uint8_t *previous_slot;

    /* newanims.s:ObjectHandler accesses ObjT_ZoneID_w+ENT_PREV after ItsAnAlien. */
    if (slot_index == 0u ||
        !object_runtime_get_slot_bytes(objects, slot_index - 1u, &previous_slot)) {
        object_handler_set_error(error, error_size,
                                 "ItsAnAlien preceding source AUX slot is unavailable");
        return 0;
    }
    if ((int16_t)object_handler_read_be16(previous_slot + OBJECT_SLOT_ZONE_ID) >= 0) {
        object_handler_write_be16(previous_slot + OBJECT_SLOT_ZONE_ID,
                                  object_handler_read_be16(slot + OBJECT_SLOT_ZONE_ID));
        object_handler_write_be16(previous_slot + OBJECT_SLOT_ENTITY_ZONE_ID,
                                  object_handler_read_be16(slot + OBJECT_SLOT_ENTITY_ZONE_ID));
    }
    return 1;
}

static int object_handler_object_holds_locks(const uint8_t *slot,
                                             const GameObjectDefinition *definition)
{
    if (definition->behaviour == OBJECT_BEHAVIOUR_COLLECTABLE ||
        definition->behaviour == OBJECT_BEHAVIOUR_ACTIVATABLE) {
        /* newaliencontrol.s:Collectable/Activatable bypass this in GUNHELD/ACTIVATED. */
        return slot[OBJECT_SLOT_WHICH_ANIMATION] == 0u;
    }
    if (definition->behaviour == OBJECT_BEHAVIOUR_DESTRUCTIBLE) {
        /* newaliencontrol.s:Destructable reaches StillHere only below the hit threshold. */
        return (uint16_t)slot[OBJECT_SLOT_ENTITY_DAMAGE_TAKEN] < definition->hit_points;
    }
    return 0;
}

int object_handler_apply_active_object_animation_slot(
    ObjectRuntime *objects, uint32_t slot_index, const GameLink *game_link,
    char *error, size_t error_size)
{
    uint8_t *slot;
    GameObjectDefinition definition;

    if (!objects || !game_link || slot_index >= objects->active_slot_count ||
        objects->active_slot_count > objects->slot_count ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot) ||
        slot[OBJECT_SLOT_TYPE_ID] != OBJECT_TYPE_OBJECT ||
        slot[OBJECT_SLOT_WHICH_ANIMATION] == 0u) {
        object_handler_set_error(error, error_size,
                                 "ACTANIMOBJ received an invalid live object slot");
        return 0;
    }
    if (!game_link_get_object_definition(game_link, slot[OBJECT_SLOT_ENTITY_TYPE],
                                         &definition, error, error_size) ||
        definition.behaviour != OBJECT_BEHAVIOUR_COLLECTABLE) {
        object_handler_set_error(error, error_size,
                                 "ACTANIMOBJ live object is not a source collectable");
        return 0;
    }
    return object_handler_apply_active_object_animation(game_link, &definition, slot,
                                                        error, error_size);
}

int object_handler_update_single_player(
    ObjectRuntime *objects, LevelDynamicState *dynamic_level,
    MechanismRuntime *mechanism_runtime, AlienRuntime *alien_runtime,
    const GameLink *game_link, const ObjectHandlerAlienContext *alien_context,
    const PlayerRuntime *player, GameInventory *inventory,
    const GameInventoryConsumableLimits *limits, uint16_t frame_ticks,
    uint32_t *out_collected_count, char *error, size_t error_size)
{
    const LevelRuntime *level;
    ObjectProjectileSourceRuntime projectile_source_runtime;
    uint32_t collected_count = 0u;

    if (!objects || !dynamic_level || !mechanism_runtime || !alien_runtime || !game_link ||
        !alien_context || !alien_context->animation_runtime || !alien_context->lighting_runtime ||
        !alien_context->navigation || !alien_context->clips || !alien_context->progression ||
        !alien_context->explosion_runtime || !alien_context->math || !alien_context->random ||
        !alien_context->observation || !alien_context->dispatch_workspace ||
        !alien_context->messages || !alien_context->preferences || !player || !inventory || !limits ||
        objects->active_slot_count > objects->slot_count ||
        player->zone_index >= dynamic_level->runtime.zone_count) {
        object_handler_set_error(error, error_size,
                                 "ObjectHandler received invalid single-player state");
        return 0;
    }
    level = &dynamic_level->runtime;
    projectile_source_runtime.blast_runtime = &alien_runtime->blast;
    projectile_source_runtime.motion_runtime = &alien_runtime->motion;
    projectile_source_runtime.visibility_runtime = &alien_runtime->visibility;
    projectile_source_runtime.clips = alien_context->clips;
    projectile_source_runtime.random = alien_context->random;
    projectile_source_runtime.audio_events = alien_context->audio_events;
    for (uint32_t slot_index = 0u; slot_index < objects->active_slot_count; ++slot_index) {
        uint8_t *slot;
        GameObjectDefinition definition;
        uint32_t newly_collected = 0u;

        if (!object_runtime_get_slot_bytes(objects, slot_index, &slot)) {
            object_handler_set_error(error, error_size,
                                     "ObjectHandler slot is outside the owned source list");
            return 0;
        }
        /* ObjectHandler itself still honors the source -1 ObjT list sentinel. */
        if ((int16_t)object_handler_read_be16(slot + OBJECT_SLOT_POINT_INDEX) < 0) {
            break;
        }
        object_handler_write_be16(slot + OBJECT_SLOT_ENTITY_ZONE_ID,
                                  object_handler_read_be16(slot + OBJECT_SLOT_ZONE_ID));
        /* ObjectHandler's shared negative ObjT_ZoneID_w gate precedes every class jump. */
        if ((int16_t)object_handler_read_be16(slot + OBJECT_SLOT_ZONE_ID) < 0) {
            continue;
        }
        if ((int8_t)slot[OBJECT_SLOT_TYPE_ID] < (int8_t)OBJECT_TYPE_OBJECT) {
            /* newanims.s:ObjectHandler:JUMPALIEN's lock preamble. */
            if (slot[OBJECT_SLOT_ENTITY_HIT_POINTS] != 0u) {
                object_handler_or_held_mechanism_locks(mechanism_runtime, slot);
            }
            if (slot[OBJECT_SLOT_WORRY] != 0u) {
                if (alien_runtime->no_enemies == 0u) {
                    /* newaliencontrol.s:ItsAnAlien:.no_enemies. */
                    object_handler_write_be16(slot + OBJECT_SLOT_ZONE_ID, UINT16_MAX);
                } else {
                    AlienSetup setup;
                    AlienDispatchState dispatch;

                    if (!alien_setup_from_slot(objects, slot_index, &dynamic_level->runtime,
                                               game_link, &setup, error, error_size) ||
                        !alien_dispatch_update(
                            objects, slot_index, alien_runtime,
                            alien_context->animation_runtime, alien_context->lighting_runtime,
                            dynamic_level, alien_context->navigation, alien_context->clips,
                            game_link, alien_context->progression,
                            alien_context->explosion_runtime, alien_context->math,
                            alien_context->random, player, &setup, alien_context->observation,
                            alien_context->audio_events, frame_ticks,
                            alien_context->dispatch_workspace, &dispatch,
                            error, error_size) ||
                        (dispatch.narrative.bytes &&
                         !message_runtime_push_line(
                             alien_context->messages, dispatch.narrative.bytes,
                             dispatch.narrative.length_and_tag,
                             alien_context->preferences->show_messages,
                             error, error_size))) {
                        return 0;
                    }
                }
                if (!object_handler_copy_alien_auxiliary(objects, slot_index, slot,
                                                         error, error_size)) {
                    return 0;
                }
            }
            continue;
        }
        if (slot[OBJECT_SLOT_TYPE_ID] == OBJECT_TYPE_PROJECTILE) {
            uint8_t popping = slot[30u];

            if ((popping != 0u &&
                 !object_projectiles_update_impact_slot(objects, slot_index, dynamic_level,
                                                        alien_context->lighting_runtime, game_link,
                                                        error, error_size)) ||
                (popping == 0u &&
                 !object_projectiles_update_flight_animation_slot_with_source_state(
                     objects, slot_index, dynamic_level, alien_context->lighting_runtime,
                     &projectile_source_runtime,
                     game_link, frame_ticks,
                     error, error_size))) {
                return 0;
            }
            continue;
        }
        if (slot[OBJECT_SLOT_TYPE_ID] != OBJECT_TYPE_OBJECT) {
            continue;
        }
        if (!game_link_get_object_definition(game_link, slot[OBJECT_SLOT_ENTITY_TYPE],
                                             &definition, error, error_size)) {
            return 0;
        }
        if (definition.behaviour == OBJECT_BEHAVIOUR_COLLECTABLE &&
            slot[OBJECT_SLOT_WHICH_ANIMATION] != 0u) {
            /* newaliencontrol.s:Collectable:GUNHELD -> ACTANIMOBJ -> return. */
            if (!object_handler_apply_active_object_animation_slot(
                    objects, slot_index, game_link, error, error_size)) {
                return 0;
            }
            continue;
        }
        /* newaliencontrol.s:Collectable/Activatable/StillHere's AI_NoEnemies lock branches. */
        if (alien_runtime->no_enemies != 0u &&
            object_handler_object_holds_locks(slot, &definition)) {
            object_handler_or_held_mechanism_locks(mechanism_runtime, slot);
        }
        if (definition.behaviour == OBJECT_BEHAVIOUR_COLLECTABLE) {
            if (!object_collectables_update_slot_single_player_with_audio(
                    objects, slot_index, level, game_link, player, inventory, limits,
                    alien_context->messages, alien_context->preferences->show_messages,
                    alien_context->message_time_milliseconds, alien_context->audio_events,
                    &newly_collected, error, error_size)) {
                return 0;
            }
            if (UINT32_MAX - collected_count < newly_collected) {
                object_handler_set_error(error, error_size,
                                         "ObjectHandler collected-count overflow");
                return 0;
            }
            collected_count += newly_collected;
        } else if (definition.behaviour == OBJECT_BEHAVIOUR_ACTIVATABLE &&
                   !object_activatables_update_slot_single_player(
                       objects, slot_index, level, game_link, player, inventory, limits,
                       frame_ticks, alien_context->messages,
                       alien_context->preferences->show_messages,
                       alien_context->message_time_milliseconds,
                       alien_context->audio_events, error, error_size)) {
            return 0;
        } else if ((definition.behaviour == OBJECT_BEHAVIOUR_DESTRUCTIBLE ||
                    definition.behaviour == OBJECT_BEHAVIOUR_DECORATION) &&
                   !object_passives_update_slot(objects, slot_index, alien_runtime,
                                                level, game_link, alien_context->clips,
                                                player, &definition, alien_context->messages,
                                                alien_context->preferences->show_messages,
                                                error, error_size)) {
            return 0;
        }
    }
    if (out_collected_count) {
        *out_collected_count = collected_count;
    }
    return 1;
}
