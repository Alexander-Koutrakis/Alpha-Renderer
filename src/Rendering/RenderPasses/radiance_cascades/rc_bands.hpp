#pragma once

#include "Rendering/rendering_constants.hpp"

#include <cstdint>

namespace Rendering {

// The part of the ray a cascade is responsible for: [start, start + length] along the ray from the probe.
struct RcBand {
    float start = 0.0f;
    float length = 0.0f;
};

// 4x branching: each cascade has 4x the directions of the one below, so its interval is 4x longer (penumbra condition).
//   cascade 0: [0, 4 * base], cascade c > 0: [4^c * base, 4^(c+1) * base]
// With RC_CASCADE_COUNT = 6 the last cascade ends at 4096 * base.
inline RcBand rcCascadeBand(uint32_t cascade) {
    const uint32_t scaleStart = (cascade == 0u) ? 0u : (1u << (2u * cascade));
    const uint32_t scaleEnd = 1u << (2u * (cascade + 1u));
    RcBand band;
    band.start = RC_BASE_INTERVAL_LENGTH * static_cast<float>(scaleStart);
    band.length = RC_BASE_INTERVAL_LENGTH * static_cast<float>(scaleEnd - scaleStart);
    return band;
}

// Distance from the probe up to which cascades 0..cascade together see: the end of that cascade's band.
inline float rcCascadeReach(uint32_t cascade) {
    const RcBand band = rcCascadeBand(cascade);
    return band.start + band.length;
}

} // namespace Rendering
