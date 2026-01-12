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

	struct PipelineConfigInfo {
    PipelineConfigInfo() = default;
    PipelineConfigInfo(const PipelineConfigInfo& other) {
        // Copy all members
        bindingDescriptions = other.bindingDescriptions;
        attributeDescriptions = other.attributeDescriptions;
        viewportInfo = other.viewportInfo;
        inputAssemblyInfo = other.inputAssemblyInfo;
        rasterizationInfo = other.rasterizationInfo;
        multisampleInfo = other.multisampleInfo;
        colorBlendAttachment = other.colorBlendAttachment;
        colorBlendInfo = other.colorBlendInfo;
        depthStencilInfo = other.depthStencilInfo;
        dynamicStateEnables = other.dynamicStateEnables;
        dynamicStateInfo = other.dynamicStateInfo;
        pipelineLayout = other.pipelineLayout;
        renderPass = other.renderPass;
        subpass = other.subpass;
    }
    
    PipelineConfigInfo& operator=(const PipelineConfigInfo& other) {
        if (this != &other) {
            bindingDescriptions = other.bindingDescriptions;
            attributeDescriptions = other.attributeDescriptions;
            viewportInfo = other.viewportInfo;
            inputAssemblyInfo = other.inputAssemblyInfo;
            rasterizationInfo = other.rasterizationInfo;
            multisampleInfo = other.multisampleInfo;
            colorBlendAttachment = other.colorBlendAttachment;
            colorBlendInfo = other.colorBlendInfo;
            depthStencilInfo = other.depthStencilInfo;
            dynamicStateEnables = other.dynamicStateEnables;
            dynamicStateInfo = other.dynamicStateInfo;
            pipelineLayout = other.pipelineLayout;
            renderPass = other.renderPass;
            subpass = other.subpass;
        }
        return *this;
    }

    std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

    VkPipelineViewportStateCreateInfo viewportInfo;
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
    VkPipelineRasterizationStateCreateInfo rasterizationInfo;
    VkPipelineMultisampleStateCreateInfo multisampleInfo;
    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    VkPipelineColorBlendStateCreateInfo colorBlendInfo;
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
    std::vector<VkDynamicState> dynamicStateEnables;
    VkPipelineDynamicStateCreateInfo dynamicStateInfo;
    VkPipelineLayout pipelineLayout = nullptr;
    VkRenderPass renderPass = nullptr;
    uint32_t subpass = 0;
};


	class Pipeline {
public:
    Pipeline(
        Device& device,
        const std::optional<std::string>& vertFilepath,
        const std::optional<std::string>& geometryFilepath,
        const std::optional<std::string>& fragFilepath,
        const PipelineConfigInfo& configInfo);

    // New flexible constructor: pass any subset/order of shader stages
    Pipeline(
        Device& device,
        const std::vector<ShaderStageInfo>& shaderStages,
        const PipelineConfigInfo& configInfo);

    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    VkPipeline getPipeline() const { return graphicsPipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }

    static void defaultPipelineConfigInfo(PipelineConfigInfo& configInfo);

private:
    static std::vector<char> readFile(const std::string& filepath);

    void createGraphicsPipeline(
        const std::optional<std::string>& vertFilepath,
        const std::optional<std::string>& geometryFilepath,
        const std::optional<std::string>& fragFilepath,
        const PipelineConfigInfo& configInfo);

    void createGraphicsPipeline(
        const std::vector<ShaderStageInfo>& shaderStages,
        const PipelineConfigInfo& configInfo);

    void createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);
    
    Device& device;
    VkPipeline graphicsPipeline;
    VkPipelineLayout pipelineLayout;  // Store just the layout instead of entire config
};
}  // namespace lve 