#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "audio_sdl.h"

enum {
    AUDIO_SDL_TEST_FRAMES = 256u,
    AUDIO_SDL_TEST_SAMPLE_A = 0u,
    AUDIO_SDL_TEST_SAMPLE_B = 1u,
    AUDIO_SDL_TEST_VOLUME = 80,
    AUDIO_SDL_TEST_DISTANCE = 512
};

static int16_t audio_sdl_test_clamp_s16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static int audio_sdl_test_render_one(const char *data_root, uint16_t sample_index,
                                     uint16_t source_id, int16_t *out_samples)
{
    AudioSdl *audio;
    GameAudioEvents events;
    PlayerRuntime listener = {0};
    char error[256] = {0};
    uint16_t scheduled;

    audio = audio_sdl_create(data_root, error, sizeof(error));
    if (!audio || !audio_sdl_is_available(audio)) {
        fprintf(stderr, "desktop test mixer is unavailable: %s\n", error);
        audio_sdl_destroy(audio);
        return 0;
    }
    game_audio_events_init(&events);
    game_audio_events_emit(&events, (int16_t)sample_index, AUDIO_SDL_TEST_VOLUME,
                           0, AUDIO_SDL_TEST_DISTANCE, source_id,
                           GAME_AUDIO_RESTART_SOURCE, 0u, 0u);
    scheduled = audio_sdl_consume_events(audio, &events, &listener, 0u);
    if (scheduled != 1u) {
        fprintf(stderr, "single source request did not acquire a mixer voice\n");
        audio_sdl_destroy(audio);
        return 0;
    }
    audio_sdl_test_mix(audio, out_samples, AUDIO_SDL_TEST_FRAMES);
    audio_sdl_destroy(audio);
    return 1;
}

int main(int argc, char **argv)
{
    AudioSdl *audio;
    GameAudioEvents events;
    PlayerRuntime listener = {0};
    int16_t first[AUDIO_SDL_TEST_FRAMES * 2u];
    int16_t second[AUDIO_SDL_TEST_FRAMES * 2u];
    int16_t combined[AUDIO_SDL_TEST_FRAMES * 2u];
    char error[256] = {0};
    uint64_t energy = 0u;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <data-root>\n", argv[0]);
        return 1;
    }
    memset(first, 0, sizeof(first));
    memset(second, 0, sizeof(second));
    memset(combined, 0, sizeof(combined));
    if (!audio_sdl_test_render_one(argv[1], AUDIO_SDL_TEST_SAMPLE_A, 1u, first) ||
        !audio_sdl_test_render_one(argv[1], AUDIO_SDL_TEST_SAMPLE_B, 2u, second)) {
        return 1;
    }

    audio = audio_sdl_create(argv[1], error, sizeof(error));
    if (!audio || !audio_sdl_is_available(audio)) {
        fprintf(stderr, "desktop overlap mixer is unavailable: %s\n", error);
        audio_sdl_destroy(audio);
        return 1;
    }
    game_audio_events_init(&events);
    game_audio_events_emit(&events, AUDIO_SDL_TEST_SAMPLE_A, AUDIO_SDL_TEST_VOLUME,
                           0, AUDIO_SDL_TEST_DISTANCE, 1u,
                           GAME_AUDIO_RESTART_SOURCE, 0u, 0u);
    game_audio_events_emit(&events, AUDIO_SDL_TEST_SAMPLE_B, AUDIO_SDL_TEST_VOLUME,
                           0, AUDIO_SDL_TEST_DISTANCE, 2u,
                           GAME_AUDIO_RESTART_SOURCE, 0u, 0u);
    if (audio_sdl_consume_events(audio, &events, &listener, 0u) != 2u) {
        fprintf(stderr, "two distinct source effects did not receive overlapping voices\n");
        audio_sdl_destroy(audio);
        return 1;
    }
    audio_sdl_test_mix(audio, combined, AUDIO_SDL_TEST_FRAMES);
    for (size_t sample = 0u; sample < sizeof(combined) / sizeof(combined[0u]); ++sample) {
        int16_t expected = audio_sdl_test_clamp_s16((int32_t)first[sample] + second[sample]);

        if (combined[sample] != expected) {
            fprintf(stderr, "overlap output differs from the saturated sum at sample %zu\n", sample);
            audio_sdl_destroy(audio);
            return 1;
        }
        energy += (uint64_t)llabs((long long)combined[sample]);
    }
    if (energy == 0u) {
        fprintf(stderr, "overlap mixer produced only silence\n");
        audio_sdl_destroy(audio);
        return 1;
    }

    game_audio_events_begin(&events);
    game_audio_events_emit(&events, AUDIO_SDL_TEST_SAMPLE_A, AUDIO_SDL_TEST_VOLUME,
                           0, AUDIO_SDL_TEST_DISTANCE, 1u,
                           GAME_AUDIO_RESTART_SOURCE, 0u, 0u);
    if (audio_sdl_consume_events(audio, &events, &listener, 0u) != 1u) {
        fprintf(stderr, "notifplaying-clear source request did not restart its voice\n");
        audio_sdl_destroy(audio);
        return 1;
    }
    game_audio_events_begin(&events);
    game_audio_events_emit(&events, AUDIO_SDL_TEST_SAMPLE_A, AUDIO_SDL_TEST_VOLUME,
                           0, AUDIO_SDL_TEST_DISTANCE, 1u,
                           GAME_AUDIO_SUPPRESS_IF_PLAYING, 0u, 0u);
    if (audio_sdl_consume_events(audio, &events, &listener, 0u) != 0u) {
        fprintf(stderr, "notifplaying source request incorrectly restarted an active voice\n");
        audio_sdl_destroy(audio);
        return 1;
    }
    audio_sdl_destroy(audio);
    return 0;
}
