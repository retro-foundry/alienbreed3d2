#include "player_runtime.h"

#include "object_collision.h"
#include "object_movement.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    /* hires.s:55, consumed by modules/player.s:Plr_Initialise. */
    PLAYER_STANDING_HEIGHT = 12 * 1024,
    /* hires.s:56, consumed by modules/player.s:plr_KeyboardControl. */
    PLAYER_CROUCH_HEIGHT = 8 * 1024,
    PLAYER_STANDING_CLEARANCE = PLAYER_STANDING_HEIGHT + 3 * 1024,
    /* hires.s:Plr1_Control -> objectmove.s:MoveObject. */
    PLAYER_EDGE_EXTENSION = 40,
    PLAYER_STEP_UP = 40 * 256,
    PLAYER_SMALL_STEP_UP = 10 * 256,
    PLAYER_STEP_DOWN = 0x1000000,
    PLAYER_CEILING_CLEARANCE = 10 * 256,
    /* modules/player.s:plr_Fall. */
    PLAYER_FALL_ACCELERATION = 64,
    PLAYER_FALL_NEAR_GROUND_DISTANCE = 16 * 64,
    PLAYER_FALL_WATER_TERMINAL_VELOCITY = 512,
    PLAYER_JUMP_SPEED_DRY = -1024,
    PLAYER_JUMP_SPEED_WATER = -512,
    PLAYER_MAX_ZONE_TRANSITIONS = 50,
    /* hires.s:SMALL_HEIGHT and its non-fullscreen View_* setup. */
    PLAYER_SMALL_VIEW_KEY_LOOK = 4,
    PLAYER_SMALL_VIEW_LOOK_LIMIT = 160 / 2,
    PLAYER_AIM_SPEED_LIMIT = 512 * 20
};

static void player_runtime_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

/* 68000 longword additions/subtractions wrap at 32 bits. */
static int32_t player_runtime_add32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left + (uint32_t)right);
}

static int32_t player_runtime_sub32(int32_t left, int32_t right)
{
    return (int32_t)((uint32_t)left - (uint32_t)right);
}

static int16_t player_runtime_low_word(int32_t value)
{
    return (int16_t)(uint16_t)value;
}

/* On the big-endian 68000, move.w writes the first (high) word of a long. */
static int32_t player_runtime_replace_position_word(int32_t value, int16_t position_word)
{
    return (int32_t)(((uint32_t)value & UINT32_C(0x0000ffff)) |
                     ((uint32_t)(uint16_t)position_word << 16u));
}

/* AimSpeed retains the port's existing low-word control representation. */
static int32_t player_runtime_replace_low_word(int32_t value, int16_t low_word)
{
    return (int32_t)(((uint32_t)value & UINT32_C(0xffff0000)) | (uint16_t)low_word);
}

/* add.w source,destination where destination is the low word of a long. */
static int32_t player_runtime_add_low_word(int32_t value, int16_t addend)
{
    return player_runtime_replace_low_word(
        value, (int16_t)((uint16_t)value + (uint16_t)addend));
}

static int16_t player_runtime_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

static int32_t player_runtime_muls16(int16_t left, int16_t right)
{
    return (int32_t)left * (int32_t)right;
}

/* asr is sign-extending; C division gives the source's later toward-zero result. */
static int32_t player_runtime_asr32(int32_t value, unsigned int count)
{
    if (value >= 0) {
        return value >> count;
    }
    return -((-(int64_t)value + ((INT64_C(1) << count) - 1)) >> count);
}

static int16_t player_runtime_asr16(int16_t value, unsigned int count)
{
    return (int16_t)player_runtime_asr32(value, count);
}

static int player_runtime_divs16(int32_t dividend, int16_t divisor, int16_t *out_quotient,
                                 char *error, size_t error_size)
{
    int32_t quotient;

    if (!out_quotient || divisor == 0) {
        player_runtime_set_error(error, error_size,
                                 "source collision attempted a zero EdgeT divisor");
        return 0;
    }
    /*
     * 68000 DIVS.W leaves Dn unchanged when its quotient overflows.  The
     * objectmove.s callers do not inspect V before consuming Dn's low word,
     * so preserve the register result rather than turning valid source motion
     * into a host-side fatal update failure.
     */
    if (dividend == INT32_MIN && divisor == -1) {
        *out_quotient = (int16_t)(uint16_t)dividend;
        return 1;
    }
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        *out_quotient = (int16_t)(uint16_t)dividend;
        return 1;
    }
    *out_quotient = (int16_t)quotient;
    return 1;
}

static int player_runtime_get_edge(const LevelRuntime *runtime, uint16_t zone_index,
                                   uint32_t list_index, int extended, LevelEdge *out_edge,
                                   uint32_t *out_edge_index, char *error, size_t error_size)
{
    uint32_t edge_index = 0u;

    if ((extended != 0 &&
         !level_runtime_get_zone_extended_edge_index(runtime, zone_index, list_index,
                                                     &edge_index, error, error_size)) ||
        (extended == 0 &&
         !level_runtime_get_zone_edge_index(runtime, zone_index, list_index,
                                            &edge_index, error, error_size))) {
        return 0;
    }
    if (!level_runtime_get_edge(runtime, edge_index, out_edge, error, error_size)) {
        return 0;
    }
    if (out_edge_index) {
        *out_edge_index = edge_index;
    }
    return 1;
}

static int player_runtime_edge_vertical_passable(int32_t crossing_y, int32_t thing_height,
                                                 int32_t step_up, int32_t lower_floor,
                                                 int32_t lower_roof, int32_t upper_floor,
                                                 int32_t upper_roof)
{
    int32_t lower_extent = player_runtime_sub32(crossing_y, step_up);

    lower_extent = player_runtime_add32(lower_extent, thing_height);
    /* objectmove.s:chkhttt through .yeshit. */
    if (lower_extent >= lower_floor) {
        return 0;
    }
    if (crossing_y > lower_roof) {
        return 1;
    }
    if (crossing_y < upper_roof) {
        return 0;
    }
    return lower_extent < upper_floor;
}

static int player_runtime_extended_edge_vertical_passable(const LevelRuntime *runtime,
                                                          const LevelEdge *edge,
                                                          int32_t new_y, int32_t thing_height,
                                                          int32_t step_up, int32_t step_down,
                                                          char *error, size_t error_size)
{
    LevelZone joined_zone;
    int32_t clearance;
    int32_t floor_delta;

    if (edge->join_zone_id < 0) {
        return 0;
    }
    if (!level_runtime_get_zone(runtime, (uint16_t)edge->join_zone_id, &joined_zone,
                                error, error_size)) {
        return 0;
    }

    /* objectmove.s:checkotherwalls lower-zone opening test. */
    clearance = player_runtime_sub32(joined_zone.floor, joined_zone.roof);
    if (clearance > thing_height) {
        floor_delta = player_runtime_sub32(player_runtime_add32(new_y, thing_height),
                                           joined_zone.floor);
        if (((floor_delta <= 0 &&
             player_runtime_sub32(0, floor_delta) < step_down) ||
             (floor_delta > 0 && floor_delta < step_up)) &&
            new_y >= joined_zone.roof) {
            return 1;
        }
    }

    /* The source reaches this upper-zone test only after the lower test blocked. */
    clearance = player_runtime_sub32(joined_zone.upper_floor, joined_zone.upper_roof);
    if (clearance <= thing_height) {
        return 0;
    }
    floor_delta = player_runtime_sub32(player_runtime_add32(new_y, thing_height),
                                       joined_zone.upper_floor);
    return ((floor_delta <= 0 &&
             player_runtime_sub32(0, floor_delta) < step_down) ||
            (floor_delta > 0 && floor_delta < step_up)) &&
           new_y >= joined_zone.upper_roof;
}

