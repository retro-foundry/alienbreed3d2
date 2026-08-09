#ifndef AB3D2_OBJECT_VIEWPOINT_H
#define AB3D2_OBJECT_VIEWPOINT_H

#include <stddef.h>
#include <stdint.h>

#include "game_math.h"

/* newaliencontrol.s:ViewpointToDraw's TOWARDS/RIGHT/LEFT/AWAY frame index. */
int object_viewpoint_select_frame(const GameMath *math, uint16_t current_angle,
                                  uint16_t viewer_angle, uint8_t *out_frame,
                                  char *error, size_t error_size);

#endif
