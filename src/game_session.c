#include "game_session.h"

#include <stdio.h>
#include <string.h>

static void game_session_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static uint16_t game_session_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static void game_session_write_be16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);
    destination[1] = (uint8_t)value;
}

static int game_session_record_size_is_valid(size_t size, char *error, size_t error_size)
{
    if (size < GAME_SESSION_RECORD_SIZE) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size,
                           "campaign record is truncated (need %u bytes, found %zu)",
                           GAME_SESSION_RECORD_SIZE, size);
        }
        return 0;
    }
    return 1;
}

static void game_session_decode_inventory(GameInventory *inventory, const uint8_t *source)
{
    uint16_t ammunition_index;
    uint16_t weapon_index;

    inventory->health = game_session_read_be16(source);
    source += 2u;
    inventory->jetpack_fuel = game_session_read_be16(source);
    source += 2u;
    for (ammunition_index = 0; ammunition_index < GAME_SESSION_AMMUNITION_COUNT;
         ++ammunition_index) {
        inventory->ammunition[ammunition_index] = game_session_read_be16(source);
        source += 2u;
    }
    inventory->shield = game_session_read_be16(source);
    source += 2u;
    inventory->jetpack = game_session_read_be16(source);
    source += 2u;
    for (weapon_index = 0; weapon_index < GAME_SESSION_WEAPON_COUNT; ++weapon_index) {
        inventory->weapons[weapon_index] = game_session_read_be16(source);
        source += 2u;
    }
}

static void game_session_encode_inventory(uint8_t *destination, const GameInventory *inventory)
{
    uint16_t ammunition_index;
    uint16_t weapon_index;

    game_session_write_be16(destination, inventory->health);
    destination += 2u;
    game_session_write_be16(destination, inventory->jetpack_fuel);
    destination += 2u;
    for (ammunition_index = 0; ammunition_index < GAME_SESSION_AMMUNITION_COUNT;
         ++ammunition_index) {
        game_session_write_be16(destination, inventory->ammunition[ammunition_index]);
        destination += 2u;
    }
    game_session_write_be16(destination, inventory->shield);
    destination += 2u;
    game_session_write_be16(destination, inventory->jetpack);
    destination += 2u;
    for (weapon_index = 0; weapon_index < GAME_SESSION_WEAPON_COUNT; ++weapon_index) {
        game_session_write_be16(destination, inventory->weapons[weapon_index]);
        destination += 2u;
    }
}

int game_session_default(GameSession *session, const GameLink *game_link,
                         char *error, size_t error_size)
{
    GameShootDefinition shoot_definition;
    uint16_t initial_ammunition_type;

    if (!session || !game_link ||
        !game_link_get_shoot_definition(game_link, 0u, &shoot_definition,
                                        error, error_size)) {
        game_session_set_error(error, error_size, "could not read the initial GLFT shoot definition");
        return 0;
    }

    /* controlloop.s:DEFAULTGAME clears these contiguous InvCT/InvIT fields. */
    memset(session, 0, sizeof(*session));
    initial_ammunition_type = shoot_definition.bullet_type;
    if (initial_ammunition_type >= GAME_SESSION_AMMUNITION_COUNT) {
        game_session_set_error(error, error_size,
                               "initial GLFT shoot definition names an invalid ammunition type");
        return 0;
    }

    /* controlloop.s:DEFAULTGAME. */
    session->menu_level_index = 0;
    session->campaign_inventory.health = 200;
    session->campaign_inventory.weapons[0] = 0x00ff;
    session->campaign_inventory.ammunition[initial_ammunition_type] = 20;
    return 1;
}

int game_session_select_level(GameSession *session, uint16_t level_index,
                              char *error, size_t error_size)
{
    if (!session || level_index >= GAME_LINK_LEVEL_COUNT) {
        game_session_set_error(error, error_size,
                               "single-player level selection is outside the 16-level campaign");
        return 0;
    }
    /* game_ReadMainMenu:playgame copies Game_LevelCounter_w to Game_LevelNumber_w. */
    session->menu_level_index = level_index;
    return 1;
}

void game_session_begin_single_player(GameSession *session)
{
    if (!session) {
        return;
    }
    /* game_DoneMenu copies Plr_ inventory into Plr1 before Game_Begin. */
    session->player1_inventory = session->campaign_inventory;
    session->active_level_index = session->menu_level_index;
    session->level_finished = 0;
}

void game_session_finish_single_player(GameSession *session, int level_finished)
{
    if (!session) {
        return;
    }
    session->level_finished = level_finished ? 1u : 0u;
    /* game_DoneMenu only copies Plr1 inventory back when Game_FinishedLevel_b is set. */
    if (session->level_finished) {
        session->campaign_inventory = session->player1_inventory;
    }
}

int game_session_decode_campaign_record(GameSession *session, const uint8_t *bytes,
                                        size_t size, char *error, size_t error_size)
{
    uint16_t level_index;
    GameInventory inventory;

    if (!session || !bytes || !game_session_record_size_is_valid(size, error, error_size)) {
        return 0;
    }
    level_index = game_session_read_be16(bytes);
    if (level_index >= GAME_LINK_LEVEL_COUNT) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size,
                           "campaign record selects invalid level %u", level_index);
        }
        return 0;
    }
    memset(&inventory, 0, sizeof(inventory));
    game_session_decode_inventory(&inventory, bytes + 2u);

    /* controlloop.s:DEFGAME/game_LoadPosition update only Plr_ and the counter. */
    session->menu_level_index = level_index;
    session->campaign_inventory = inventory;
    return 1;
}

int game_session_encode_campaign_record(const GameSession *session, uint8_t *out_bytes,
                                        size_t out_size, char *error, size_t error_size)
{
    if (!session || !out_bytes || !game_session_record_size_is_valid(out_size, error, error_size)) {
        return 0;
    }
    if (session->menu_level_index >= GAME_LINK_LEVEL_COUNT) {
        if (error && error_size > 0) {
            (void)snprintf(error, error_size,
                           "cannot encode campaign record with invalid level %u",
                           session->menu_level_index);
        }
        return 0;
    }
    game_session_write_be16(out_bytes, session->menu_level_index);
    game_session_encode_inventory(out_bytes + 2u, &session->campaign_inventory);
    return 1;
}

int game_session_load_level_definition(GameSession *session, const GameLink *game_link,
                                       const uint8_t *definition_bytes, size_t definition_size,
                                       int definition_found, char *error, size_t error_size)
{
    if (!definition_found) {
        /* controlloop.s:DEFGAME.error_nodef branches to DEFAULTGAME. */
        return game_session_default(session, game_link, error, error_size);
    }
    return game_session_decode_campaign_record(session, definition_bytes, definition_size,
                                               error, error_size);
}