static int player_runtime_primary_edge_hit(const LevelRuntime *runtime, const LevelEdge *edge,
                                           int16_t old_x, int16_t old_z, int16_t new_x,
                                           int16_t new_z, int32_t old_y, int32_t new_y,
                                           int32_t thing_height, int32_t step_up,
                                           int16_t *out_x, int16_t *out_z, int *out_hit,
                                           char *error, size_t error_size)
{
    LevelZone joined_zone;
    int16_t shift_x = edge->unknown_byte_12;
    int16_t shift_z = edge->unknown_byte_13;
    int16_t delta_x = (int16_t)((int32_t)edge->x_length - shift_x - shift_z);
    int16_t delta_z = (int16_t)((int32_t)edge->z_length + shift_x - shift_z);
    int16_t denominator = (int16_t)((int32_t)edge->unknown_word + PLAYER_EDGE_EXTENSION);
    int16_t local_x;
    int16_t local_z;
    int32_t cross;
    int16_t crossing_distance;
    int16_t total_distance;
    int32_t crossing_y;
    int16_t hit_x;
    int16_t hit_z;

    *out_hit = 0;
    if (edge->join_zone_id >= 0) {
        if (!level_runtime_get_zone(runtime, (uint16_t)edge->join_zone_id, &joined_zone,
                                    error, error_size)) {
            return 0;
        }
    } else {
        /* objectmove.s initializes all four heights to -65536*256 for a wall. */
        memset(&joined_zone, 0, sizeof(joined_zone));
        joined_zone.floor = -65536 * 256;
        joined_zone.roof = -65536 * 256;
        joined_zone.upper_floor = -65536 * 256;
        joined_zone.upper_roof = -65536 * 256;
    }

    local_x = (int16_t)((int32_t)new_x - edge->x - shift_x);
    local_z = (int16_t)((int32_t)new_z - edge->z - shift_z);
    cross = player_runtime_sub32(player_runtime_muls16(delta_x, local_z),
                                 player_runtime_muls16(delta_z, local_x));
    if (cross > 0) {
        /* This source branch only marks EdgeT_Flags_w; it cannot alter movement. */
        if (!player_runtime_divs16(cross, denominator, &crossing_distance,
                                   error, error_size)) {
            return 0;
        }
        *out_hit = 1;
        return 1;
    }
    if (!player_runtime_divs16(cross, denominator, &crossing_distance,
                               error, error_size)) {
        return 0;
    }

    local_x = (int16_t)((int32_t)old_x - edge->x - shift_x);
    local_z = (int16_t)((int32_t)old_z - edge->z - shift_z);
    cross = player_runtime_sub32(player_runtime_muls16(delta_x, local_z),
                                 player_runtime_muls16(delta_z, local_x));
    if (!player_runtime_divs16(cross, denominator, &total_distance,
                               error, error_size)) {
        return 0;
    }
    total_distance = (int16_t)((int32_t)total_distance - crossing_distance);
    if (total_distance <= 0) {
        total_distance = 1;
    }
    crossing_y = new_y;
    if (new_y != old_y) {
        int16_t interpolation;
        int16_t delta_y = player_runtime_low_word(player_runtime_sub32(new_y, old_y));

        if (!player_runtime_divs16(delta_y, total_distance, &interpolation,
                                   error, error_size)) {
            return 0;
        }
        crossing_y = player_runtime_add32(
            new_y, player_runtime_muls16(interpolation, crossing_distance));
    }
    if (player_runtime_edge_vertical_passable(crossing_y, thing_height, step_up,
                                              joined_zone.floor, joined_zone.roof,
                                              joined_zone.upper_floor,
                                              joined_zone.upper_roof)) {
        return 1;
    }

    /* objectmove.s:.calcalong, then .othercheck endpoint validation. */
    if (!player_runtime_divs16(player_runtime_muls16(crossing_distance, delta_z), denominator,
                               &hit_x, error, error_size) ||
        !player_runtime_divs16(player_runtime_muls16(crossing_distance, delta_x), denominator,
                               &hit_z, error, error_size)) {
        return 0;
    }
    hit_x = (int16_t)((int32_t)new_x - hit_x);
    hit_z = (int16_t)((int32_t)new_z + hit_z);
    local_x = (int16_t)((int32_t)hit_x - edge->x - shift_x);
    local_z = (int16_t)((int32_t)hit_z - edge->z - shift_z);
    if ((local_x >= 0 ? local_x : (int16_t)-local_x) >=
        (local_z >= 0 ? local_z : (int16_t)-local_z)) {
        if (local_x <= 0) {
            if (delta_x > 4 || local_x < (int16_t)(delta_x - 4)) {
                return 1;
            }
        } else if (delta_x < -4 || local_x > (int16_t)(delta_x + 4)) {
            return 1;
        }
    } else if (local_z <= 0) {
        if (delta_z > 4 || local_z < (int16_t)(delta_z - 4)) {
            return 1;
        }
    } else if (delta_z < -4 || local_z > (int16_t)(delta_z + 4)) {
        return 1;
    }
    *out_x = hit_x;
    *out_z = hit_z;
    *out_hit = 1;
    return 1;
}

