#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/buffer.hpp"
#include <memory>
namespace Rendering {

class Texture {
public:

    Texture(
        Device& device,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties,
        const void* data,
        uint32_t faces = 1,
        const std::string& debugName = "");
    ~Texture();

    // Delete copy constructors
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Get descriptor info for binding texture to shaders
    VkDescriptorImageInfo getDescriptorInfo() const;  
    VkImageView getImageView() const { return imageView; }
    VkSampler getSampler() const { return sampler; }
    VkImage getImage() const { return image; }
    uint32_t getMipLevels() const { return mipLevels; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    VkFormat getFormat() const { return imageFormat; }
    
    static std::unique_ptr<Texture> createCubemap(
        Device& device,
        uint32_t size,  // cubemap faces are square
        VkFormat format,
        const std::vector<const void*>& faceData  // array of 6 face data pointers  
    );

    Texture(
        Device& device,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImage existingImage,
        VkImageView existingImageView,
        VkDeviceMemory existingImageMemory,
        uint32_t existingMipLevels,
        const std::string& debugName = ""
    );

protected:
    // Protected constructor for internal use
    explicit Texture(Device& device) : device{device} {}
    friend std::unique_ptr<Texture> createCubemap(
        Device& device,
        uint32_t size,
        VkFormat format,
        const std::vector<const void*>& faceData
    );
private:
    void setDebugName(VkObjectType objectType, uint64_t handle, const std::string& name);
    
    void createTextureImageView();
    void createTextureSampler();
    void transitionImageLayout(VkImage vkImage,VkImageLayout oldLayout,VkImageLayout newLayout,uint32_t layerCount = 1);
    void generateMipmaps();
    static uint32_t getFormatSize(VkFormat format);
    
    void createCubemapView();
    void createCubemapSampler();
    
    bool isBlockCompressedFormat(VkFormat format);
    
    Device& device;
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory imageMemory{VK_NULL_HANDLE};
    VkImageView imageView{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE};
    
    VkFormat imageFormat;
    uint32_t mipLevels{1};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t channels{0};
    VkImageLayout imageLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    std::string textureName;


};

} // namespace Rendering