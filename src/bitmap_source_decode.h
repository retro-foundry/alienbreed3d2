#ifndef AB3D2_BITMAP_SOURCE_DECODE_H
#define AB3D2_BITMAP_SOURCE_DECODE_H

#include <stdint.h>

/*
 * objdrawhires.s:draw_Bitmap stores three independent five-bit source texels
 * in every big-endian WAD word. The PTR long's high byte selects the one its
 * column uses. Keep this format rule independent of either OpenGL or the
 * source palette conversion so it is testable on the original WAD bytes.
 */
static inline uint8_t bitmap_source_decode_packed_texel(uint16_t source_word,
                                                         uint8_t packed_third)
{
    switch (packed_third) {
    case 0u:
        /* move.b 1(a0,d1.w*2),d0; and.b #31,d0 */
        return (uint8_t)(source_word & 31u);
    case 1u:
        /* move.w (a0,d1.w*2),d0; lsr.w #5,d0; and.w #31,d0 */
        return (uint8_t)((source_word >> 5u) & 31u);
    default:
        /* move.b (a0,d1.w*2),d0; lsr.b #2,d0; and.b #31,d0 */
        return (uint8_t)((source_word >> 10u) & 31u);
    }
}

#endif
