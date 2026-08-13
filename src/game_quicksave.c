#include "game_quicksave.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GAME_QUICKSAVE_VERSION = 1u,
    GAME_QUICKSAVE_ENDIAN_TAG = 0x01020304u,
    GAME_QUICKSAVE_MAX_CHUNK_BYTES = 256u * 1024u * 1024u
};

static const char game_quicksave_magic[8] = {'A', 'B', '3', 'D', '2', 'Q', 'S', '\0'};

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t endian_tag;
    uint32_t state_size;
    uint32_t level_bytes;
    uint32_t graphics_bytes;
    uint32_t object_slot_bytes;
    uint32_t object_point_bytes;
    uint32_t extended_animation_workspace_bytes;
} GameQuicksaveHeader;

/* Pointer-free mutable state. Owned source-layout byte ranges follow it. */
typedef struct {
    uint16_t active_level_index;
    GameControls controls;
    GamePreferences preferences;
    GameProgression progression;
    GameRandom random;
    AlienRuntime alien_runtime;
    uint8_t object_animation_thistime;
    uint8_t object_animation_workspace[OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT]
                                      [OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT];
    LightingRuntime lighting_runtime;
    ObjectExplosionRuntime object_explosion_runtime;
    AlienDispatchWorkspace alien_dispatch_workspace;
    GameSession session;
    MechanismRuntime mechanism_runtime;
    ObjectObservation object_observation;
    ObjectHandlerViewWeaponAnimationRuntime view_weapon_animation_runtime;
    GameBackgroundAudioRuntime background_audio_runtime;
    PlayerRuntime player;
    PlayerHazardRuntime player_hazard_runtime;
    GameInput input;
    uint64_t message_time_milliseconds;
    uint64_t message_next_scroll_time_milliseconds;
    uint64_t message_next_duplicate_time_milliseconds;
    uint32_t presentation_frame;
    int16_t audio_source_sample_index;
    uint16_t audio_source_id_register;
} GameQuicksaveState;

typedef struct {
    GameQuicksaveHeader header;
    GameQuicksaveState state;
    uint8_t *level_bytes;
    uint8_t *graphics_bytes;
    uint8_t *object_slot_bytes;
    uint8_t *object_point_bytes;
    uint8_t *extended_animation_workspace;
} GameQuicksavePending;

static void game_quicksave_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static void game_quicksave_destroy_pending(GameQuicksavePending *pending)
{
    if (!pending) {
        return;
    }
    free(pending->level_bytes);
    free(pending->graphics_bytes);
    free(pending->object_slot_bytes);
    free(pending->object_point_bytes);
    free(pending->extended_animation_workspace);
    memset(pending, 0, sizeof(*pending));
}

static int game_quicksave_size_to_u32(size_t value, uint32_t *out_value)
{
    if (!out_value || value > UINT32_MAX) {
        return 0;
    }
    *out_value = (uint32_t)value;
    return 1;
}

static int game_quicksave_write_exact(FILE *file, const void *bytes, size_t byte_count)
{
    return byte_count == 0u ||
        (file && bytes && fwrite(bytes, 1u, byte_count, file) == byte_count);
}

static int game_quicksave_read_exact(FILE *file, void *bytes, size_t byte_count)
{
    return byte_count == 0u ||
        (file && bytes && fread(bytes, 1u, byte_count, file) == byte_count);
}