static int player_runtime_extended_edge_hit(const LevelRuntime *runtime, const LevelEdge *edge,
                                            int16_t old_x, int16_t old_z, int16_t new_x,
                                            int16_t new_z, int32_t new_y, int32_t thing_height,
                                            int32_t step_up, int32_t step_down,
                                            int16_t *out_x, int16_t *out_z, int *out_hit,
                                            char *error, size_t error_size)
{
    int16_t shift_x = edge->unknown_byte_12;
    int16_t shift_z = edge->unknown_byte_13;
    int16_t delta_x = (int16_t)((int32_t)edge->x_length - shift_x - shift_z);
    int16_t delta_z = (int16_t)((int32_t)edge->z_length + shift_x - shift_z);
    int16_t denominator = (int16_t)((int32_t)edge->unknown_word + PLAYER_EDGE_EXTENSION);
    int16_t local_x;
    int16_t local_z;
    int16_t movement_x;
    int16_t movement_z;
    int32_t cross;
    int32_t crossing_test;
    int32_t travel_cross;
    int16_t distance;
    int16_t hit_x;
    int16_t hit_z;

    *out_hit = 0;
    if (player_runtime_extended_edge_vertical_passable(runtime, edge, new_y, thing_height,
                                                       step_up, step_down, error, error_size)) {
        return 1;
    }
    local_x = (int16_t)((int32_t)new_x - edge->x - shift_x);
    local_z = (int16_t)((int32_t)new_z - edge->z - shift_z);
    cross = player_runtime_sub32(player_runtime_muls16(delta_x, local_z),
                                 player_runtime_muls16(delta_z, local_x));
    if (cross >= 0) {
        return 1;
    }
    movement_x = (int16_t)((int32_t)new_x - old_x);
    movement_z = (int16_t)((int32_t)new_z - old_z);
    local_x = (int16_t)((int32_t)old_x - edge->x - shift_x);
    local_z = (int16_t)((int32_t)edge->z + shift_z - old_z);
    crossing_test = player_runtime_add32(player_runtime_muls16(movement_z, local_x),
                                         player_runtime_muls16(movement_x, local_z));
    travel_cross = player_runtime_sub32(player_runtime_muls16(delta_x, movement_z),
                                        player_runtime_muls16(delta_z, movement_x));
    if (travel_cross == 0 ||
        (travel_cross < 0 && (crossing_test > 0 || travel_cross > crossing_test)) ||
        (travel_cross > 0 && (crossing_test < 0 || crossing_test > travel_cross))) {
        return 1;
    }
    if (!player_runtime_divs16(cross, denominator, &distance, error, error_size)) {
        return 0;
    }
    distance = (int16_t)(distance - 3);
    if (!player_runtime_divs16(player_runtime_muls16(distance, delta_z), denominator,
                               &hit_x, error, error_size) ||
        !player_runtime_divs16(player_runtime_muls16(distance, delta_x), denominator,
                               &hit_z, error, error_size)) {
        return 0;
    }
    hit_x = (int16_t)((int32_t)new_x - hit_x);
    hit_z = (int16_t)((int32_t)new_z + hit_z);
    local_x = (int16_t)((int32_t)old_x - edge->x - shift_x);
    local_z = (int16_t)((int32_t)old_z - edge->z - shift_z);
    cross = player_runtime_sub32(player_runtime_muls16(delta_x, local_z),
                                 player_runtime_muls16(delta_z, local_x));
    if (cross < 0) {
        return 1;
    }
    *out_x = hit_x;
    *out_z = hit_z;
    *out_hit = 1;
    return 1;
}

int player_runtime_init_single_player(const LevelBootstrap *level,
                                      const LevelRuntime *runtime,
                                      PlayerRuntime *out_player,
                                      char *error, size_t error_size)
{
    LevelZone start_zone;
    PlayerRuntime player;

    if (!level || !runtime || !out_player || level->player1_start_zone >= runtime->zone_count) {
        player_runtime_set_error(error, error_size, "single-player start zone is invalid");
        return 0;
    }
    if (!level_runtime_get_zone(runtime, level->player1_start_zone, &start_zone,
                                error, error_size)) {
        return 0;
    }

    /* modules/player.s:Plr_Initialise player-one branch. */
    memset(&player, 0, sizeof(player));
    player.zone_index = level->player1_start_zone;
    player.x = player_runtime_world_to_position(level->player1_start_x);
    player.z = player_runtime_world_to_position(level->player1_start_z);
    player.snap_x = player_runtime_world_to_position(level->player1_start_x);
    player.snap_z = player_runtime_world_to_position(level->player1_start_z);
    player.height = PLAYER_STANDING_HEIGHT;
    player.snap_height = PLAYER_STANDING_HEIGHT;
    player.snap_target_height = PLAYER_STANDING_HEIGHT;
    player.snap_squished_height = PLAYER_STANDING_HEIGHT;
    player.health = 200u;
    player.y = start_zone.floor - player.height;
    player.snap_y = player.y;
    player.snap_target_y = player.y;
    player.tmp_x = player.x;
    player.tmp_y = player.y;
    player.tmp_z = player.z;
    player.tmp_height = player.height;
    player.tmp_yaw = player.yaw;
    player.tmp_gun_selected = player.gun_selected;
    player.tmp_fire = player.fire;
    player.default_enemy_flags = 0x23u; /* %100011 in Plr_Initialise. */
    /* hires.s's non-CD32 default control method sets Plr1_Mouse_b. */
    player.mouse_active = UINT8_MAX;
    *out_player = player;
    return 1;
}

int player_runtime_update_discrete_controls(PlayerRuntime *player, GameInput *input,
                                            const GameControls *controls,
                                            const LevelRuntime *runtime,
                                            const GameInventory *inventory,
                                            char *error, size_t error_size)
{
    LevelZone zone;
    int fire_down;
    int32_t available_height;
    int32_t target_height;

    if (!player || !input || !controls || !runtime || !inventory ||
        player->zone_index >= runtime->zone_count) {
        player_runtime_set_error(error, error_size,
                                 "discrete player control received invalid source state");
        return 0;
    }

    /*
     * modules/player.s:plr_KeyboardControl clears only on the next controller
     * tick. A direct number-key selection below reasserts the request while
     * that key remains held, matching the source Timer1 write.
     */
    player->reset_weapon_animation = 0u;

    /* modules/player.s only advances one owned weapon per next-weapon press. */
    if (game_input_is_control_down(input, controls, GAME_CONTROL_NEXT_WEAPON)) {
        if (player->previous_next_weapon_key_state == 0u) {
            uint16_t candidate = player->gun_selected;
            uint16_t attempts;

            player->previous_next_weapon_key_state = UINT8_MAX;
            for (attempts = 0u; attempts < GAME_INVENTORY_WEAPON_COUNT; ++attempts) {
                candidate = (uint16_t)((candidate + 1u) % GAME_INVENTORY_WEAPON_COUNT);
                if (inventory->weapons[candidate] != 0u) {
                    player->gun_selected = (uint8_t)candidate;
                    break;
                }
            }
            if (attempts == GAME_INVENTORY_WEAPON_COUNT) {
                player_runtime_set_error(error, error_size,
                                         "source next-weapon control has no owned weapon");
                return 0;
            }
        }
    } else {
        player->previous_next_weapon_key_state = 0u;
    }

    /*
     * modules/player.s:.pickweap scans RAWKEY_1 through RAWKEY_0 (raw codes
     * 1..10) in weapon-table order. Unlike next-weapon it chooses the first
     * held, owned weapon directly and restarts the Player 1 companion weapon
     * animation by clearing ENT_NEXT_2+EntT_Timer1_w.
     */
    for (uint16_t weapon_index = 0u; weapon_index < GAME_INVENTORY_WEAPON_COUNT;
         ++weapon_index) {
        if (inventory->weapons[weapon_index] != 0u &&
            game_input_is_raw_key_down(input, (uint8_t)(weapon_index + 1u))) {
            player->gun_selected = (uint8_t)weapon_index;
            player->reset_weapon_animation = UINT8_MAX;
            break;
        }
    }

    /* modules/player.s: plr_PrevUseKeyState_b gates one Used_b pulse per press. */
    if (game_input_is_control_down(input, controls, GAME_CONTROL_OPERATE) &&
        player->previous_use_key_state == 0u) {
        player->used = UINT8_MAX;
    }
    player->previous_use_key_state =
        game_input_is_control_down(input, controls, GAME_CONTROL_OPERATE) ? UINT8_MAX : 0u;

    /* The source consumes duck_key itself before toggling Ducked_b with not.b. */
    if (game_input_is_control_down(input, controls, GAME_CONTROL_CROUCH)) {
        uint8_t crouch_raw_key = controls->assigned_raw_keys[GAME_CONTROL_CROUCH];

        if (!game_input_set_raw_key(input, crouch_raw_key, 0, error, error_size)) {
            return 0;
        }
        player->snap_target_height = PLAYER_STANDING_HEIGHT;
        player->ducked = (uint8_t)~player->ducked;
        if (player->ducked != 0u) {
            player->snap_target_height = PLAYER_CROUCH_HEIGHT;
        }
    }

    if (!level_runtime_get_zone(runtime, player->zone_index, &zone, error, error_size)) {
        return 0;
    }
    /* modules/player.s selects the current lower or upper zone vertical span. */
    available_height = player->stood_in_top != 0u ?
        (int32_t)((uint32_t)zone.upper_floor - (uint32_t)zone.upper_roof) :
        (int32_t)((uint32_t)zone.floor - (uint32_t)zone.roof);
    player->squished = 0u;
    player->snap_squished_height = PLAYER_STANDING_HEIGHT;
    if (available_height <= PLAYER_STANDING_CLEARANCE) {
        player->squished = UINT8_MAX;
        player->snap_squished_height = PLAYER_CROUCH_HEIGHT;
    }
    target_height = player->snap_target_height;
    if (target_height > player->snap_squished_height) {
        target_height = player->snap_squished_height;
    }
    if (player->snap_height < target_height) {
        player->snap_height += 1024;
    } else if (player->snap_height > target_height) {
        player->snap_height -= 1024;
    }

    /* The final plr_KeyboardControl branch latches Fire_b and Clicked_b. */
    fire_down = game_input_is_control_down(input, controls, GAME_CONTROL_FIRE);
    if (player->fire != 0u) {
        player->fire = fire_down ? UINT8_MAX : 0u;
    } else if (fire_down) {
        player->clicked = UINT8_MAX;
        player->fire = UINT8_MAX;
    }
    return 1;
}

