#include "game_hud.h"

#include <stdio.h>
#include <string.h>

enum {
    /* controlloop.s:DEFAULTGAME starts Plr_Health_w at this full-health value. */
    GAME_HUD_SOURCE_FULL_HEALTH = 200u,
    /* Alien Breed 3D I display.c:MAX_AMMO_RAW and three-slot display. */
    GAME_HUD_FIRST_PORT_AMMUNITION_LIMIT = 999u
};

static void game_hud_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0u) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static int game_hud_submit_value(SceneFrame *frame, uint16_t value,
                                 SceneHudFont font, SceneHudLayout layout)
{
    SceneCommand command;
    char text[4];
    int written;

    memset(&command, 0, sizeof(command));
    written = snprintf(text, sizeof(text), "%u", (unsigned)value);
    if (written < 1 || written >= (int)sizeof(text)) {
        return 0;
    }
    command.type = SCENE_COMMAND_HUD_TEXT;
    if (!scene_hud_text_set(&command.data.hud_text, text, (size_t)written)) {
        return 0;
    }
    command.data.hud_text.font = font;
    command.data.hud_text.layout = layout;
    return scene_frame_submit(frame, &command);
}

int game_hud_submit_first_port_status(const PlayerRuntime *player,
                                      const GameInventory *inventory,
                                      const GameLink *game_link,
                                      uint8_t infinite_ammo,
                                      SceneFrame *frame,
                                      char *error, size_t error_size)
{
    GameShootDefinition shoot;
    uint32_t health;
    uint32_t health_percent;
    uint32_t ammunition;

    if (!player || !inventory || !game_link || !frame ||
        player->gun_selected >= GAME_INVENTORY_WEAPON_COUNT ||
        !game_link_get_shoot_definition(game_link, player->gun_selected,
                                        &shoot, error, error_size)) {
        game_hud_set_error(error, error_size,
                           "first-port HUD has no valid selected weapon state");
        return 0;
    }
    if (shoot.bullet_type >= GAME_INVENTORY_AMMUNITION_COUNT) {
        game_hud_set_error(error, error_size,
                           "first-port HUD selected weapon has an invalid ammunition class");
        return 0;
    }

    /* Alien Breed 3D I display_hud_stats_sdl_overlay rounds health to percent. */
    health = inventory->health;
    if (health > GAME_HUD_SOURCE_FULL_HEALTH) {
        health = GAME_HUD_SOURCE_FULL_HEALTH;
    }
    health_percent = (health * 100u + GAME_HUD_SOURCE_FULL_HEALTH / 2u) /
        GAME_HUD_SOURCE_FULL_HEALTH;
    ammunition = infinite_ammo != 0u ? GAME_HUD_FIRST_PORT_AMMUNITION_LIMIT :
        inventory->ammunition[shoot.bullet_type];
    if (ammunition > GAME_HUD_FIRST_PORT_AMMUNITION_LIMIT) {
        ammunition = GAME_HUD_FIRST_PORT_AMMUNITION_LIMIT;
    }

    if (!game_hud_submit_value(
            frame, (uint16_t)health_percent,
            SCENE_HUD_FONT_FIRST_PORT_HEALTH_DIGITS,
            SCENE_HUD_LAYOUT_FIRST_PORT_HEALTH) ||
        !game_hud_submit_value(
            frame, (uint16_t)ammunition,
            SCENE_HUD_FONT_FIRST_PORT_AMMO_DIGITS,
            SCENE_HUD_LAYOUT_FIRST_PORT_AMMUNITION)) {
        game_hud_set_error(error, error_size,
                           "first-port HUD could not submit retained status text");
        return 0;
    }
    return 1;
}