static void game_quicksave_capture_state(const GameBootstrap *game,
                                         GameQuicksaveState *state)
{
    memset(state, 0, sizeof(*state));
    state->active_level_index = game->active_level_index;
    state->controls = game->controls;
    state->preferences = game->preferences;
    state->progression = game->progression;
    state->random = game->random;
    state->alien_runtime = game->alien_runtime;
    state->object_animation_thistime = game->object_animation_runtime.thistime;
    memcpy(state->object_animation_workspace, game->object_animation_runtime.workspace,
           sizeof(state->object_animation_workspace));
    state->lighting_runtime = game->lighting_runtime;
    state->object_explosion_runtime = game->object_explosion_runtime;
    state->alien_dispatch_workspace = game->alien_dispatch_workspace;
    state->session = game->session;
    state->mechanism_runtime = game->mechanism_runtime;
    state->object_observation = game->object_observation;
    state->view_weapon_animation_runtime = game->view_weapon_animation_runtime;
    state->background_audio_runtime = game->background_audio_runtime;
    state->player = game->player;
    state->player_hazard_runtime = game->player_hazard_runtime;
    state->input = game->input;
    state->message_time_milliseconds = game->message_time_milliseconds;
    state->message_next_scroll_time_milliseconds =
        game->message_runtime.next_scroll_time_milliseconds;
    state->message_next_duplicate_time_milliseconds =
        game->message_runtime.next_duplicate_time_milliseconds;
    state->presentation_frame = game->presentation_frame;
    state->audio_source_sample_index = game->audio_events.source_sample_index;
    state->audio_source_id_register = game->audio_events.source_id_register;
}

static int game_quicksave_build_header(const GameBootstrap *game,
                                       GameQuicksaveHeader *header,
                                       char *error, size_t error_size)
{
    size_t object_slot_bytes;
    size_t object_point_bytes;
    size_t extended_workspace_bytes;

    if (!game || !header || game->active_level_index >= GAME_LINK_LEVEL_COUNT ||
        !game->dynamic_level.level_bytes || !game->dynamic_level.graphics_bytes ||
        !game->object_runtime.slot_bytes || !game->object_runtime.point_bytes) {
        game_quicksave_set_error(error, error_size,
                                 "quicksave requires an active single-player level");
        return 0;
    }
    if (game->object_runtime.slot_count > SIZE_MAX / OBJECT_RUNTIME_SLOT_BYTE_COUNT ||
        game->object_runtime.point_count > SIZE_MAX / OBJECT_RUNTIME_POINT_BYTE_COUNT ||
        game->object_animation_runtime.extended_workspace_slot_count >
            SIZE_MAX / OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT) {
        game_quicksave_set_error(error, error_size, "quicksave runtime size overflow");
        return 0;
    }
    object_slot_bytes = (size_t)game->object_runtime.slot_count *
        OBJECT_RUNTIME_SLOT_BYTE_COUNT;
    object_point_bytes = (size_t)game->object_runtime.point_count *
        OBJECT_RUNTIME_POINT_BYTE_COUNT;
    extended_workspace_bytes =
        (size_t)game->object_animation_runtime.extended_workspace_slot_count *
        OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT;
    memset(header, 0, sizeof(*header));
    memcpy(header->magic, game_quicksave_magic, sizeof(header->magic));
    header->version = GAME_QUICKSAVE_VERSION;
    header->endian_tag = GAME_QUICKSAVE_ENDIAN_TAG;
    header->state_size = (uint32_t)sizeof(GameQuicksaveState);
    if (!game_quicksave_size_to_u32(game->dynamic_level.runtime.level_size,
                                    &header->level_bytes) ||
        !game_quicksave_size_to_u32(game->dynamic_level.runtime.graphics_size,
                                    &header->graphics_bytes) ||
        !game_quicksave_size_to_u32(object_slot_bytes, &header->object_slot_bytes) ||
        !game_quicksave_size_to_u32(object_point_bytes, &header->object_point_bytes) ||
        !game_quicksave_size_to_u32(extended_workspace_bytes,
                                    &header->extended_animation_workspace_bytes)) {
        game_quicksave_set_error(error, error_size, "quicksave runtime exceeds file limits");
        return 0;
    }
    return 1;
}

