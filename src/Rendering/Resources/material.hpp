#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Resources/texture.hpp"
#include "Rendering/Core/pipeline.hpp"
#include "Rendering/Core/buffer.hpp"
#include "Rendering/Core/descriptors.hpp"

#include <memory>
#include <mutex>

#include "core.hpp"

namespace Rendering {

enum class TransparencyType {
    TYPE_OPAQUE,
    TYPE_MASK,
    TYPE_TRANSPARENT
};

struct MaterialUbo {
		alignas(16) glm::vec4 albedoColor{1.0f}; 
		alignas(4) float metallic{0.0f};
		alignas(4) float smoothness{0.5f};
		alignas(4) float ao{1.0f};
		alignas(4) float alphaCutoff{0.5f};
		alignas(4) int isMasked{0};
		alignas(4) int isEmissive{0};
        alignas(4) int hasAlbedoMap{0};
        alignas(4) int hasMetallicSmoothnessMap{0};
        alignas(4) int hasNormalMap{0};
        alignas(4) int hasOcclusionMap{0};
		alignas(4) float normalStrength{1.0};
	};



class Material {
    public:
        struct MaterialInfo {
            std::string name;
            TransparencyType transparencyType;
            MaterialUbo properties;
            bool enableGPUInstancing{false};
        };

        Material(
            Device& device,
            const MaterialInfo& materialInfo,
            DescriptorPool& descriptorPool,
            VkDescriptorSetLayout materialSetLayout
        );
        ~Material();

        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;

        // Texture setters
        void setAlbedoTexture(Texture* texture);
        void setNormalTexture(Texture* texture);
        void setMetallicSmoothnessTexture(Texture* texture);
        void setOcclusionTexture(Texture* texture);

        // Property setters
        void setAlbedoColor(glm::vec4 color);
        void setMetallic(float metallic);
        void setSmoothness(float roughness);
        void setAO(float ao);
        void setAlpha(float alpha);
        // Getters
        const std::string& getName() const { return info.name; }
        const MaterialUbo& getProperties() const { return properties; }
        VkDescriptorSet getMaterialDescriptorSet() const { return materialDescriptorSet; }
        TransparencyType getTransparencyType() const { return transparencyType; }
        bool isGPUInstancingEnabled() const { return info.enableGPUInstancing; }

        // Static cleanup function for default texture
        static void cleanupDefaultTexture();

    private:
        // Static default texture shared by all materials
        static std::unique_ptr<Texture> s_defaultTexture;
        static std::once_flag s_defaultTextureInitFlag;
        void createMaterialDescriptorSet();
        void updateDescriptorSet();
        void setDebugName(VkObjectType objectType, uint64_t handle, const std::string& name);

        Device& device;
        MaterialInfo info;
        MaterialUbo properties;

        std::unique_ptr<Buffer> propertiesBuffer{nullptr};
        DescriptorPool& descriptorPool;
        VkDescriptorSet materialDescriptorSet{VK_NULL_HANDLE};
        VkDescriptorSetLayout materialSetLayout{VK_NULL_HANDLE};
        
        // Textures
        Texture* albedoTexture{nullptr};
        Texture* normalTexture{nullptr};
        Texture* metallicSmoothnessTexture{nullptr};
        Texture* occlusionTexture{nullptr};

        TransparencyType transparencyType;
};

}