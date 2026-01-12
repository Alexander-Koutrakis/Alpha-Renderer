#include "mesh.hpp"
#include <cassert>
#include <stdexcept>

namespace Rendering {

void Mesh::setDebugName(VkObjectType objectType, uint64_t handle, const std::string& name) {
    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = objectType;
    nameInfo.objectHandle = handle;
    nameInfo.pObjectName = name.c_str();
    
    auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device.getDevice(), "vkSetDebugUtilsObjectNameEXT");
    if (func != nullptr) {
        func(device.getDevice(), &nameInfo);
    }
}

Mesh::~Mesh() {}

void Mesh::createVertexBuffers(const std::vector<Vertex>& vertices) {
    vertexCount = static_cast<uint32_t>(vertices.size());
    assert(vertexCount >= 3 && "Vertex count must be at least 3");
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
    uint32_t vertexSize = sizeof(vertices[0]);

    Buffer stagingBuffer{
        device,
        vertexSize,
        vertexCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };
    
    stagingBuffer.map();
    stagingBuffer.writeToBuffer((void*)vertices.data());

    vertexBuffer = std::make_unique<Buffer>(
        device,
        vertexSize,
        vertexCount,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    device.copyBuffer(stagingBuffer.getBuffer(), vertexBuffer->getBuffer(), bufferSize);
    
    // Set debug name for vertex buffer
    if (!meshName.empty()) {
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)vertexBuffer->getBuffer(), "VertexBuffer_" + meshName);
    }
}

void Mesh::createIndexBuffers(const std::vector<uint32_t>& indices) {
    indexCount = static_cast<uint32_t>(indices.size());
    hasIndexBuffer = indexCount > 0;

    if (!hasIndexBuffer) {
        return;
    }

    VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
    uint32_t indexSize = sizeof(indices[0]);

    Buffer stagingBuffer{
        device,
        indexSize,
        indexCount,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };

    stagingBuffer.map();
    stagingBuffer.writeToBuffer((void*)indices.data());

    indexBuffer = std::make_unique<Buffer>(
        device,
        indexSize,
        indexCount,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    device.copyBuffer(stagingBuffer.getBuffer(), indexBuffer->getBuffer(), bufferSize);
    
    // Set debug name for index buffer
    if (!meshName.empty()) {
        setDebugName(VK_OBJECT_TYPE_BUFFER, (uint64_t)indexBuffer->getBuffer(), "IndexBuffer_" + meshName);
    }
}

void Mesh::bind(VkCommandBuffer commandBuffer) {
    VkBuffer buffers[] = {vertexBuffer->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

    if (hasIndexBuffer) {
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
    }
}



void Mesh::draw(VkCommandBuffer commandBuffer) {
    if (hasIndexBuffer) {
        vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
    } else {
        vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
    }
}

void Mesh::drawInstanced(VkCommandBuffer commandBuffer, uint32_t instanceCount) {
    if (hasIndexBuffer) {
        vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, 0, 0, 0);
    } else {
        vkCmdDraw(commandBuffer, vertexCount, instanceCount, 0, 0);
    }
}

void Mesh::drawSubmesh(VkCommandBuffer commandBuffer, uint32_t submeshIndex) {
    // Draw only the indices for this submesh
    const auto& submesh = submeshes[submeshIndex];
    vkCmdDrawIndexed(
        commandBuffer, 
        submesh.indexCount,  // Number of indices
        1,                   // Instance count
        submesh.indexStart,  // First index
        0,                   // Vertex offset
        0                    // First instance
    );
}

void Mesh::drawSubmeshInstanced(VkCommandBuffer commandBuffer, uint32_t submeshIndex, uint32_t instanceCount) {
    // Draw only the indices for this submesh with instances
    const auto& submesh = submeshes[submeshIndex];
    vkCmdDrawIndexed(
        commandBuffer, 
        submesh.indexCount,  // Number of indices
        instanceCount,       // Instance count
        submesh.indexStart,  // First index
        0,                   // Vertex offset
        0                    // First instance
    );
}



void Mesh::addSubmesh(uint32_t indexStart, uint32_t indexCount) {
    Submesh submesh;
    submesh.indexStart = indexStart;
    submesh.indexCount = indexCount;
    submeshes.push_back(submesh);
}


Mesh::Mesh(Device& device, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const std::string& debugName) 
    : device{device}, meshName{debugName} {
    createVertexBuffers(vertices);
    createIndexBuffers(indices);
    calculateLocalBounds(vertices);
}


// Regular vertex constructor implementation
std::vector<VkVertexInputBindingDescription> Mesh::Vertex::getBindingDescriptions() {
    std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
    bindingDescriptions[0].binding = 0;
    bindingDescriptions[0].stride = sizeof(Vertex);
    bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescriptions;
}

std::vector<VkVertexInputAttributeDescription> Mesh::Vertex::getAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
    
    attributeDescriptions.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)});
    attributeDescriptions.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)});
    attributeDescriptions.push_back({2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});
    attributeDescriptions.push_back({3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)});
    
    return attributeDescriptions;
}



void Mesh::calculateLocalBounds(const std::vector<Vertex>& vertices) {
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());
    
    for(const auto& vertex : vertices) {
        min = glm::min(min, vertex.position);
        max = glm::max(max, vertex.position);
    }
    
    // Calculate center and extents
    localAABB.center = (min + max) * 0.5f;
    localAABB.extents =glm::max(glm::vec3(0.01f), (max - min) * 0.5f);// Add minimum thickness to prevent zero-volume AABBs
    
}


} // namespace Rendering