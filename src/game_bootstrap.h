#ifndef AB3D2_GAME_BOOTSTRAP_H
#define AB3D2_GAME_BOOTSTRAP_H

#include <stddef.h>
#include <stdint.h>

#include "alien_runtime.h"
#include "alien_dispatch.h"
#include "asset_io.h"
#include "desktop_settings.h"
#include "game_controls.h"
#include "game_audio.h"
#include "game_input.h"
#include "game_link.h"
#include "game_math.h"
#include "game_preferences.h"
#include "game_progression.h"
#include "game_random.h"
#include "game_resources.h"
#include "game_session.h"
#include "level_bootstrap.h"
#include "level_dynamic_state.h"
#include "level_mechanisms.h"
#include "level_navigation.h"
#include "level_runtime.h"
#include "level_static_scene.h"
#include "lighting_runtime.h"
#include "message_runtime.h"
#include "mechanism_runtime.h"
#include "object_runtime.h"
#include "object_animation.h"
#include "object_explosion.h"
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
    /* data/draw_data.s incbins the proportional source message spacing table. */
    AssetBlob glyph_spacing;
    GameSharedResources shared_resources;
    GameControls controls;
    /* Source MakeSomeNoise requests for the current completed VBlank. */
    GameAudioEvents audio_events;
    /* newanims.s:BACKSFX BSS persists across campaign-level loads. */
    GameBackgroundAudioRuntime background_audio_runtime;
    GameInput input;
    GamePreferences preferences;
    /* User-owned desktop configuration, applied without changing source data. */
    DesktopSettings desktop_settings;
    /* macros.i:STATS_KILL's source-owned progression subset. */
    GameProgression progression;
    /* objectmove.s:Rand1 persists across campaign-level loads. */
    GameRandom random;
    /* hires.s:Game_Begin and modules/ai.s source-owned AI storage. */
    AlienRuntime alien_runtime;
    /* bss/tables_bss.s:ObjectWorkspace_vl, consumed by DOALLANIMS and AI. */
    ObjectAnimationRuntime object_animation_runtime;
    /* Source dynamic light state; its room-brightness output feeds AI. */
    LightingRuntime lighting_runtime;
    /* newanims.s:Anim_ExplodeIntoBits source-global radius state. */
    ObjectExplosionRuntime object_explosion_runtime;
    /* ItsAnAlien's retained cross-object motion/torch globals. */
    AlienDispatchWorkspace alien_dispatch_workspace;
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
    /* c/message.c:Msg_Init/Msg_PushLine's per-level source line ring. */
    MessageRuntime message_runtime;
    /* c/message.c Sys_FrameTimeECV_q[0], supplied by the native platform boundary. */
    uint64_t message_time_milliseconds;
    /* hires.s water-frame/scroll VBlank presentation state. */
    uint32_t presentation_frame;
    LevelStaticScene static_scene;
    PlayerRuntime player;
    /* hires.s:dosomething's process-lifetime hazardous-floor timer. */
    PlayerHazardRuntime player_hazard_runtime;
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
/* Apply desktop configuration before selecting and starting a direct-play session. */
void game_bootstrap_apply_desktop_settings(GameBootstrap *game,
                                           const DesktopSettings *settings);
/*
 * Single-player source order: hires.s:DOALLANIMS, plr_KeyboardControl, Plr1_Control, Plr1_Shot,
 * ObjectHandler's source ObjT dispatch (including worry-gated ItsAnAlien), DoorRoutine, LiftRoutine,
 * CalcPLR1InLine's object observation workspace, then a retained whole-level
 * scene refresh from the mutable graph, followed by the source single-player
 * exit-zone completion check. The API-neutral renderer consumes the resulting
 * scene frame separately from source simulation.
 */
int game_bootstrap_update_single_player(GameBootstrap *game,
                                        char *error, size_t error_size);
/* Same source update with the platform's monotonic time for message EClock semantics. */
int game_bootstrap_update_single_player_at_time(GameBootstrap *game,
                                                uint64_t message_time_milliseconds,
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

/* Emits the complete gameplay scene plus GPU-neutral message/status UI commands. */
int game_bootstrap_submit_scene_frame(GameBootstrap *game, SceneFrame *frame);

#endif
