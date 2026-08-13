#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_bootstrap.h"
#include "game_quicksave.h"

typedef struct {
    uint8_t *level_bytes;
    uint8_t *graphics_bytes;
    uint8_t *object_slot_bytes;
    uint8_t *object_point_bytes;
    size_t level_size;
    size_t graphics_size;
    size_t object_slot_size;
    size_t object_point_size;
} ExpectedRuntimeBytes;

static void expected_runtime_bytes_destroy(ExpectedRuntimeBytes *expected)
{
    if (!expected) {
        return;
    }
    free(expected->level_bytes);
    free(expected->graphics_bytes);
    free(expected->object_slot_bytes);
    free(expected->object_point_bytes);
    memset(expected, 0, sizeof(*expected));
}

static int expected_runtime_bytes_capture(const GameBootstrap *game,
                                          ExpectedRuntimeBytes *expected)
{
    expected->level_size = game->dynamic_level.runtime.level_size;
    expected->graphics_size = game->dynamic_level.runtime.graphics_size;
    expected->object_slot_size = (size_t)game->object_runtime.slot_count *
        OBJECT_RUNTIME_SLOT_BYTE_COUNT;
    expected->object_point_size = (size_t)game->object_runtime.point_count *
        OBJECT_RUNTIME_POINT_BYTE_COUNT;
    expected->level_bytes = malloc(expected->level_size);
    expected->graphics_bytes = malloc(expected->graphics_size);
    expected->object_slot_bytes = malloc(expected->object_slot_size);
    expected->object_point_bytes = expected->object_point_size != 0u ?
        malloc(expected->object_point_size) : NULL;
    if (!expected->level_bytes || !expected->graphics_bytes ||
        !expected->object_slot_bytes ||
        (expected->object_point_size != 0u && !expected->object_point_bytes)) {
        expected_runtime_bytes_destroy(expected);
        return 0;
    }
    memcpy(expected->level_bytes, game->dynamic_level.level_bytes, expected->level_size);
    memcpy(expected->graphics_bytes, game->dynamic_level.graphics_bytes,
           expected->graphics_size);
    memcpy(expected->object_slot_bytes, game->object_runtime.slot_bytes,
           expected->object_slot_size);
    if (expected->object_point_size != 0u) {
        memcpy(expected->object_point_bytes, game->object_runtime.point_bytes,
               expected->object_point_size);
    }
    return 1;
}

static int expected_runtime_bytes_match(const GameBootstrap *game,
                                        const ExpectedRuntimeBytes *expected)
{
    size_t object_slot_size = (size_t)game->object_runtime.slot_count *
        OBJECT_RUNTIME_SLOT_BYTE_COUNT;
    size_t object_point_size = (size_t)game->object_runtime.point_count *
        OBJECT_RUNTIME_POINT_BYTE_COUNT;

    return game->dynamic_level.runtime.level_size == expected->level_size &&
        game->dynamic_level.runtime.graphics_size == expected->graphics_size &&
        object_slot_size == expected->object_slot_size &&
        object_point_size == expected->object_point_size &&
        memcmp(game->dynamic_level.level_bytes, expected->level_bytes,
               expected->level_size) == 0 &&
        memcmp(game->dynamic_level.graphics_bytes, expected->graphics_bytes,
               expected->graphics_size) == 0 &&
        memcmp(game->object_runtime.slot_bytes, expected->object_slot_bytes,
               expected->object_slot_size) == 0 &&
        (expected->object_point_size == 0u ||
         memcmp(game->object_runtime.point_bytes, expected->object_point_bytes,
                expected->object_point_size) == 0);
}

