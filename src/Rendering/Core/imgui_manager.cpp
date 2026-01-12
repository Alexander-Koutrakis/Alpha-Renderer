#include "imgui_manager.hpp"
#include <stdexcept>
#include <array>
#include <iostream>

namespace Rendering {

ImGuiManager::ImGuiManager(Device& device, Window& window, SwapChain& swapChain, uint32_t imageCount)
    : device{device} {
    createDescriptorPool();
    createRenderPass(swapChain);
    createFramebuffers(swapChain);
    initImGui(window, imageCount);
    initialized = true;
}

ImGuiManager::~ImGuiManager() {
    cleanup();
}

void ImGuiManager::createDescriptorPool() {
    // Create descriptor pool for ImGui
    // ImGui needs a fairly large pool for its internal descriptor sets
    std::array<VkDescriptorPoolSize, 11> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(device.getDevice(), &poolInfo, nullptr, &imguiDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool!");
    }
}

void ImGuiManager::createRenderPass(SwapChain& swapChain) {
    // Create a simple render pass for ImGui that renders directly to the swap chain
    VkAttachmentDescription attachment{};
    attachment.format = swapChain.getSwapChainImageFormat();
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Load existing content (our rendered scene)
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device.getDevice(), &renderPassInfo, nullptr, &imguiRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui render pass!");
    }
}

void ImGuiManager::createFramebuffers(SwapChain& swapChain) {
    framebuffers.resize(swapChain.imageCount());
    
    for (size_t i = 0; i < swapChain.imageCount(); i++) {
        VkImageView attachments[] = {
            swapChain.getImageView(i)
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = imguiRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapChain.getExtent().width;
        framebufferInfo.height = swapChain.getExtent().height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device.getDevice(), &framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui framebuffer!");
        }
    }
}

void ImGuiManager::initImGui(Window& window, uint32_t imageCount) {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // Alternative: ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window.getGLFWwindow(), true);
    
    // Initialize ImGui Vulkan backend
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = device.getInstance();
    initInfo.PhysicalDevice = device.getPhysicalDevice();
    initInfo.Device = device.getDevice();
    initInfo.QueueFamily = device.findPhysicalQueueFamilies().graphicsFamily;
    initInfo.Queue = device.getGraphicsQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = imguiDescriptorPool;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = nullptr;
    
    // Set up pipeline info for the main viewport
    initInfo.PipelineInfoMain.RenderPass = imguiRenderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = false;

    ImGui_ImplVulkan_Init(&initInfo);
    
    // Font texture is now automatically created on first NewFrame() call
}

void ImGuiManager::run(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    beginFrame();
    
    // Render all ImGui UI elements here
    renderFPSCounter();
    
    endFrame(commandBuffer, imageIndex);
}

void ImGuiManager::setFrameStats(float fps, float frameTime) {
    currentFPS = fps;
    currentFrameTime = frameTime;
}

void ImGuiManager::beginFrame() {
    // Start the Dear ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::endFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    // Render ImGui
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    
    // Begin ImGui render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = imguiRenderPass;
    renderPassInfo.framebuffer = framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent.width = static_cast<uint32_t>(drawData->DisplaySize.x);
    renderPassInfo.renderArea.extent.height = static_cast<uint32_t>(drawData->DisplaySize.y);
    
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Record ImGui draw commands into the command buffer
    ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
    
    vkCmdEndRenderPass(commandBuffer);
}

void ImGuiManager::renderFPSCounter() {
    // Create a window in the top-right corner
    ImGuiIO& io = ImGui::GetIO();
    
    // Set window position to top-right
    ImGui::SetNextWindowPos(ImVec2(10.0f, io.DisplaySize.y - 10.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    
    // Create a small, semi-transparent window
    ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
    
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | 
                                   ImGuiWindowFlags_AlwaysAutoResize | 
                                   ImGuiWindowFlags_NoSavedSettings | 
                                   ImGuiWindowFlags_NoFocusOnAppearing | 
                                   ImGuiWindowFlags_NoNav;
    
    if (ImGui::Begin("FPS Counter", nullptr, windowFlags)) {
        ImGui::Text("FPS: %.1f", currentFPS);
        ImGui::Text("Frame Time: %.2f ms", currentFrameTime);
    }
    ImGui::End();
}

void ImGuiManager::onWindowResize(SwapChain& swapChain) {
    // Cleanup old framebuffers
    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device.getDevice(), framebuffer, nullptr);
    }
    framebuffers.clear();
    
    // Recreate framebuffers with new swap chain
    createFramebuffers(swapChain);
}

void ImGuiManager::cleanup() {
    if (initialized) {

        vkDeviceWaitIdle(device.getDevice());
    
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        
        // Cleanup framebuffers
        for (auto framebuffer : framebuffers) {
            vkDestroyFramebuffer(device.getDevice(), framebuffer, nullptr);
        }
        framebuffers.clear();
        
        if (imguiRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device.getDevice(), imguiRenderPass, nullptr);
            imguiRenderPass = VK_NULL_HANDLE;
        }
        
        if (imguiDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device.getDevice(), imguiDescriptorPool, nullptr);
            imguiDescriptorPool = VK_NULL_HANDLE;
        }
        
        initialized = false;
    }
}

} // namespace Rendering