static int player_runtime_move_static(const LevelRuntime *runtime, uint16_t *io_zone_index,
                                      uint8_t *io_stood_in_top, int16_t old_x, int16_t old_z,
                                      int32_t old_y, int32_t new_y, int32_t thing_height,
                                      int32_t step_up, int16_t *io_new_x, int16_t *io_new_z,
                                      LevelDynamicState *dynamic_state,
                                      char *error, size_t error_size)
{
    uint16_t backup_zone = *io_zone_index;
    uint8_t backup_stood_in_top = *io_stood_in_top;

    for (uint16_t transition_count = 0u;
         transition_count < PLAYER_MAX_ZONE_TRANSITIONS; ++transition_count) {
        uint32_t edge_count;
        uint32_t extended_edge_count;
        int changed_zone = 0;

        if (!level_runtime_get_zone_edge_count(runtime, *io_zone_index, &edge_count,
                                               error, error_size)) {
            return 0;
        }
        for (uint32_t edge_list_index = 0u; edge_list_index < edge_count; ++edge_list_index) {
            LevelEdge edge;
            uint32_t edge_index;
            int hit;

            if (!player_runtime_get_edge(runtime, *io_zone_index, edge_list_index, 0, &edge,
                                         &edge_index, error, error_size) ||
                !player_runtime_primary_edge_hit(runtime, &edge, old_x, old_z, *io_new_x,
                                                 *io_new_z, old_y, new_y, thing_height, step_up,
                                                 io_new_x, io_new_z, &hit, error, error_size)) {
                return 0;
            }
            if (hit != 0 && dynamic_state != NULL &&
                !level_dynamic_state_or_edge_flags(dynamic_state, edge_index, 0x0100u)) {
                player_runtime_set_error(error, error_size,
                                         "failed to record source player edge contact");
                return 0;
            }
        }

        if (!level_runtime_get_zone_extended_edge_count(runtime, *io_zone_index,
                                                        &extended_edge_count,
                                                        error, error_size)) {
            return 0;
        }
        for (uint32_t edge_list_index = 0u;
             edge_list_index < extended_edge_count; ++edge_list_index) {
            LevelEdge edge;
            uint32_t edge_index;
            int hit;

            if (!player_runtime_get_edge(runtime, *io_zone_index, edge_list_index, 1, &edge,
                                         &edge_index, error, error_size) ||
                !player_runtime_extended_edge_hit(runtime, &edge, old_x, old_z, *io_new_x,
                                                  *io_new_z, new_y, thing_height, step_up,
                                                  PLAYER_STEP_DOWN, io_new_x, io_new_z, &hit,
                                                  error, error_size)) {
                return 0;
            }
            if (hit != 0 && dynamic_state != NULL &&
                !level_dynamic_state_or_edge_flags(dynamic_state, edge_index, 0x0100u)) {
                player_runtime_set_error(error, error_size,
                                         "failed to record source player edge contact");
                return 0;
            }
        }

        /* objectmove.s:CheckMoreFloorLines.  Only primary edges change rooms. */
        for (uint32_t edge_list_index = 0u; edge_list_index < edge_count; ++edge_list_index) {
            LevelEdge edge;
            LevelZone joined_zone;
            int16_t local_x;
            int16_t local_z;
            int32_t new_side;
            int16_t move_x;
            int16_t move_z;
            int32_t start_cross;
            int32_t end_cross;
            int16_t new_distance;
            int16_t old_distance;
            int16_t travel_distance;
            int32_t crossing_y;

            if (!player_runtime_get_edge(runtime, *io_zone_index, edge_list_index, 0, &edge,
                                         NULL, error, error_size)) {
                return 0;
            }
            if (edge.join_zone_id < 0) {
                continue;
            }
            if (!level_runtime_get_zone(runtime, (uint16_t)edge.join_zone_id, &joined_zone,
                                        error, error_size)) {
                return 0;
            }
            local_x = (int16_t)((int32_t)*io_new_x - edge.x);
            local_z = (int16_t)((int32_t)*io_new_z - edge.z);
            new_side = player_runtime_sub32(player_runtime_muls16(edge.x_length, local_z),
                                             player_runtime_muls16(edge.z_length, local_x));
            if (new_side >= 0) {
                continue;
            }
            move_x = (int16_t)((int32_t)*io_new_x - old_x);
            move_z = (int16_t)((int32_t)*io_new_z - old_z);
            local_x = (int16_t)((int32_t)edge.x - old_x);
            local_z = (int16_t)((int32_t)edge.z - old_z);
            start_cross = player_runtime_sub32(player_runtime_muls16(local_x, move_z),
                                               player_runtime_muls16(local_z, move_x));
            if (start_cross > 0) {
                continue;
            }
            local_x = (int16_t)((int32_t)edge.x + edge.x_length - old_x);
            local_z = (int16_t)((int32_t)edge.z + edge.z_length - old_z);
            end_cross = player_runtime_sub32(player_runtime_muls16(local_x, move_z),
                                             player_runtime_muls16(local_z, move_x));
            if (end_cross < 0) {
                continue;
            }
            if (!player_runtime_divs16(new_side, edge.unknown_word, &new_distance,
                                       error, error_size)) {
                return 0;
            }
            local_x = (int16_t)((int32_t)old_x - edge.x);
            local_z = (int16_t)((int32_t)old_z - edge.z);
            start_cross = player_runtime_sub32(player_runtime_muls16(edge.x_length, local_z),
                                               player_runtime_muls16(edge.z_length, local_x));
            if (!player_runtime_divs16(start_cross, edge.unknown_word, &old_distance,
                                       error, error_size)) {
                return 0;
            }
            travel_distance = (int16_t)((int32_t)old_distance - new_distance);
            if (travel_distance <= 0) {
                travel_distance = 1;
            }
            crossing_y = new_y;
            if (new_y != old_y) {
                int16_t interpolation;
                int16_t delta_y = player_runtime_low_word(player_runtime_sub32(new_y, old_y));

                if (!player_runtime_divs16(delta_y, travel_distance, &interpolation,
                                           error, error_size)) {
                    return 0;
                }
                crossing_y = player_runtime_add32(
                    new_y, player_runtime_muls16(interpolation, new_distance));
            }
            *io_stood_in_top = crossing_y < joined_zone.roof ? UINT8_MAX : 0u;
            *io_zone_index = (uint16_t)edge.join_zone_id;
            changed_zone = 1;
            break;
        }
        if (!changed_zone) {
            return 1;
        }
    }

    /* objectmove.s:ERRORINMOVEMENT restores the pre-MoveObject room/position. */
    *io_zone_index = backup_zone;
    *io_stood_in_top = backup_stood_in_top;
    *io_new_x = old_x;
    *io_new_z = old_z;
    return 1;
}

