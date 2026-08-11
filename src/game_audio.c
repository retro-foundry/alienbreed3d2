#include "game_audio.h"

#include <string.h>

#include "game_link.h"

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

void game_audio_events_emit(GameAudioEvents *events, int16_t sample_index, int16_t volume,
                            int16_t world_x, int16_t world_z, uint16_t source_id,
                            uint8_t channel_pick, uint8_t echo)
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
    event->channel_pick = channel_pick;
    event->echo = echo;
}
