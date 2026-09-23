/*
 * The level's lighting is the only lighting Alien Breed 3D II has, so the
 * conversion in dxr_source_lighting.h is the ray tracer's entire light source.
 * These check the properties the renderer depends on rather than restating the
 * table: a wrong curve here is a dark or blown level with nothing else to
 * blame it on.
 */
#include "../src/renderer_dxr/dxr_source_lighting.h"

#include <cstdio>
#include <cmath>

namespace {

using namespace ab3d2::dxr::source_lighting;

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void check_close(float actual, float expected, float tolerance,
                 const char *what)
{
    if (!(std::fabs(actual - expected) <= tolerance)) {
        std::fprintf(stderr, "FAIL: %s (got %f, wanted %f)\n", what, actual,
                     expected);
        ++failures;
    }
}

/* An unlit vertex must contribute nothing. The scene compiler leaves
 * source_light_level at zero for geometry the source never lit, and treating
 * that as "fully lit" would light the level from its own unlit surfaces. */
void unlit_levels_emit_nothing()
{
    check_close(world_irradiance(0.0f), 0.0f, 0.0f, "level zero is unlit");
    check_close(world_irradiance(-1.0f), 0.0f, 0.0f, "negative is unlit");
}

/* The source's own origin: at the full-brightness level a surface is fully
 * lit, and the curve starts at exactly one. */
void full_brightness_is_unit_irradiance()
{
    check_close(world_irradiance(world_full_brightness_level), 1.0f, 1.0e-6f,
                "full brightness is unit irradiance");
    check_close(shade_curve[0], 1.0f, 0.0f, "curve starts at one");
}

/* Light may only fall off as the shading row advances. A non-monotonic curve
 * would make a darker sector brighter than the one beside it. */
void the_curve_only_darkens()
{
    for (size_t row = 1u; row < shade_row_count; ++row) {
        check(shade_curve[row] < shade_curve[row - 1u],
              "each shading row is darker than the one before it");
    }
    check(shade_curve[shade_row_count - 1u] > 0.0f,
          "the darkest row is still above black");
}

/* Levels past the darkest row clamp rather than wrapping or going negative.
 * The source value is continuous and is not bounded to the table. */
void levels_outside_the_table_clamp()
{
    const float darkest = shade_curve[shade_row_count - 1u];
    check_close(
        world_irradiance(world_full_brightness_level +
                         static_cast<float>(shade_row_count - 1u)),
        darkest, 1.0e-6f, "the last row is reachable");
    check_close(world_irradiance(world_full_brightness_level + 1000.0f),
                darkest, 1.0e-6f, "beyond the table clamps to the darkest row");
}

/* Interpolation, not stepping: the source picked one row per span, and a
 * continuous level stepped through the table banded across a floor. */
void between_rows_interpolates()
{
    const float middle = world_irradiance(world_full_brightness_level + 0.5f);
    const float expected = (shade_curve[0] + shade_curve[1]) * 0.5f;
    check_close(middle, expected, 1.0e-6f, "half a row is half way between");
    check(middle < shade_curve[0] && middle > shade_curve[1],
          "an interpolated value stays between its neighbours");
}

/*
 * Sprites encode brightness in the opposite direction to world geometry.
 * Getting this backwards lights entities brightly in the dark and blacks them
 * out under a lamp, which is why it is a separate function with its own test.
 */
void sprites_brighten_as_the_level_rises()
{
    check(sprite_irradiance(400) > sprite_irradiance(300),
          "a higher sprite level is brighter");
    check(world_irradiance(400.0f) < world_irradiance(300.0f),
          "a higher world level is darker");
}

/* The sprite response is affine between its clamps, and clamps outside them.
 * The ceiling is above one on purpose: a lit model may read brighter than a
 * fully lit wall, and clamping to one darkened every entity. */
void the_sprite_response_matches_the_source()
{
    check_close(sprite_irradiance(300), 0.45f, 1.0e-6f,
                "the sprite response passes through its source origin");
    /* Half a step, which stays below the ceiling: a whole one would reach
     * 1.45 and clamp, testing the clamp instead of the slope. */
    check_close(sprite_irradiance(348), 0.45f + 0.5f, 1.0e-5f,
                "the sprite response rises by one per 96 levels");
    check_close(sprite_irradiance(-32768), sprite_minimum_irradiance, 0.0f,
                "the sprite response has a floor");
    check_close(sprite_irradiance(32767), sprite_maximum_irradiance, 0.0f,
                "the sprite response has a ceiling");
    check(sprite_maximum_irradiance > 1.0f,
          "the sprite ceiling is above fully lit");
}

/*
 * The renderer multiplies irradiance by a surface's reflectance, so a value
 * above the sprite ceiling would let a surface return more light than reached
 * it. World geometry must stay within unit irradiance for that reason.
 */
void irradiance_stays_in_range()
{
    for (int level = 0; level <= 1200; ++level) {
        const float value = world_irradiance(static_cast<float>(level));
        check(value >= 0.0f && value <= 1.0f,
              "world irradiance stays within unit range");
        if (value < 0.0f || value > 1.0f) {
            break;
        }
    }
}

}  // namespace

int main()
{
    unlit_levels_emit_nothing();
    full_brightness_is_unit_irradiance();
    the_curve_only_darkens();
    levels_outside_the_table_clamp();
    between_rows_interpolates();
    sprites_brighten_as_the_level_rises();
    the_sprite_response_matches_the_source();
    irradiance_stays_in_range();
    if (failures != 0) {
        std::fprintf(stderr, "%d source lighting checks failed\n", failures);
        return 1;
    }
    return 0;
}
