#include "gbuffer.hpp"
#include "Rendering/Core/barriers.hpp"
#include "Rendering/Core/debug_names.hpp"
#include "Rendering/Core/samplers.hpp"

namespace Rendering {

GBuffer::GBuffer(Device& device, const CreateInfo& createInfo)
    : device{device}, width{createInfo.width}, height{createInfo.height} {
    createAttachments();
    createSampler();
}

GBuffer::~GBuffer() {
    vkDestroySampler(device.getDevice(), sampler, nullptr);
}

void GBuffer::createAttachments() {
    const VkFormatFeatureFlags features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    auto attachment = [&](VkFormat format, const std::string& name) {
        FrameImage::CreateInfo info{};
        info.format = format;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.extent = {width, height};
        info.name = name;
        return std::make_unique<FrameImage>(device, info);
    };

    positionImage =
        attachment(device.findSupportedFormat({VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT},
                                              VK_IMAGE_TILING_OPTIMAL, features),
                   "GBuffer_Position");

    // Normal attachment format (optimized for normal storage)
    normalImage = attachment(device.findSupportedFormat({VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                                                         VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
                                                        VK_IMAGE_TILING_OPTIMAL, features),
                             "GBuffer_Normal");

    // Albedo attachment format (standard color)
    albedoImage = attachment(device.findSupportedFormat({VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM},
                                                        VK_IMAGE_TILING_OPTIMAL, features),
                             "GBuffer_Albedo");

    // Material attachment format (PBR properties)
    materialImage = attachment(device.findSupportedFormat({VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM},
                                                          VK_IMAGE_TILING_OPTIMAL, features),
                               "GBuffer_Material");

    // Initialize all GBuffer images to SHADER_READ_ONLY_OPTIMAL layout
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    for (const FrameImage* image : {positionImage.get(), normalImage.get(), albedoImage.get(), materialImage.get()}) {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            cmdImageBarriers(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             ImageBarrierDesc::undefinedToShaderRead(image->image(i), VK_IMAGE_ASPECT_COLOR_BIT));
        }
    }
    device.endSingleTimeCommands(cmd);
}

void GBuffer::createSampler() {
    sampler = Rendering::createSampler(device.getDevice(), VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                                       VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, 0.0f);

    setDebugName(device.getDevice(), VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, "GBufferSampler");
}

} // namespace Rendering
