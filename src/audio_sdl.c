#include "audio_sdl.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_link.h"

enum {
    AUDIO_SDL_RATE = 48000,
    AUDIO_SDL_CHANNELS = 2,
    AUDIO_SDL_BUFFER_SAMPLES = 1024,
    AUDIO_SDL_MUSIC_GAIN = 11000
};

static const float audio_sdl_pi = 3.14159265358979323846f;

typedef struct {
    int16_t *samples;
    uint32_t frame_count;
} AudioSdlSample;

typedef struct {
    const AudioSdlSample *sample;
    uint32_t frame_index;
    uint16_t source_id;
    uint16_t priority;
    uint16_t left_gain;
    uint16_t right_gain;
} AudioSdlVoice;

struct AudioSdl {
    SDL_AudioDeviceID device;
    AudioSdlSample sound_effects[GAME_LINK_SFX_LOAD_COUNT];
    AudioSdlSample music;
    uint32_t music_frame_index;
    AudioSdlVoice voices[GAME_AUDIO_SOURCE_VOICE_COUNT];
    uint8_t music_enabled;
    uint8_t available;
};

static void audio_sdl_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int16_t audio_sdl_clamp_s16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static void audio_sdl_release_sample(AudioSdlSample *sample)
{
    if (sample) {
        SDL_free(sample->samples);
        memset(sample, 0, sizeof(*sample));
    }
}

static int audio_sdl_load_wav(const char *path, AudioSdlSample *out_sample,
                              char *error, size_t error_size)
{
    SDL_AudioSpec source_spec;
    SDL_AudioCVT converter;
    Uint8 *source_bytes = NULL;
    Uint32 source_length = 0u;
    Uint8 *converted_bytes;
    int conversion_needed;

    if (!path || !out_sample || !SDL_LoadWAV(path, &source_spec, &source_bytes, &source_length)) {
        audio_sdl_set_error(error, error_size, SDL_GetError());
        return 0;
    }
    conversion_needed = SDL_BuildAudioCVT(&converter, source_spec.format, source_spec.channels,
                                          source_spec.freq, AUDIO_S16SYS, AUDIO_SDL_CHANNELS,
                                          AUDIO_SDL_RATE);
    if (conversion_needed < 0) {
        SDL_FreeWAV(source_bytes);
        audio_sdl_set_error(error, error_size, SDL_GetError());
        return 0;
    }
    if (conversion_needed == 0) {
        converted_bytes = source_bytes;
    } else {
        size_t capacity = (size_t)source_length * (size_t)converter.len_mult;

        if (capacity == 0u || capacity > UINT32_MAX) {
            SDL_FreeWAV(source_bytes);
            audio_sdl_set_error(error, error_size, "WAV conversion output is outside host bounds");
            return 0;
        }
        converter.buf = SDL_malloc(capacity);
        if (!converter.buf) {
            SDL_FreeWAV(source_bytes);
            audio_sdl_set_error(error, error_size, "out of memory converting WAV audio");
            return 0;
        }
        memcpy(converter.buf, source_bytes, source_length);
        converter.len = (int)source_length;
        if (SDL_ConvertAudio(&converter) != 0) {
            SDL_free(converter.buf);
            SDL_FreeWAV(source_bytes);
            audio_sdl_set_error(error, error_size, SDL_GetError());
            return 0;
        }
        SDL_FreeWAV(source_bytes);
        converted_bytes = converter.buf;
        source_length = (Uint32)converter.len_cvt;
    }
    if ((source_length % (sizeof(int16_t) * AUDIO_SDL_CHANNELS)) != 0u) {
        SDL_free(converted_bytes);
        audio_sdl_set_error(error, error_size, "converted WAV is not interleaved S16 stereo");
        return 0;
    }
    out_sample->samples = (int16_t *)converted_bytes;
    out_sample->frame_count = source_length / (uint32_t)(sizeof(int16_t) * AUDIO_SDL_CHANNELS);
    return 1;
}

