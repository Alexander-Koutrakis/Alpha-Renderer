#pragma once
#include "device.hpp"
#include <string>
#include <vector>
#include <optional>
#include "Rendering/Resources/mesh.hpp"

namespace Rendering {

struct ShaderStageInfo {
    VkShaderStageFlagBits stage;
    std::string spirvFilepath;
};

// Holds the fixed-function state for a graphics pipeline. Some members point into other members
// (colorBlendInfo.pAttachments, dynamicStateInfo.pDynamicStates), so copying would leave the copy
// pointing at the original: the type is non-copyable on purpose. Fill it in place with
// Pipeline::defaultPipelineConfigInfo and pass it by reference.
struct PipelineConfigInfo {
    PipelineConfigInfo() = default;
    PipelineConfigInfo(const PipelineConfigInfo&) = delete;
    PipelineConfigInfo& operator=(const PipelineConfigInfo&) = delete;

    std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

    VkPipelineViewportStateCreateInfo viewportInfo{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    VkPipelineRasterizationStateCreateInfo rasterizationInfo{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    VkPipelineMultisampleStateCreateInfo multisampleInfo{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    VkPipelineColorBlendStateCreateInfo colorBlendInfo{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    std::vector<VkDynamicState> dynamicStateEnables{};
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    uint32_t subpass = 0;
};

class Pipeline {
public:
    Pipeline(Device& device, const std::optional<std::string>& vertFilepath,
             const std::optional<std::string>& geometryFilepath, const std::optional<std::string>& fragFilepath,
             const PipelineConfigInfo& configInfo);

    // New flexible constructor: pass any subset/order of shader stages
    Pipeline(Device& device, const std::vector<ShaderStageInfo>& shaderStages, const PipelineConfigInfo& configInfo);

    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    VkPipeline getPipeline() const { return graphicsPipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }

    static void defaultPipelineConfigInfo(PipelineConfigInfo& configInfo);

private:
    void createGraphicsPipeline(const std::optional<std::string>& vertFilepath,
                                const std::optional<std::string>& geometryFilepath,
                                const std::optional<std::string>& fragFilepath, const PipelineConfigInfo& configInfo);

    void createGraphicsPipeline(const std::vector<ShaderStageInfo>& shaderStages, const PipelineConfigInfo& configInfo);

    void createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);

    Device& device;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE; // Store just the layout instead of entire config
};
} // namespace Rendering