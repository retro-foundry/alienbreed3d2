#include "player_runtime.h"

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

/* move.w source,destination where destination is the low word of a long. */
static int32_t player_runtime_replace_low_word(int32_t value, int16_t low_word)
{
    return (int32_t)(((uint32_t)value & UINT32_C(0xffff0000)) | (uint16_t)low_word);
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
    quotient = dividend / divisor;
    if (quotient < INT16_MIN || quotient > INT16_MAX) {
        player_runtime_set_error(error, error_size,
                                 "source collision DIVS result is outside its word destination");
        return 0;
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
    player.x = level->player1_start_x;
    player.z = level->player1_start_z;
    player.snap_x = level->player1_start_x;
    player.snap_z = level->player1_start_z;
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

static int player_runtime_apply_fall(PlayerRuntime *player, const GameInput *input,
                                     const GameControls *controls,
                                     const LevelRuntime *runtime,
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
        /* LiftRoutine's signed word speed is applied as a 32-bit << 6 here. */
        velocity = (int32_t)player->floor_speed * 64;
        player->decelerate = UINT8_MAX;
        player->bobble = game_math_wrap_angle_address(
            (uint16_t)((uint32_t)player->bobble + (uint16_t)player->add_to_bobble));
        if (game_input_is_control_down(input, controls, GAME_CONTROL_JUMP) &&
            player->health != 0u) {
            velocity = -1024;
        }
        if (velocity > 0) {
            velocity = 0;
        }
        y = player_runtime_add32(y, velocity);
    } else {
        player->decelerate = player_runtime_sub32(target_y, y) <= 16 * 64 ? UINT8_MAX : 0u;
        y = player_runtime_add32(y, velocity);
        if (target_y > y) {
            velocity = player_runtime_add32(velocity, 64);
            if (velocity >= 512) {
                velocity = 512;
            }
        } else {
            velocity = 0;
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
        strafe = move_speed;
    }
    if (game_input_is_control_down(input, controls, strafe_right_binding)) {
        strafe = (int16_t)-move_speed;
    }
    forward = 0;
    if (game_input_is_control_down(input, controls, GAME_CONTROL_FORWARDS)) {
        forward = (int16_t)-move_speed;
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
        player->aim_speed = player_runtime_sub32(player->aim_speed, 512);
        look_offset = (int16_t)((int32_t)look_offset - PLAYER_SMALL_VIEW_KEY_LOOK);
        if (look_offset <= -PLAYER_SMALL_VIEW_LOOK_LIMIT) {
            player->aim_speed = -PLAYER_AIM_SPEED_LIMIT;
            look_offset = -PLAYER_SMALL_VIEW_LOOK_LIMIT;
        }
    }
    if (game_input_is_control_down(input, controls, GAME_CONTROL_LOOK_DOWN)) {
        player->aim_speed = player_runtime_add32(player->aim_speed, 512);
        look_offset = (int16_t)((int32_t)look_offset + PLAYER_SMALL_VIEW_KEY_LOOK);
        if (look_offset >= PLAYER_SMALL_VIEW_LOOK_LIMIT) {
            player->aim_speed = PLAYER_AIM_SPEED_LIMIT;
            look_offset = PLAYER_SMALL_VIEW_LOOK_LIMIT;
        }
    }
    if (game_input_is_control_down(input, controls, GAME_CONTROL_CENTRE_VIEW)) {
        if (player->previous_centre_view_key_state == 0u) {
            player->previous_centre_view_key_state = UINT8_MAX;
            player->aim_speed = 0;
            look_offset = 0;
        }
    } else {
        player->previous_centre_view_key_state = 0u;
    }
    player->look_offset = look_offset;
}

int player_runtime_update_spatial(PlayerRuntime *player, const GameInput *input,
                                  const GameControls *controls,
                                  const GamePreferences *preferences,
                                  const GameMath *math,
                                  const LevelRuntime *runtime,
                                  LevelDynamicState *dynamic_state,
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
    player_runtime_update_keyboard_look(player, input, controls);
    if (!player_runtime_update_keyboard_motion(player, input, controls, preferences, math,
                                               error, error_size) ||
        !player_runtime_apply_fall(player, input, controls, runtime, error, error_size) ||
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

    old_x = player_runtime_low_word(player->x);
    old_z = player_runtime_low_word(player->z);
    new_x = player_runtime_low_word(player->snap_x);
    new_z = player_runtime_low_word(player->snap_z);
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

    /* hires.s:Plr1_Control's teleport path precedes static MoveObject. */
    if (zone.teleport_zone >= 0) {
        LevelZone destination;
        int32_t relative_y = player_runtime_sub32(visual_y, zone.floor);

        if (!level_runtime_get_zone(runtime, (uint16_t)zone.teleport_zone, &destination,
                                    error, error_size)) {
            return 0;
        }
        player->zone_index = (uint16_t)zone.teleport_zone;
        player->x = player_runtime_replace_low_word(player->snap_x, zone.teleport_x);
        player->z = player_runtime_replace_low_word(player->snap_z, zone.teleport_z);
        player->snap_x = player->x;
        player->snap_z = player->z;
        player->y = player_runtime_add32(destination.floor, relative_y);
        player->snap_y = player->y;
        player->snap_target_y = player->y;
        player->snap_target_y = player_runtime_sub32(destination.floor, player->height);
        return 1;
    }

    if (!player_runtime_move_static(runtime, &player->zone_index, &player->stood_in_top,
                                    old_x, old_z, visual_y, visual_y, thing_height, step_up,
                                    &new_x, &new_z, dynamic_state, error, error_size) ||
        !level_runtime_get_zone(runtime, player->zone_index, &zone, error, error_size)) {
        return 0;
    }
    player->x = player_runtime_replace_low_word(player->snap_x, new_x);
    player->z = player_runtime_replace_low_word(player->snap_z, new_z);
    player->snap_x = player->x;
    player->snap_z = player->z;
    player->y = visual_y;
    player->snap_target_y = player_runtime_sub32(
        player->stood_in_top != 0u ? zone.upper_floor : zone.floor, player->height);
    return 1;
}
