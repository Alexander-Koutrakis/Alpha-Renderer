// Descriptor pool, set layouts, set allocation and the per-consumer descriptor writes of RenderingResources.
#include "rendering_resources.hpp"
#include "Engine/log.hpp"
#include "Rendering/Resources/frame_descriptor.hpp"
#include "Scene/scene.hpp"
#include <tuple>
#include <type_traits>

namespace Rendering {

void RenderingResources::createDescriptorPool(const DescriptorBudget& budget) {
    const std::vector<VkDescriptorPoolSize> poolSizes = budget.poolSizes();
    for (const VkDescriptorPoolSize& size : poolSizes) {
        Log::debug("Pool size: type ", size.type, " x", size.descriptorCount);
    }
    Log::debug("Pool sets: ", budget.maxSets());

    descriptorPool = DescriptorPool::Builder(device).fromBudget(budget).build();
}

DescriptorBudget RenderingResources::createDescriptorSetLayouts() {
    // Every FrameDescriptor reserves the sets it will allocate in the budget; the pool is sized from it.
    DescriptorBudget budget;
    auto frameDescriptor = [&](const std::string& name, std::vector<VkDescriptorSetLayoutBinding> bindings,
                               uint32_t frameCount = MAX_FRAMES_IN_FLIGHT, uint32_t setsPerFrame = 1) {
        FrameDescriptor::CreateInfo info{};
        info.bindings = std::move(bindings);
        info.frameCount = frameCount;
        info.setsPerFrame = setsPerFrame;
        info.name = name;
        return std::make_unique<FrameDescriptor>(device, info, budget);
    };

    // Create descriptor set layout for instance storage buffers
    modelsDescriptor =
        frameDescriptor("Models", {layoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                                   layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)});

    //Create descriptor set layout for camera uniform buffer
    cameraDescriptor =
        frameDescriptor("Camera", {layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)});

    gBufferDescriptor = frameDescriptor(
        "GBuffer", {layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                    layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});

    lightArrayDescriptor = frameDescriptor(
        "LightArray", {layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)});

    cascadeSplitsDescriptor = frameDescriptor(
        "CascadeSplits", {layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)});

    sceneLightingDescriptor = frameDescriptor(
        "SceneLighting", {layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)});

    // Create descriptor set layout (shared between both pipelines)
    lightMatrixDescriptor = frameDescriptor(
        "ShadowLightMatrix",
        {layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)});

    // Create descriptor set layout for shadow map samplers
    shadowMapSamplerDescriptor = frameDescriptor(
        "ShadowMapSampler",
        {layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
                       MAX_DIRECTIONAL_LIGHTS),
         layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_SPOT_LIGHTS),
         layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, MAX_POINT_LIGHTS)});

    //Create descriptor set layout for shadow model matrix
    shadowModelMatrixDescriptor = frameDescriptor(
        "ShadowModelMatrix", {layoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)});

    // Skybox: a single set, not per frame. updateSkyboxDescriptorSet writes it.
    skyboxDescriptor = frameDescriptor("Skybox",
                                       {layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                      VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)},
                                       1);

    //Create descriptor set layout for transparency model matrix
    transparencyModelDescriptor = frameDescriptor(
        "TransparencyModel", {layoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                              layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)});

    // Create descriptor set layout for composition textures
    compositionDescriptor = frameDescriptor(
        "Composition", {layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                        layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                        layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                        layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});

    // SMAA edge descriptor set layout (compositionColor input)
    smaaEdgeDescriptor = frameDescriptor(
        "SMAAEdge", {layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});

    // SMAA weight descriptor set layout (edges + area/search LUT)
    smaaWeightDescriptor = frameDescriptor(
        "SMAAWeight", {layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                       layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                       layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});

    // SMAA blend descriptor set layout (compositionColor + blend weights)
    smaaBlendDescriptor = frameDescriptor(
        "SMAABlend", {layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
                      layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});

    // Color correction descriptor set layout (post-AA color input)
    colorCorrectionDescriptor = frameDescriptor(
        "ColorCorrection", {layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)});

    // RC Build descriptor set layout
    // NOTE: β is packed into uRadiance alpha (radiance.rgb, beta.a), so we only need one storage atlas array.
    rcBuildDescriptor = frameDescriptor(
        "RCBuild", {layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                    layoutBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, RC_CASCADE_COUNT)});

    // RC Resolve descriptor set layout
    // NOTE: keep binding numbers stable (skip binding 6) to avoid shifting shader bindings:
    // binding 5 = radiance (rgba16f, beta in alpha), binding 7 = gi out, binding 8/9/10 = history/prev pos/prev normal.
    rcResolveDescriptor = frameDescriptor(
        "RCResolve", {layoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
                      layoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
                      layoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
                      layoutBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
                      layoutBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
                      layoutBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, RC_CASCADE_COUNT),
                      layoutBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT),
                      layoutBinding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                      layoutBinding(9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
                      layoutBinding(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)});

    // Depth pyramid: per frame, set 0 seeds mip 0 from depth and set m (1..N-1) downsamples into mip m.
    depthPyramidDescriptor =
        frameDescriptor("DepthPyramid",
                        {layoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT),
                         layoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)},
                        MAX_FRAMES_IN_FLIGHT, depthPyramidImage->mipLevels());

    return budget;
}

