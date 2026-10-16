#pragma once

#include "device.hpp"
#include "timestamp_math.hpp"

#include <string>
#include <vector>

namespace Rendering {

// Per-pass GPU timings from timestamp queries. One query pool per frame in flight; a frame's results are read back
// when that slot comes round again, after its fence has been waited on, so reading never stalls.
//
// Per frame: beginFrame(), any number of Scopes, markSubmitted() once the command buffer has been submitted.
// The renderer creates a GpuTimer only when ALPHA_GPU_TIMING is set, so with timing off nothing is recorded.
class GpuTimer {
public:
    struct Timing {
        std::string name;
        float ms = 0.0f; // smoothed over recent frames
    };

    // RAII: writes a start timestamp now and an end timestamp when it goes out of scope.
    class Scope {
    public:
        Scope(GpuTimer& timer, VkCommandBuffer commandBuffer, const char* name);
        ~Scope();

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        GpuTimer& timer;
        VkCommandBuffer commandBuffer;
        uint32_t endQuery;
    };

    // True when the ALPHA_GPU_TIMING environment variable is set to anything but "0".
    static bool requestedByEnvironment();

    // Throws std::runtime_error if the graphics queue cannot write timestamps.
    GpuTimer(Device& device, uint32_t frameCount, uint32_t maxScopes);
    ~GpuTimer();

    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    // Reads back this slot's previous frame, then resets its queries. Call outside a render pass.
    void beginFrame(VkCommandBuffer commandBuffer, uint32_t frame);
    // Call after the command buffer holding this frame's scopes was submitted: only then may its queries be read.
    void markSubmitted(uint32_t frame);

    const std::vector<Timing>& timings() const { return smoothed; }
    float totalMs() const;

private:
    void readBack(uint32_t frame);

    Device& device;
    uint32_t maxScopes;
    uint32_t validBits = 0;
    float periodNs = 0.0f;

    std::vector<VkQueryPool> pools;              // per frame in flight, 2 queries per scope
    std::vector<std::vector<std::string>> names; // per frame: the scopes recorded into that frame's pool
    std::vector<bool> submitted;                 // per frame: its queries hold results that may be read
    uint32_t currentFrame = 0;

    std::vector<Timing> smoothed;
    std::vector<SmoothedMs> averages;
};

} // namespace Rendering
