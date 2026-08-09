#ifndef AB3D2_GAME_BOOTSTRAP_H
#define AB3D2_GAME_BOOTSTRAP_H

#include <stddef.h>

#include "asset_io.h"
#include "game_controls.h"
#include "game_input.h"
#include "game_link.h"
#include "game_math.h"
#include "game_preferences.h"
#include "game_random.h"
#include "game_resources.h"
#include "game_session.h"
#include "level_bootstrap.h"
#include "level_dynamic_state.h"
#include "level_mechanisms.h"
#include "level_navigation.h"
#include "level_runtime.h"
#include "level_static_scene.h"
#include "mechanism_runtime.h"
#include "object_runtime.h"
#include "object_observation.h"
#include "player_runtime.h"
#include "scene_frame.h"

typedef struct {
    AssetBlob game_link;
    GameLink game_link_catalog;
    /* c/game_properties.c: optional global ab3:Includes/game.props. */
    AssetBlob game_properties;
    GameInventoryConsumableLimits inventory_limits;
    AssetBlob sine_table;
    GameMath math;
    GameSharedResources shared_resources;
    GameControls controls;
    GameInput input;
    GamePreferences preferences;
    /* objectmove.s:Rand1 persists across campaign-level loads. */
    GameRandom random;
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
    LevelMechanisms level_mechanisms;
    LevelNavigation level_navigation;
    LevelRuntime level_runtime;
    LevelDynamicState dynamic_level;
    MechanismRuntime mechanism_runtime;
    ObjectRuntime object_runtime;
    ObjectObservation object_observation;
    LevelStaticScene static_scene;
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
 * Single-player source order: plr_KeyboardControl, Plr1_Control, Plr1_Shot,
 * the partial ObjectHandler dispatch (collectables, activatables,
 * destructibles, and decorations in source ObjT slot order), DoorRoutine, LiftRoutine,
 * CalcPLR1InLine's object observation workspace, then a retained whole-level
 * scene refresh from the mutable graph, followed by the source single-player
 * exit-zone completion check. Alien and projectile paths remain outside this
 * focused update.
 */
int game_bootstrap_update_single_player(GameBootstrap *game,
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