void RenderingResources::allocateDescriptorSets() {
    // In the order the layouts are created; each family allocates all its frames before the next.
    for (FrameDescriptor* descriptor :
         {modelsDescriptor.get(), cameraDescriptor.get(), gBufferDescriptor.get(), lightArrayDescriptor.get(),
          cascadeSplitsDescriptor.get(), sceneLightingDescriptor.get(), lightMatrixDescriptor.get(),
          shadowMapSamplerDescriptor.get(), shadowModelMatrixDescriptor.get(), skyboxDescriptor.get(),
          transparencyModelDescriptor.get(), compositionDescriptor.get(), smaaEdgeDescriptor.get(),
          smaaWeightDescriptor.get(), smaaBlendDescriptor.get(), colorCorrectionDescriptor.get(),
          rcBuildDescriptor.get(), rcResolveDescriptor.get(), depthPyramidDescriptor.get()}) {
        descriptor->allocate(*descriptorPool);
    }
}

void RenderingResources::writeBufferSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Instance buffers
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, modelMatrixBuffers[i]->descriptorInfo())
            .buffer(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, normalMatrixBuffers[i]->descriptorInfo())
            .update(device, modelsDescriptor->set(i));

        // Camera buffer
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cameraUniformBuffers[i]->descriptorInfo())
            .update(device, cameraDescriptor->set(i));

        // Light array buffer
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    lightArrayUniformBuffers[i]->descriptorInfo(lightArrayUniformBuffers[i]->getBufferSize()))
            .update(device, lightArrayDescriptor->set(i));

        // Cascade splits buffer
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    cascadeSplitsBuffers[i]->descriptorInfo(cascadeSplitsBuffers[i]->getBufferSize()))
            .update(device, cascadeSplitsDescriptor->set(i));

        // Scene lighting buffer
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    sceneLightingBuffers[i]->descriptorInfo(sceneLightingBuffers[i]->getBufferSize()))
            .update(device, sceneLightingDescriptor->set(i));

        // Light matrices
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    lightMatrixBuffers[i]->descriptorInfo(lightMatrixBuffers[i]->getBufferSize()))
            .update(device, lightMatrixDescriptor->set(i));

        // Shadow model matrices
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    shadowModelMatrixBuffers[i]->descriptorInfo(shadowModelMatrixBuffers[i]->getBufferSize()))
            .update(device, shadowModelMatrixDescriptor->set(i));

        // Transparency model matrices
        DescriptorWriter()
            .buffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, transparencyModelMatrixBuffers[i]->descriptorInfo())
            .buffer(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, transparencyNormalMatrixBuffers[i]->descriptorInfo())
            .update(device, transparencyModelDescriptor->set(i));
    }
}

