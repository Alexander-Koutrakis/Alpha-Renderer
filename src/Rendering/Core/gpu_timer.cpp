#include "gpu_timer.hpp"

#include "vk_check.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Rendering {

bool GpuTimer::requestedByEnvironment() {
    const char* value = std::getenv("ALPHA_GPU_TIMING");
    return value != nullptr && std::strcmp(value, "0") != 0;
}

GpuTimer::GpuTimer(Device& device, uint32_t frameCount, uint32_t maxScopes) : device{device}, maxScopes{maxScopes} {
    const uint32_t family = device.findPhysicalQueueFamilies().graphicsFamily;
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device.getPhysicalDevice(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device.getPhysicalDevice(), &familyCount, families.data());

    validBits = families.at(family).timestampValidBits;
    if (validBits == 0 || device.deviceProperties.limits.timestampComputeAndGraphics == VK_FALSE) {
        throw std::runtime_error("ALPHA_GPU_TIMING is set but the graphics queue cannot write timestamps");
    }
    periodNs = device.deviceProperties.limits.timestampPeriod;

    pools.assign(frameCount, VK_NULL_HANDLE);
    names.resize(frameCount);
    submitted.assign(frameCount, false);

    VkQueryPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = maxScopes * 2;
    for (VkQueryPool& pool : pools) {
        VK_CHECK(vkCreateQueryPool(device.getDevice(), &poolInfo, nullptr, &pool));
    }
}

GpuTimer::~GpuTimer() {
    vkDeviceWaitIdle(device.getDevice());
    for (VkQueryPool pool : pools) {
        vkDestroyQueryPool(device.getDevice(), pool, nullptr);
    }
}

void GpuTimer::beginFrame(VkCommandBuffer commandBuffer, uint32_t frame) {
    currentFrame = frame;
    if (submitted[frame]) {
        readBack(frame);
        submitted[frame] = false;
    }
    names[frame].clear();
    vkCmdResetQueryPool(commandBuffer, pools[frame], 0, maxScopes * 2);
}

void GpuTimer::markSubmitted(uint32_t frame) {
    submitted[frame] = !names[frame].empty();
}

void GpuTimer::readBack(uint32_t frame) {
    const std::vector<std::string>& scopeNames = names[frame];
    const uint32_t queryCount = static_cast<uint32_t>(scopeNames.size()) * 2;

    // Two 64-bit words per query: the timestamp, then non-zero when it is available.
    std::vector<uint64_t> data(queryCount * 2);
    const VkResult result = vkGetQueryPoolResults(device.getDevice(), pools[frame], 0, queryCount,
                                                  data.size() * sizeof(uint64_t), data.data(), 2 * sizeof(uint64_t),
                                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result == VK_NOT_READY) {
        return; // the frame is not finished: keep the previous timings
    }
    VK_CHECK(result);

    const bool sameScopes = smoothed.size() == scopeNames.size();
    if (!sameScopes) {
        smoothed.assign(scopeNames.size(), Timing{});
        averages.assign(scopeNames.size(), SmoothedMs{});
    }
    for (size_t i = 0; i < scopeNames.size(); ++i) {
        if (smoothed[i].name != scopeNames[i]) {
            smoothed[i].name = scopeNames[i];
            averages[i] = SmoothedMs{};
        }
        averages[i].add(static_cast<float>(timestampDeltaMs(data[i * 4], data[i * 4 + 2], validBits, periodNs)));
        smoothed[i].ms = averages[i].value;
    }
}

float GpuTimer::totalMs() const {
    float total = 0.0f;
    for (const Timing& timing : smoothed) {
        total += timing.ms;
    }
    return total;
}

GpuTimer::Scope::Scope(GpuTimer& timer, VkCommandBuffer commandBuffer, const char* name)
    : timer{timer}, commandBuffer{commandBuffer} {
    std::vector<std::string>& scopeNames = timer.names[timer.currentFrame];
    if (scopeNames.size() >= timer.maxScopes) {
        throw std::runtime_error("GpuTimer: more scopes in one frame than maxScopes");
    }
    const uint32_t startQuery = static_cast<uint32_t>(scopeNames.size()) * 2;
    endQuery = startQuery + 1;
    scopeNames.emplace_back(name);
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timer.pools[timer.currentFrame], startQuery);
}

GpuTimer::Scope::~Scope() {
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timer.pools[timer.currentFrame], endQuery);
}

} // namespace Rendering
