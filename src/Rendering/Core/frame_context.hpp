#pragma once

#include "Math/AABB.hpp"
#include "Rendering/RenderPasses/render_passes_buffers.hpp"
#include "Rendering/RenderPasses/Shadowmapping/shadow_map.hpp"
#include "ECS/ecs_types.hpp"
#include "core.hpp"
#include "ECS/ecs.hpp"
#include "Rendering/Resources/material.hpp"
#include "Math/view_frustum.hpp"
#include <array>
using namespace ECS;
using namespace Math;
namespace Rendering{

    // Maps for instanced rendering - use mesh pointer as key along with material
           struct MeshMaterialSubmeshKey {
               Mesh* mesh;
               Material* material;
               uint32_t submeshIndex;
               
               bool operator==(const MeshMaterialSubmeshKey& other) const {
                   return mesh == other.mesh && 
                          material == other.material && 
                          submeshIndex == other.submeshIndex;
               }
           };
   
          
   }
   
   namespace std {
       template <>
       struct hash<Rendering::MeshMaterialSubmeshKey> {
           std::size_t operator()(const Rendering::MeshMaterialSubmeshKey& key) const {
               std::size_t h1 = std::hash<Mesh*>{}(key.mesh);
               std::size_t h2 = std::hash<Material*>{}(key.material);
               std::size_t h3 = std::hash<uint32_t>{}(key.submeshIndex);
               return h1 ^ (h2 << 1) ^ (h3 << 2);
           }
       };
   
   
   
   }


namespace Rendering {

    struct LightData{
		std::vector<SpotLight*> spotLights;
		std::vector<PointLight*> pointLights;
		std::vector<DirectionalLight*> directionalLights;
	};

	struct ShadowcastingData{
		// Per-light storage of model matrices to keep cascades/faces independent
		std::unordered_map<DirectionalLight*,std::array<std::unordered_map<MeshMaterialSubmeshKey,std::vector<glm::mat4>>, MAX_SHADOW_CASCADE_COUNT>> directionalShadowModelsByCascade;
		std::unordered_map<SpotLight*,std::unordered_map<MeshMaterialSubmeshKey,std::vector<glm::mat4>>> spotShadowModels;
		std::unordered_map<PointLight*,std::array<std::unordered_map<MeshMaterialSubmeshKey,std::vector<glm::mat4>>, 6>> pointShadowModelsByFace;

		std::unordered_map<DirectionalLight*,std::array<std::vector<MeshMaterialSubmeshKey>, MAX_SHADOW_CASCADE_COUNT>> directionalShadowcastingKeyMapByCascade;
		std::unordered_map<SpotLight*,std::vector<MeshMaterialSubmeshKey>> spotShadowcastingKeyMap;
		std::unordered_map<PointLight*,std::array<std::vector<MeshMaterialSubmeshKey>, 6>> pointShadowcastingKeyMapByFace;
		uint32_t directionalShadowCastingCount=0;
		uint32_t spotShadowCastingCount=0;
		uint32_t pointShadowCastingCount=0;
	};



	struct MaterialBatch{
		Material* material;
		Mesh* mesh;
		uint32_t submeshIndex;
		uint32_t matrixOffset;
		uint32_t instanceCount;
	};

	struct MeshRenderingData{
		std::unordered_map<MeshMaterialSubmeshKey,std::vector<glm::mat4>> opaqueModelMap;
		std::unordered_map<MeshMaterialSubmeshKey,std::vector<glm::mat4>> opaqueNormalMap;
		std::unordered_map<MeshMaterialSubmeshKey,std::vector<glm::mat4>> transparentModelMap;
		std::unordered_map<MeshMaterialSubmeshKey,std::vector<glm::mat4>> transparentNormalMap;		
		uint32_t opaqueInstanceCount=0;
		uint32_t transparentInstanceCount=0;
	};



	struct CameraData{
		ViewFrustum viewFrustum;
		glm::mat4 viewProjectionMatrix;
		glm::mat4 viewMatrix;
		glm::mat4 invViewMatrix;
		glm::mat4 invProjectionMatrix;
		glm::mat4 projectionMatrix;
		glm::vec3 position;
		float fov;
		float aspectRatio;
		float nearPlane;
		float farPlane;	
	};

	// Previous frame camera data for temporal reprojection
	struct PrevCameraData {
		glm::mat4 viewProjectionMatrix;
		glm::mat4 invViewProjectionMatrix;
	};



    struct FrameContext {
        // === CORE FRAME DATA ===
        uint32_t frameIndex;
        VkCommandBuffer commandBuffer;
        VkExtent2D extent;
        float frameTime;
        