int main(int argc, char **argv)
{
    GameBootstrap game = {0};
    ExpectedRuntimeBytes expected_bytes = {0};
    PlayerRuntime expected_player;
    GameSession expected_session;
    MechanismRuntime expected_mechanisms;
    AlienRuntime expected_aliens;
    LightingRuntime expected_lighting;
    ObjectObservation expected_observation;
    GameProgression expected_progression;
    GameRandom expected_random;
    ObjectHandlerViewWeaponAnimationRuntime expected_weapon_animation;
    char error[256] = {0};
    FILE *invalid_file;
    int result = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <data-root> <save-path>\n", argv[0]);
        return 1;
    }
    (void)remove(argv[2]);
    if (!game_bootstrap_init(&game, argv[1], error, sizeof(error)) ||
        !game_session_select_level(&game.session, 0u, error, sizeof(error)) ||
        !game_bootstrap_start_selected_single_player(&game, argv[1],
                                                     error, sizeof(error))) {
        fprintf(stderr, "could not start quicksave fixture: %s\n", error);
        goto done;
    }
    for (uint64_t time = 20u; time <= 60u; time += 20u) {
        if (!game_bootstrap_update_single_player_at_time(&game, time,
                                                        error, sizeof(error))) {
            fprintf(stderr, "could not advance quicksave fixture: %s\n", error);
            goto done;
        }
    }

    /* Distinct, valid live values make a reset-to-level-start false positive impossible. */
    game.player.yaw = 0x0620u;
    game.player.snap_yaw = game.player.yaw;
    game.session.player1_inventory.health = 137u;
    game.player.health = 137u;
    game.mechanism_runtime.door_open_timers[0] = 73u;
    game.alien_runtime.heading_angle = 0x0440u;
    game.lighting_runtime.animation_timer = -17;
    game.progression.signal = 0x12345678u;
    game.random.state = 0x5a3cu;
    game.view_weapon_animation_runtime.held_ticks = 3u;
    game.presentation_frame = 919u;
    game.message_time_milliseconds = 12340u;
    game.audio_events.source_sample_index = 11;
    game.audio_events.source_id_register = 0x7f02u;
    game.input.mouse_y = 45;
    game.input.old_mouse_y = 39;
    if (!game_input_set_raw_key(&game.input, 0x11u, 1, error, sizeof(error))) {
        fprintf(stderr, "could not seed quicksave input state: %s\n", error);
        goto done;
    }

    expected_player = game.player;
    expected_session = game.session;
    expected_mechanisms = game.mechanism_runtime;
    expected_aliens = game.alien_runtime;
    expected_lighting = game.lighting_runtime;
    expected_observation = game.object_observation;
    expected_progression = game.progression;
    expected_random = game.random;
    expected_weapon_animation = game.view_weapon_animation_runtime;
    if (!expected_runtime_bytes_capture(&game, &expected_bytes) ||
        !game_quicksave_write(&game, argv[2], error, sizeof(error))) {
        fprintf(stderr, "could not write complete quicksave fixture: %s\n", error);
        goto done;
    }

    /* F9 must support a save from a different level, as in the first port. */
    if (!game_session_select_level(&game.session, 1u, error, sizeof(error)) ||
        !game_bootstrap_start_selected_single_player(&game, argv[1],
                                                     error, sizeof(error)) ||
        game.active_level_index != 1u ||
        !game_quicksave_load(&game, argv[1], argv[2], error, sizeof(error))) {
        fprintf(stderr, "cross-level quickload failed: %s\n", error);
        goto done;
    }
    if (game.active_level_index != 0u ||
        memcmp(&game.player, &expected_player, sizeof(expected_player)) != 0 ||
        memcmp(&game.session, &expected_session, sizeof(expected_session)) != 0 ||
        memcmp(&game.mechanism_runtime, &expected_mechanisms,
               sizeof(expected_mechanisms)) != 0 ||
        memcmp(&game.alien_runtime, &expected_aliens, sizeof(expected_aliens)) != 0 ||
        memcmp(&game.lighting_runtime, &expected_lighting,
               sizeof(expected_lighting)) != 0 ||
        memcmp(&game.object_observation, &expected_observation,
               sizeof(expected_observation)) != 0 ||
        memcmp(&game.progression, &expected_progression,
               sizeof(expected_progression)) != 0 ||
        memcmp(&game.random, &expected_random, sizeof(expected_random)) != 0 ||
        memcmp(&game.view_weapon_animation_runtime, &expected_weapon_animation,
               sizeof(expected_weapon_animation)) != 0 ||
        !expected_runtime_bytes_match(&game, &expected_bytes) ||
        game.presentation_frame != 919u ||
        game.message_time_milliseconds != 12340u ||
        game.audio_events.source_sample_index != 11 ||
        game.audio_events.source_id_register != 0x7f02u ||
        game.input.mouse_y != 45 || game.input.old_mouse_y != 39) {
        fprintf(stderr, "quickload did not restore the complete mutable source state\n");
        goto done;
    }
    for (size_t key_index = 0u; key_index < GAME_INPUT_KEY_MAP_SIZE; ++key_index) {
        if (game.input.key_map[key_index] != 0u) {
            fprintf(stderr, "quickload retained a stale held-key state\n");
            goto done;
        }
    }

    invalid_file = fopen(argv[2], "wb");
    if (!invalid_file) {
        fprintf(stderr, "could not create invalid quicksave fixture\n");
        goto done;
    }
    if (fwrite("bad", 1u, 3u, invalid_file) != 3u) {
        (void)fclose(invalid_file);
        fprintf(stderr, "could not create invalid quicksave fixture\n");
        goto done;
    }
    if (fclose(invalid_file) != 0) {
        fprintf(stderr, "could not create invalid quicksave fixture\n");
        goto done;
    }
    if (game_quicksave_load(&game, argv[1], argv[2], error, sizeof(error)) ||
        game.active_level_index != 0u) {
        fprintf(stderr, "truncated quicksave was not rejected before level mutation\n");
        goto done;
    }

    result = 0;

done:
    expected_runtime_bytes_destroy(&expected_bytes);
    game_bootstrap_destroy(&game);
    (void)remove(argv[2]);
    return result;
}
