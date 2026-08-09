#ifndef AB3D2_ASSET_IO_H
#define AB3D2_ASSET_IO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *bytes;
    size_t size;
} AssetBlob;

/* Builds an executable-local data path. Returns 1 on success. */
int asset_io_join(const char *data_root, const char *relative_path,
                  char *out_path, size_t out_path_size);

/*
 * Loads an authoritative asset in full. =SB= packed assets are unpacked using
 * the same io_HandlePacked boundary as modules/file_io.s; no replacement is
 * attempted when an asset is missing or malformed.
 */
int asset_io_load(const char *data_root, const char *relative_path,
                  AssetBlob *out_blob, char *error, size_t error_size);

/*
 * Same decoder as asset_io_load, but a missing file is an expected outcome.
 * Returns 1 for both a loaded and a missing optional asset; out_found reports
 * which happened. Any other I/O or packed-data failure returns 0.
 */
int asset_io_load_optional(const char *data_root, const char *relative_path,
                           AssetBlob *out_blob, int *out_found,
                           char *error, size_t error_size);

void asset_blob_release(AssetBlob *blob);

#endif
