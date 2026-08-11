#ifndef AB3D2_AUDIO_SDL_H
#define AB3D2_AUDIO_SDL_H

#include <stddef.h>
#include <stdint.h>

#include "game_audio.h"
#include "player_runtime.h"

typedef struct AudioSdl AudioSdl;

/*
 * Host-only WAV presentation for GameAudioEvents. The game core owns source
 * timing, selection and state; SDL only resamples/mixes already-converted WAVs.
 */
AudioSdl *audio_sdl_create(const char *data_root, char *error, size_t error_size);
void audio_sdl_destroy(AudioSdl *audio);
int audio_sdl_is_available(const AudioSdl *audio);
void audio_sdl_set_music_enabled(AudioSdl *audio, uint8_t enabled);
void audio_sdl_consume_events(AudioSdl *audio, const GameAudioEvents *events,
                              const PlayerRuntime *listener, uint16_t listener_yaw);

#endif
