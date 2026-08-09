#ifndef AB3D2_ALIEN_DECISION_H
#define AB3D2_ALIEN_DECISION_H

#include <stddef.h>
#include <stdint.h>

#include "game_math.h"
#include "level_navigation.h"
#include "level_runtime.h"
#include "object_runtime.h"
#include "player_runtime.h"

/*
 * modules/ai.s:ai_CheckInFront.  It reads the high words of this object's
 * Vec2L point and tests the source Plr1_Tmp* snapshot against CurrentAngle.
 * `out_in_front` receives the source byte result: zero or $ff.
 */
int alien_decision_check_in_front(const ObjectRuntime *objects, uint32_t slot_index,
                                  const PlayerRuntime *player, const GameMath *math,
                                  uint8_t *out_in_front,
                                  char *error, size_t error_size);

/*
 * modules/ai.s:ai_CheckAttackOnGround.  Its source callers have already
 * cleared AI_FlyABit_w, so GetNextCPt reads the walk map.  The result is the
 * source byte result: zero or $ff; GetNextCPt's ONLYSEE result is ignored by
 * this routine just as it is in the maintained source.
 */
int alien_decision_check_attack_on_ground(const ObjectRuntime *objects,
                                          uint32_t slot_index,
                                          const LevelRuntime *level,
                                          const LevelNavigation *navigation,
                                          const PlayerRuntime *player,
                                          uint8_t *out_can_attack,
                                          char *error, size_t error_size);

#endif
