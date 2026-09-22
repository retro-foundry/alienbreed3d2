/*
 * Footprint reconnection tests.
 *
 * These check the behaviour the footprint criterion exists to get right, rather
 * than re-deriving its arithmetic. The cases that matter are the ones a
 * roughness cutoff decides wrongly: a narrow lobe that a threshold calls rough
 * enough, and a broad one seen so close that reconnecting through it would
 * still pivot the path. Delta chains get their own test because reconnecting
 * through a mirror is the single most visible way to be wrong here.
 */

#include "renderer_dxr/dxr_restir_reconnection.h"

#include <cmath>
#include <cstdio>

using ab3d2::dxr::brdf::Vec3;
using namespace ab3d2::dxr::restir;

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

const Vec3 up = {0.0f, 0.0f, 1.0f};

ScatterEvent diffuse_event(float distance)
{
    ScatterEvent event = {};
    /* A cosine lobe integrates to roughly 1/pi at normal incidence. */
    event.pdf = 1.0f / pi;
    event.ray_distance = distance;
    event.normal = up;
    event.view_direction = up;
    event.roughness = 0.8f;
    event.is_delta = false;
    return event;
}

ScatterEvent glossy_event(float distance, float pdf)
{
    ScatterEvent event = diffuse_event(distance);
    event.pdf = pdf;
    event.roughness = 0.05f;
    return event;
}

ScatterEvent mirror_event(float distance)
{
    ScatterEvent event = diffuse_event(distance);
    event.is_delta = true;
    event.roughness = 0.0f;
    event.pdf = 1.0f;
    return event;
}

FootprintLimits limits_for(float primary_distance,
                           const ReconnectionThresholds &thresholds)
{
    const Vec3 camera = {0.0f, 0.0f, 0.0f};
    const Vec3 hit = {0.0f, 0.0f, primary_distance};
    const float primary = primary_ray_footprint(hit, camera, up, up);
    return footprint_limits(primary, thresholds);
}

/*
 * A footprint grows with distance and shrinks as the sampling density
 * concentrates. Both directions have to hold or the criterion is measuring
 * something else.
 */
void footprint_scaling_test()
{
    const float near_footprint = ray_footprint(diffuse_event(1.0f));
    const float far_footprint = ray_footprint(diffuse_event(10.0f));
    check(far_footprint > near_footprint,
          "a longer segment has a larger footprint");
    check(std::fabs(far_footprint / near_footprint - 100.0f) < 1.0f,
          "footprint grows with the square of distance");

    const float broad = ray_footprint(diffuse_event(4.0f));
    const float narrow = ray_footprint(glossy_event(4.0f, 200.0f));
    check(narrow < broad, "a narrower lobe has a smaller footprint");

    /* Grazing view shrinks the geometry factor and so widens the footprint. */
    ScatterEvent grazing = diffuse_event(4.0f);
    grazing.view_direction = {0.99f, 0.0f, 0.141f};
    check(ray_footprint(grazing) > broad,
          "a grazing segment has a larger footprint than a head-on one");
}

/*
 * The case the criterion exists for. A glossy vertex whose roughness clears a
 * fixed cutoff still concentrates its samples enough that reconnecting through
 * it is unsound, and the footprint test refuses it where the roughness test
 * does not.
 */
void footprint_rejects_what_roughness_accepts_test()
{
    ReconnectionThresholds thresholds = {};
    thresholds.roughness_threshold = 0.2f;
    thresholds.minimum_connection_footprint = 1.0f;
    thresholds.minimum_pdf_roughness = 0.4f;

    const FootprintLimits limits = limits_for(20.0f, thresholds);

    /* Roughness 0.25 clears the fixed cutoff, but the lobe is narrow and the
     * segment short, so the footprint stays far below the threshold. */
    ScatterEvent narrow = glossy_event(0.4f, 400.0f);
    narrow.roughness = 0.25f;

    check(reconnectible(ReconnectionMode::fixed_threshold, up, narrow, narrow,
                        limits, thresholds),
          "the roughness cutoff accepts a narrow close lobe");
    check(!reconnectible(ReconnectionMode::footprint, up, narrow, narrow,
                         limits, thresholds),
          "the footprint criterion rejects a narrow close lobe");

    /* A broad lobe over a long segment is reconnectible under both. */
    const ScatterEvent broad = diffuse_event(120.0f);
    check(reconnectible(ReconnectionMode::footprint, up, broad, broad, limits,
                        thresholds),
          "the footprint criterion accepts a broad distant pair");
    check(reconnectible(ReconnectionMode::fixed_threshold, up, broad, broad,
                        limits, thresholds),
          "the roughness cutoff accepts a broad distant pair");
}

/*
 * Reconnecting through a delta event is never permitted. A mirror's outgoing
 * direction is determined by its incoming one, so a path displaced to a
 * neighbouring pixel cannot reproduce it, and forcing the connection invents
 * transport that does not exist.
 */