static void audio_sdl_callback(void *userdata, Uint8 *stream, int byte_count)
{
    AudioSdl *audio = userdata;
    int16_t *output = (int16_t *)stream;
    uint32_t frame_count = (uint32_t)byte_count / (sizeof(int16_t) * AUDIO_SDL_CHANNELS);

    memset(stream, 0, (size_t)byte_count);
    if (!audio) {
        return;
    }
    for (uint32_t frame = 0u; frame < frame_count; ++frame) {
        int32_t left = 0;
        int32_t right = 0;

        if (audio->music_enabled != 0u && audio->music.samples && audio->music.frame_count > 0u) {
            uint32_t music_index = audio->music_frame_index * AUDIO_SDL_CHANNELS;

            left += ((int32_t)audio->music.samples[music_index] * AUDIO_SDL_MUSIC_GAIN) >> 15;
            right += ((int32_t)audio->music.samples[music_index + 1u] * AUDIO_SDL_MUSIC_GAIN) >> 15;
            ++audio->music_frame_index;
            if (audio->music_frame_index >= audio->music.frame_count) {
                audio->music_frame_index = 0u;
            }
        }
        for (uint32_t voice_index = 0u; voice_index < GAME_AUDIO_SOURCE_VOICE_COUNT;
             ++voice_index) {
            AudioSdlVoice *voice = &audio->voices[voice_index];

            if (!voice->sample || voice->frame_index >= voice->sample->frame_count) {
                voice->sample = NULL;
                continue;
            }
            {
                uint32_t sample_index = voice->frame_index * AUDIO_SDL_CHANNELS;
                left += ((int32_t)voice->sample->samples[sample_index] * voice->left_gain) >> 15;
                right += ((int32_t)voice->sample->samples[sample_index + 1u] * voice->right_gain) >> 15;
            }
            ++voice->frame_index;
        }
        output[frame * AUDIO_SDL_CHANNELS] = audio_sdl_clamp_s16(left);
        output[frame * AUDIO_SDL_CHANNELS + 1u] = audio_sdl_clamp_s16(right);
    }
}

AudioSdl *audio_sdl_create(const char *data_root, char *error, size_t error_size)
{
    AudioSdl *audio;
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    char path[1024];

    if (!data_root) {
        audio_sdl_set_error(error, error_size, "audio data root is null");
        return NULL;
    }
    audio = calloc(1u, sizeof(*audio));
    if (!audio) {
        audio_sdl_set_error(error, error_size, "out of memory creating desktop audio");
        return NULL;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        /* Audio is a presentation device: absent hardware never changes source simulation. */
        audio_sdl_set_error(error, error_size, SDL_GetError());
        return audio;
    }
    SDL_zero(desired);
    desired.freq = AUDIO_SDL_RATE;
    desired.format = AUDIO_S16SYS;
    desired.channels = AUDIO_SDL_CHANNELS;
    desired.samples = AUDIO_SDL_BUFFER_SAMPLES;
    desired.callback = audio_sdl_callback;
    desired.userdata = audio;
    audio->device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (audio->device == 0u || obtained.freq != desired.freq || obtained.format != desired.format ||
        obtained.channels != desired.channels) {
        if (audio->device != 0u) {
            SDL_CloseAudioDevice(audio->device);
            audio->device = 0u;
        }
        audio_sdl_set_error(error, error_size, SDL_GetError());
        return audio;
    }
    for (uint16_t index = 0u; index < 46u; ++index) {
        int written = snprintf(path, sizeof(path), "%s/audio/sfx_%02u.wav", data_root,
                               (unsigned)index);

        if (written < 0 || (size_t)written >= sizeof(path) ||
            !audio_sdl_load_wav(path, &audio->sound_effects[index], error, error_size)) {
            audio_sdl_destroy(audio);
            return NULL;
        }
    }
    {
        int written = snprintf(path, sizeof(path), "%s/audio/packedtest.wav", data_root);

        if (written < 0 || (size_t)written >= sizeof(path) ||
            !audio_sdl_load_wav(path, &audio->music, error, error_size)) {
            audio_sdl_destroy(audio);
            return NULL;
        }
    }
    /* Game_Begin's mt_init owns the actual start; create only preloads the WAV. */
    audio->music_enabled = 0u;
    audio->available = UINT8_MAX;
    SDL_PauseAudioDevice(audio->device, 0);
    return audio;
}

void audio_sdl_destroy(AudioSdl *audio)
{
    if (!audio) {
        return;
    }
    if (audio->device != 0u) {
        SDL_CloseAudioDevice(audio->device);
    }
    for (uint16_t index = 0u; index < GAME_LINK_SFX_LOAD_COUNT; ++index) {
        audio_sdl_release_sample(&audio->sound_effects[index]);
    }
    audio_sdl_release_sample(&audio->music);
    free(audio);
}

