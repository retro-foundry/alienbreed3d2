#include "asset_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sb_decompress.h"

static void asset_io_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint32_t asset_io_read_be32(const uint8_t *source)
{
    return ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
           ((uint32_t)source[2] << 8) | source[3];
}

int asset_io_join(const char *data_root, const char *relative_path,
                  char *out_path, size_t out_path_size)
{
    size_t root_length;
    int written;

    if (!data_root || !data_root[0] || !relative_path || !relative_path[0] ||
        !out_path || out_path_size == 0) {
        return 0;
    }

    root_length = strlen(data_root);
    written = snprintf(out_path, out_path_size, "%s%s%s", data_root,
                       (root_length > 0 && data_root[root_length - 1] != '/' &&
                        data_root[root_length - 1] != '\\') ? "/" : "",
                       relative_path);
    return written >= 0 && (size_t)written < out_path_size;
}

int asset_io_load(const char *data_root, const char *relative_path,
                  AssetBlob *out_blob, char *error, size_t error_size)
{
    char path[1024];
    FILE *file = NULL;
    long signed_size;
    size_t size;
    uint8_t *bytes = NULL;

    if (!out_blob) {
        asset_io_set_error(error, error_size, "asset output pointer is null");
        return 0;
    }
    out_blob->bytes = NULL;
    out_blob->size = 0;

    if (!asset_io_join(data_root, relative_path, path, sizeof(path))) {
        asset_io_set_error(error, error_size, "asset path is invalid or too long");
        return 0;
    }

    file = fopen(path, "rb");
    if (!file) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "required asset '%s' cannot be opened: %s",
                           path, strerror(errno));
        }
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0 || (signed_size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        asset_io_set_error(error, error_size, "could not determine required asset size");
        return 0;
    }

    size = (size_t)signed_size;
    bytes = malloc(size == 0 ? 1 : size);
    if (!bytes) {
        (void)fclose(file);
        asset_io_set_error(error, error_size, "out of memory while loading required asset");
        return 0;
    }

    if (size > 0 && fread(bytes, 1, size, file) != size) {
        free(bytes);
        (void)fclose(file);
        asset_io_set_error(error, error_size, "could not read complete required asset");
        return 0;
    }
    (void)fclose(file);

    /*
     * modules/file_io.s:io_LoadCommon checks this marker, then
     * io_HandlePacked invokes unLHA and returns the unpacked buffer. Keep the
     * same ownership boundary for all native consumers.
     */
    if (sb_is_compressed(bytes, size)) {
        uint32_t unpacked_size = sb_unpacked_size(bytes, size);
        uint32_t packed_size = asset_io_read_be32(bytes + 8);
        uint8_t *unpacked_bytes;
        size_t decoded_size;

        if (unpacked_size == 0 || (size_t)packed_size != size - 12u) {
            free(bytes);
            asset_io_set_error(error, error_size,
                               "packed asset declares inconsistent payload sizes");
            return 0;
        }
        unpacked_bytes = malloc(unpacked_size);
        if (!unpacked_bytes) {
            free(bytes);
            asset_io_set_error(error, error_size,
                               "out of memory while unpacking required asset");
            return 0;
        }
        /*
         * =SB= normally carries an LHA bitstream. The original archive also
         * uses the format's stored form (packed length equals output length),
         * for example LEVEL_A/twolev.clips. io_HandlePacked sends both forms
         * through unLHA; copy the stored payload directly on native hosts.
         */
        if (packed_size == unpacked_size) {
            memcpy(unpacked_bytes, bytes + 12u, unpacked_size);
            decoded_size = unpacked_size;
        } else {
            decoded_size = sb_decompress(bytes, size, unpacked_bytes, unpacked_size);
        }
        free(bytes);
        if (decoded_size != unpacked_size) {
            free(unpacked_bytes);
            if (error && error_size > 0) {
                (void)snprintf(error, error_size,
                               "packed asset could not be fully unpacked (%zu of %u bytes)",
                               decoded_size, unpacked_size);
            }
            return 0;
        }
        bytes = unpacked_bytes;
        size = decoded_size;
    }

    out_blob->bytes = bytes;
    out_blob->size = size;
    return 1;
}

void asset_blob_release(AssetBlob *blob)
{
    if (!blob) {
        return;
    }
    free(blob->bytes);
    blob->bytes = NULL;
    blob->size = 0;
}
