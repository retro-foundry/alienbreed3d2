#include "game_audio.h"

#include <stdio.h>
#include <string.h>

#include "game_link.h"

static void game_audio_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

void game_audio_events_init(GameAudioEvents *events)
{
    if (events) {
        memset(events, 0, sizeof(*events));
    }
}

void game_audio_events_begin(GameAudioEvents *events)
{
    if (events) {
        events->count = 0u;
        events->dropped_count = 0u;
    }
}

static void game_audio_events_queue(GameAudioEvents *events, int16_t sample_index,
                                    int16_t volume, int16_t world_x, int16_t world_z,
                                    uint16_t source_id, uint8_t suppress_if_playing,
                                    uint8_t channel_pick, uint8_t echo,
                                    uint8_t listener_relative)
{
    GameAudioEvent *event;

    /* Source routines use a negative number as their no-sound sentinel. */
    if (!events || sample_index < 0 || sample_index >= GAME_LINK_SFX_LOAD_COUNT || volume <= 0) {
        return;
    }
    if (events->count >= GAME_AUDIO_EVENT_CAPACITY) {
        ++events->dropped_count;
        return;
    }
    event = &events->events[events->count++];
    event->sample_index = (uint16_t)sample_index;
    event->volume = (uint16_t)volume;
    event->world_x = world_x;
    event->world_z = world_z;
    event->source_id = source_id;
    event->suppress_if_playing = suppress_if_playing != 0u ? UINT8_MAX : 0u;
    event->channel_pick = channel_pick;
    event->echo = echo;
    event->listener_relative = listener_relative != 0u ? UINT8_MAX : 0u;
}

void game_audio_events_emit(GameAudioEvents *events, int16_t sample_index, int16_t volume,
                            int16_t world_x, int16_t world_z, uint16_t source_id,
                            uint8_t suppress_if_playing, uint8_t channel_pick, uint8_t echo)
{
    if (!events) {
        return;
    }
    /* Every explicit source caller writes Aud_SampleNum_w before MakeSomeNoise. */
    events->source_sample_index = sample_index;
    game_audio_events_queue(events, sample_index, volume, world_x, world_z, source_id,
                            suppress_if_playing, channel_pick, echo, 0u);
}

void game_audio_events_emit_relative(
    GameAudioEvents *events, int16_t sample_index, int16_t volume,
    int16_t relative_x, int16_t relative_z, uint16_t source_id,
    uint8_t suppress_if_playing, uint8_t channel_pick, uint8_t echo)
{
    if (!events) {
        return;
    }
    events->source_sample_index = sample_index;
    game_audio_events_queue(events, sample_index, volume, relative_x, relative_z, source_id,
                            suppress_if_playing, channel_pick, echo, UINT8_MAX);
}

void game_audio_events_emit_current_sample(
    GameAudioEvents *events, int16_t volume, int16_t world_x, int16_t world_z,
    uint16_t source_id, uint8_t suppress_if_playing, uint8_t channel_pick, uint8_t echo)
{
    if (!events) {
        return;
    }
    game_audio_events_queue(events, events->source_sample_index, volume, world_x, world_z,
                            source_id, suppress_if_playing, channel_pick, echo, 0u);
}

void game_background_audio_runtime_init(GameBackgroundAudioRuntime *runtime)
{
    if (runtime) {
        memset(runtime, 0, sizeof(*runtime));
    }
}

int game_background_audio_update(GameBackgroundAudioRuntime *runtime, uint16_t frame_ticks,
                                 const LevelZone *zone, const GameLink *game_link,
                                 GameRandom *random, GameAudioEvents *events,
                                 char *error, size_t error_size)
{
    uint16_t mask;
    uint16_t random_value;
    uint16_t ambient_index;
    uint16_t sample_index;
    uint16_t volume;

    if (!runtime || !zone || !game_link || !random || !events) {
        game_audio_set_error(error, error_size,
                             "BACKSFX requires source runtime, zone, GLFT, random, and audio state");
        return 0;
    }

    /* SUB.W Anim_TempFrames_w,anim_TimeToNoise_w followed by signed BGT. */
    runtime->time_to_noise = (int16_t)((uint16_t)runtime->time_to_noise - frame_ticks);
    if (runtime->time_to_noise > 0) {
        return 1;
    }

    random_value = game_random_next(random);
    runtime->time_to_noise = (int16_t)(((random_value >> 3u) & UINT16_C(127)) +
                                       UINT16_C(100));

    /*
     * BACKSFX alternates a byte offset of zero and two before reading
     * ZoneT_BackSFXMask_w. At +0 this is the authored mask; at +2 the source
     * deliberately treats DrawBackdrop/Echo as a second big-endian mask.
     */
    if (runtime->odd_even == 0u) {
        mask = zone->background_sfx_mask;
    } else if (runtime->odd_even == 2u) {
        mask = (uint16_t)(((uint16_t)zone->draw_backdrop << 8u) | zone->echo);
    } else {
        game_audio_set_error(error, error_size,
                             "BACKSFX odd/even byte offset is outside source state");
        return 0;
    }
    runtime->odd_even = (uint16_t)(UINT16_C(2) - runtime->odd_even);
    if (mask == 0u) {
        return 1;
    }

    random_value = (uint16_t)(game_random_next(random) >> 3u);
    do {
        random_value = (uint16_t)(random_value + 1u);
        ambient_index = (uint16_t)(random_value & UINT16_C(15));
    } while ((mask & (uint16_t)(UINT16_C(1) << ambient_index)) == 0u);

    if (!game_link_get_ambient_sfx(game_link, ambient_index, &sample_index,
                                   error, error_size)) {
        return 0;
    }
    volume = (uint16_t)((game_random_next(random) & UINT16_C(15)) + UINT16_C(32));
    game_audio_events_emit_relative(
        events, (int16_t)sample_index, (int16_t)volume, 0, 0,
        UINT16_C(0xfff0), GAME_AUDIO_SUPPRESS_IF_PLAYING, 0u, 0u);
    return 1;
}
