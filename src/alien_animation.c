#include "alien_animation.h"

#include <stdio.h>
#include <string.h>

#include "object_viewpoint.h"

enum {
    /* defs.i:ObjT/EntT/ShotT offsets used by modules/ai.s:ai_DoWalkAnim. */
    ALIEN_ANIMATION_SLOT_VERTICAL_POSITION = 4u,
    ALIEN_ANIMATION_SLOT_DISPLAY_SIZE = 6u,
    ALIEN_ANIMATION_SLOT_DISPLAY_GRAPHICS = 8u,
    ALIEN_ANIMATION_SLOT_DISPLAY_EFFECT = 10u,
    ALIEN_ANIMATION_SLOT_DISPLAY_FRAME = 11u,
    ALIEN_ANIMATION_SLOT_ZONE_ID = 12u,
    ALIEN_ANIMATION_SLOT_ENTITY_ZONE_ID = 26u,
    ALIEN_ANIMATION_SLOT_CURRENT_ANGLE = 30u,
    ALIEN_ANIMATION_SLOT_TIMER2 = 40u,
    ALIEN_ANIMATION_SLOT_AUX_OFFSET_X = 44u,
    ALIEN_ANIMATION_SLOT_AUX_OFFSET_Y = 46u,
    ALIEN_ANIMATION_SLOT_IN_UPPER_ZONE = 63u,
    /* bss/tables_bss.s:ObjectWorkspace_vl bytes. */
    ALIEN_ANIMATION_WORKSPACE_ACTION = 0u,
    ALIEN_ANIMATION_WORKSPACE_SPECIAL_FRAME = 1u,
    ALIEN_ANIMATION_WORKSPACE_OPTION = 2u,
    ALIEN_ANIMATION_WORKSPACE_FINISHED = 3u,
    ALIEN_ANIMATION_OPTION_COUNT = GAME_LINK_ALIEN_ANIMATION_OPTION_COUNT
};

static void alien_animation_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t alien_animation_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void alien_animation_write_be16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void alien_animation_write_be32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static int alien_animation_prepare_auxiliary_frame(
    const GameLink *game_link, const AlienSetup *setup,
    const GameAlienAnimationFrame *alien_frame,
    GameObjectDefinition *out_definition, GameObjectAnimationFrame *out_frame,
    char *error, size_t error_size)
{
    uint16_t auxiliary_type;
    uint16_t frame_index;

    if (setup->auxiliary_object_type < 0 || (int8_t)alien_frame->bytes[8u] < 0) {
        return 0;
    }
    auxiliary_type = (uint16_t)setup->auxiliary_object_type;
    frame_index = alien_frame->bytes[8u];
    if (!game_link_get_object_definition(game_link, auxiliary_type, out_definition,
                                         error, error_size) ||
        !game_link_get_object_animation_frame(
            game_link, GAME_LINK_OBJECT_ANIMATION_DEFAULT, auxiliary_type, frame_index,
            out_frame, error, error_size)) {
        return -1;
    }
    return 1;
}

static void alien_animation_apply_auxiliary_descriptor(
    uint8_t *auxiliary_slot, const GameObjectDefinition *definition,
    const GameObjectAnimationFrame *frame)
{
    int16_t graphics_type = (int16_t)definition->graphics_type;

    /* ai_DoWalkAnim clears +8 before every source auxiliary descriptor path. */
    alien_animation_write_be32(auxiliary_slot + ALIEN_ANIMATION_SLOT_DISPLAY_GRAPHICS, 0u);
    if (graphics_type < 1) {
        auxiliary_slot[ALIEN_ANIMATION_SLOT_DISPLAY_GRAPHICS + 1u] = frame->byte_0;
        auxiliary_slot[ALIEN_ANIMATION_SLOT_DISPLAY_FRAME] = frame->byte_1;
        alien_animation_write_be16(auxiliary_slot + ALIEN_ANIMATION_SLOT_DISPLAY_SIZE,
                                   frame->word_2);
    } else if (graphics_type == 1) {
        auxiliary_slot[ALIEN_ANIMATION_SLOT_DISPLAY_GRAPHICS + 1u] = frame->byte_0;
        auxiliary_slot[ALIEN_ANIMATION_SLOT_DISPLAY_FRAME] = frame->byte_1;
        alien_animation_write_be16(auxiliary_slot + ALIEN_ANIMATION_SLOT_DISPLAY_SIZE,
                                   UINT16_MAX);
    } else {
        alien_animation_write_be16(
            auxiliary_slot + ALIEN_ANIMATION_SLOT_DISPLAY_GRAPHICS,
            (uint16_t)(int16_t)-(int16_t)(int8_t)frame->byte_0);
        auxiliary_slot[ALIEN_ANIMATION_SLOT_DISPLAY_FRAME] = frame->byte_1;
        alien_animation_write_be16(auxiliary_slot + ALIEN_ANIMATION_SLOT_DISPLAY_SIZE,
                                   frame->word_2);
    }
}