/*
 * MakeSomeNoise consumes listener-relative coordinates. The player-only
 * plr_DoFootstepFX caller supplies (0, 100), so rotate that source point back
 * into map words for the API-neutral event queue consumed by the desktop
 * listener.
 */
static int player_runtime_emit_relative_player_sound(
    const PlayerRuntime *player, const GameMath *math, GameAudioEvents *audio_events,
    int16_t sample_index, int16_t volume, uint8_t echo,
    char *error, size_t error_size)
{
    int16_t sine;
    int16_t cosine;
    int16_t world_x;
    int16_t world_z;

    if (!audio_events) {
        return 1;
    }
    if (!game_math_sine(math, player->snap_yaw, &sine, error, error_size) ||
        !game_math_cosine(math, player->snap_yaw, &cosine, error, error_size)) {
        return 0;
    }
    /* transform.s's table values are 2.14 fixed point. */
    world_x = (int16_t)((int32_t)player_runtime_position_to_world(player->snap_x) +
                        ((int32_t)sine * 100) / 16384);
    world_z = (int16_t)((int32_t)player_runtime_position_to_world(player->snap_z) +
                        ((int32_t)cosine * 100) / 16384);
    game_audio_events_emit(audio_events, sample_index, volume, world_x, world_z,
                           UINT16_C(0xfff8), GAME_AUDIO_RESTART_SOURCE, 0u, echo);
    return 1;
}

/* modules/player.s:plr_DoFootstepFX. */
static int player_runtime_emit_footstep(PlayerRuntime *player, const LevelZone *zone,
                                        const GameMath *math, const GameLink *game_link,
                                        GameAudioEvents *audio_events,
                                        char *error, size_t error_size)
{
    GameFloorData floor_data;
    int16_t sample_index;
    uint16_t floor_index;

    if (!audio_events) {
        return 1;
    }
    if (zone->water < zone->floor && zone->water >= player->y &&
        player->stood_in_top == 0u) {
        /* The water branch writes slot six directly, rather than one-based GLFT data. */
        sample_index = 6;
    } else {
        floor_index = player->stood_in_top != 0u ? zone->upper_floor_noise : zone->floor_noise;
        if (!game_link || !game_link_get_floor_data(game_link, floor_index, &floor_data,
                                                     error, error_size)) {
            if (!game_link) {
                player_runtime_set_error(error, error_size,
                                         "source footstep sound requires the GLFT catalog");
            }
            return 0;
        }
        /* GLFT's LSW is one based; signed negative after decrement is silent. */
        sample_index = (int16_t)(uint16_t)(floor_data.sound_effect - 1u);
        if (sample_index < 0) {
            return 1;
        }
    }
    return player_runtime_emit_relative_player_sound(player, math, audio_events,
                                                      sample_index, 80, zone->echo,
                                                      error, error_size);
}

static int player_runtime_apply_fall(PlayerRuntime *player, const GameInput *input,
                                     const GameControls *controls, const GameMath *math,
                                     const LevelRuntime *runtime, const GameLink *game_link,
                                     GameAudioEvents *audio_events,
                                     char *error, size_t error_size)
{
    LevelZone zone;
    int32_t target_y;
    int32_t y;
    int32_t velocity;
    int32_t ceiling;

    if (!level_runtime_get_zone(runtime, player->zone_index, &zone, error, error_size)) {
        return 0;
    }
    target_y = player->snap_target_y;
    y = player->snap_y;
    velocity = player->snap_y_velocity;
    if (target_y < y) {
        int32_t correction = player_runtime_sub32(target_y, y);

        player->decelerate = UINT8_MAX;
        if (correction < -512) {
            correction = -512;
        }
        y = player_runtime_add32(y, correction);
    } else if (target_y == y) {
        uint16_t walk_sound_accumulator;

        /* LiftRoutine's signed word speed is applied as a 32-bit << 6 here. */
        velocity = (int32_t)player->floor_speed * 64;
        player->decelerate = UINT8_MAX;
        /* plr_Fall consumes and then clears the previous airborne accumulation. */
        player->fall_damage = 0;
        player->bobble = game_math_wrap_angle_address(
            (uint16_t)((uint32_t)player->bobble + (uint16_t)player->add_to_bobble));
        walk_sound_accumulator =
            (uint16_t)(player->walk_sfx_time + (uint16_t)player->add_to_bobble);
        player->walk_sfx_time = (uint16_t)(walk_sound_accumulator & 4095u);
        if ((walk_sound_accumulator & UINT16_C(0xf000)) != 0u &&
            !player_runtime_emit_footstep(player, &zone, math, game_link, audio_events,
                                           error, error_size)) {
            return 0;
        }
        if (game_input_is_control_down(input, controls, GAME_CONTROL_JUMP) &&
            player->health != 0u) {
            /* ZoneT_Water_l selects plr_Fall's shallow-water jump speed. */
            velocity = y >= zone.water ? PLAYER_JUMP_SPEED_WATER : PLAYER_JUMP_SPEED_DRY;
        }
        if (velocity > 0) {
            velocity = 0;
        }
        y = player_runtime_add32(y, velocity);
    } else {
        /* modules/player.s:plr_Fall .above_ground through .still_above. */
        player->decelerate = player_runtime_sub32(target_y, y) <=
                PLAYER_FALL_NEAR_GROUND_DISTANCE ?
            UINT8_MAX : 0u;
        y = player_runtime_add32(y, velocity);
        if (target_y > y) {
            velocity = player_runtime_add32(velocity, PLAYER_FALL_ACCELERATION);
            player->fall_damage = player_runtime_add16(player->fall_damage, 1);

            /*
             * The source applies its 512 terminal cap only once the player
             * reaches ZoneT_Water_l.  Dry falls deliberately retain their
             * accumulated 8.8 fixed-point velocity.
             */
            if (y >= zone.water) {
                player->decelerate = UINT8_MAX;
                player->fall_damage = 0;
                if (velocity >= PLAYER_FALL_WATER_TERMINAL_VELOCITY) {
                    velocity = PLAYER_FALL_WATER_TERMINAL_VELOCITY;
                }
            }
        } else {
            /* plr_Fall retains the crossed target Y and hands off FloorSpd. */
            player->fall_damage = 0;
            velocity = (int32_t)player->floor_speed * 64;
        }
    }
    ceiling = player->stood_in_top != 0u ? zone.upper_roof : zone.roof;
    ceiling = player_runtime_add32(ceiling, PLAYER_CEILING_CLEARANCE);
    if (y < ceiling) {
        y = ceiling;
        if (velocity < 0) {
            velocity = 0;
        }
    }
    player->snap_y_velocity = velocity;
    player->snap_y = y;
    return 1;
}

