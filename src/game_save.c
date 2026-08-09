#include "game_save.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void game_save_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int game_save_slot_offset(uint16_t slot_index, size_t *out_offset,
                                 char *error, size_t error_size)
{
    if (slot_index >= GAME_SAVE_SLOT_COUNT || !out_offset) {
        game_save_set_error(error, error_size, "saved-game slot index is outside source boot.dat");
        return 0;
    }
    *out_offset = (size_t)slot_index * GAME_SESSION_RECORD_SIZE;
    return 1;
}

static uint16_t game_save_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

int game_save_load_file(GameSaveSlots *slots, const char *path,
                        char *error, size_t error_size)
{
    FILE *file;
    long file_size;
    GameSaveSlots loaded_slots;

    if (!slots || !path || !path[0]) {
        game_save_set_error(error, error_size, "saved-game file load received null state or path");
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "source boot.dat '%s' cannot be opened: %s",
                           path, strerror(errno));
        }
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        game_save_set_error(error, error_size, "could not determine source boot.dat size");
        return 0;
    }
    if ((size_t)file_size != GAME_SAVE_FILE_SIZE) {
        (void)fclose(file);
        if (error && error_size > 0) {
            (void)snprintf(error, error_size,
                           "source boot.dat must be exactly %u bytes (found %ld)",
                           GAME_SAVE_FILE_SIZE, file_size);
        }
        return 0;
    }
    if (fread(loaded_slots.bytes, 1, sizeof(loaded_slots.bytes), file) !=
        sizeof(loaded_slots.bytes)) {
        (void)fclose(file);
        game_save_set_error(error, error_size, "could not read complete source boot.dat");
        return 0;
    }
    if (fclose(file) != 0) {
        game_save_set_error(error, error_size, "could not close source boot.dat after reading");
        return 0;
    }
    *slots = loaded_slots;
    return 1;
}

int game_save_write_file(const GameSaveSlots *slots, const char *path,
                         char *error, size_t error_size)
{
    FILE *file;

    if (!slots || !path || !path[0]) {
        game_save_set_error(error, error_size, "saved-game file write received null state or path");
        return 0;
    }
    /* controlloop.s:game_SavePosition opens MODE_NEWFILE then writes SAVEGAMELEN. */
    file = fopen(path, "wb");
    if (!file) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size, "source boot.dat '%s' cannot be written: %s",
                           path, strerror(errno));
        }
        return 0;
    }
    if (fwrite(slots->bytes, 1, sizeof(slots->bytes), file) != sizeof(slots->bytes)) {
        (void)fclose(file);
        game_save_set_error(error, error_size, "could not write complete source boot.dat");
        return 0;
    }
    if (fclose(file) != 0) {
        game_save_set_error(error, error_size, "could not close source boot.dat after writing");
        return 0;
    }
    return 1;
}

int game_save_load_campaign_slot(const GameSaveSlots *slots, uint16_t slot_index,
                                 GameSession *session,
                                 char *error, size_t error_size)
{
    size_t offset;

    if (!slots || !session ||
        !game_save_slot_offset(slot_index, &offset, error, error_size)) {
        return 0;
    }
    return game_session_decode_campaign_record(session, slots->bytes + offset,
                                               GAME_SESSION_RECORD_SIZE,
                                               error, error_size);
}

int game_save_store_campaign_slot(GameSaveSlots *slots, uint16_t user_slot_index,
                                  const GameSession *session,
                                  char *error, size_t error_size)
{
    size_t offset;
    uint16_t source_slot_index;

    if (!slots || !session || user_slot_index >= GAME_SAVE_USER_SLOT_COUNT) {
        game_save_set_error(error, error_size, "saved-game menu slot is outside source boot.dat");
        return 0;
    }
    source_slot_index = (uint16_t)(user_slot_index + 1u);
    if (!game_save_slot_offset(source_slot_index, &offset, error, error_size)) {
        return 0;
    }
    return game_session_encode_campaign_record(session, slots->bytes + offset,
                                               GAME_SESSION_RECORD_SIZE,
                                               error, error_size);
}

int game_save_slot_level_index(const GameSaveSlots *slots, uint16_t slot_index,
                               uint16_t *out_level_index,
                               char *error, size_t error_size)
{
    size_t offset;

    if (!slots || !out_level_index ||
        !game_save_slot_offset(slot_index, &offset, error, error_size)) {
        return 0;
    }
    *out_level_index = game_save_read_be16(slots->bytes + offset);
    return 1;
}
