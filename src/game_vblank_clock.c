#include "game_vblank_clock.h"

#define GAME_VBLANK_CLOCK_PER_SECOND UINT64_C(50)
/* Match the former 200 ms recovery cap without quantising regular samples. */
#define GAME_VBLANK_CLOCK_MAX_HOST_DELTA_DIVISOR UINT64_C(5)

void game_vblank_clock_reset(GameVBlankClock *clock, uint64_t host_counter,
                             uint64_t host_counter_frequency)
{
    if (!clock) {
        return;
    }
    clock->last_host_counter = host_counter;
    clock->remainder_counter_units = 0u;
    clock->host_counter_frequency = host_counter_frequency;
    clock->initialized = host_counter_frequency != 0u ? 1u : 0u;
}

uint32_t game_vblank_clock_advance(GameVBlankClock *clock, uint64_t host_counter)
{
    uint64_t elapsed_counter;
    uint64_t max_elapsed_counter;
    uint64_t accumulated_counter_units;

    if (!clock) {
        return 0u;
    }
    if (clock->initialized == 0u) {
        return 0u;
    }
    /* SDL's performance counter is monotonic; discard a bad sample rather than underflow. */
    if (host_counter < clock->last_host_counter) {
        game_vblank_clock_reset(clock, host_counter, clock->host_counter_frequency);
        return 0u;
    }
    elapsed_counter = host_counter - clock->last_host_counter;
    clock->last_host_counter = host_counter;
    max_elapsed_counter = clock->host_counter_frequency /
        GAME_VBLANK_CLOCK_MAX_HOST_DELTA_DIVISOR;
    if (elapsed_counter > max_elapsed_counter) {
        elapsed_counter = max_elapsed_counter;
    }
    /*
     * hires.s:dosomething executes once per source VBlank. Keep the source
     * 50 Hz accumulator in the native counter's units, so 120/144 Hz present
     * samples cannot quantise or accelerate the companion action sequence.
     */
    accumulated_counter_units = elapsed_counter * GAME_VBLANK_CLOCK_PER_SECOND +
        clock->remainder_counter_units;
    clock->remainder_counter_units = accumulated_counter_units % clock->host_counter_frequency;
    return (uint32_t)(accumulated_counter_units / clock->host_counter_frequency);
}

float game_vblank_clock_interpolation_alpha(const GameVBlankClock *clock)
{
    if (!clock || clock->initialized == 0u) {
        return 0.0f;
    }
    return (float)clock->remainder_counter_units /
        (float)clock->host_counter_frequency;
}