static int player_runtime_update_keyboard_motion(PlayerRuntime *player, const GameInput *input,
                                                  const GameControls *controls,
                                                  const GamePreferences *preferences,
                                                  const GameMath *math,
                                                  char *error, size_t error_size)
{
    int16_t turn_limit;
    int16_t move_speed;
    int16_t turn_speed;
    int16_t angular_speed;
    int16_t strafe;
    int16_t forward;
    int32_t speed_x;
    int32_t speed_z;
    int16_t sine;
    int16_t cosine;
    uint16_t left_binding = GAME_CONTROL_TURN_LEFT;
    uint16_t right_binding = GAME_CONTROL_TURN_RIGHT;
    uint16_t strafe_left_binding = GAME_CONTROL_SIDESTEP_LEFT;
    uint16_t strafe_right_binding = GAME_CONTROL_SIDESTEP_RIGHT;

    if (preferences->always_run != 0u) {
        turn_limit = 60;
        move_speed = 3;
        turn_speed = 14;
        if (game_input_is_control_down(input, controls, GAME_CONTROL_RUN)) {
            turn_limit = 35;
            move_speed = 2;
            turn_speed = 10;
        }
    } else {
        turn_limit = 35;
        move_speed = 2;
        turn_speed = 10;
        if (game_input_is_control_down(input, controls, GAME_CONTROL_RUN)) {
            turn_limit = 60;
            move_speed = 3;
            turn_speed = 14;
        }
    }
    if (player->squished != 0u || player->ducked != 0u) {
        move_speed = player_runtime_asr16(move_speed, 1u);
    }

    angular_speed = player->snap_yaw_speed;
    if (player->decelerate != 0u) {
        angular_speed = (int16_t)(((int32_t)angular_speed * 3) / 4);
    }
    if (game_input_is_control_down(input, controls, GAME_CONTROL_FORCE_SIDESTEP)) {
        left_binding = GAME_CONTROL_SIDESTEP_LEFT;
        right_binding = GAME_CONTROL_SIDESTEP_RIGHT;
        strafe_left_binding = GAME_CONTROL_TURN_LEFT;
        strafe_right_binding = GAME_CONTROL_TURN_RIGHT;
    }
    if (player->decelerate != 0u) {
        if (game_input_is_control_down(input, controls, left_binding)) {
            angular_speed = (int16_t)((int32_t)angular_speed - turn_speed);
        }
        if (game_input_is_control_down(input, controls, right_binding)) {
            angular_speed = (int16_t)((int32_t)angular_speed + turn_speed);
        }
        if (angular_speed > turn_limit) {
            angular_speed = turn_limit;
        }
        if (angular_speed < -turn_limit) {
            angular_speed = (int16_t)-turn_limit;
        }
    }
    player->snap_yaw = game_math_wrap_angle_address(
        (uint16_t)((uint32_t)player->snap_yaw + (uint32_t)(int32_t)angular_speed * 2u));
    player->snap_yaw_speed = angular_speed;

    strafe = 0;
    if (game_input_is_control_down(input, controls, strafe_left_binding)) {
        /* modules/player.s mutates d4 with both ADD.W instructions. */
        strafe = player_runtime_add16(strafe, move_speed);
        strafe = player_runtime_add16(strafe, move_speed);
        strafe = player_runtime_asr16(strafe, 1u);
    }
    if (game_input_is_control_down(input, controls, strafe_right_binding)) {
        /* A simultaneous left input remains in d4 on this source path. */
        strafe = player_runtime_add16(strafe, move_speed);
        strafe = player_runtime_add16(strafe, move_speed);
        strafe = player_runtime_asr16(strafe, 1u);
        strafe = (int16_t)(UINT16_C(0) - (uint16_t)strafe);
    }
    forward = 0;
    if (game_input_is_control_down(input, controls, GAME_CONTROL_FORWARDS)) {
        /* NEG.W changes d2 itself before the later backwards-key test. */
        move_speed = (int16_t)(UINT16_C(0) - (uint16_t)move_speed);
        forward = move_speed;
    }
    if (game_input_is_control_down(input, controls, GAME_CONTROL_BACKWARDS)) {
        forward = move_speed;
    }
    player->add_to_bobble = (int16_t)((int32_t)forward << 6);
    if (!game_math_sine(math, player->snap_yaw, &sine, error, error_size) ||
        !game_math_cosine(math, player->snap_yaw, &cosine, error, error_size)) {
        return 0;
    }
    speed_x = player->snap_x_speed;
    speed_z = player->snap_z_speed;
    if (player->decelerate != 0u) {
        speed_x = player_runtime_sub32(0, speed_x);
        if (speed_x > 0) {
            speed_x = player_runtime_add32(player_runtime_asr32(speed_x, 3u), 1);
        } else {
            speed_x = player_runtime_asr32(speed_x, 3u);
        }
        speed_z = player_runtime_sub32(0, speed_z);
        if (speed_z > 0) {
            speed_z = player_runtime_add32(player_runtime_asr32(speed_z, 3u), 1);
        } else {
            speed_z = player_runtime_asr32(speed_z, 3u);
        }
    }
    speed_x = player_runtime_sub32(speed_x, player_runtime_muls16(sine, forward));
    speed_z = player_runtime_sub32(speed_z, player_runtime_muls16(cosine, forward));
    speed_x = player_runtime_sub32(speed_x, player_runtime_muls16(cosine, strafe));
    speed_z = player_runtime_add32(speed_z, player_runtime_muls16(sine, strafe));
    if (player->decelerate != 0u) {
        player->snap_x_speed = player_runtime_add32(player->snap_x_speed, speed_x);
        player->snap_z_speed = player_runtime_add32(player->snap_z_speed, speed_z);
    }
    player->snap_x = player_runtime_add32(player->snap_x, player->snap_x_speed);
    player->snap_z = player_runtime_add32(player->snap_z, player->snap_z_speed);
    return 1;
}

