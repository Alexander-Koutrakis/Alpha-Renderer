#pragma once

#include "Rendering/Core/device.hpp"
#include "Rendering/Core/buffer.hpp"
#include <memory>
#include <vector>
#include "Systems/bounding_box_system.hpp"
#include "Math/AABB.hpp"
#include "core.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"

namespace Rendering {
    class Mesh {
    public:
       
        struct Submesh {
            uint32_t indexStart;     // First index in the index buffer
            uint32_t indexCount;     // Number of indices in this submesh
        };

        struct Vertex {
            glm::vec3 position{};
            glm::vec3 normal{};
            glm::vec2 uv{};
            glm::vec4 tangent{};

            static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
            static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

            bool operator==(const Vertex& other) const {
                return position == other.position && 
                normal == other.normal && 
                uv == other.uv &&
                tangent == other.tangent;
            }
        };
  
        Mesh(Device& device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const std::string& debugName = "");
        ~Mesh();

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;

        void bind(VkCommandBuffer commandBuffer);
        
        // Draw entire mesh
        void draw(VkCommandBuffer commandBuffer);
        
        // Draw specific submesh
        void drawSubmesh(VkCommandBuffer commandBuffer, uint32_t submeshIndex);
        
        
        
        // Instanced drawing methods
        void drawInstanced(VkCommandBuffer commandBuffer, uint32_t instanceCount);
        void drawSubmeshInstanced(VkCommandBuffer commandBuffer, uint32_t submeshIndex, uint32_t instanceCount);
        
        // Create a submesh division in the mesh
        void addSubmesh(uint32_t indexStart, uint32_t indexCount);
        
        // Get number of submeshes
        uint32_t getSubmeshCount() const { return static_cast<uint32_t>(submeshes.size()); }
        
        const Math::AABB& getLocalBounds() const { return localAABB; }
        
     
    private:
        void setDebugName(VkObjectType objectType, uint64_t handle, const std::string& name);

        void createVertexBuffers(const std::vector<Vertex>& vertices);
        void createIndexBuffers(const std::vector<uint32_t>& indices);

        void calculateLocalBounds(const std::vector<Vertex>& vertices);
        Device& device;
        std::string meshName;
        std::unique_ptr<Buffer> vertexBuffer;
        uint32_t vertexCount;
        
        bool hasIndexBuffer = false;
        std::unique_ptr<Buffer> indexBuffer;
        uint32_t indexCount;
        Math::AABB localAABB;
        

        // Submesh data for multi-material meshes
        std::vector<Submesh> submeshes;
    };
}