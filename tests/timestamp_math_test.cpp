#include "doctest.h"
#include "Rendering/Core/timestamp_math.hpp"

using namespace Rendering;

TEST_CASE("timestamp delta converts ticks to milliseconds with the device period") {
    // 1,000,000 ticks of 1 ns is 1 ms; of 52.08 ns (a typical NVIDIA period) is 52.08 ms.
    CHECK(timestampDeltaMs(0, 1'000'000, 64, 1.0f) == doctest::Approx(1.0));
    CHECK(timestampDeltaMs(500, 1'000'500, 64, 52.08f) == doctest::Approx(52.08));
    CHECK(timestampDeltaMs(7, 7, 64, 1.0f) == doctest::Approx(0.0));
}

TEST_CASE("timestamp delta survives the counter wrapping at validBits") {
    // 36 valid bits: the counter goes 2^36-10 -> 5, which is 15 ticks, not a huge negative difference.
    const uint64_t wrapAt = uint64_t{1} << 36;
    CHECK(timestampDeltaMs(wrapAt - 10, 5, 36, 1e6f) == doctest::Approx(15.0));
    // A delta that needs the top valid bit is kept whole: 2^35 + 3 ticks of 1 ns.
    CHECK(timestampDeltaMs(0, (uint64_t{1} << 35) + 3, 36, 1.0f) == doctest::Approx(((uint64_t{1} << 35) + 3) * 1e-6));
    // High bits beyond validBits are not meaningful and must not leak into the result.
    CHECK(timestampDeltaMs(0xF000000000000000ull | 100, 0xF000000000000000ull | 160, 36, 1e6f) ==
          doctest::Approx(60.0));
}

TEST_CASE("smoothed time is seeded by the first sample, then moves a fixed share toward each new one") {
    SmoothedMs average;
    average.add(10.0f);
    CHECK(average.value == doctest::Approx(10.0f));
    average.add(20.0f);
    CHECK(average.value == doctest::Approx(11.0f)); // 10 + 0.1 * (20 - 10)

    // A constant input converges to that input.
    for (int i = 0; i < 200; ++i) {
        average.add(4.0f);
    }
    CHECK(average.value == doctest::Approx(4.0f).epsilon(0.001));
}