static void player_runtime_update_keyboard_look(PlayerRuntime *player, const GameInput *input,
                                                const GameControls *controls)
{
    int16_t look_offset = player->look_offset;

    /* modules/player.s:plr_KeyboardControl, small-screen View_* branch. */
    if (game_input_is_control_down(input, controls, GAME_CONTROL_LOOK_UP)) {
        player->aim_speed = player_runtime_add_low_word(player->aim_speed, -512);
        look_offset = (int16_t)((int32_t)look_offset - PLAYER_SMALL_VIEW_KEY_LOOK);
        if (look_offset <= -PLAYER_SMALL_VIEW_LOOK_LIMIT) {
            player->aim_speed = player_runtime_replace_low_word(
                player->aim_speed, -PLAYER_AIM_SPEED_LIMIT);
            look_offset = -PLAYER_SMALL_VIEW_LOOK_LIMIT;
        }
    }
    if (game_input_is_control_down(input, controls, GAME_CONTROL_LOOK_DOWN)) {
        player->aim_speed = player_runtime_add_low_word(player->aim_speed, 512);
        look_offset = (int16_t)((int32_t)look_offset + PLAYER_SMALL_VIEW_KEY_LOOK);
        if (look_offset >= PLAYER_SMALL_VIEW_LOOK_LIMIT) {
            player->aim_speed = player_runtime_replace_low_word(
                player->aim_speed, PLAYER_AIM_SPEED_LIMIT);
            look_offset = PLAYER_SMALL_VIEW_LOOK_LIMIT;
        }
    }
    if (game_input_is_control_down(input, controls, GAME_CONTROL_CENTRE_VIEW)) {
        if (player->previous_centre_view_key_state == 0u) {
            player->previous_centre_view_key_state = UINT8_MAX;
            player->aim_speed = player_runtime_replace_low_word(player->aim_speed, 0);
            look_offset = 0;
        }
    } else {
        player->previous_centre_view_key_state = 0u;
    }
    player->look_offset = look_offset;
}

static void player_runtime_update_mouse_controls(PlayerRuntime *player, GameInput *input)
{
    int16_t mouse_x;
    int16_t mouse_y;
    int16_t mouse_y_delta;
    int16_t look_offset;
    int16_t aim_delta;

    if (player->mouse_active == 0u) {
        return;
    }

    /*
     * c/system.c:Sys_ReadMouse advances Vis_AngPos_w by four source angle
     * bytes for each horizontal counter step.  In this single-player runtime
     * the committed Plr1_AngPos_w is that previous-frame view angle.
     */
    mouse_x = game_input_take_mouse_x(input);
    player->snap_yaw = game_math_wrap_angle_address(
        (uint16_t)player_runtime_add16((int16_t)player->yaw,
                                       (int16_t)((uint16_t)mouse_x << 2u)));

    /* modules/player.s:plr_MouseControl's Sys_MouseY/Sys_OldMouseY path. */
    mouse_y = input->mouse_y;
    if (player->invert_mouse != 0u) {
        mouse_y = (int16_t)(0u - (uint16_t)mouse_y);
    }
    mouse_y_delta = player_runtime_add16(
        mouse_y, (int16_t)(0u - (uint16_t)input->old_mouse_y));
    input->old_mouse_y = player_runtime_add16(input->old_mouse_y, mouse_y_delta);

    /* Vid_FullScreen_b is clear in the direct PC diagnostic path: .small. */
    aim_delta = (int16_t)((uint16_t)mouse_y_delta << 7u);
    player->aim_speed = player_runtime_add_low_word(player->aim_speed, aim_delta);
    look_offset = player_runtime_add16(player->look_offset, mouse_y_delta);
    if (look_offset <= -PLAYER_SMALL_VIEW_LOOK_LIMIT) {
        player->aim_speed = player_runtime_replace_low_word(
            player->aim_speed, -PLAYER_AIM_SPEED_LIMIT);
        look_offset = -PLAYER_SMALL_VIEW_LOOK_LIMIT;
    }
    if (look_offset >= PLAYER_SMALL_VIEW_LOOK_LIMIT) {
        player->aim_speed = player_runtime_replace_low_word(
            player->aim_speed, PLAYER_AIM_SPEED_LIMIT);
        look_offset = PLAYER_SMALL_VIEW_LOOK_LIMIT;
    }
    player->look_offset = look_offset;
}

