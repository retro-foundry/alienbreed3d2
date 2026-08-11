/*
 * Convert authoritative AB3D2 source audio to simple host WAV assets.
 *
 * Sound effects are loaded through asset_io_load's exact CSFX decoder
 * (modules/file_io.s:io_LoadSample).  The music module is only unpacked here;
 * ffmpeg/libopenmpt then renders the resulting ProTracker module to WAV.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "asset_io.h"
#include "game_bootstrap.h"

enum {
    /* modules/music.s:mt_init uses PAL Amiga audio period 443 for the base rate. */
    AUDIO_EXPORT_SFX_RATE = (3546895 + (443 / 2)) / 443
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static void write_u16_le(FILE *stream, uint16_t value)
{
    (void)fputc((int)(value & 0xffu), stream);
    (void)fputc((int)((value >> 8) & 0xffu), stream);
}

static void write_u32_le(FILE *stream, uint32_t value)
{
    write_u16_le(stream, (uint16_t)(value & 0xffffu));
    write_u16_le(stream, (uint16_t)(value >> 16));
}

static int write_sfx_wav(const char *path, const AssetBlob *sample,
                         char *error, size_t error_size)
{
    FILE *stream;
    size_t index;

    if (!sample || !sample->bytes || sample->size == 0 || sample->size > UINT32_MAX) {
        set_error(error, error_size, "sound effect has no valid decoded PCM data");
        return 0;
    }
    stream = fopen(path, "wb");
    if (!stream) {
        set_error(error, error_size, "could not create output WAV");
        return 0;
    }

    (void)fwrite("RIFF", 1, 4, stream);
    write_u32_le(stream, (uint32_t)(36u + sample->size));
    (void)fwrite("WAVEfmt ", 1, 8, stream);
    write_u32_le(stream, 16);
    write_u16_le(stream, 1); /* PCM */
    write_u16_le(stream, 1); /* mono */
    write_u32_le(stream, AUDIO_EXPORT_SFX_RATE);
    write_u32_le(stream, AUDIO_EXPORT_SFX_RATE);
    write_u16_le(stream, 1);
    write_u16_le(stream, 8);
    (void)fwrite("data", 1, 4, stream);
    write_u32_le(stream, (uint32_t)sample->size);
    for (index = 0; index < sample->size; ++index) {
        /* Source bytes are signed 8-bit Paula PCM; WAV U8 is biased. */
        (void)fputc((int)((uint8_t)(sample->bytes[index] + 128u)), stream);
    }
    if (fclose(stream) != 0) {
        set_error(error, error_size, "failed while writing WAV output");
        return 0;
    }
    return 1;
}

static int write_module(const char *path, const AssetBlob *module,
                        char *error, size_t error_size)
{
    FILE *stream;

    if (!module || !module->bytes || module->size == 0) {
        set_error(error, error_size, "packed music module did not decode");
        return 0;
    }
    stream = fopen(path, "wb");
    if (!stream) {
        set_error(error, error_size, "could not create decoded music module");
        return 0;
    }
    if (fwrite(module->bytes, 1, module->size, stream) != module->size || fclose(stream) != 0) {
        set_error(error, error_size, "failed while writing decoded music module");
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    GameBootstrap game;
    AssetBlob music = {0};
    char error[256];
    char path[1024];
    uint16_t index;

    if (argc != 3) {
        (void)fprintf(stderr, "usage: %s <source-data-root> <existing-output-directory>\n", argv[0]);
        return 2;
    }
    if (!game_bootstrap_init(&game, argv[1], error, sizeof(error))) {
        (void)fprintf(stderr, "failed to load source audio: %s\n", error);
        return 1;
    }
    for (index = 0; index < GAME_LINK_SFX_LOAD_COUNT; ++index) {
        const AssetBlob *sample = &game.shared_resources.sound_effects[index];
        int written;

        if (!sample->bytes || sample->size == 0) {
            continue;
        }
        written = snprintf(path, sizeof(path), "%s/sfx_%02u.wav", argv[2], (unsigned)index);
        if (written < 0 || (size_t)written >= sizeof(path) ||
            !write_sfx_wav(path, sample, error, sizeof(error))) {
            (void)fprintf(stderr, "failed to export SFX slot %u: %s\n", (unsigned)index, error);
            game_bootstrap_destroy(&game);
            return 1;
        }
    }
    if (!asset_io_load(argv[1], "music/packedtest", &music, error, sizeof(error))) {
        (void)fprintf(stderr, "failed to unpack source music: %s\n", error);
        game_bootstrap_destroy(&game);
        return 1;
    }
    {
        const int written = snprintf(path, sizeof(path), "%s/packedtest.mod", argv[2]);
        if (written < 0 || (size_t)written >= sizeof(path) ||
            !write_module(path, &music, error, sizeof(error))) {
        (void)fprintf(stderr, "failed to export music module: %s\n", error);
        asset_blob_release(&music);
        game_bootstrap_destroy(&game);
        return 1;
        }
    }
    asset_blob_release(&music);
    game_bootstrap_destroy(&game);
    return 0;
}