void RenderingResources::writeGBufferSets() {
    const VkSampler gBufferSampler = gBuffer->getSampler();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBufferSampler, gBuffer->getPositionView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBufferSampler, gBuffer->getNormalView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBufferSampler, gBuffer->getAlbedoView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBufferSampler, gBuffer->getMaterialView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, gBufferDescriptor->set(i));
    }
}

void RenderingResources::writeShadowMapSamplerSets() {
    // Each frame's set holds that frame's shadow maps.
    for (size_t frameIndex = 0; frameIndex < MAX_FRAMES_IN_FLIGHT; frameIndex++) {
        // createShadowMapResources made every shadow map of every frame, so each array is written in full.
        auto imageInfos = [frameIndex](const auto& maps) {
            std::array<VkDescriptorImageInfo, std::tuple_size_v<std::decay_t<decltype(maps)>>> infos{};
            for (size_t lightIndex = 0; lightIndex < infos.size(); lightIndex++) {
                const ShadowMap& map = *maps[lightIndex][frameIndex];
                infos[lightIndex] = {map.getSampler(), map.getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            }
            return infos;
        };
        const auto directionalImageInfos = imageInfos(directionalMaps);
        const auto spotImageInfos = imageInfos(spotlightMaps);
        const auto pointImageInfos = imageInfos(pointlightMaps);

        DescriptorWriter()
            .images(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, directionalImageInfos.data(),
                    static_cast<uint32_t>(directionalImageInfos.size()))
            .images(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, spotImageInfos.data(),
                    static_cast<uint32_t>(spotImageInfos.size()))
            .images(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, pointImageInfos.data(),
                    static_cast<uint32_t>(pointImageInfos.size()))
            .update(device, shadowMapSamplerDescriptor->set(frameIndex));
    }
}

void RenderingResources::writeCompositionSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // The indirect GI buffer stays in GENERAL because RCGI computes and readers share it within one frame.
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, lightPassResultImage->view(i),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}) // opaque result of the light pass
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, accumulationImage->view(i),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}) // transparency accumulation
            .image(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, revealageImage->view(i),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}) // transparency revealage
            .image(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, giIndirectImage->view(i), VK_IMAGE_LAYOUT_GENERAL}) // indirect GI
            .update(device, compositionDescriptor->set(i));
    }
}

void RenderingResources::writePostProcessSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // SMAA edge pass
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, compositionColorImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, smaaEdgeDescriptor->set(i));

        // SMAA weight pass
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, smaaEdgeImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {smaaAreaSampler, smaaAreaView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {smaaSearchSampler, smaaSearchView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, smaaWeightDescriptor->set(i));

        // SMAA blend pass
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, compositionColorImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, smaaBlendImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, smaaBlendDescriptor->set(i));

        // Color correction pass
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {postProcessSampler, postAAColorImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, colorCorrectionDescriptor->set(i));
    }
}

void RenderingResources::writeDepthPyramidSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // Seed set: source depth is point sampled; the destination is pyramid mip 0 as a storage image,
        // transitioned to GENERAL before the dispatch.
        DescriptorWriter()
            .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {depthPyramidSampler, depthImage->view(i), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL})
            .image(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                   {VK_NULL_HANDLE, depthPyramidImage->mipView(i, 0), VK_IMAGE_LAYOUT_GENERAL})
            .update(device, depthPyramidDescriptor->set(i));

        for (uint32_t m = 1; m < depthPyramidImage->mipLevels(); ++m) {
            // Source: previous mip level (m-1), sampled. It is in SHADER_READ_ONLY_OPTIMAL at dispatch time
            // (transitioned by setMipLevelBarriers).
            // Destination: current mip level (m), written as a storage image. It is in GENERAL at dispatch time
            // (also transitioned by setMipLevelBarriers).
            DescriptorWriter()
                .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                       {depthPyramidSampler, depthPyramidImage->mipView(i, m - 1),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
                .image(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                       {VK_NULL_HANDLE, depthPyramidImage->mipView(i, m), VK_IMAGE_LAYOUT_GENERAL})
                .update(device, depthPyramidDescriptor->set(i, m));
        }
    }
}