void delta_chain_test()
{
    ReconnectionThresholds thresholds = {};
    const FootprintLimits limits = limits_for(20.0f, thresholds);

    const ScatterEvent broad = diffuse_event(40.0f);
    const ScatterEvent mirror = mirror_event(40.0f);

    check(ray_footprint(mirror) == 0.0f, "a delta bounce has no footprint");
    check(!reconnectible(ReconnectionMode::footprint, up, mirror, broad,
                         limits, thresholds),
          "a delta preceding vertex is never reconnectible");
    check(!reconnectible(ReconnectionMode::footprint, up, broad, mirror,
                         limits, thresholds),
          "a delta receiving segment is never reconnectible");
    check(!reconnectible(ReconnectionMode::fixed_threshold, up, mirror, broad,
                         limits, thresholds),
          "the fixed mode also refuses a delta vertex");
}

/*
 * The threshold has to respond to its control, and to the primary ray's
 * footprint it is expressed relative to. A threshold that ignored either would
 * behave as a world-space constant and mean different things at different
 * distances and resolutions.
 */
void threshold_response_test()
{
    ReconnectionThresholds thresholds = {};
    thresholds.minimum_connection_footprint = 1.0f;

    const FootprintLimits near_limits = limits_for(5.0f, thresholds);
    const FootprintLimits far_limits = limits_for(50.0f, thresholds);
    check(far_limits.footprint > near_limits.footprint,
          "the threshold scales with the primary ray footprint");

    ReconnectionThresholds loose = thresholds;
    loose.minimum_connection_footprint = 0.25f;
    const FootprintLimits loose_limits = limits_for(5.0f, loose);
    check(loose_limits.footprint < near_limits.footprint,
          "a smaller connection footprint lowers the threshold");

    /* A segment that fails at the strict threshold passes at the loose one,
     * which is what makes the setting a real reuse/quality control. */
    const ScatterEvent marginal = diffuse_event(3.0f);
    check(!reconnectible(ReconnectionMode::footprint, up, marginal, marginal,
                         near_limits, thresholds),
          "a marginal pair is rejected at the strict threshold");
    check(reconnectible(ReconnectionMode::footprint, up, marginal, marginal,
                        loose_limits, loose),
          "the same pair is accepted at the loose threshold");

    /* The pdf threshold is an adaptive roughness floor: a vertex that scattered
     * too sharply is refused however long its segment is. */
    ReconnectionThresholds strict_pdf = thresholds;
    strict_pdf.minimum_pdf_roughness = 0.9f;
    const FootprintLimits pdf_limits = limits_for(50.0f, strict_pdf);
    const ScatterEvent sharp = glossy_event(2000.0f, 50.0f);
    check(ray_footprint(sharp) > pdf_limits.footprint,
          "the sharp segment is long enough to clear the footprint test");
    check(!reconnectible(ReconnectionMode::footprint, up, sharp, sharp,
                         pdf_limits, strict_pdf),
          "a too-sharp preceding lobe is refused despite a large footprint");
}

/*
 * Degenerate geometry must produce a refusal rather than an infinity that
 * compares true against everything.
 */
void degenerate_input_test()
{
    ReconnectionThresholds thresholds = {};
    const FootprintLimits limits = limits_for(20.0f, thresholds);

    ScatterEvent zero_distance = diffuse_event(0.0f);
    check(ray_footprint(zero_distance) == 0.0f,
          "a zero-length segment has no footprint");

    ScatterEvent zero_pdf = diffuse_event(4.0f);
    zero_pdf.pdf = 0.0f;
    check(ray_footprint(zero_pdf) == 0.0f,
          "an impossible direction has no footprint");

    ScatterEvent perpendicular = diffuse_event(4.0f);
    perpendicular.view_direction = {1.0f, 0.0f, 0.0f};
    check(ray_footprint(perpendicular) == 0.0f,
          "a view along the surface has no footprint");

    check(!reconnectible(ReconnectionMode::footprint, up, zero_distance,
                         zero_distance, limits, thresholds),
          "degenerate events are not reconnectible");

    /* The primary footprint of a grazing surface must stay finite. */
    const float grazing = primary_ray_footprint({0.0f, 0.0f, 10.0f},
                                                {0.0f, 0.0f, 0.0f}, up,
                                                {1.0f, 0.0f, 0.0f});
    check(grazing == 0.0f || std::isfinite(grazing),
          "a grazing primary footprint stays finite");

    check(partial_jacobian(0.0f, {0.0f, 0.0f, -1.0f}, up) == 0.0f ||
          std::isfinite(partial_jacobian(0.0f, {0.0f, 0.0f, -1.0f}, up)),
          "a degenerate Jacobian stays finite");
    check(partial_jacobian(4.0f, {0.0f, 0.0f, 1.0f}, up) == 0.0f,
          "a connection from behind the surface has no Jacobian");
    const float jacobian = partial_jacobian(4.0f, {0.0f, 0.0f, -1.0f}, up);
    check(std::fabs(jacobian - 16.0f) < 1.0e-3f,
          "the Jacobian is the squared distance over the emission cosine");
}

}  // namespace

int main()
{
    footprint_scaling_test();
    footprint_rejects_what_roughness_accepts_test();
    delta_chain_test();
    threshold_response_test();
    degenerate_input_test();

    if (failures != 0) {
        std::fprintf(stderr, "%d reconnection check(s) failed\n", failures);
        return 1;
    }
    std::printf("reconnection checks passed\n");
    return 0;
}
