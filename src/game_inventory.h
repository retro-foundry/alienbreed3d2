#ifndef AB3D2_GAME_INVENTORY_H
#define AB3D2_GAME_INVENTORY_H

#include <stddef.h>
#include <stdint.h>

/* defs.i:InvCT and InvIT. */
#define GAME_INVENTORY_AMMUNITION_COUNT 20u
#define GAME_INVENTORY_WEAPON_COUNT 10u

/*
 * Source order is significant: controlloop.s copies these two contiguous
 * ranges and c/game_properties.c treats each range as a UWORD array.
 */
typedef struct {
    uint16_t health;
    uint16_t jetpack_fuel;
    uint16_t ammunition[GAME_INVENTORY_AMMUNITION_COUNT];
    uint16_t shield;
    uint16_t jetpack;
    uint16_t weapons[GAME_INVENTORY_WEAPON_COUNT];
} GameInventory;

/* c/player.h:InventoryConsumables, used for Game_ModProperties limits. */
typedef struct {
    uint16_t health;
    uint16_t jetpack_fuel;
    uint16_t ammunition[GAME_INVENTORY_AMMUNITION_COUNT];
} GameInventoryConsumableLimits;

/* c/game.h defaults and c/game_properties.c's uncapped sentinel. */
enum {
    GAME_INVENTORY_DEFAULT_AMMUNITION_LIMIT = 10000,
    GAME_INVENTORY_DEFAULT_HEALTH_LIMIT = 10000,
    GAME_INVENTORY_DEFAULT_FUEL_LIMIT = 250,
    GAME_INVENTORY_UNCAPPED_LIMIT = 32000,
    /* sizeof(Game_ModProperties) on the 68000: 44-byte InvCT plus two UWORDs. */
    GAME_INVENTORY_GAME_PROPERTIES_SIZE = 48
};

void game_inventory_default_limits(GameInventoryConsumableLimits *out_limits);

/*
 * c/game_properties.c:game_LoadModProperties. `bytes` may be absent or too
 * short, in which case the source retains the default limits. Achievements
 * follow the 48-byte header and are intentionally not interpreted here.
 */
int game_inventory_decode_game_properties(const uint8_t *bytes, size_t size,
                                          GameInventoryConsumableLimits *out_limits);

/* c/game_properties.c:Game_CheckInventoryLimits, single-player branch. */
int game_inventory_can_collect_single_player(
    const GameInventory *inventory, const GameInventory *grant,
    const GameInventoryConsumableLimits *limits);

/* c/game_properties.c:Game_AddToInventory / Game_ApplyInventoryLimits. */
void game_inventory_apply_grant(GameInventory *inventory, const GameInventory *grant,
                                const GameInventoryConsumableLimits *limits);
void game_inventory_apply_limits(GameInventory *inventory,
                                 const GameInventoryConsumableLimits *limits);

#endif
