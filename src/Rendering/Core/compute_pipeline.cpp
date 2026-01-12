#include "compute_pipeline.hpp"
#include <cassert>
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace Rendering {

ComputePipeline::ComputePipeline(
    Device& device,
    const std::string& computeFilepath,
    const ComputePipelineConfigInfo& configInfo
) : device{device}, pipelineLayout{configInfo.pipelineLayout} {
    createComputePipeline(computeFilepath, configInfo);
}

ComputePipeline::~ComputePipeline() {
    if (computeShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device.getDevice(), computeShaderModule, nullptr);
    }
    if (computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device.getDevice(), computePipeline, nullptr);
    }
}

std::vector<char> ComputePipeline::readFile(const std::string& filepath) {
    std::ifstream file{filepath, std::ios::ate | std::ios::binary};

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open compute shader file: " + filepath);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

void ComputePipeline::createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    if (vkCreateShaderModule(device.getDevice(), &createInfo, nullptr, shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute shader module");
    }
}

void ComputePipeline::createComputePipeline(
    const std::string& computeFilepath,
    const ComputePipelineConfigInfo& configInfo
) {
    assert(configInfo.pipelineLayout != VK_NULL_HANDLE && 
           "Cannot create compute pipeline: no pipelineLayout provided in configInfo");

    // Load and create compute shader module
    auto computeCode = readFile(computeFilepath);
    createShaderModule(computeCode, &computeShaderModule);

    // Create compute shader stage
    VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
    computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    computeShaderStageInfo.module = computeShaderModule;
    computeShaderStageInfo.pName = "main";

    // Create compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = computeShaderStageInfo;
    pipelineInfo.layout = configInfo.pipelineLayout;

    if (vkCreateComputePipelines(device.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline");
    }
}

void ComputePipeline::dispatch(
    VkCommandBuffer commandBuffer, 
    uint32_t groupCountX, 
    uint32_t groupCountY, 
    uint32_t groupCountZ
) const {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

} // namespace Rendering
