#include "game_vblank_clock.h"

#define GAME_VBLANK_CLOCK_MAX_HOST_DELTA_MILLISECONDS UINT64_C(200)

void game_vblank_clock_reset(GameVBlankClock *clock, uint64_t host_milliseconds)
{
    if (!clock) {
        return;
    }
    clock->last_host_milliseconds = host_milliseconds;
    clock->remainder_milliseconds = 0u;
    clock->initialized = 1u;
}

uint32_t game_vblank_clock_advance(GameVBlankClock *clock, uint64_t host_milliseconds)
{
    uint64_t elapsed_milliseconds;
    uint64_t accumulated_milliseconds;

    if (!clock) {
        return 0u;
    }
    if (clock->initialized == 0u) {
        game_vblank_clock_reset(clock, host_milliseconds);
        return 0u;
    }
    /* SDL_GetTicks64 is monotonic, but discard a bad sample rather than underflow. */
    if (host_milliseconds < clock->last_host_milliseconds) {
        game_vblank_clock_reset(clock, host_milliseconds);
        return 0u;
    }
    elapsed_milliseconds = host_milliseconds - clock->last_host_milliseconds;
    clock->last_host_milliseconds = host_milliseconds;
    if (elapsed_milliseconds > GAME_VBLANK_CLOCK_MAX_HOST_DELTA_MILLISECONDS) {
        elapsed_milliseconds = GAME_VBLANK_CLOCK_MAX_HOST_DELTA_MILLISECONDS;
    }
    accumulated_milliseconds = elapsed_milliseconds + clock->remainder_milliseconds;
    clock->remainder_milliseconds =
        (uint32_t)(accumulated_milliseconds % GAME_VBLANK_CLOCK_MILLISECONDS);
    return (uint32_t)(accumulated_milliseconds / GAME_VBLANK_CLOCK_MILLISECONDS);
}