int game_quicksave_write(const GameBootstrap *game, const char *path,
                         char *error, size_t error_size)
{
    GameQuicksaveHeader header;
    GameQuicksaveState state;
    FILE *file;
    int write_ok;

    if (!path || !path[0]) {
        game_quicksave_set_error(error, error_size, "quicksave path is empty");
        return 0;
    }
    if (!game_quicksave_build_header(game, &header, error, error_size)) {
        return 0;
    }
    game_quicksave_capture_state(game, &state);
    file = fopen(path, "wb");
    if (!file) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size, "could not write %s: %s", path,
                           strerror(errno));
        }
        return 0;
    }
    write_ok =
        game_quicksave_write_exact(file, &header, sizeof(header)) &&
        game_quicksave_write_exact(file, &state, sizeof(state)) &&
        game_quicksave_write_exact(file, game->dynamic_level.level_bytes,
                                   header.level_bytes) &&
        game_quicksave_write_exact(file, game->dynamic_level.graphics_bytes,
                                   header.graphics_bytes) &&
        game_quicksave_write_exact(file, game->object_runtime.slot_bytes,
                                   header.object_slot_bytes) &&
        game_quicksave_write_exact(file, game->object_runtime.point_bytes,
                                   header.object_point_bytes) &&
        game_quicksave_write_exact(file, game->object_animation_runtime.extended_workspace,
                                   header.extended_animation_workspace_bytes);
    if (fclose(file) != 0) {
        write_ok = 0;
    }
    if (!write_ok) {
        game_quicksave_set_error(error, error_size, "quicksave file write was incomplete");
        return 0;
    }
    return 1;
}

static int game_quicksave_header_is_valid(const GameQuicksaveHeader *header,
                                          long file_size,
                                          char *error, size_t error_size)
{
    uint64_t expected_size;

    if (memcmp(header->magic, game_quicksave_magic, sizeof(header->magic)) != 0) {
        game_quicksave_set_error(error, error_size, "savegame.bin has invalid quicksave magic");
        return 0;
    }
    if (header->version != GAME_QUICKSAVE_VERSION ||
        header->endian_tag != GAME_QUICKSAVE_ENDIAN_TAG ||
        header->state_size != sizeof(GameQuicksaveState)) {
        game_quicksave_set_error(error, error_size,
                                 "savegame.bin is from an incompatible port build");
        return 0;
    }
    if (header->level_bytes == 0u || header->graphics_bytes == 0u ||
        header->object_slot_bytes == 0u ||
        header->level_bytes > GAME_QUICKSAVE_MAX_CHUNK_BYTES ||
        header->graphics_bytes > GAME_QUICKSAVE_MAX_CHUNK_BYTES ||
        header->object_slot_bytes > GAME_QUICKSAVE_MAX_CHUNK_BYTES ||
        header->object_point_bytes > GAME_QUICKSAVE_MAX_CHUNK_BYTES ||
        header->extended_animation_workspace_bytes > GAME_QUICKSAVE_MAX_CHUNK_BYTES ||
        header->object_slot_bytes % OBJECT_RUNTIME_SLOT_BYTE_COUNT != 0u ||
        header->object_point_bytes % OBJECT_RUNTIME_POINT_BYTE_COUNT != 0u ||
        header->extended_animation_workspace_bytes %
                OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT != 0u) {
        game_quicksave_set_error(error, error_size,
                                 "savegame.bin contains invalid runtime sizes");
        return 0;
    }
    expected_size = sizeof(*header) + (uint64_t)header->state_size +
        header->level_bytes + header->graphics_bytes + header->object_slot_bytes +
        header->object_point_bytes + header->extended_animation_workspace_bytes;
    if (file_size < 0 || expected_size != (uint64_t)file_size) {
        game_quicksave_set_error(error, error_size,
                                 "savegame.bin is truncated or has trailing data");
        return 0;
    }
    return 1;
}

static int game_quicksave_allocate_chunk(uint8_t **out_bytes, uint32_t byte_count)
{
    if (!out_bytes) {
        return 0;
    }
    *out_bytes = byte_count != 0u ? malloc(byte_count) : NULL;
    return byte_count == 0u || *out_bytes != NULL;
}

