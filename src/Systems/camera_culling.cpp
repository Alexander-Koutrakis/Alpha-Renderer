#include "camera_culling.hpp"

using namespace ECS;
using namespace Math;
using namespace Scene;
using namespace Rendering;
    
namespace Systems{

   void CameraCulling::frustumCullRenderers(
        const ViewFrustum viewFrustum,
        AABB& frameSceneBounds,
        MeshRenderingData& meshRenderingData)
    {


        auto& scene = Scene::Scene::getInstance();
        
        auto visibleObjects=scene.getVisibleRenderers(viewFrustum);
        scene.getVisibleBounds(viewFrustum,frameSceneBounds);

        // Process visible objects
        for (const auto& renderable : visibleObjects) {
            uint32_t submeshCount = renderable->meshRenderer.materials.size();
            Mesh* mesh = renderable->meshRenderer.mesh;
            for (uint32_t i = 0; i < submeshCount; i++) {
                Material* material = renderable->meshRenderer.materials[i];                
                // Only now create a batch
                bool isTransparent = material->getTransparencyType() == TransparencyType::TYPE_TRANSPARENT;
                
                // Create a key for this mesh-material-submesh combination
                MeshMaterialSubmeshKey key{mesh, material, i};
                if(isTransparent){
                    meshRenderingData.transparentModelMap[key].push_back(renderable->transform.modelMatrix);
                    meshRenderingData.transparentNormalMap[key].push_back(renderable->transform.normalMatrix);
                    meshRenderingData.transparentInstanceCount++;
                }else{
                    meshRenderingData.opaqueModelMap[key].push_back(renderable->transform.modelMatrix);
                    meshRenderingData.opaqueNormalMap[key].push_back(renderable->transform.normalMatrix);
                    meshRenderingData.opaqueInstanceCount++;
                }
            }
        }

       
    }

    void CameraCulling::updateFrameContext(FrameContext& frameContext){

        MeshRenderingData meshRenderingData{};
        AABB frameSceneBounds{};
        frustumCullRenderers(frameContext.cameraData.viewFrustum,frameSceneBounds,meshRenderingData);
        updateOpaqueModelBuffers(frameContext,meshRenderingData);
        updateTransparentModelBuffers(frameContext,meshRenderingData);
    }

    void CameraCulling::updateOpaqueModelBuffers(FrameContext& frameContext,MeshRenderingData& meshRenderingData){
        VkDeviceSize modelBufferOffset=0;
        VkDeviceSize normalBufferOffset=0;
        uint32_t matrixOffset=0;
        uint32_t opaqueMaterialBatchCount=0;
        uint32_t mat4size=sizeof(glm::mat4);
        auto& opaqueModelMap=meshRenderingData.opaqueModelMap;
        auto& opaqueNormalMap=meshRenderingData.opaqueNormalMap;

        for(auto& [key,instances]:opaqueModelMap){
            size_t instancesSize=instances.size();

            frameContext.modelMatrixBuffer->writeToBuffer(instances.data(),instancesSize*mat4size,modelBufferOffset);
            std::vector<glm::mat4>& normalMatrices=opaqueNormalMap.at(key);
            frameContext.normalMatrixBuffer->writeToBuffer(normalMatrices.data(),instancesSize*mat4size,normalBufferOffset);

            Rendering::MaterialBatch& materialBatch=frameContext.opaqueMaterialBatches[opaqueMaterialBatchCount];
            materialBatch.mesh=key.mesh;
            materialBatch.material=key.material;
            materialBatch.submeshIndex=key.submeshIndex;
            materialBatch.instanceCount=instancesSize;
            materialBatch.matrixOffset=matrixOffset;
            
            opaqueMaterialBatchCount++;
            modelBufferOffset += instancesSize * mat4size;
            normalBufferOffset += instancesSize * mat4size;
            matrixOffset += instancesSize;
        }

        frameContext.opaqueMaterialBatchCount=opaqueMaterialBatchCount;
    }

    void CameraCulling::updateTransparentModelBuffers(FrameContext& frameContext,MeshRenderingData& meshRenderingData){
        VkDeviceSize modelBufferOffset=0;
        VkDeviceSize normalBufferOffset=0;
        uint32_t matrixOffset=0;
        uint32_t transparentMaterialBatchCount=0;
        uint32_t mat4size=sizeof(glm::mat4);

        auto& transparentModelMap=meshRenderingData.transparentModelMap;
        auto& transparentNormalMap=meshRenderingData.transparentNormalMap;

        for(auto& [key,instances]:transparentModelMap){
            size_t instancesSize=instances.size();

            // Write to TRANSPARENCY buffers, not opaque buffers!
            frameContext.transparencyModelMatrixBuffer->writeToBuffer(instances.data(),instancesSize*mat4size,modelBufferOffset);
            std::vector<glm::mat4>& normalMatrices=transparentNormalMap.at(key);
            frameContext.transparencyNormalMatrixBuffer->writeToBuffer(normalMatrices.data(),instancesSize*mat4size,normalBufferOffset);

            Rendering::MaterialBatch& materialBatch=frameContext.transparentMaterialBatches[transparentMaterialBatchCount];
            materialBatch.mesh=key.mesh;
            materialBatch.material=key.material;
            materialBatch.submeshIndex=key.submeshIndex;
            materialBatch.instanceCount=instancesSize;
            materialBatch.matrixOffset=matrixOffset;

            transparentMaterialBatchCount++;
            modelBufferOffset += instancesSize * mat4size;
            normalBufferOffset += instancesSize * mat4size;
            matrixOffset += instancesSize;
        }

        frameContext.transparentMaterialBatchCount=transparentMaterialBatchCount;
    }

}