int player_runtime_update_spatial_with_motion_and_audio(
    PlayerRuntime *player, GameInput *input, const GameControls *controls,
    const GamePreferences *preferences, const GameMath *math, const LevelRuntime *runtime,
    LevelDynamicState *dynamic_state, ObjectMotionRuntime *motion_runtime,
    const PlayerObjectCollisionContext *object_collision,
    const GameLink *game_link, GameAudioEvents *audio_events,
    char *error, size_t error_size)
{
    LevelZone zone;
    int16_t old_x;
    int16_t old_z;
    int16_t new_x;
    int16_t new_z;
    int16_t sine;
    int32_t bobble;
    int32_t visual_y;
    int32_t thing_height;
    int32_t step_up;
    int object_blocked = 0;
    int teleported = 0;
    int16_t published_new_x;
    int16_t published_new_z;

    if (!player || !input || !controls || !preferences || !math || !runtime ||
        player->zone_index >= runtime->zone_count) {
        player_runtime_set_error(error, error_size,
                                 "spatial player update received invalid source state");
        return 0;
    }
    if (dynamic_state != NULL &&
        (dynamic_state->runtime.level_bytes != runtime->level_bytes ||
         dynamic_state->runtime.graphics_bytes != runtime->graphics_bytes)) {
        player_runtime_set_error(error, error_size,
                                 "spatial update received a different mutable source level");
        return 0;
    }
    /* hires.s runs Plr1_MouseControl before the optional keyboard controller. */
    player_runtime_update_mouse_controls(player, input);
    player_runtime_update_keyboard_look(player, input, controls);
    if (!player_runtime_update_keyboard_motion(player, input, controls, preferences, math,
                                               error, error_size) ||
        !player_runtime_apply_fall(player, input, controls, math, runtime, game_link,
                                   audio_events, error, error_size) ||
        !level_runtime_get_zone(runtime, player->zone_index, &zone, error, error_size)) {
        return 0;
    }

    /* hires.s:game_main_loop copies Snap* and transient input into Plr1_Tmp*. */
    player->tmp_x = player->snap_x;
    player->tmp_y = player->snap_y;
    player->tmp_z = player->snap_z;
    player->tmp_height = player->snap_height;
    player->tmp_yaw = player->snap_yaw;
    player->tmp_clicked = player->clicked;
    player->clicked = 0u;
    player->tmp_fire = player->fire;
    player->tmp_used = player->used;
    player->used = 0u;
    player->tmp_gun_selected = player->gun_selected;

    old_x = player_runtime_position_to_world(player->x);
    old_z = player_runtime_position_to_world(player->z);
    new_x = player_runtime_position_to_world(player->snap_x);
    new_z = player_runtime_position_to_world(player->snap_z);
    published_new_x = new_x;
    published_new_z = new_z;
    player->height = player->snap_height;
    player->yaw = player->snap_yaw;
    if (!game_math_sine(math, player->bobble, &sine, error, error_size)) {
        return 0;
    }
    bobble = sine;
    if (bobble > 0) {
        bobble = -bobble;
    }
    bobble = player_runtime_asr32(player_runtime_add32(bobble, 16384), 4u);
    if (player->ducked == 0u && player->squished == 0u) {
        bobble = player_runtime_add32(bobble, bobble);
    }
    player->bobble_y = bobble;
    visual_y = player_runtime_add32(player->snap_y, bobble);
    thing_height = player_runtime_sub32(player->height, bobble);
    step_up = (player->ducked != 0u || player->squished != 0u) ?
        PLAYER_SMALL_STEP_UP : PLAYER_STEP_UP;

    if (object_collision != NULL) {
        ObjectCollisionTrace collision = {0};
        LevelZone destination_zone;
        uint8_t *player_slot;
        uint8_t hit_wall = 0u;

        if (!object_collision->objects || !object_collision->source_a2_words ||
            !object_runtime_get_player1_slot_bytes(
                object_collision->objects, &player_slot)) {
            player_runtime_set_error(error, error_size,
                                     "Plr1_Control has no source player collision state");
            return 0;
        }
        collision.collision_id = (uint16_t)(((uint16_t)player_slot[0u] << 8u) |
                                            player_slot[1u]);
        collision.old_x = old_x;
        collision.old_z = old_z;
        collision.new_x = new_x;
        collision.new_z = new_z;
        collision.new_y = visual_y;
        collision.thing_height = thing_height;
        collision.stood_in_top = player->stood_in_top;

        /* hires.s:Plr1_Control probes the authored destination before .noteleport. */
        if (zone.teleport_zone >= 0) {
            if ((uint16_t)zone.teleport_zone >= runtime->zone_count ||
                !level_runtime_get_zone(runtime, (uint16_t)zone.teleport_zone,
                                        &destination_zone, error, error_size)) {
                player_runtime_set_error(error, error_size,
                                         "Plr1_Control teleport destination is invalid");
                return 0;
            }
            collision.new_x = zone.teleport_x;
            collision.new_z = zone.teleport_z;
            if (!object_collision_check(
                    object_collision->objects, game_link,
                    object_collision->source_a2_words,
                    object_collision->source_a2_word_count,
                    &collision, &hit_wall, error, error_size)) {
                return 0;
            }
            if (hit_wall == 0u) {
                /*
                 * The player path does not use CheckTeleport's temporary Y
                 * probe adjustment. It preserves height above the source
                 * floor only after the destination X/Z collision succeeds.
                 */
                new_x = zone.teleport_x;
                new_z = zone.teleport_z;
                visual_y = player_runtime_add32(
                    player_runtime_sub32(visual_y, zone.floor), destination_zone.floor);
                player->snap_y = visual_y;
                player->zone_index = (uint16_t)zone.teleport_zone;
                published_new_x = new_x;
                published_new_z = new_z;
                teleported = 1;
                game_audio_events_emit(audio_events, 26, 100, new_x, new_z,
                                       UINT16_C(0xfff9), GAME_AUDIO_RESTART_SOURCE,
                                       0u, destination_zone.echo);
            }
        }

        /* A rejected teleport restores attempted movement before .noteleport. */
        if (teleported == 0) {
            collision.new_x = new_x;
            collision.new_z = new_z;
            collision.new_y = visual_y;
            /* hires.s:Plr1_Control .noteleport calls this before MoveObject. */
            if (!object_collision_check(
                    object_collision->objects, game_link,
                    object_collision->source_a2_words,
                    object_collision->source_a2_word_count,
                    &collision, &hit_wall, error, error_size)) {
                return 0;
            }
        }
        if (teleported == 0 && hit_wall != 0u) {
            /*
             * The two move.w writes restore only the integer position words;
             * the attempted snap state's fractional words keep accumulating.
             * Source newx/newz remain the attempted coordinates on this path.
             */
            new_x = old_x;
            new_z = old_z;
            object_blocked = 1;
        }
    }

    if (teleported == 0 && object_blocked == 0 && dynamic_state != NULL) {
        ObjectMovementTrace movement = {0};

        /* hires.s:Plr1_Control's .nothitanything -> objectmove.s:MoveObject. */
        movement.zone_index = player->zone_index;
        movement.old_x = old_x;
        movement.old_z = old_z;
        movement.new_x = new_x;
        movement.new_z = new_z;
        movement.old_y = visual_y;
        movement.new_y = visual_y;
        movement.thing_height = thing_height;
        movement.step_up = step_up;
        movement.step_down = PLAYER_STEP_DOWN;
        movement.extension_length = PLAYER_EDGE_EXTENSION;
        movement.wall_flags = 0x0100u;
        movement.away_from_wall = 0;
        if (!object_movement_trace(dynamic_state, &movement, error, error_size)) {
            return 0;
        }
        player->zone_index = movement.zone_index;
        player->stood_in_top = movement.stood_in_top;
        new_x = movement.new_x;
        new_z = movement.new_z;
        published_new_x = new_x;
        published_new_z = new_z;
    } else if (teleported == 0 && object_blocked == 0 && !player_runtime_move_static(
                   runtime, &player->zone_index, &player->stood_in_top, old_x, old_z,
                   visual_y, visual_y, thing_height, step_up, &new_x, &new_z, NULL,
                   error, error_size)) {
        return 0;
    } else if (teleported == 0 && object_blocked == 0) {
        published_new_x = new_x;
        published_new_z = new_z;
    }
    if (!level_runtime_get_zone(runtime, player->zone_index, &zone, error, error_size)) {
        return 0;
    }
    player->x = player_runtime_replace_position_word(player->snap_x, new_x);
    player->z = player_runtime_replace_position_word(player->snap_z, new_z);
    player->snap_x = player->x;
    player->snap_z = player->z;
    player->y = visual_y;
    player->snap_target_y = player_runtime_sub32(
        player->stood_in_top != 0u ? zone.upper_floor : zone.floor, player->height);
    /* hires.s:Plr1_Control leaves MoveObject's final newx/newz words live. */
    object_motion_runtime_set_new_words(motion_runtime, published_new_x, published_new_z);
    return 1;
}

int player_runtime_update_spatial_with_motion(
    PlayerRuntime *player, GameInput *input, const GameControls *controls,
    const GamePreferences *preferences, const GameMath *math, const LevelRuntime *runtime,
    LevelDynamicState *dynamic_state, ObjectMotionRuntime *motion_runtime,
    char *error, size_t error_size)
{
    return player_runtime_update_spatial_with_motion_and_audio(
        player, input, controls, preferences, math, runtime, dynamic_state, motion_runtime,
        NULL, NULL, NULL, error, error_size);
}

int player_runtime_update_spatial(PlayerRuntime *player, GameInput *input,
                                  const GameControls *controls,
                                  const GamePreferences *preferences,
                                  const GameMath *math,
                                  const LevelRuntime *runtime,
                                  LevelDynamicState *dynamic_state,
                                  char *error, size_t error_size)
{
    return player_runtime_update_spatial_with_motion(
        player, input, controls, preferences, math, runtime, dynamic_state, NULL,
        error, error_size);
}
