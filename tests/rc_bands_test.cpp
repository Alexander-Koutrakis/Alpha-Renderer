#include "doctest.h"
#include "Rendering/RenderPasses/radiance_cascades/rc_bands.hpp"

#include <cmath>

using namespace Rendering;

TEST_CASE("RC bands: the cascades cover the ray from 0 with no gap and 4x growth") {
    CHECK(rcCascadeBand(0).start == 0.0f);
    for (uint32_t c = 0; c + 1 < RC_CASCADE_COUNT; ++c) {
        const RcBand band = rcCascadeBand(c);
        const RcBand next = rcCascadeBand(c + 1);
        INFO("cascade " << c);
        CHECK(band.start + band.length == doctest::Approx(next.start).epsilon(1e-6));
        if (c > 0) {
            CHECK(next.length == doctest::Approx(4.0f * band.length).epsilon(1e-6));
        }
    }
    const RcBand last = rcCascadeBand(RC_CASCADE_COUNT - 1);
    CHECK(last.start + last.length == doctest::Approx(4096.0f * RC_BASE_INTERVAL_LENGTH).epsilon(1e-6));
}

TEST_CASE("RC bands: the reach of cascade k is 4^(k+1) times the base interval") {
    for (uint32_t c = 0; c < RC_CASCADE_COUNT; ++c) {
        INFO("cascade " << c);
        CHECK(rcCascadeReach(c) ==
              doctest::Approx(std::pow(4.0f, float(c + 1)) * RC_BASE_INTERVAL_LENGTH).epsilon(1e-6));
    }
}
