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

void asset_blob_release(AssetBlob *blob);

#endif
