#include "source_bitmap_lighting.h"

#include <stdint.h>
#include <stdio.h>

static int check_direct_palette_table(void)
{
    for (int16_t brightness = 0; brightness <= 80; ++brightness) {
        uint8_t expected = brightness == 0 ? 0u :
            brightness >= 61 ? 31u : (uint8_t)((brightness + 1) / 2);
        uint8_t actual = source_bitmap_direct_palette_row(brightness);

        if (actual != expected) {
            fprintf(stderr, "direct bitmap brightness %d selected row %u, expected %u\n",
                    brightness, actual, expected);
            return 0;
        }
    }
    if (source_bitmap_direct_palette_row(INT16_MIN) != 0u ||
        source_bitmap_direct_palette_row(-1) != 0u ||
        source_bitmap_direct_palette_row(INT16_MAX) != 31u) {
        fprintf(stderr, "direct bitmap palette endpoint clamp is inconsistent\n");
        return 0;
    }
    return 1;
}

static int check_lighted_palette_arithmetic(void)
{
    struct {
        int8_t curve;
        int16_t strongest;
        int16_t bright_to_add;
        int16_t pixel_adjustment;
        int16_t expected;
    } cases[] = {
        /* Positive BrightToAdd+willybright is discarded by .add_it_in. */
        {10, 30, 0, 30, 28},
        /* A non-positive result brightens the selected source shade. */
        {10, 30, -40, 30, 18},
        {10, 30, -3, 3, 28},
        /* .down_loop is byte arithmetic before EXT.W. */
        {120, 0, 0, 0, -88},
        {-120, 48, 0, 0, -120},
        /* ADD.W retains 68000 wraparound before the signed branch. */
        {0, 48, INT16_MAX, 1, INT16_MIN},
    };

    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        int16_t actual = source_bitmap_lighted_palette_shade(
            cases[index].curve, cases[index].strongest, cases[index].bright_to_add,
            cases[index].pixel_adjustment);

        if (actual != cases[index].expected) {
            fprintf(stderr, "lighted bitmap case %zu produced %d, expected %d\n",
                    index, actual, cases[index].expected);
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    if (source_bitmap_bright_to_add(UINT16_C(0xffec), 16) != -4 ||
        source_bitmap_bright_to_add(UINT16_C(0x7fff), 1) != INT16_MIN) {
        fprintf(stderr, "bitmap BrightToAdd source-word arithmetic is inconsistent\n");
        return 1;
    }
    if (!check_direct_palette_table() || !check_lighted_palette_arithmetic()) {
        return 1;
    }
    return 0;
}
