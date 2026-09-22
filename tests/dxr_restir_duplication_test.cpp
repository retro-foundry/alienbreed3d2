/*
 * Duplication-based history reduction tests.
 *
 * The failure this guards against is subtle: nothing crashes and no value goes
 * out of range when a neighbourhood fills with descendants of one path. The
 * image simply stops adapting, holds its fireflies, and looks stable enough
 * that a denoiser trusts it. So these tests build the impoverished
 * configurations directly and check that confidence actually falls, and just as
 * importantly that it does not fall where samples are genuinely independent.
 */

#include "renderer_dxr/dxr_restir_duplication.h"

#include <cstdio>
#include <vector>

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

constexpr int32_t width = 64;
constexpr int32_t height = 64;

std::vector<uint32_t> filled(uint32_t ancestry)
{
    return std::vector<uint32_t>(static_cast<size_t>(width * height), ancestry);
}

/* Distinct ancestry everywhere: every pixel holds its own canonical path. */
std::vector<uint32_t> distinct()
{
    std::vector<uint32_t> map(static_cast<size_t>(width * height));
    for (size_t i = 0u; i < map.size(); ++i) {
        map[i] = static_cast<uint32_t>(i) + 1u;
    }
    return map;
}

/*
 * Counting. A neighbourhood of one repeated ancestry is the impoverished case;
 * a neighbourhood of distinct ancestries is the healthy one.
 */
void counting_test()
{
    const std::vector<uint32_t> same = filled(7u);
    const uint32_t centre = duplication_count(same, width, height, 32, 32);
    /* All 288 neighbours match, but the count is stored in eight bits, so it
     * saturates at 255. Impoverishment therefore tops out just under one, which
     * is why the floor below is approached rather than reached exactly. */
    check(centre == duplication_maximum_count,
          "a wholly duplicated neighbourhood saturates the count");

    const std::vector<uint32_t> unique = distinct();
    check(duplication_count(unique, width, height, 32, 32) == 0u,
          "independent samples show no duplication");

    /* A pixel with no sample is not impoverished, it is empty. Counting it as
     * duplicated would cut history precisely where there is none to cut. */
    const std::vector<uint32_t> empty = filled(0u);
    check(duplication_count(empty, width, height, 32, 32) == 0u,
          "an absent sample never counts as duplicated");

    /* Half the window duplicated should land near half the neighbour count. */
    std::vector<uint32_t> half = distinct();
    for (int32_t y = 24; y <= 40; ++y) {
        for (int32_t x = 32; x <= 40; ++x) {
            half[static_cast<size_t>(y * width + x)] = 99u;
        }
    }
    const uint32_t partial = duplication_count(half, width, height, 32, 32);
    check(partial > 100u && partial < 200u,
          "a partly duplicated neighbourhood counts in between");

    /* Near an edge the window is clipped, so the count falls rather than
     * reading outside the frame. */
    const uint32_t corner = duplication_count(same, width, height, 0, 0);
    check(corner > 0u && corner < centre,
          "a clipped window counts fewer neighbours");
}

/*
 * The response curve. Confidence must fall as duplication rises and must reach
 * the floor when a neighbourhood is entirely one ancestry.
 */
void reduction_response_test()
{
    const float maximum = 20.0f;
    const float strength = 1.0f;

    const float healthy = reduced_history_length(maximum, 0u, strength);
    check(healthy == maximum,
          "independent samples keep the configured confidence cap");

    const uint32_t full = duplication_maximum_count;
    const float impoverished = reduced_history_length(maximum, full, strength);
    check(impoverished < 1.1f,
          "a wholly duplicated neighbourhood falls to the floor");

    float previous = maximum + 1.0f;
    bool monotonic = true;
    for (uint32_t count = 0u; count <= full; count += 8u) {
        const float value = reduced_history_length(maximum, count, strength);
        if (value > previous + 1.0e-5f) {
            monotonic = false;
        }
        previous = value;
        if (value < 1.0f || value > maximum) {
            check(false, "the reduced cap stays within its bounds");
            break;
        }
    }
    check(monotonic, "confidence falls monotonically as duplication rises");
}

/*
 * The strength control has to span a useful range: a low setting should only
 * bite under heavy duplication, a high one should react to a little.
 */
void strength_response_test()
{
    const float maximum = 32.0f;
    const uint32_t moderate = 64u;

    const float gentle = reduced_history_length(maximum, moderate, 0.25f);
    const float firm = reduced_history_length(maximum, moderate, 1.0f);
    const float aggressive = reduced_history_length(maximum, moderate, 2.0f);

    check(gentle > firm, "a lower strength preserves more history");
    check(firm > aggressive, "a higher strength cuts more history");
    check(aggressive >= 1.0f, "even the most aggressive setting keeps a floor");

    /* Zero disables the feature rather than reducing to the floor. */
    check(reduced_history_length(maximum, moderate, 0.0f) == maximum,
          "zero strength leaves the configured cap alone");
    check(reduced_history_length(maximum, duplication_maximum_count, 0.0f) ==
              maximum,
          "zero strength ignores even total duplication");
}

/* Degenerate configurations must not produce a cap below one or a non-finite
 * one, either of which would disable temporal reuse in a way nothing reports. */
void degenerate_test()
{
    check(reduced_history_length(1.0f, 200u, 1.0f) == 1.0f,
          "a cap of one cannot be reduced further");
    check(reduced_history_length(0.0f, 200u, 1.0f) == 1.0f,
          "a degenerate cap is raised to the floor");
    const float huge = reduced_history_length(65535.0f, 1u, 4.0f);
    check(huge >= 1.0f && huge <= 65535.0f,
          "an extreme configuration stays in range");
    check(impoverishment(100000u) == 1.0f,
          "impoverishment saturates rather than exceeding one");
    check(duplication_count(filled(3u), width, height, -1, 0) == 0u,
          "a pixel outside the frame has no duplication");
}

}  // namespace

int main()
{
    counting_test();
    reduction_response_test();
    strength_response_test();
    degenerate_test();

    if (failures != 0) {
        std::fprintf(stderr, "%d duplication check(s) failed\n", failures);
        return 1;
    }
    std::printf("duplication checks passed\n");
    return 0;
}