static int game_quicksave_read_pending(const char *path, GameQuicksavePending *pending,
                                       char *error, size_t error_size)
{
    FILE *file;
    long file_size;
    int read_ok;

    memset(pending, 0, sizeof(*pending));
    file = fopen(path, "rb");
    if (!file) {
        if (error && error_size > 0u) {
            (void)snprintf(error, error_size, "could not open %s: %s", path,
                           strerror(errno));
        }
        return 0;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (file_size = ftell(file)) < 0L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        game_quicksave_set_error(error, error_size,
                                 "could not measure savegame.bin");
        return 0;
    }
    if (!game_quicksave_read_exact(file, &pending->header, sizeof(pending->header))) {
        (void)fclose(file);
        game_quicksave_set_error(error, error_size,
                                 "savegame.bin is truncated before its header");
        return 0;
    }
    if (!game_quicksave_header_is_valid(&pending->header, file_size,
                                        error, error_size)) {
        (void)fclose(file);
        return 0;
    }
    if (!game_quicksave_allocate_chunk(&pending->level_bytes,
                                       pending->header.level_bytes) ||
        !game_quicksave_allocate_chunk(&pending->graphics_bytes,
                                       pending->header.graphics_bytes) ||
        !game_quicksave_allocate_chunk(&pending->object_slot_bytes,
                                       pending->header.object_slot_bytes) ||
        !game_quicksave_allocate_chunk(&pending->object_point_bytes,
                                       pending->header.object_point_bytes) ||
        !game_quicksave_allocate_chunk(&pending->extended_animation_workspace,
                         pending->header.extended_animation_workspace_bytes)) {
        (void)fclose(file);
        game_quicksave_destroy_pending(pending);
        game_quicksave_set_error(error, error_size,
                                 "out of memory reading savegame.bin");
        return 0;
    }
    read_ok =
        game_quicksave_read_exact(file, &pending->state, sizeof(pending->state)) &&
        game_quicksave_read_exact(file, pending->level_bytes,
                                  pending->header.level_bytes) &&
        game_quicksave_read_exact(file, pending->graphics_bytes,
                                  pending->header.graphics_bytes) &&
        game_quicksave_read_exact(file, pending->object_slot_bytes,
                                  pending->header.object_slot_bytes) &&
        game_quicksave_read_exact(file, pending->object_point_bytes,
                                  pending->header.object_point_bytes) &&
        game_quicksave_read_exact(file, pending->extended_animation_workspace,
                         pending->header.extended_animation_workspace_bytes);
    if (fclose(file) != 0) {
        read_ok = 0;
    }
    if (!read_ok) {
        game_quicksave_destroy_pending(pending);
        game_quicksave_set_error(error, error_size, "savegame.bin read was incomplete");
        return 0;
    }
    if (pending->state.active_level_index >= GAME_LINK_LEVEL_COUNT ||
        pending->state.session.active_level_index != pending->state.active_level_index ||
        pending->state.session.menu_level_index >= GAME_LINK_LEVEL_COUNT) {
        game_quicksave_destroy_pending(pending);
        game_quicksave_set_error(error, error_size,
                                 "savegame.bin selects an invalid campaign level");
        return 0;
    }
    return 1;
}

static int game_quicksave_runtime_sizes_match(const GameBootstrap *game,
                                              const GameQuicksaveHeader *header,
                                              const GameQuicksaveState *state,
                                              char *error, size_t error_size)
{
    uint64_t slot_bytes = (uint64_t)game->object_runtime.slot_count *
        OBJECT_RUNTIME_SLOT_BYTE_COUNT;
    uint64_t point_bytes = (uint64_t)game->object_runtime.point_count *
        OBJECT_RUNTIME_POINT_BYTE_COUNT;

    if (game->dynamic_level.runtime.level_size != header->level_bytes ||
        game->dynamic_level.runtime.graphics_size != header->graphics_bytes ||
        slot_bytes != header->object_slot_bytes ||
        point_bytes != header->object_point_bytes ||
        state->player.zone_index >= game->dynamic_level.runtime.zone_count) {
        game_quicksave_set_error(error, error_size,
                                 "savegame.bin runtime does not match its source level assets");
        return 0;
    }
    return 1;
}

