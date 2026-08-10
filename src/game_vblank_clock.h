#ifndef AB3D2_GAME_VBLANK_CLOCK_H
#define AB3D2_GAME_VBLANK_CLOCK_H

#include <stdint.h>

/* PAL source VBlank cadence used by hires.s:VBlankInterrupt. */
#define GAME_VBLANK_CLOCK_MILLISECONDS UINT64_C(20)

typedef struct {
    uint64_t last_host_counter;
    /* Remainder in one-fiftieth host-counter units, always below frequency. */
    uint64_t remainder_counter_units;
    uint64_t host_counter_frequency;
    uint8_t initialized;
} GameVBlankClock;

/*
 * Start (or resume) the host-to-source timing boundary at a monotonic host
 * performance-counter sample.  `host_counter_frequency` is SDL's frequency
 * for that same counter.
 */
void game_vblank_clock_reset(GameVBlankClock *clock, uint64_t host_counter,
                             uint64_t host_counter_frequency);

/*
 * Return the number of 50 Hz source VBlanks elapsed since the previous
 * high-resolution host-counter sample. Long host stalls are capped so
 * resuming a desktop window cannot force unbounded catch-up simulation.
 */
uint32_t game_vblank_clock_advance(GameVBlankClock *clock, uint64_t host_counter);

/* Fraction of the current 20 ms source VBlank elapsed at the last host sample. */
float game_vblank_clock_interpolation_alpha(const GameVBlankClock *clock);

#endif
