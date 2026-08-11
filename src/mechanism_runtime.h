#ifndef AB3D2_MECHANISM_RUNTIME_H
#define AB3D2_MECHANISM_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "level_dynamic_state.h"
#include "level_mechanisms.h"
#include "game_audio.h"
#include "player_runtime.h"

/* newanims.s:DoorRoutine's persistent non-level-data globals. */
typedef struct {
    uint16_t door_open_timers[LEVEL_MECHANISMS_MAX_DOORS];
    int16_t lift_heights[LEVEL_MECHANISMS_MAX_LIFTS];
    uint16_t current_door_state;
    /* High/low words of bss/anim_bss.s:Anim_DoorAndLiftLocks_l. */
    uint16_t door_and_lift_locks;
    uint16_t lift_only_locks;
} MechanismRuntime;

void mechanism_runtime_init(MechanismRuntime *runtime);

/*
 * Single-player newanims.s:DoorRoutine. This mutates only the cloned source
 * ZoneT, EdgeT, ZLiftableT, and door-graphics records; it emits no pixels or
 * sounds. frame_ticks is Anim_TempFrames_w for this simulation update.
 */
int mechanism_runtime_update_doors_single_player(MechanismRuntime *runtime,
                                                 LevelDynamicState *dynamic_level,
                                                 const LevelMechanisms *mechanisms,
                                                 const PlayerRuntime *player,
                                                 uint16_t frame_ticks,
                                                 char *error, size_t error_size);

/* Same DoorRoutine update with its source one-based liftable SFX requests. */
int mechanism_runtime_update_doors_single_player_with_audio(
    MechanismRuntime *runtime, LevelDynamicState *dynamic_level,
    const LevelMechanisms *mechanisms, const PlayerRuntime *player,
    uint16_t frame_ticks, GameAudioEvents *audio_events,
    char *error, size_t error_size);

/* Single-player newanims.s:LiftRoutine, including its trailing DoWaterAnims pass. */
int mechanism_runtime_update_lifts_single_player(MechanismRuntime *runtime,
                                                 LevelDynamicState *dynamic_level,
                                                 const LevelMechanisms *mechanisms,
                                                 PlayerRuntime *player,
                                                 uint16_t frame_ticks,
                                                 char *error, size_t error_size);

/* Same LiftRoutine update with its source one-based liftable SFX requests. */
int mechanism_runtime_update_lifts_single_player_with_audio(
    MechanismRuntime *runtime, LevelDynamicState *dynamic_level,
    const LevelMechanisms *mechanisms, PlayerRuntime *player,
    uint16_t frame_ticks, GameAudioEvents *audio_events,
    char *error, size_t error_size);

/* Standalone source DoWaterAnims pass used after LiftRoutine's 999 terminator. */
int mechanism_runtime_update_water_animations(LevelDynamicState *dynamic_level,
                                              const LevelMechanisms *mechanisms,
                                              uint16_t frame_ticks,
                                              char *error, size_t error_size);

#endif
