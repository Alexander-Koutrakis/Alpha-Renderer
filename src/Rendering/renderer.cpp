class AlphaEngine;

#include "renderer.hpp"
#include "Engine/alpha_engine.hpp"
#include <iostream>
#include <array>


using namespace ECS;
using namespace Systems;
namespace Rendering {

    Renderer::Renderer(Window& window, Device& device) 
        : window{window}, device{device} {
        recreateSwapChain();
        recreateWindowDependentResources();
        createCommandBuffers();
        
        // Initialize ImGui after swap chain and command buffers are ready
        imguiManager = std::make_unique<ImGuiManager>(
            device, 
            window, 
            *swapChain, 
            static_cast<uint32_t>(swapChain->imageCount())
        );
    }

    Renderer::~Renderer() {
        // Cleanup ImGui first
        imguiManager.reset();
        
        cleanupWindowDependentResources();
        freeCommandBuffers();
        swapChain.reset();
    }

    void Renderer::recreateSwapChain() {
        auto extent = window.getExtent();
        // Wait for window to be restored if minimized
        while (window.isMinimized() || extent.width == 0 || extent.height == 0) {
            glfwWaitEvents();
            extent = window.getExtent();
            // If window is closing, break to avoid infinite loop
            if (window.shouldClose()) {
                return;
            }
        }
        
        vkDeviceWaitIdle(device.getDevice());

        if (swapChain == nullptr) {
            swapChain = std::make_shared<SwapChain>(device, extent);
        } else {
            std::shared_ptr<SwapChain> oldSwapChain = std::move(swapChain);
            swapChain = std::make_shared<SwapChain>(device, extent, oldSwapChain);
        }
    }

    void Renderer::cleanupWindowDependentResources() {
        if (gBuffer) gBuffer.reset();
        if (geometryPass) geometryPass.reset();
        if (skyboxPass) skyboxPass.reset();
        if (transparencyPass) transparencyPass.reset();
        if (shadowmapPass) shadowmapPass.reset();
        if (lightPass) lightPass.reset();
        if (rcgiPass) rcgiPass.reset();
        if (compositionPass) compositionPass.reset();
        if (smaaEdgePass) smaaEdgePass.reset();
        if (smaaWeightPass) smaaWeightPass.reset();
        if (smaaBlendPass) smaaBlendPass.reset();
        if (colorCorrectionPass) colorCorrectionPass.reset();
    }

    void Renderer::recreateWindowDependentResources() {
        createRenderingResources();
        createGeometryPass();          
        createShadowPass();
        createSkyboxPass();
        createLightPass();    
        createRCGIPass();
        createTransparencyPass();
        createCompositionPass();
        createSMAAPasses();
        createColorCorrectionPass();
    }

    void Renderer::handleWindowResize() {
        // Don't handle resize if window is minimized - wait until it's restored
        if (window.isMinimized() || window.getExtent().width == 0 || window.getExtent().height == 0) {
            framebufferResized = false;
            return;
        }
        
        vkDeviceWaitIdle(device.getDevice());
        
        // First clean up resources that depend on the swapchain
        cleanupWindowDependentResources();
        
        // Then recreate the swapchain
        recreateSwapChain();
        
        // Check if swapchain was created successfully (might be null if window closed)
        if (swapChain == nullptr) {
            framebufferResized = false;
            return;
        }
        
        // Finally recreate all the dependent resources with the new dimensions
        recreateWindowDependentResources();
        
        // Notify ImGui about the resize
        imguiManager->onWindowResize(*swapChain);

        
        framebufferResized = false;
    }

