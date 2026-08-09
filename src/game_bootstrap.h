#ifndef AB3D2_GAME_BOOTSTRAP_H
#define AB3D2_GAME_BOOTSTRAP_H

#include <stddef.h>

#include "asset_io.h"
#include "game_link.h"
#include "level_bootstrap.h"
#include "scene_frame.h"

typedef struct {
    AssetBlob game_link;
    GameLink game_link_catalog;
    AssetBlob story_text;
    uint16_t active_level_index;
    AssetBlob level_map;
    AssetBlob level_fly_map;
    AssetBlob level_music;
    AssetBlob level_data;
    AssetBlob level_graphics;
    AssetBlob level_clips;
    LevelBootstrap level;
    LevelGraphicsBootstrap level_graphics_header;
} GameBootstrap;

/*
 * Source mapping: controlloop.s:Game_Start and hires.s:Game_Begin.
 * This establishes only source-backed resource ownership; menu, player,
 * simulation, and DrawDisplay remain to be ported as later complete slices.
 */
int game_bootstrap_init(GameBootstrap *game, const char *data_root,
                        char *error, size_t error_size);
/* Source mapping: controlloop.s:SETPLAYERS (Game_LevelNumber_w + 'a'). */
int game_bootstrap_load_level(GameBootstrap *game, const char *data_root,
                              uint16_t level_index, char *error, size_t error_size);
void game_bootstrap_destroy(GameBootstrap *game);

/* Emits diagnostic-only HUD status; it is not original game UI. */
int game_bootstrap_submit_diagnostic_frame(const GameBootstrap *game, SceneFrame *frame);

#endif
