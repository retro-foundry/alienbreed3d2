#ifndef AB3D2_GAME_AUDIO_H
#define AB3D2_GAME_AUDIO_H

#include <stdint.h>

/* hires.s:MakeSomeNoise has eight candidate source voices (four per side). */
enum {
    GAME_AUDIO_EVENT_CAPACITY = 128u,
    GAME_AUDIO_SOURCE_VOICE_COUNT = 8u
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
    uint8_t channel_pick;
    uint8_t echo;
} GameAudioEvent;

typedef struct {
    GameAudioEvent events[GAME_AUDIO_EVENT_CAPACITY];
    uint16_t count;
    uint16_t dropped_count;
} GameAudioEvents;

void game_audio_events_init(GameAudioEvents *events);
/* Call once before each source VBlank; completed events are then host-consumed. */
void game_audio_events_begin(GameAudioEvents *events);
/* Invalid/saturated source requests have no gameplay effect, matching MakeSomeNoise rejection. */
void game_audio_events_emit(GameAudioEvents *events, int16_t sample_index, int16_t volume,
                            int16_t world_x, int16_t world_z, uint16_t source_id,
                            uint8_t channel_pick, uint8_t echo);

#endif
