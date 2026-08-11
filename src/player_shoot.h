#ifndef AB3D2_PLAYER_SHOOT_H
#define AB3D2_PLAYER_SHOOT_H

#include <stddef.h>
#include <stdint.h>

#include "game_random.h"
#include "game_audio.h"
#include "game_link.h"
#include "object_movement.h"
#include "object_motion.h"
#include "object_observation.h"
#include "object_runtime.h"
#include "player_runtime.h"

/* newplayershoot.s:Plr1_Shot target candidate retained for its later fire path. */
typedef struct {
    uint8_t found;
    uint32_t slot_index;
    uint16_t point_index;
    uint16_t distance;
    int32_t vertical_difference;
    int16_t vertical_speed;
} PlayerShotTarget;

/*
 * Selects Plr1_Shot's closest in-line, visible, targetable ObjT record and
 * derives its original auto-aim vertical speed.  This has no firing, ammo,
 * random, projectile, sound, renderer, or visibility-side effects.
 */
int player_shoot_find_target_single_player(const ObjectRuntime *objects,
                                           const ObjectObservation *observation,
                                           const PlayerRuntime *player,
                                           const GameBulletDefinition *bullet,
                                           PlayerShotTarget *out_target,
                                           char *error, size_t error_size);

/*
 * newplayershoot.s:Plr1_Shot .fire_hitscanned_bullets. Advances GetRand once
 * and compares its source-shaped roll against the selected live target point.
 * The caller owns the resulting hit or miss mutation and the bullet loop.
 */
int player_shoot_hitscan_roll_is_hit(const ObjectRuntime *objects,
                                     const PlayerShotTarget *target,
                                     const PlayerRuntime *player,
                                     GameRandom *random,
                                     uint8_t *out_hit,
                                     char *error, size_t error_size);

/*
 * newplayershoot.s:Plr1_Shot. Runs the source cooldown, GLFT weapon lookup,
 * ammunition check/debit, aim choice, and hitscan/projectile dispatch once in
 * objmoveanim order. Audio playback and weapon-view rendering remain with
 * their separate unported source consumers.
 */
int player_shoot_update_single_player(ObjectRuntime *objects,
                                      LevelDynamicState *dynamic_level,
                                      const ObjectObservation *observation,
                                      PlayerRuntime *player,
                                      GameInventory *inventory,
                                      const GameLink *game_link,
                                      const GamePreferences *preferences,
                                      const GameMath *math,
                                      GameRandom *random,
                                      uint16_t frame_ticks,
                                      char *error, size_t error_size);

/* Same Plr1_Shot update with source newx/newz publication retained. */
int player_shoot_update_single_player_with_motion(
    ObjectRuntime *objects, LevelDynamicState *dynamic_level,
    const ObjectObservation *observation, PlayerRuntime *player,
    ObjectMotionRuntime *motion_runtime, GameInventory *inventory,
    const GameLink *game_link, const GamePreferences *preferences,
    const GameMath *math, GameRandom *random, uint16_t frame_ticks,
    char *error, size_t error_size);

/* Same source Plr1_Shot pass, publishing its MakeSomeNoise calls when requested. */
int player_shoot_update_single_player_with_motion_and_audio(
    ObjectRuntime *objects, LevelDynamicState *dynamic_level,
    const ObjectObservation *observation, PlayerRuntime *player,
    ObjectMotionRuntime *motion_runtime, GameInventory *inventory,
    const GameLink *game_link, const GamePreferences *preferences,
    const GameMath *math, GameRandom *random, uint16_t frame_ticks,
    uint8_t infinite_ammo, GameAudioEvents *audio_events, char *error, size_t error_size);

/*
 * newplayershoot.s:plr1_HitscanSucceded.  Creates the source impact ObjT
 * when a player-shot slot is free, then applies the source byte-sized damage
 * and impact direction to the selected target.  Hit probability, misses, and
 * projectile updates remain separate source paths.
 */
int player_shoot_apply_hitscan_success(ObjectRuntime *objects,
                                       const PlayerShotTarget *target,
                                       uint16_t bullet_type,
                                       const GameBulletDefinition *bullet,
                                       int16_t player_sine, int16_t player_cosine,
                                       uint8_t *out_impact_spawned,
                                       char *error, size_t error_size);

/*
 * newplayershoot.s:plr1_HitscanFailed. Casts the source one-word forward
 * ray repeatedly through MoveObject's zero-extension path, then creates the
 * stationary source miss effect in the first free player-shot ObjT slot.
 * This routine owns its one GetRand vertical-spread advance; hit probability,
 * cooldown/ammunition, input wiring, and its later ItsABullet dispatch remain
 * in the parent Plr1_Shot path.
 */
int player_shoot_apply_hitscan_miss(ObjectRuntime *objects,
                                    LevelDynamicState *dynamic_level,
                                    const PlayerRuntime *player, const GameMath *math,
                                    GameRandom *random, uint16_t bullet_type,
                                    uint8_t *out_impact_spawned,
                                    char *error, size_t error_size);

/* Same plr1_HitscanFailed path with source newx/newz publication retained. */
int player_shoot_apply_hitscan_miss_with_motion(
    ObjectRuntime *objects, LevelDynamicState *dynamic_level,
    const PlayerRuntime *player, const GameMath *math,
    ObjectMotionRuntime *motion_runtime, GameRandom *random, uint16_t bullet_type,
    uint8_t *out_impact_spawned, char *error, size_t error_size);

/*
 * newplayershoot.s:firefive, reached from plr1_FireProjectile after Plr1_Shot
 * has selected a non-hitscan BulT. It reserves source player-shot slots and
 * writes the complete source launch state for one or more projectiles. The
 * later ItsABullet moving-projectile update owns motion and impact handling.
 */
int player_shoot_spawn_projectile_volley(ObjectRuntime *objects, const GameMath *math,
                                         const PlayerRuntime *player,
                                         uint16_t bullet_type,
                                         const GameBulletDefinition *bullet,
                                         uint16_t bullet_count,
                                         int16_t vertical_speed,
                                         uint32_t *out_spawned_count,
                                         char *error, size_t error_size);

#endif
