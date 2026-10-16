// RenderingResources::createFrameContexts: the per-frame handles each pass reads.
#include "rendering_resources.hpp"

namespace Rendering {

std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> RenderingResources::createFrameContexts() {
    std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> contexts;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        FrameContext& ctx = contexts[i];

        // Core frame data
        ctx.frameIndex = i;
        ctx.commandBuffer = VK_NULL_HANDLE; // Will be set by Renderer
        ctx.extent = {0, 0};                // Will be set by Renderer
        ctx.frameTime = 0.0f;               // Will be set by Renderer

        // Descriptor sets
        ctx.cameraDescriptorSet = cameraDescriptor->set(i);
        ctx.modelsDescriptorSet = modelsDescriptor->set(i);
        ctx.gBufferDescriptorSet = gBufferDescriptor->set(i);
        ctx.lightArrayDescriptorSet = lightArrayDescriptor->set(i);
        ctx.cascadeSplitsDescriptorSet = cascadeSplitsDescriptor->set(i);
        ctx.sceneLightingDescriptorSet = sceneLightingDescriptor->set(i);
        ctx.lightMatrixDescriptorSet = lightMatrixDescriptor->set(i);
        ctx.shadowModelMatrixDescriptorSet = shadowModelMatrixDescriptor->set(i);
        ctx.shadowMapSamplerDescriptorSet = shadowMapSamplerDescriptor->set(i);
        ctx.skyboxDescriptorSet = skyboxDescriptor->set(0); // Single set, not per frame
        ctx.transparencyModelDescriptorSet = transparencyModelDescriptor->set(i);
        ctx.compositionDescriptorSet = compositionDescriptor->set(i);
        ctx.depthPyramidDescriptorSet = depthPyramidDescriptor->set(i);
        ctx.rcBuildDescriptorSet = rcBuildDescriptor->set(i);
        ctx.rcResolveDescriptorSet = rcResolveDescriptor->set(i);
        ctx.smaaEdgeDescriptorSet = smaaEdgeDescriptor->set(i);
        ctx.smaaWeightDescriptorSet = smaaWeightDescriptor->set(i);
        ctx.smaaBlendDescriptorSet = smaaBlendDescriptor->set(i);
        ctx.colorCorrectionDescriptorSet = colorCorrectionDescriptor->set(i);

        // Buffers
        ctx.cameraUniformBuffer = cameraUniformBuffers[i].get();
        ctx.modelMatrixBuffer = modelMatrixBuffers[i].get();
        ctx.normalMatrixBuffer = normalMatrixBuffers[i].get();
        ctx.lightArrayUniformBuffer = lightArrayUniformBuffers[i].get();
        ctx.cascadeSplitsBuffer = cascadeSplitsBuffers[i].get();
        ctx.sceneLightingBuffer = sceneLightingBuffers[i].get();
        ctx.lightMatrixBuffer = lightMatrixBuffers[i].get();
        ctx.shadowModelMatrixBuffer = shadowModelMatrixBuffers[i].get();
        ctx.transparencyModelMatrixBuffer = transparencyModelMatrixBuffers[i].get();
        ctx.transparencyNormalMatrixBuffer = transparencyNormalMatrixBuffers[i].get();

        // Depth resources
        ctx.depthView = depthImage->view(i);
        ctx.depthImage = depthImage->image(i);
        // Depth pyramid
        ctx.depthPyramidView = depthPyramidImage->view(i);
        ctx.depthPyramidImage = depthPyramidImage->image(i);
        ctx.depthPyramidMipLevels = depthPyramidImage->mipLevels();
        ctx.depthPyramidMipStorageViews = depthPyramidImage->mipViews(i);
        ctx.depthPyramidMipDescriptorSets = depthPyramidDescriptor->sets(i);

        // Light pass resources
        ctx.lightPassResultView = lightPassResultImage->view(i);
        ctx.lightPassSampler = lightPassSampler; // Single sampler, not per frame
        ctx.lightIncidentView = lightIncidentImage->view(i);

        // Transparency resources
        ctx.accumulationView = accumulationImage->view(i);
        ctx.revealageView = revealageImage->view(i);

        // GI indirect buffer
        ctx.giIndirectView = giIndirectImage->view(i);
        ctx.giIndirectImage = giIndirectImage->image(i);

        // Post-process render targets
        ctx.compositionColorView = compositionColorImage->view(i);
        ctx.compositionColorImage = compositionColorImage->image(i);
        ctx.smaaEdgeView = smaaEdgeImage->view(i);
        ctx.smaaEdgeImage = smaaEdgeImage->image(i);
        ctx.smaaBlendView = smaaBlendImage->view(i);
        ctx.smaaBlendImage = smaaBlendImage->image(i);
        ctx.postAAColorView = postAAColorImage->view(i);
        ctx.postAAColorImage = postAAColorImage->image(i);

        // GI history for temporal accumulation (previous frame's output)
        ctx.giHistoryView = giIndirectImage->previousView(i);
        ctx.giHistorySampler = lightPassSampler;

        // Initialize temporal frame index
        ctx.temporalFrameIndex = 0;

        // GBuffer resources
        ctx.gBufferPositionView = gBuffer->getPositionView(i);
        ctx.gBufferNormalView = gBuffer->getNormalView(i);
        ctx.gBufferAlbedoView = gBuffer->getAlbedoView(i);
        ctx.gBufferMaterialView = gBuffer->getMaterialView(i);
        ctx.gBufferPositionImage = gBuffer->getPositionImage(i);
        ctx.gBufferNormalImage = gBuffer->getNormalImage(i);
        ctx.gBufferAlbedoImage = gBuffer->getAlbedoImage(i);
        ctx.gbufferMaterialImage = gBuffer->getMaterialImage(i);

        // RC atlas views for this frame
        for (uint32_t cascade = 0; cascade < RC_CASCADE_COUNT; ++cascade) {
            ctx.rcRadianceViews[cascade] = rcRadianceImages[cascade]->view(i);
        }

        // Shadow map references - now frame-specific shadow maps
        for (size_t j = 0; j < MAX_DIRECTIONAL_LIGHTS; j++) {
            ctx.directionalShadowMaps[j] = directionalMaps[j][i].get(); // [lightIndex][frameIndex]
        }
        for (size_t j = 0; j < MAX_SPOT_LIGHTS; j++) {
            ctx.spotShadowMaps[j] = spotlightMaps[j][i].get(); // [lightIndex][frameIndex]
        }
        for (size_t j = 0; j < MAX_POINT_LIGHTS; j++) {
            ctx.pointShadowMaps[j] = pointlightMaps[j][i].get(); // [lightIndex][frameIndex]
        }

        // Material batches (will be populated by Renderer during updates)
        ctx.opaqueMaterialBatchCount = 0;
    }

    return contexts;
}

} // namespace Rendering
