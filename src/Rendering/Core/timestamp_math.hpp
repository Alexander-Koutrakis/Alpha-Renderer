#pragma once

#include <cstdint>

namespace Rendering {

// Timestamp arithmetic for GpuTimer, kept free of Vulkan calls so tests can run it without a device.

// Elapsed milliseconds between two timestamps. A queue only guarantees `validBits` low bits of a timestamp, so the
// counter wraps at 2^validBits: the difference is taken modulo that. `periodNs` is VkPhysicalDeviceLimits::timestampPeriod.
inline double timestampDeltaMs(uint64_t begin, uint64_t end, uint32_t validBits, float periodNs) {
    const uint64_t mask = validBits >= 64 ? ~uint64_t{0} : (uint64_t{1} << validBits) - 1;
    const uint64_t ticks = (end - begin) & mask;
    return static_cast<double>(ticks) * static_cast<double>(periodNs) * 1e-6;
}

// Exponential moving average of a per-frame time. The first sample seeds it, so the value does not climb from zero.
struct SmoothedMs {
    static constexpr float kWeight = 0.1f; // share of each new sample

    float value = 0.0f;
    bool seeded = false;

    void add(float sampleMs) {
        value = seeded ? value + kWeight * (sampleMs - value) : sampleMs;
        seeded = true;
    }
};

} // namespace Rendering
