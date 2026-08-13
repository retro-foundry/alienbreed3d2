#include "source_bitmap_lighting.h"

static int16_t source_bitmap_add16(int16_t left, int16_t right)
{
    return (int16_t)((uint16_t)left + (uint16_t)right);
}

int16_t source_bitmap_bright_to_add(uint16_t object_brightness,
                                    int16_t source_depth_term)
{
    /* draw_Bitmap: ASR.W #6,d6 followed by ADD.W (a0)+,d6. */
    return (int16_t)(object_brightness + (uint16_t)source_depth_term);
}

uint8_t source_bitmap_direct_palette_row(int16_t bright_to_add)
{
    /*
     * draw_ObjScaleCols_vw starts with row zero, repeats rows 1..30 twice,
     * then repeats row 31 for the remainder of the source-visible range.
     */
    if (bright_to_add <= 0) {
        return 0u;
    }
    if (bright_to_add >= 61) {
        return 31u;
    }
    return (uint8_t)(((uint16_t)bright_to_add + 1u) / 2u);
}

int16_t source_bitmap_lighted_palette_shade(int8_t directional_curve_value,
                                            int16_t strongest_light,
                                            int16_t bright_to_add,
                                            int16_t authored_pixel_adjustment)
{
    int8_t source_byte;
    int16_t shade;
    int16_t adjustment;

    /*
     * draw_bitmap_lighted:.down_loop adds `48 - strongest` with ADD.B and
     * then sign-extends the wrapped byte into willy.
     */
    source_byte = (int8_t)((uint8_t)directional_curve_value +
                           (uint8_t)(48 - strongest_light));
    shade = source_byte;

    /*
     * .add_it_in retains BrightToAdd+willybright only when the signed word
     * is non-positive. A positive result is explicitly replaced with zero.
     */
    adjustment = source_bitmap_add16(bright_to_add, authored_pixel_adjustment);
    if (adjustment > 0) {
        adjustment = 0;
    }
    return source_bitmap_add16(shade, adjustment);
}
