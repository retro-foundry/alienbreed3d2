#ifndef AB3D2_GAME_VBLANK_CLOCK_H
#define AB3D2_GAME_VBLANK_CLOCK_H

#include <stdint.h>

/* PAL source VBlank cadence used by hires.s:VBlankInterrupt. */
#define GAME_VBLANK_CLOCK_MILLISECONDS UINT64_C(20)

typedef struct {
    uint64_t last_host_milliseconds;
    uint32_t remainder_milliseconds;
    uint8_t initialized;
} GameVBlankClock;

/* Start (or resume) the host-to-source timing boundary at a monotonic host time. */
void game_vblank_clock_reset(GameVBlankClock *clock, uint64_t host_milliseconds);

/*
 * Return the number of 50 Hz source VBlanks elapsed since the previous host
 * sample.  Long host stalls are capped so resuming a desktop window cannot
 * force unbounded catch-up simulation.
 */
uint32_t game_vblank_clock_advance(GameVBlankClock *clock, uint64_t host_milliseconds);

/* Fraction of the current 20 ms source VBlank elapsed at the last host sample. */
float game_vblank_clock_interpolation_alpha(const GameVBlankClock *clock);

#endif
