#ifndef AB3D2_SOURCE_BITMAP_LIGHTING_H
#define AB3D2_SOURCE_BITMAP_LIGHTING_H

#include <stdint.h>

/*
 * Renderer-neutral translations of objdrawhires.s:draw_Bitmap and
 * draw_bitmap_lighted. The caller supplies the source rotated-depth term;
 * these helpers retain the 68000 word/byte arithmetic and palette selection.
 */
int16_t source_bitmap_bright_to_add(uint16_t object_brightness,
                                    int16_t source_depth_term);
uint8_t source_bitmap_direct_palette_row(int16_t bright_to_add);
int16_t source_bitmap_lighted_palette_shade(int8_t directional_curve_value,
                                            int16_t strongest_light,
                                            int16_t bright_to_add,
                                            int16_t authored_pixel_adjustment);

#endif
