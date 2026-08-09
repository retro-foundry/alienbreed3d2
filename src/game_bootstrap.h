#ifndef AB3D2_GAME_BOOTSTRAP_H
#define AB3D2_GAME_BOOTSTRAP_H

#include <stddef.h>

#include "asset_io.h"
#include "game_link.h"
#include "game_resources.h"
#include "game_session.h"
#include "level_bootstrap.h"
#include "level_runtime.h"
#include "player_runtime.h"
#include "scene_frame.h"

typedef struct {
    AssetBlob game_link;
    GameLink game_link_catalog;
    GameSharedResources shared_resources;
    GameSession session;
    AssetBlob story_text;
    uint16_t active_level_index;
    AssetBlob level_map;
    AssetBlob level_fly_map;
    AssetBlob level_music;
    AssetBlob level_data;
    AssetBlob level_graphics;
    AssetBlob level_clips;
    /* modules/res.s:Res_LoadLevelData optional per-level source overrides. */
    AssetBlob level_floor_override;
    AssetBlob level_property_overrides;
    AssetBlob level_errata;
    AssetBlob level_wall_overrides[GAME_LINK_WALL_COUNT];
    LevelBootstrap level;
    LevelGraphicsBootstrap level_graphics_header;
    LevelRuntime level_runtime;
    PlayerRuntime player;
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
/* game_ReadMainMenu:playgame followed by game_DoneMenu and Game_Begin. */
int game_bootstrap_start_selected_single_player(GameBootstrap *game, const char *data_root,
                                                char *error, size_t error_size);
/*
 * controlloop.s:levelMenu/DEFGAME. This reads the selected level's optional
 * deflev.dat record; when it is absent, the source resets to DEFAULTGAME.
 * It changes menu/campaign state only and does not enter a level.
 */
int game_bootstrap_load_level_definition(GameBootstrap *game, const char *data_root,
                                         uint16_t selected_level_index,
                                         char *error, size_t error_size);
void game_bootstrap_destroy(GameBootstrap *game);

/* Emits diagnostic-only HUD status; it is not original game UI. */
int game_bootstrap_submit_diagnostic_frame(const GameBootstrap *game, SceneFrame *frame);

#endif