		VkDescriptorSet cameraDescriptorSet;
		VkDescriptorSet modelsDescriptorSet;
		VkDescriptorSet gBufferDescriptorSet;
		VkDescriptorSet lightArrayDescriptorSet;
		VkDescriptorSet cascadeSplitsDescriptorSet;
		VkDescriptorSet sceneLightingDescriptorSet;
		VkDescriptorSet lightMatrixDescriptorSet;
		VkDescriptorSet shadowModelMatrixDescriptorSet;
		VkDescriptorSet shadowMapSamplerDescriptorSet;
		VkDescriptorSet skyboxDescriptorSet;
		VkDescriptorSet transparencyModelDescriptorSet;
		VkDescriptorSet compositionDescriptorSet;
		VkDescriptorSet depthPyramidDescriptorSet;
		VkDescriptorSet rcBuildDescriptorSet;
		VkDescriptorSet rcResolveDescriptorSet;
		VkDescriptorSet smaaEdgeDescriptorSet;
		VkDescriptorSet smaaWeightDescriptorSet;
		VkDescriptorSet smaaBlendDescriptorSet;
		VkDescriptorSet colorCorrectionDescriptorSet;

        Buffer* cameraUniformBuffer;
        Buffer* modelMatrixBuffer;
		Buffer* normalMatrixBuffer;
		Buffer* lightArrayUniformBuffer;
		Buffer* cascadeSplitsBuffer;
		Buffer* sceneLightingBuffer;
		Buffer* lightMatrixBuffer;
		Buffer* shadowModelMatrixBuffer;
		Buffer* transparencyModelMatrixBuffer;
		Buffer* transparencyNormalMatrixBuffer;
		
		VkImageView depthView;
		VkImage depthImage;

		// Depth pyramid (sampled for screenspace raymarching)
		VkImageView depthPyramidView;
		VkImage depthPyramidImage;
		uint32_t depthPyramidMipLevels;
		std::vector<VkImageView> depthPyramidMipStorageViews;
		std::vector<VkDescriptorSet> depthPyramidMipDescriptorSets;

		VkImageView lightPassResultView;
		VkSampler lightPassSampler;
		VkImageView lightIncidentView;

		VkImageView accumulationView;
		VkImageView revealageView;

		// Indirect GI buffer
		VkImageView giIndirectView;
		VkImage giIndirectImage;

		// Post-process render targets
		VkImageView compositionColorView;
		VkImage compositionColorImage;
		VkImageView smaaEdgeView;
		VkImage smaaEdgeImage;
		VkImageView smaaBlendView;
		VkImage smaaBlendImage;
		VkImageView postAAColorView;
		VkImage postAAColorImage;

		// GI history for temporal accumulation (previous frame's GI output)
		VkImageView giHistoryView;
		VkSampler giHistorySampler;

		// Previous frame camera data for motion vector computation
		PrevCameraData prevCameraData;

		// Frame counter for temporal jittering
		uint32_t temporalFrameIndex;

		VkImageView gBufferPositionView;
		VkImageView gBufferNormalView;
		VkImageView gBufferAlbedoView;
		VkImageView gBufferMaterialView;

		// RC per-cascade atlas views for this frame
		std::array<VkImageView, RC_CASCADE_COUNT> rcRadianceViews{};

		VkImage gBufferPositionImage;
		VkImage gBufferNormalImage;
		VkImage gBufferAlbedoImage;
		VkImage gbufferMaterialImage;

		// Shadow map references for this frame
		std::array<ShadowMap*, MAX_DIRECTIONAL_LIGHTS> directionalShadowMaps{};
		std::array<ShadowMap*, MAX_SPOT_LIGHTS> spotShadowMaps{};
		std::array<ShadowMap*, MAX_POINT_LIGHTS> pointShadowMaps{};

		AABB frameSceneBounds;
		CameraData cameraData;
        std::array<MaterialBatch,BASE_INSTANCED_RENDERABLES> opaqueMaterialBatches;
    	uint32_t opaqueMaterialBatchCount = 0;

		std::array<MaterialBatch,BASE_INSTANCED_RENDERABLES> transparentMaterialBatches;
		uint32_t transparentMaterialBatchCount = 0;

		std::unordered_map<DirectionalLight*,std::array<std::vector<MaterialBatch>, MAX_SHADOW_CASCADE_COUNT>> directionalShadowcastingMaterialMap;
		std::unordered_map<SpotLight*,std::vector<MaterialBatch>> spotShadowcastingMaterialMap;
		std::unordered_map<PointLight*,std::array<std::vector<MaterialBatch>, 6>> pointShadowcastingMaterialMapByFace;

		// Matrix base indices in lightMatrixBuffer for each light type
		std::unordered_map<DirectionalLight*, uint32_t> directionalLightMatrixBase;
		std::unordered_map<SpotLight*, uint32_t> spotLightMatrixBase;
		std::unordered_map<PointLight*, uint32_t> pointLightMatrixBase;
    };
    
}