    void Renderer::createCommandBuffers() {
        commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = device.getCommandPool();
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

        if (vkAllocateCommandBuffers(device.getDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
        }
    }

    void Renderer::freeCommandBuffers() {
        vkFreeCommandBuffers(
            device.getDevice(),
            device.getCommandPool(),
            static_cast<uint32_t>(commandBuffers.size()),
            commandBuffers.data());
        commandBuffers.clear();
    }

    VkCommandBuffer Renderer::beginFrame() {
        assert(!isFrameStarted && "Can't call beginFrame while frame is already in progress");

        // Skip if window is minimized
        if (window.isMinimized() || window.getExtent().width == 0 || window.getExtent().height == 0) {
            return nullptr;
        }

        // Check if window was resized before acquiring the next image
        if (framebufferResized || window.wasWindowResized()) {
            window.resetWindowResizedFlag();
            handleWindowResize();
            return nullptr;
        }

        auto result = swapChain->acquireNextImage(&currentImageIndex);
        
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            handleWindowResize();
            return nullptr;
        }

        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        isFrameStarted = true;
        
        auto commandBuffer = getCurrentCommandBuffer();
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer!");
        }
        
        return commandBuffer;
    }

    void Renderer::endFrame() {
        assert(isFrameStarted && "Can't call endFrame while frame is not in progress");
        
        // Safety check: if window became minimized during frame, skip presentation
        if (window.isMinimized() || swapChain == nullptr) {
            isFrameStarted = false;
            return;
        }
        
        auto commandBuffer = getCurrentCommandBuffer();
        
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer!");
        }

        auto result = swapChain->submitCommandBuffers(&commandBuffer, &currentImageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || 
            window.wasWindowResized()) {
            window.resetWindowResizedFlag();
            handleWindowResize();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image!");
        }

        isFrameStarted = false;
        currentFrameIndex = (currentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::createShadowPass(){     
        ShadowPass::CreateInfo createInfo{};
        createInfo.lightMatrixDescriptorSetLayout = renderingResources->getShadowcastingLightMatrixDescriptorSetLayout();
        createInfo.shadowModelMatrixDescriptorSetLayout = renderingResources->getShadowModelMatrixDescriptorSetLayout();
        createInfo.materialDescriptorSetLayout = renderingResources->getMaterialDescriptorSetLayout();
        createInfo.directionalShadowMaps = &renderingResources->getDirectionalLightMaps();
        createInfo.pointShadowMaps = &renderingResources->getPointLightMaps();
        createInfo.spotShadowMaps = &renderingResources->getSpotLightMaps();
        shadowmapPass = std::make_unique<ShadowPass>(device,createInfo);
    }

    void Renderer::createGeometryPass(){
        GeometryPass::CreateInfo createInfo{};
        createInfo.width=swapChain->getExtent().width;
        createInfo.height=swapChain->getExtent().height;
        createInfo.gBuffer=&renderingResources->getGBuffer();
        createInfo.depthViewsPtr=&renderingResources->getDepthViews();
        createInfo.depthFormat=renderingResources->getDepthFormat();
        createInfo.positionFormat=renderingResources->getPositionFormat();
        createInfo.normalFormat=renderingResources->getNormalFormat();
        createInfo.albedoFormat=renderingResources->getAlbedoFormat();
        createInfo.materialFormat=renderingResources->getMaterialFormat();
        createInfo.cameraDescriptorSetLayout=renderingResources->getCameraDescriptorSetLayout();
        createInfo.modelsDescriptorSetLayout=renderingResources->getModelsDescriptorSetLayout();
        createInfo.materialDescriptorSetLayout=renderingResources->getMaterialDescriptorSetLayout();
        geometryPass=std::make_unique<GeometryPass>(device,createInfo);
    }

    void Renderer::createSkyboxPass() {
        SkyboxPass::CreateInfo createInfo{};
        createInfo.width = swapChain->getExtent().width;
        createInfo.height = swapChain->getExtent().height;
        createInfo.cameraDescriptorSetLayout = renderingResources->getCameraDescriptorSetLayout();
        createInfo.skyboxDescriptorSetLayout = renderingResources->getSkyboxDescriptorSetLayout();
        createInfo.albedoFormat = renderingResources->getAlbedoFormat();
        createInfo.albedoViewsPtr = &renderingResources->getAlbedoViews();
        createInfo.depthViewsPtr = &renderingResources->getDepthViews();
        skyboxPass = std::make_unique<SkyboxPass>(device, createInfo);
    }

    void Renderer::createLightPass(){
        LightPass::CreateInfo createInfo{};
        createInfo.width = swapChain->getExtent().width;
        createInfo.height = swapChain->getExtent().height;
        createInfo.cameraDescriptorSetLayout = renderingResources->getCameraDescriptorSetLayout();
        createInfo.modelsDescriptorSetLayout = renderingResources->getModelsDescriptorSetLayout();
        createInfo.materialDescriptorSetLayout = renderingResources->getMaterialDescriptorSetLayout();
        createInfo.gBufferDescriptorSetLayout = renderingResources->getGBufferDescriptorSetLayout();
        createInfo.lightArrayDescriptorSetLayout = renderingResources->getLightArrayDescriptorSetLayout();
        createInfo.cascadeSplitsSetLayout = renderingResources->getCascadeSplitsDescriptorSetLayout();
        createInfo.sceneLightingDescriptorSetLayout = renderingResources->getSceneLightingDescriptorSetLayout();
        createInfo.shadowSamplerSetLayout = renderingResources->getShadowSamplerDescriptorSetLayout();
        createInfo.shadowMatrixSetLayout = renderingResources->getShadowcastingLightMatrixDescriptorSetLayout();
        createInfo.enviromentalReflectionsSetLayout = renderingResources->getEnvironmentalReflectionsDescriptorSetLayout();
        createInfo.lightPassFormat = renderingResources->getHDRFormat();
        createInfo.lightPassResultViewsPtr = &renderingResources->getLightPassResultViews();
        createInfo.lightIncidentViewsPtr = &renderingResources->getLightIncidentViews();
        
        lightPass=std::make_unique<LightPass>(
            device,
            *swapChain,
            createInfo);
    }

    void Renderer::createRCGIPass() {
        RCGIPass::CreateInfo createInfo{};
        createInfo.width = swapChain->getExtent().width;
        createInfo.height = swapChain->getExtent().height;
        createInfo.depthPyramidSetLayout = renderingResources->getDepthPyramidDescriptorSetLayout();
        createInfo.depthPyramidFormat = renderingResources->getDepthPyramidFormat();
        createInfo.rcBuildSetLayout = renderingResources->getRCBuildDescriptorSetLayout();
        createInfo.rcResolveSetLayout = renderingResources->getRCResolveDescriptorSetLayout();
        createInfo.skyboxSetLayout = renderingResources->getSkyboxDescriptorSetLayout();
        rcgiPass = std::make_unique<RCGIPass>(device, createInfo);
    }

    void Renderer::createRenderingResources(){
        renderingResources = std::make_unique<RenderingResources>(device,*swapChain);
        frameContexts = renderingResources->createFrameContexts();
    }

    void Renderer::createTransparencyPass() {
        TransparencyPass::CreateInfo createInfo{};
        createInfo.width = swapChain->getExtent().width;
        createInfo.height = swapChain->getExtent().height;
        createInfo.cameraDescriptorSetLayout = renderingResources->getCameraDescriptorSetLayout();
        createInfo.lightArrayDescriptorSetLayout = renderingResources->getLightArrayDescriptorSetLayout();
        createInfo.shadowMapSamplerLayout = renderingResources->getShadowSamplerDescriptorSetLayout();
        createInfo.transparencyModelDescriptorSetLayout = renderingResources->getTransparencyModelDescriptorSetLayout();
        createInfo.materialDescriptorSetLayout = renderingResources->getMaterialDescriptorSetLayout();
        createInfo.sceneLightingDescriptorSetLayout = renderingResources->getSceneLightingDescriptorSetLayout();
        createInfo.lightMatrixDescriptorSetLayout = renderingResources->getShadowcastingLightMatrixDescriptorSetLayout();
        createInfo.cascadeSplitsDescriptorSetLayout = renderingResources->getCascadeSplitsDescriptorSetLayout();
        createInfo.hdrFormat = renderingResources->getHDRFormat();
        createInfo.revealageFormat = renderingResources->getRevealageFormat();
        createInfo.depthFormat = renderingResources->getDepthFormat();
        createInfo.accumulationViewsPtr = &renderingResources->getAccumulationViews();
        createInfo.revealageViewsPtr = &renderingResources->getRevealageViews();
        createInfo.depthViewsPtr = &renderingResources->getDepthViews();
        transparencyPass = std::make_unique<TransparencyPass>(
            device, 
            createInfo);
    }
    
    void Renderer::createCompositionPass() {
        CompositionPass::CreateInfo createInfo{};
        createInfo.width = swapChain->getExtent().width;
        createInfo.height = swapChain->getExtent().height;
        createInfo.compositionDescriptorSetLayout = renderingResources->getCompositionDescriptorSetLayout();
        createInfo.targetFormat = renderingResources->getHDRFormat();
        createInfo.targetViews = &renderingResources->getCompositionColorViews();
        compositionPass = std::make_unique<CompositionPass>(device, createInfo);
    }

    void Renderer::createSMAAPasses() {
        const uint32_t w = swapChain->getExtent().width;
        const uint32_t h = swapChain->getExtent().height;

        SMAAEdgePass::CreateInfo edgeInfo{};
        edgeInfo.width = w;
        edgeInfo.height = h;
        edgeInfo.targetFormat = renderingResources->getSMAAEdgeFormat();
        edgeInfo.descriptorSetLayout = renderingResources->getSMAAEdgeSetLayout();
        edgeInfo.targetViews = &renderingResources->getSMAAEdgeViews();
        smaaEdgePass = std::make_unique<SMAAEdgePass>(device, edgeInfo);

        SMAAWeightPass::CreateInfo weightInfo{};
        weightInfo.width = w;
        weightInfo.height = h;
        weightInfo.targetFormat = renderingResources->getSMAABlendFormat();
        weightInfo.descriptorSetLayout = renderingResources->getSMAAWeightSetLayout();
        weightInfo.targetViews = &renderingResources->getSMAABlendViews();
        smaaWeightPass = std::make_unique<SMAAWeightPass>(device, weightInfo);

        SMAABlendPass::CreateInfo blendInfo{};
        blendInfo.width = w;
        blendInfo.height = h;
        blendInfo.targetFormat = renderingResources->getPostProcessFormat();
        blendInfo.descriptorSetLayout = renderingResources->getSMAABlendSetLayout();
        blendInfo.targetViews = &renderingResources->getPostAAColorViews();
        smaaBlendPass = std::make_unique<SMAABlendPass>(device, blendInfo);
    }

    void Renderer::createColorCorrectionPass() {
        const uint32_t w = swapChain->getExtent().width;
        const uint32_t h = swapChain->getExtent().height;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            swapchainImageViews[i] = swapChain->getImageView(static_cast<uint32_t>(i));
        }

        ColorCorrectionPass::CreateInfo info{};
        info.width = w;
        info.height = h;
        info.targetFormat = swapChain->getSwapChainImageFormat();
        info.descriptorSetLayout = renderingResources->getColorCorrectionSetLayout();
        info.targetViews = &swapchainImageViews;

        colorCorrectionPass = std::make_unique<ColorCorrectionPass>(device, info);
    }

    void Renderer::run(){
        // Skip rendering if window is minimized or has zero extent
        if (window.isMinimized() || window.getExtent().width == 0 || window.getExtent().height == 0) {
            return;
        }

        // Begin frame
        VkCommandBuffer commandBuffer = beginFrame();
        if (commandBuffer == nullptr) {
            return;
        }
        
        // Get the current frame context (match resources to the acquired swapchain image)
        FrameContext& frameContext = frameContexts[currentImageIndex];
        updateFrameContext(commandBuffer, frameContext);

        shadowmapPass->run(frameContext);
        geometryPass->run(frameContext);
        skyboxPass->run(frameContext);
        lightPass->run(frameContext);
        rcgiPass->run(frameContext);
        transparencyPass->run(frameContext);
        compositionPass->run(frameContext);
        smaaEdgePass->run(frameContext);
        smaaWeightPass->run(frameContext);
        smaaBlendPass->run(frameContext);
        colorCorrectionPass->run(frameContext);

        // Render ImGui overlay
        imguiManager->run(commandBuffer, currentImageIndex);

        endFrame();
    }

    void Renderer::updateFrameContext(VkCommandBuffer commandBuffer, FrameContext& frameContext){
        
        auto& ecsManager = ECSManager::getInstance();   
        Camera& camera=*ecsManager.getFirstComponent<Camera>();
        Transform& transform=*ecsManager.getComponent<Transform>(camera.owner);
        
        // Camera data
        frameContext.cameraData.viewMatrix=camera.viewMatrix;
        frameContext.cameraData.viewProjectionMatrix=camera.viewProjectionMatrix;
        frameContext.cameraData.projectionMatrix=camera.projectionMatrix;
        frameContext.cameraData.position=transform.position;
        frameContext.cameraData.nearPlane=camera.nearPlane;
        frameContext.cameraData.farPlane=camera.farPlane;
        frameContext.cameraData.fov=camera.fov;
        frameContext.cameraData.aspectRatio=camera.aspectRatio;
        frameContext.cameraData.invViewMatrix=glm::inverse(camera.viewMatrix);
        frameContext.cameraData.invProjectionMatrix=glm::inverse(camera.projectionMatrix);
        
        frameContext.commandBuffer=commandBuffer;
        frameContext.frameIndex=currentImageIndex;
        frameContext.extent = swapChain->getExtent();
        frameContext.frameTime=AlphaEngine::getDeltaTime();

        // Temporal accumulation: set previous camera data for reprojection
        // (prevViewProjMatrix contains last frame's matrix, current frame's is already in cameraData)
        if (hasPreviousFrame) {
            frameContext.prevCameraData.viewProjectionMatrix = prevViewProjMatrix;
            frameContext.prevCameraData.invViewProjectionMatrix = glm::inverse(prevViewProjMatrix);
        } else {
            // First frame: use current matrices (no history available, temporal blend will be 1.0)
            frameContext.prevCameraData.viewProjectionMatrix = camera.viewProjectionMatrix;
            frameContext.prevCameraData.invViewProjectionMatrix = glm::inverse(camera.viewProjectionMatrix);
        }
        
        // Update temporal frame index for jittering
        frameContext.temporalFrameIndex = temporalFrameCounter++;
        
        // Store current view-proj for next frame's history (after rendering completes)
        prevViewProjMatrix = camera.viewProjectionMatrix;
        hasPreviousFrame = true;

        CameraUbo cameraUbo = {
            frameContext.cameraData.viewMatrix, 
            frameContext.cameraData.projectionMatrix, 
            frameContext.cameraData.viewProjectionMatrix ,
            glm::vec4(frameContext.cameraData.position,1.0f),
            glm::vec4(frameContext.cameraData.nearPlane,frameContext.cameraData.farPlane,frameContext.cameraData.farPlane - frameContext.cameraData.nearPlane,frameContext.cameraData.nearPlane * frameContext.cameraData.farPlane)};
        frameContext.cameraUniformBuffer->writeToBuffer(&cameraUbo,sizeof(CameraUbo));
        frameContext.cameraData.viewFrustum=CameraSystem::createFrustumFromCamera(camera);       
        
        CameraCulling::updateFrameContext(frameContext);
        LightSystem::updateFrameContext(frameContext);

    }

} 