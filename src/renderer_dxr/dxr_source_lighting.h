#ifndef AB3D2_DXR_SOURCE_LIGHTING_H
#define AB3D2_DXR_SOURCE_LIGHTING_H

#include <cstddef>
#include <cstdint>

namespace ab3d2::dxr::source_lighting {

/*
 * Alien Breed 3D II's own lighting, converted for the ray tracer.
 *
 * The game has no emissive surfaces. Its palette remap table
 * (includes/newtexturemaps.pal) darkens all 256 entries to black, so no
 * colour survives the darkest shading row and no texture can be self-lit.
 * Every lit surface in the game is lit by the sector brightness tables, which
 * reach the renderer as SceneVertex::source_light_level.
 *
 * Both other backends already convert that value: renderer_opengl.c shades
 * with it, and tools/ab3d_levels_to_quake.py turns it into Quake `light`
 * entities for Q2RTX. This header is the ray tracer's conversion, producing
 * incident irradiance as a fraction of a fully lit surface.
 *
 * The source uses two different encodings, which is why there are two
 * functions rather than one. World geometry selects a palette row and counts
 * upward into darkness; sprites carry a brightness that counts upward into
 * light. Neither is a choice this renderer gets to make.
 */

/*
 * Mean luminance of each shading row relative to the identity row, in linear
 * light, measured over the 223 palette entries with meaningful luminance.
 *
 * Row 32 of the remap table is the identity row -- the texture undarkened --
 * and each row after it is dimmer. renderer_opengl.c approximates this run
 * with the straight line `1 - shade / 31`, which is visibly wrong in the
 * mid-tones; these are the table's actual values.
 */
inline constexpr size_t shade_row_count = 32u;
inline constexpr float shade_curve[shade_row_count] = {
    1.0000f, 0.9453f, 0.8741f, 0.8274f, 0.7496f, 0.7018f, 0.6479f, 0.5994f,
    0.5088f, 0.4673f, 0.4266f, 0.3880f, 0.3507f, 0.3180f, 0.2780f, 0.2540f,
    0.2231f, 0.2010f, 0.1760f, 0.1517f, 0.1329f, 0.1142f, 0.0974f, 0.0743f,
    0.0601f, 0.0472f, 0.0364f, 0.0274f, 0.0193f, 0.0130f, 0.0077f, 0.0028f,
};

/*
 * The source light level at which a world surface is fully lit. Both of
 * renderer_opengl.c's conversions subtract it, so it is the source's own
 * origin rather than a constant chosen here.
 */
inline constexpr float world_full_brightness_level = 300.0f;

/*
 * Irradiance on a world surface, as a fraction of fully lit.
 *
 * renderer_opengl.c selects the shading row as `source_light_level - 300`
 * plus a term in view depth. That depth term is distance fog: a raster
 * approximation that belongs to a rasteriser's view, not to the scene. A path
 * tracer that baked it in would darken a wall for being far away and then
 * light the same wall correctly through a bounce, so only the view-independent
 * part is converted here.
 */
inline float world_irradiance(float source_light_level)
{
    if (!(source_light_level > 0.0f)) {
        return 0.0f;
    }
    float shade = source_light_level - world_full_brightness_level;
    if (shade < 0.0f) {
        shade = 0.0f;
    }
    const float last = static_cast<float>(shade_row_count - 1u);
    if (shade > last) {
        shade = last;
    }
    const size_t lower = static_cast<size_t>(shade);
    const size_t upper = lower + 1u < shade_row_count ? lower + 1u : lower;
    const float fraction = shade - static_cast<float>(lower);
    /*
     * The source picked one row per span. Interpolating instead is a
     * deliberate difference: the level value arrives continuous, and stepping
     * it banded visibly across a floor.
     */
    return shade_curve[lower] +
        (shade_curve[upper] - shade_curve[lower]) * fraction;
}

/*
 * Irradiance on a sprite or vector model, as a fraction of fully lit.
 *
 * Sprites encode brightness in the opposite direction to world geometry, and
 * the source's own response is the affine one reproduced here from
 * renderer_opengl.c:renderer_opengl_vector_base_light. Its ceiling is above
 * one because a lit model in the source may read brighter than a fully lit
 * wall; clamping it to one instead would darken every entity.
 */
inline constexpr float sprite_minimum_irradiance = 0.05f;
inline constexpr float sprite_maximum_irradiance = 1.25f;

inline float sprite_irradiance(int16_t source_light_level)
{
    const float result = 0.45f +
        (static_cast<float>(source_light_level) - world_full_brightness_level) /
            96.0f;
    if (result < sprite_minimum_irradiance) {
        return sprite_minimum_irradiance;
    }
    if (result > sprite_maximum_irradiance) {
        return sprite_maximum_irradiance;
    }
    return result;
}

}  // namespace ab3d2::dxr::source_lighting

#endif