/*
 * ai_DoWalkAnim begins with ItsAnAlien's GLFT_AlienShootDefs_l a2 base. Its
 * auxiliary descriptor path alone replaces a2 with GLFT_ObjectDefs before
 * returning. Obj_DoCollision later reads 2(a2, type * 8) and 4(a2, type * 8),
 * so retain the exact raw leading words instead of assigning collision sizes.
 */
static int alien_animation_capture_collision_a2(const GameLink *game_link,
                                                int has_auxiliary_frame,
                                                AlienAnimationState *state,
                                                char *error, size_t error_size)
{
    if (has_auxiliary_frame != 0) {
        GameObjectDefinition definition;

        if (!game_link_get_object_definition(game_link, 0u, &definition, error, error_size)) {
            return 0;
        }
        state->collision_a2_words[0u] = (int16_t)definition.behaviour;
        state->collision_a2_words[1u] = (int16_t)definition.graphics_type;
        state->collision_a2_words[2u] = definition.active_timeout;
        state->collision_a2_words[3u] = (int16_t)definition.hit_points;
        state->collision_a2_words[4u] = (int16_t)definition.explosive_force;
        state->collision_a2_words[5u] = (int16_t)definition.impassible;
        state->collision_a2_words[6u] = (int16_t)definition.default_animation_length;
        state->collision_a2_words[7u] = (int16_t)definition.collision_radius;
    } else {
        GameShootDefinition first_definition;
        GameShootDefinition second_definition;

        if (!game_link_get_alien_shoot_definition(game_link, 0u, &first_definition,
                                                  error, error_size) ||
            !game_link_get_alien_shoot_definition(game_link, 1u, &second_definition,
                                                  error, error_size)) {
            return 0;
        }
        state->collision_a2_words[0u] = (int16_t)first_definition.bullet_type;
        state->collision_a2_words[1u] = (int16_t)first_definition.delay;
        state->collision_a2_words[2u] = (int16_t)first_definition.bullet_count;
        state->collision_a2_words[3u] = (int16_t)first_definition.sound_effect;
        state->collision_a2_words[4u] = (int16_t)second_definition.bullet_type;
        state->collision_a2_words[5u] = (int16_t)second_definition.delay;
        state->collision_a2_words[6u] = (int16_t)second_definition.bullet_count;
        state->collision_a2_words[7u] = (int16_t)second_definition.sound_effect;
    }
    return 1;
}