int audio_sdl_is_available(const AudioSdl *audio)
{
    return audio && audio->available != 0u;
}

void audio_sdl_set_music_enabled(AudioSdl *audio, uint8_t enabled)
{
    if (!audio || audio->device == 0u) {
        return;
    }
    SDL_LockAudioDevice(audio->device);
    audio->music_enabled = enabled != 0u ? UINT8_MAX : 0u;
    SDL_UnlockAudioDevice(audio->device);
}

static uint16_t audio_sdl_clamp_gain(float value)
{
    if (value <= 0.0f) {
        return 0u;
    }
    if (value >= 1.0f) {
        return UINT16_MAX;
    }
    return (uint16_t)(value * (float)UINT16_MAX + 0.5f);
}

void audio_sdl_consume_events(AudioSdl *audio, const GameAudioEvents *events,
                              const PlayerRuntime *listener, uint16_t listener_yaw)
{
    int16_t listener_x;
    int16_t listener_z;
    float yaw;
    float sine;
    float cosine;

    if (!audio || !events || !listener || audio->device == 0u || audio->available == 0u) {
        return;
    }
    listener_x = player_runtime_position_to_world(listener->x);
    listener_z = player_runtime_position_to_world(listener->z);
    yaw = (float)listener_yaw * (2.0f * audio_sdl_pi / 8192.0f);
    sine = SDL_sinf(yaw);
    cosine = SDL_cosf(yaw);
    SDL_LockAudioDevice(audio->device);
    for (uint16_t event_index = 0u; event_index < events->count; ++event_index) {
        const GameAudioEvent *event = &events->events[event_index];
        const AudioSdlSample *sample;
        float delta_x;
        float delta_z;
        float depth;
        float right;
        float distance;
        float loudness;
        float pan;
        int selected_voice = -1;
        uint16_t weakest_priority = UINT16_MAX;

        if (event->sample_index >= GAME_LINK_SFX_LOAD_COUNT) {
            continue;
        }
        sample = &audio->sound_effects[event->sample_index];
        if (!sample->samples || sample->frame_count == 0u) {
            continue;
        }
        delta_x = (float)((int32_t)event->world_x - listener_x);
        delta_z = (float)((int32_t)event->world_z - listener_z);
        depth = delta_x * sine + delta_z * cosine;
        right = delta_x * cosine - delta_z * sine;
        distance = SDL_sqrtf(depth * depth + right * right);
        /* MakeSomeNoise: (Aud_NoiseVol << 6) / ((sqrt(distance) >> 2) + 1), cap 64. */
        loudness = ((float)event->volume * 64.0f) / (distance * 0.25f + 1.0f);
        if (loudness > 64.0f) {
            loudness = 64.0f;
        }
        if (loudness <= 0.0f) {
            continue;
        }
        pan = distance > 0.0f ? right / distance : 0.0f;
        if (pan < -1.0f) {
            pan = -1.0f;
        } else if (pan > 1.0f) {
            pan = 1.0f;
        }
        for (uint16_t voice_index = 0u; voice_index < GAME_AUDIO_SOURCE_VOICE_COUNT;
             ++voice_index) {
            AudioSdlVoice *voice = &audio->voices[voice_index];

            if (voice->sample && voice->frame_index < voice->sample->frame_count &&
                voice->source_id == event->source_id) {
                selected_voice = -2; /* MakeSomeNoise's SameAsMe suppression. */
                break;
            }
            if (!voice->sample || voice->frame_index >= voice->sample->frame_count) {
                selected_voice = (int)voice_index;
                weakest_priority = UINT16_MAX;
                break;
            }
            if (voice->priority < weakest_priority) {
                weakest_priority = voice->priority;
                selected_voice = (int)voice_index;
            }
        }
        if (selected_voice < 0 ||
            (weakest_priority != UINT16_MAX && loudness * 1024.0f <= weakest_priority)) {
            continue;
        }
        {
            AudioSdlVoice *voice = &audio->voices[(uint16_t)selected_voice];
            float gain = loudness / 64.0f;

            voice->sample = sample;
            voice->frame_index = 0u;
            voice->source_id = event->source_id;
            voice->priority = (uint16_t)(loudness * 1024.0f + 0.5f);
            voice->left_gain = audio_sdl_clamp_gain(gain * (1.0f - pan));
            voice->right_gain = audio_sdl_clamp_gain(gain * (1.0f + pan));
        }
    }
    SDL_UnlockAudioDevice(audio->device);
}
