#ifndef AB3D2_GAME_HUD_H
#define AB3D2_GAME_HUD_H

#include <stddef.h>
#include <stdint.h>

#include "game_inventory.h"
#include "game_link.h"
#include "player_runtime.h"
#include "scene_frame.h"

/*
 * Publish the first PC port's health/ammunition readout as GPU-neutral text.
 * Keys are deliberately absent from this slice.
 */
int game_hud_submit_first_port_status(const PlayerRuntime *player,
                                      const GameInventory *inventory,
                                      const GameLink *game_link,
                                      uint8_t infinite_ammo,
                                      SceneFrame *frame,
                                      char *error, size_t error_size);

#endif