void RenderingResources::writeRCSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        // GBuffer images (position, normal, albedo, material), shared by the build and resolve sets.
        const std::array<VkDescriptorImageInfo, 4> gbInfos{{
            {gBuffer->getSampler(), gBuffer->getPositionView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gBuffer->getSampler(), gBuffer->getNormalView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gBuffer->getSampler(), gBuffer->getAlbedoView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {gBuffer->getSampler(), gBuffer->getMaterialView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        }};

        // RC build: RC atlases as a storage image array
        std::vector<VkDescriptorImageInfo> radStorageInfos(RC_CASCADE_COUNT);
        for (uint32_t c = 0; c < RC_CASCADE_COUNT; ++c) {
            radStorageInfos[c] = {VK_NULL_HANDLE, rcRadianceImages[c]->view(i), VK_IMAGE_LAYOUT_GENERAL};
        }

        DescriptorWriter buildWriter;
        buildWriter.buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cameraUniformBuffers[i]->descriptorInfo());
        for (uint32_t b = 0; b < 4; ++b) {
            buildWriter.image(1 + b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, gbInfos[b]);
        }
        // Depth pyramid must use point sampling; the light pass result can use linear.
        buildWriter
            .image(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {depthPyramidSampler, depthPyramidImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, lightIncidentImage->view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .images(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, radStorageInfos.data(), RC_CASCADE_COUNT)
            .update(device, rcBuildDescriptor->set(i));

        // RC resolve: RC atlases as sampled arrays (lightPassSampler as the generic sampler). They stay in GENERAL
        // during build and resolve, so sample from GENERAL to avoid layout mismatches.
        std::vector<VkDescriptorImageInfo> radSampleInfos(RC_CASCADE_COUNT);
        for (uint32_t c = 0; c < RC_CASCADE_COUNT; ++c) {
            radSampleInfos[c] = {lightPassSampler, rcRadianceImages[c]->view(i), VK_IMAGE_LAYOUT_GENERAL};
        }

        // GI history: frame i uses the previous frame's GI output for temporal accumulation, and the previous
        // frame's position and normal buffers for temporal validation.
        DescriptorWriter resolveWriter;
        resolveWriter.buffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cameraUniformBuffers[i]->descriptorInfo());
        for (uint32_t b = 0; b < 4; ++b) {
            resolveWriter.image(1 + b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, gbInfos[b]);
        }
        resolveWriter.images(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, radSampleInfos.data(), RC_CASCADE_COUNT)
            .image(7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                   {VK_NULL_HANDLE, giIndirectImage->view(i), VK_IMAGE_LAYOUT_GENERAL}) // GI output
            .image(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {lightPassSampler, giIndirectImage->previousView(i), VK_IMAGE_LAYOUT_GENERAL})
            .image(
                9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {gBuffer->getSampler(), gBuffer->position().previousView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .image(10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   {gBuffer->getSampler(), gBuffer->normal().previousView(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
            .update(device, rcResolveDescriptor->set(i));
    }
}

void RenderingResources::updateSkyboxDescriptorSet(VkImageView skyboxImageView, VkSampler skyboxSampler) {
    DescriptorWriter()
        .image(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
               {skyboxSampler, skyboxImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL})
        .update(device, skyboxDescriptor->set(0));
}

void RenderingResources::initializeSkyboxFromScene() {
    // Get the scene environment lighting
    const Scene::EnvironmentLighting& envLighting = Scene::Scene::getInstance().getEnvironmentLighting();

    if (envLighting.skyboxTexture != nullptr) {
        // Use the actual skybox cubemap from the scene
        updateSkyboxDescriptorSet(envLighting.skyboxTexture->getImageView(), envLighting.skyboxTexture->getSampler());
    } else {
        // Fallback to placeholder if no skybox texture is found
        Log::warn("No skybox texture found in scene, using placeholder");
        VkImageView placeholderView = gBuffer->getAlbedoView(0);
        VkSampler placeholderSampler = lightPassSampler;
        updateSkyboxDescriptorSet(placeholderView, placeholderSampler);
    }
}

} // namespace Rendering