int alien_animation_update_walk_or_attack(
    ObjectRuntime *objects, uint32_t slot_index,
    ObjectAnimationRuntime *animation_runtime, const GameLink *game_link,
    const GameMath *math, const AlienSetup *setup, uint16_t viewer_yaw,
    AlienAnimationState *out_state, char *error, size_t error_size)
{
    uint8_t *slot;
    uint8_t *auxiliary_slot;
    uint8_t *workspace;
    uint16_t frame_index;
    uint8_t animation_option;
    uint8_t viewpoint_frame;
    GameAlienAnimationFrame alien_frame;
    GameObjectDefinition auxiliary_definition;
    GameObjectAnimationFrame auxiliary_frame;
    int has_auxiliary_frame;
    int16_t display_frame;
    AlienAnimationState state;

    if (!objects || !objects->slot_bytes || !animation_runtime || !game_link || !math ||
        !setup || !out_state || slot_index == 0u ||
        slot_index >= objects->active_slot_count ||
        objects->active_slot_count > objects->slot_count ||
        objects->active_slot_count > OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT ||
        !object_runtime_get_slot_bytes(objects, slot_index, &slot) ||
        !object_runtime_get_slot_bytes(objects, slot_index - 1u, &auxiliary_slot)) {
        alien_animation_set_error(error, error_size,
                                  "ai_DoWalkAnim received invalid source animation state");
        return 0;
    }

    workspace = animation_runtime->workspace[slot_index];
    animation_option = workspace[ALIEN_ANIMATION_WORKSPACE_OPTION];
    if (animation_option == 0u && setup->vector_object_flag != 1u) {
        if (!object_viewpoint_select_frame(
                math, alien_animation_read_be16(slot + ALIEN_ANIMATION_SLOT_CURRENT_ANGLE),
                viewer_yaw, &viewpoint_frame, error, error_size)) {
            return 0;
        }
        animation_option = (uint8_t)(viewpoint_frame * 2u);
    }
    if (animation_option >= ALIEN_ANIMATION_OPTION_COUNT) {
        alien_animation_set_error(error, error_size,
                                  "ai_DoWalkAnim animation option is outside GLFT bounds");
        return 0;
    }

    frame_index = alien_animation_read_be16(slot + ALIEN_ANIMATION_SLOT_TIMER2);
    if ((int8_t)workspace[ALIEN_ANIMATION_WORKSPACE_SPECIAL_FRAME] >= 0) {
        frame_index = workspace[ALIEN_ANIMATION_WORKSPACE_SPECIAL_FRAME];
    }
    if (!game_link_get_alien_animation_frame(game_link, setup->alien_type, animation_option,
                                              frame_index, &alien_frame,
                                              error, error_size)) {
        return 0;
    }
    has_auxiliary_frame = alien_animation_prepare_auxiliary_frame(
        game_link, setup, &alien_frame, &auxiliary_definition, &auxiliary_frame,
        error, error_size);
    if (has_auxiliary_frame < 0) {
        return 0;
    }

    memset(&state, 0, sizeof(state));
    /* modules/ai.s's ai_DoAction_b, ai_FinishedAnim_b, and ai_AnimFacing_w. */
    state.action = workspace[ALIEN_ANIMATION_WORKSPACE_ACTION];
    workspace[ALIEN_ANIMATION_WORKSPACE_ACTION] = 0u;
    state.finished = workspace[ALIEN_ANIMATION_WORKSPACE_FINISHED];
    workspace[ALIEN_ANIMATION_WORKSPACE_FINISHED] = 0u;
    state.facing = 0u;
    if (setup->vector_object_flag == 1u) {
        state.facing = alien_animation_read_be16(alien_frame.bytes + 2u);
    }
    workspace[ALIEN_ANIMATION_WORKSPACE_SPECIAL_FRAME] = UINT8_MAX;
    if (!alien_animation_capture_collision_a2(game_link, has_auxiliary_frame, &state,
                                              error, error_size)) {
        return 0;
    }

    alien_animation_write_be32(slot + ALIEN_ANIMATION_SLOT_DISPLAY_GRAPHICS, 0u);
    slot[ALIEN_ANIMATION_SLOT_DISPLAY_GRAPHICS + 1u] = alien_frame.bytes[0u];
    display_frame = (int8_t)alien_frame.bytes[1u];
    if (display_frame <= 0) {
        slot[ALIEN_ANIMATION_SLOT_DISPLAY_EFFECT] = 128u;
        display_frame = (int16_t)-display_frame;
    }
    slot[ALIEN_ANIMATION_SLOT_DISPLAY_FRAME] = (uint8_t)(display_frame - 1);

    /* macros.i:FREE_ENT_2 a0,ENT_PREV. */
    alien_animation_write_be16(auxiliary_slot + ALIEN_ANIMATION_SLOT_ZONE_ID, UINT16_MAX);
    alien_animation_write_be16(auxiliary_slot + ALIEN_ANIMATION_SLOT_ENTITY_ZONE_ID, UINT16_MAX);
    if (has_auxiliary_frame != 0) {
        int16_t offset_x = (int16_t)(int8_t)alien_frame.bytes[9u];
        int16_t offset_y = (int16_t)(int8_t)alien_frame.bytes[10u];

        alien_animation_write_be16(
            auxiliary_slot + ALIEN_ANIMATION_SLOT_ZONE_ID,
            alien_animation_read_be16(slot + ALIEN_ANIMATION_SLOT_ZONE_ID));
        alien_animation_write_be16(
            auxiliary_slot + ALIEN_ANIMATION_SLOT_ENTITY_ZONE_ID,
            alien_animation_read_be16(slot + ALIEN_ANIMATION_SLOT_ZONE_ID));
        alien_animation_write_be16(
            auxiliary_slot + ALIEN_ANIMATION_SLOT_VERTICAL_POSITION,
            alien_animation_read_be16(slot + ALIEN_ANIMATION_SLOT_VERTICAL_POSITION));
        auxiliary_slot[ALIEN_ANIMATION_SLOT_IN_UPPER_ZONE] =
            slot[ALIEN_ANIMATION_SLOT_IN_UPPER_ZONE];
        alien_animation_write_be16(auxiliary_slot + ALIEN_ANIMATION_SLOT_AUX_OFFSET_X,
                                   (uint16_t)(offset_x * 2));
        alien_animation_write_be16(auxiliary_slot + ALIEN_ANIMATION_SLOT_AUX_OFFSET_Y,
                                   (uint16_t)(offset_y * 2));
        alien_animation_apply_auxiliary_descriptor(auxiliary_slot, &auxiliary_definition,
                                                   &auxiliary_frame);
    }

    alien_animation_write_be16(slot + ALIEN_ANIMATION_SLOT_DISPLAY_SIZE, UINT16_MAX);
    if (setup->vector_object_flag != 1u) {
        alien_animation_write_be16(slot + ALIEN_ANIMATION_SLOT_DISPLAY_SIZE,
                                   alien_animation_read_be16(alien_frame.bytes + 2u));
        if ((int8_t)setup->vector_object_flag > 1) {
            slot[ALIEN_ANIMATION_SLOT_DISPLAY_EFFECT] |= setup->vector_object_flag;
        }
    }
    *out_state = state;
    return 1;
}
