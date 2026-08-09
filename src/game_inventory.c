#include "game_inventory.h"

#include <string.h>

static uint16_t game_inventory_read_be16(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);
}

static uint16_t game_inventory_add_saturated(uint16_t left, uint16_t right, uint16_t limit)
{
    uint16_t sum = (uint16_t)(left + right);

    /* c/game_properties.c:addSaturated's two overflow tests then cap test. */
    return sum < left || sum < right || sum > limit ? limit : sum;
}

void game_inventory_default_limits(GameInventoryConsumableLimits *out_limits)
{
    if (!out_limits) {
        return;
    }
    out_limits->health = GAME_INVENTORY_DEFAULT_HEALTH_LIMIT;
    out_limits->jetpack_fuel = GAME_INVENTORY_DEFAULT_FUEL_LIMIT;
    for (uint16_t index = 0u; index < GAME_INVENTORY_AMMUNITION_COUNT; ++index) {
        out_limits->ammunition[index] = GAME_INVENTORY_DEFAULT_AMMUNITION_LIMIT;
    }
}

int game_inventory_decode_game_properties(const uint8_t *bytes, size_t size,
                                          GameInventoryConsumableLimits *out_limits)
{
    GameInventoryConsumableLimits limits;

    if (!out_limits) {
        return 0;
    }
    game_inventory_default_limits(&limits);
    /* c/game_properties.c leaves the defaults intact when Open/size/Read fails. */
    if (bytes && size >= GAME_INVENTORY_GAME_PROPERTIES_SIZE) {
        uint16_t candidate = game_inventory_read_be16(bytes + 0u);

        if (candidate < GAME_INVENTORY_UNCAPPED_LIMIT) {
            limits.health = candidate;
        }
        candidate = game_inventory_read_be16(bytes + 2u);
        if (candidate < GAME_INVENTORY_UNCAPPED_LIMIT) {
            limits.jetpack_fuel = candidate;
        }
        for (uint16_t index = 0u; index < GAME_INVENTORY_AMMUNITION_COUNT; ++index) {
            candidate = game_inventory_read_be16(bytes + 4u + (size_t)index * 2u);
            if (candidate < GAME_INVENTORY_UNCAPPED_LIMIT) {
                limits.ammunition[index] = candidate;
            }
        }
    }
    *out_limits = limits;
    return 1;
}

int game_inventory_can_collect_single_player(
    const GameInventory *inventory, const GameInventory *grant,
    const GameInventoryConsumableLimits *limits)
{
    uint16_t gives_anything = 0u;

    if (!inventory || !grant || !limits) {
        return 0;
    }
    /* c/game_properties.c returns immediately for any single-player item bit. */
    if (grant->shield != 0u || grant->jetpack != 0u) {
        return 1;
    }
    for (uint16_t index = 0u; index < GAME_INVENTORY_WEAPON_COUNT; ++index) {
        if (grant->weapons[index] != 0u) {
            return 1;
        }
    }

    gives_anything = grant->health;
    if (grant->health != 0u && inventory->health < limits->health) {
        return 1;
    }
    gives_anything = (uint16_t)(gives_anything + grant->jetpack_fuel);
    if (grant->jetpack_fuel != 0u && inventory->jetpack_fuel < limits->jetpack_fuel) {
        return 1;
    }
    for (uint16_t index = 0u; index < GAME_INVENTORY_AMMUNITION_COUNT; ++index) {
        gives_anything = (uint16_t)(gives_anything + grant->ammunition[index]);
        if (grant->ammunition[index] != 0u &&
            inventory->ammunition[index] < limits->ammunition[index]) {
            return 1;
        }
    }
    return gives_anything == 0u;
}

void game_inventory_apply_grant(GameInventory *inventory, const GameInventory *grant,
                                const GameInventoryConsumableLimits *limits)
{
    if (!inventory || !grant || !limits) {
        return;
    }
    inventory->shield = (uint16_t)(inventory->shield | grant->shield);
    inventory->jetpack = (uint16_t)(inventory->jetpack | grant->jetpack);
    for (uint16_t index = 0u; index < GAME_INVENTORY_WEAPON_COUNT; ++index) {
        inventory->weapons[index] = (uint16_t)(inventory->weapons[index] | grant->weapons[index]);
    }
    inventory->health = game_inventory_add_saturated(inventory->health, grant->health,
                                                      limits->health);
    inventory->jetpack_fuel = game_inventory_add_saturated(inventory->jetpack_fuel,
                                                            grant->jetpack_fuel,
                                                            limits->jetpack_fuel);
    for (uint16_t index = 0u; index < GAME_INVENTORY_AMMUNITION_COUNT; ++index) {
        inventory->ammunition[index] = game_inventory_add_saturated(
            inventory->ammunition[index], grant->ammunition[index], limits->ammunition[index]);
    }
}

void game_inventory_apply_limits(GameInventory *inventory,
                                 const GameInventoryConsumableLimits *limits)
{
    if (!inventory || !limits) {
        return;
    }
    if (inventory->health > limits->health) {
        inventory->health = limits->health;
    }
    if (inventory->jetpack_fuel > limits->jetpack_fuel) {
        inventory->jetpack_fuel = limits->jetpack_fuel;
    }
    for (uint16_t index = 0u; index < GAME_INVENTORY_AMMUNITION_COUNT; ++index) {
        if (inventory->ammunition[index] > limits->ammunition[index]) {
            inventory->ammunition[index] = limits->ammunition[index];
        }
    }
}
