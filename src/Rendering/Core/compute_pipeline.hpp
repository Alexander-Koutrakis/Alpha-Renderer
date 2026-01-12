#pragma once
#include "device.hpp"
#include <string>
#include <vector>

namespace Rendering {

struct ComputePipelineConfigInfo {
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    // Compute pipelines are much simpler - just layout needed
};

class ComputePipeline {
public:
    ComputePipeline(
        Device& device,
        const std::string& computeFilepath,
        const ComputePipelineConfigInfo& configInfo
    );
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    VkPipeline getPipeline() const { return computePipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }

    // Dispatch helper
    void dispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ = 1) const;

private:
    static std::vector<char> readFile(const std::string& filepath);
    void createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);
    void createComputePipeline(const std::string& computeFilepath, const ComputePipelineConfigInfo& configInfo);

    Device& device;
    VkPipeline computePipeline{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkShaderModule computeShaderModule{VK_NULL_HANDLE};
};

} // namespace Rendering