static int game_quicksave_apply_pending(GameBootstrap *game, const char *data_root,
                                        const GameQuicksavePending *pending,
                                        char *error, size_t error_size)
{
    const GameQuicksaveState *state = &pending->state;
    DesktopSettings desktop_settings = game->desktop_settings;
    size_t existing_extended_workspace_bytes;
    uint32_t saved_extended_workspace_slots =
        pending->header.extended_animation_workspace_bytes /
        OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT;

    /* Level reload reconstructs every pointer-owning immutable/native view. */
    game->session = state->session;
    if (!game_bootstrap_load_level(game, data_root, state->active_level_index,
                                   error, error_size) ||
        !game_quicksave_runtime_sizes_match(game, &pending->header, state,
                                            error, error_size)) {
        return 0;
    }
    if (!object_animation_runtime_reserve(&game->object_animation_runtime,
            OBJECT_ANIMATION_WORKSPACE_SLOT_COUNT + saved_extended_workspace_slots,
            error, error_size)) {
        return 0;
    }

    memcpy(game->dynamic_level.level_bytes, pending->level_bytes,
           pending->header.level_bytes);
    memcpy(game->dynamic_level.graphics_bytes, pending->graphics_bytes,
           pending->header.graphics_bytes);
    memcpy(game->object_runtime.slot_bytes, pending->object_slot_bytes,
           pending->header.object_slot_bytes);
    if (pending->header.object_point_bytes != 0u) {
        memcpy(game->object_runtime.point_bytes, pending->object_point_bytes,
               pending->header.object_point_bytes);
    }

    game->controls = state->controls;
    game->preferences = state->preferences;
    game->desktop_settings = desktop_settings;
    game->progression = state->progression;
    game->random = state->random;
    game->alien_runtime = state->alien_runtime;
    game->object_animation_runtime.thistime = state->object_animation_thistime;
    memcpy(game->object_animation_runtime.workspace, state->object_animation_workspace,
           sizeof(state->object_animation_workspace));
    existing_extended_workspace_bytes =
        (size_t)game->object_animation_runtime.extended_workspace_slot_count *
        OBJECT_ANIMATION_WORKSPACE_BYTE_COUNT;
    if (existing_extended_workspace_bytes != 0u) {
        memset(game->object_animation_runtime.extended_workspace, 0,
               existing_extended_workspace_bytes);
    }
    if (pending->header.extended_animation_workspace_bytes != 0u) {
        memcpy(game->object_animation_runtime.extended_workspace,
               pending->extended_animation_workspace,
               pending->header.extended_animation_workspace_bytes);
    }
    game->lighting_runtime = state->lighting_runtime;
    game->object_explosion_runtime = state->object_explosion_runtime;
    game->alien_dispatch_workspace = state->alien_dispatch_workspace;
    game->session = state->session;
    game->mechanism_runtime = state->mechanism_runtime;
    game->object_observation = state->object_observation;
    game->view_weapon_animation_runtime = state->view_weapon_animation_runtime;
    game->background_audio_runtime = state->background_audio_runtime;
    game->player = state->player;
    game->player_hazard_runtime = state->player_hazard_runtime;
    game_input_init(&game->input);
    game->input.mouse_y = state->input.mouse_y;
    game->input.old_mouse_y = state->input.old_mouse_y;
    game_audio_events_init(&game->audio_events);
    game->audio_events.source_sample_index = state->audio_source_sample_index;
    game->audio_events.source_id_register = state->audio_source_id_register;
    game->message_time_milliseconds = state->message_time_milliseconds;
    game->message_runtime.next_scroll_time_milliseconds =
        state->message_next_scroll_time_milliseconds;
    game->message_runtime.next_duplicate_time_milliseconds =
        state->message_next_duplicate_time_milliseconds;
    game->presentation_frame = state->presentation_frame;
    game->active_level_index = state->active_level_index;

    if (!level_static_scene_apply_runtime(
            &game->static_scene, &game->dynamic_level.runtime,
            game->shared_resources.wall_texture_count,
            game->level_floor_override.bytes != NULL ? game->level_floor_override.size :
                                                       game->shared_resources.floor_texture.size,
            error, error_size)) {
        return 0;
    }
    return 1;
}

int game_quicksave_load(GameBootstrap *game, const char *data_root, const char *path,
                        char *error, size_t error_size)
{
    GameQuicksavePending pending;
    int applied;

    if (!game || !data_root || !path || !path[0]) {
        game_quicksave_set_error(error, error_size,
                                 "quickload received null state, data root, or path");
        return 0;
    }
    if (!game_quicksave_read_pending(path, &pending, error, error_size)) {
        return 0;
    }
    applied = game_quicksave_apply_pending(game, data_root, &pending,
                                           error, error_size);
    game_quicksave_destroy_pending(&pending);
    return applied;
}
