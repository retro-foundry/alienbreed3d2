#ifndef AB3D2_GAME_QUICKSAVE_H
#define AB3D2_GAME_QUICKSAVE_H

#include <stddef.h>

#include "game_bootstrap.h"

#define GAME_QUICKSAVE_FILE_NAME "savegame.bin"

/*
 * First-port-compatible desktop quicksave boundary. The versioned host file
 * stores the complete mutable AB3D2 simulation state and source-layout level
 * buffers; immutable assets are reloaded through GameBootstrap on F9.
 */
int game_quicksave_write(const GameBootstrap *game, const char *path,
                         char *error, size_t error_size);
int game_quicksave_load(GameBootstrap *game, const char *data_root, const char *path,
                        char *error, size_t error_size);

#endif
