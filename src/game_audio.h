#ifndef AB3D2_GAME_AUDIO_H
#define AB3D2_GAME_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "game_link.h"
#include "game_random.h"
#include "level_runtime.h"

/* hires.s:MakeSomeNoise has eight candidate source voices (four per side). */
enum {
    GAME_AUDIO_EVENT_CAPACITY = 128u,
    GAME_AUDIO_SOURCE_VOICE_COUNT = 8u,
    /* hires.s:notifplaying clear/set values. */
    GAME_AUDIO_RESTART_SOURCE = 0u,
    GAME_AUDIO_SUPPRESS_IF_PLAYING = UINT8_MAX
};

/*
 * One source MakeSomeNoise request, kept renderer- and host-audio-neutral.
 * Coordinates are the source map's signed world words.  The desktop boundary
 * applies listener-relative attenuation/panning only when it presents this.
 */
typedef struct {
    uint16_t sample_index;
    uint16_t volume;
    int16_t world_x;
    int16_t world_z;
    uint16_t source_id;
    /* hires.s:notifplaying: reject only when this ID already owns a voice. */
    uint8_t suppress_if_playing;
    uint8_t channel_pick;
    uint8_t echo;
} GameAudioEvent;

typedef struct {
    GameAudioEvent events[GAME_AUDIO_EVENT_CAPACITY];
    uint16_t count;
    uint16_t dropped_count;
    /* hires.s:Aud_SampleNum_w persists between MakeSomeNoise callers. */
    int16_t source_sample_index;
} GameAudioEvents;

/* bss/anim_bss.s state consumed by newanims.s:BACKSFX. */
typedef struct {
    int16_t time_to_noise;
    uint16_t odd_even;
} GameBackgroundAudioRuntime;

void game_audio_events_init(GameAudioEvents *events);
/* Call once before each source VBlank; completed events are then host-consumed. */
void game_audio_events_begin(GameAudioEvents *events);
/* Invalid/saturated source requests have no gameplay effect, matching MakeSomeNoise rejection. */
void game_audio_events_emit(GameAudioEvents *events, int16_t sample_index, int16_t volume,
                            int16_t world_x, int16_t world_z, uint16_t source_id,
                            uint8_t suppress_if_playing, uint8_t channel_pick, uint8_t echo);
/* MakeSomeNoise using the last value written to hires.s:Aud_SampleNum_w. */
void game_audio_events_emit_current_sample(
    GameAudioEvents *events, int16_t volume, int16_t world_x, int16_t world_z,
    uint16_t source_id, uint8_t suppress_if_playing, uint8_t channel_pick, uint8_t echo);

void game_background_audio_runtime_init(GameBackgroundAudioRuntime *runtime);
/* Exact BACKSFX timer, alternating zone mask, random selection, and event order. */
int game_background_audio_update(GameBackgroundAudioRuntime *runtime, uint16_t frame_ticks,
                                 const LevelZone *zone, const GameLink *game_link,
                                 GameRandom *random, GameAudioEvents *events,
                                 char *error, size_t error_size);

#endif
