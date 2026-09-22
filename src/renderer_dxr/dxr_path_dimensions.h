#ifndef AB3D2_DXR_PATH_DIMENSIONS_H
#define AB3D2_DXR_PATH_DIMENSIONS_H

#include "dxr_sample_stream.h"

#include <array>
#include <cstdint>

/*
 * A fixed address for every stochastic decision a path makes.
 *
 * Shift mapping regenerates one pixel's path from a different surface by
 * replaying the same random numbers. That only works if a decision's random
 * number depends on WHICH decision it is rather than on how many decisions came
 * before it. A sequentially consumed stream fails that immediately: the moment
 * a replayed path takes a different branch -- misses a light, terminates a
 * lobe early, skips next event estimation because the surface turned out to be
 * specular -- every subsequent draw shifts by one and the replay silently
 * becomes a different path that merely looks plausible.
 *
 * So each decision is addressed by (bounce, dimension) and hashed statelessly.
 * Skipping a decision costs nothing and moves nothing. This is the
 * deterministic-replay requirement of ReSTIR PT, and the hybrid shift depends
 * on it completely.
 *
 * The seed and index come from the reservoir being replayed, never from the
 * pixel doing the replaying: the point is to reproduce the SOURCE path's
 * choices at a DIFFERENT surface.
 *
 * Mirrored by the path dimension constants in shaders/path_trace.hlsl.
 */

namespace ab3d2::dxr::path_dimensions {

/*
 * The decisions one bounce can make. Every slot is reserved whether or not a
 * given bounce uses it, because the cost of an unused slot is nothing and the
 * cost of a shared one is a replay that diverges.
 */
enum Dimension : uint32_t {
    /* Which BSDF lobe to sample. */
    bsdf_component = 0u,
    /* The two numbers that pick a direction within that lobe. */
    bsdf_u = 1u,
    bsdf_v = 2u,
    /* Which light next event estimation should connect to. */
    nee_selector = 3u,
    /* The point chosen on that light. */
    nee_u = 4u,
    nee_v = 5u,
    /* Path termination. */
    russian_roulette = 6u,
    /* Reserved so the stride stays a power of two and later decisions can be
     * added without moving any existing dimension. */
    reserved = 7u,
};

/* Dimensions reserved per bounce. */
constexpr uint32_t per_bounce = 8u;

/* Bounces addressable by replay, matching MaximumDiffusePathDepth. */
constexpr uint32_t maximum_bounces = 8u;

/* Identifies the path being replayed, and travels inside the reservoir. */
struct PathRng {
    uint32_t seed = 0u;
    uint32_t index = 0u;
};

/*
 * The stream address of one decision. Flat and injective across the addressable
 * bounces, so no two decisions can collide.
 */
inline uint32_t address(uint32_t bounce, Dimension dimension)
{
    return bounce * per_bounce + static_cast<uint32_t>(dimension);
}

/*
 * Hashes one decision's address together with the path's identity. The address
 * is the only thing that distinguishes decisions here, which is what makes the
 * injectivity of `address` load-bearing rather than decorative: two decisions
 * sharing an address would draw the same number for the rest of time.
 */
inline std::array<uint32_t, 4> hash_decision(PathRng rng, uint32_t bounce,
                                             Dimension dimension)
{
    constexpr uint32_t salt = 0x9e3779b9u;
    return sample_stream::pcg4d(
        {rng.seed, rng.index, address(bounce, dimension), salt});
}

/*
 * One uniform in [0, 1) for one decision.
 *
 * Stateless by construction: the result depends only on the path's identity and
 * the address of the decision, never on the order in which decisions were
 * reached.
 */
inline float uniform(PathRng rng, uint32_t bounce, Dimension dimension)
{
    return sample_stream::uniform(hash_decision(rng, bounce, dimension)[0]);
}

/* The two numbers of a paired decision, drawn from one hash so a direction
 * costs a single evaluation. */
inline std::array<float, 2> uniform_pair(PathRng rng, uint32_t bounce,
                                         Dimension first)
{
    const std::array<uint32_t, 4> hashed = hash_decision(rng, bounce, first);
    return {sample_stream::uniform(hashed[0]),
            sample_stream::uniform(hashed[1])};
}

/* The BSDF direction sample for a bounce. */
inline std::array<float, 2> bsdf_direction(PathRng rng, uint32_t bounce)
{
    return uniform_pair(rng, bounce, bsdf_u);
}

/* The light-surface sample for a bounce. */
inline std::array<float, 2> nee_position(PathRng rng, uint32_t bounce)
{
    return uniform_pair(rng, bounce, nee_u);
}

}  // namespace ab3d2::dxr::path_dimensions

#endif
