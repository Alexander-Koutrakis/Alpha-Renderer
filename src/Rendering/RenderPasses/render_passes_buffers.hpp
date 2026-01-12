#pragma once

#include "Rendering/rendering_constants.hpp"
#include "core.hpp"
namespace Rendering{

	struct Light {
		alignas(16) glm::vec4 positionAndData;     // xyz=pos, w=range (0 for directional)
		alignas(16) glm::vec4 colorAndIntensity;    // rgb=color, a=intensity
		alignas(16) glm::vec4 directionAndRange;     // xyz=direction, w=spot data or unused
		alignas(16) glm::vec4 attenuationParams;    // distance/angle attenuation params
		alignas(4) uint32_t lightType;              // 0 = directional, 1 = spot, 2 = point
		alignas(4) uint32_t lightMatrixOffset;      // offset into the shadowcastingLightMatrices array
		alignas(4) uint32_t shadowmapIndex;
		alignas(4) uint32_t isCastingShadow;        // 0 = no, 1 = yes
		alignas(4) float shadowStrength;
	};



    struct ShadowcastingLightMatrices {
    	alignas(16)glm::mat4 shadowcastingLightMatrices[MAX_SHADOWCASTING_LIGHT_MATRICES];
	};

    struct UnifiedLightBuffer {
        alignas(16) Light lights[MAX_LIGHTS];
		alignas(4) uint32_t lightCount;
    };
	
	struct DirectionalLightCascadesBuffer{
		alignas(16) glm::vec4 cascadeSplits[MAX_SHADOWCASTING_DIRECTIONAL];
	};

	struct SceneLightingUbo {
		alignas(16) glm::mat4 viewMatrix;
		alignas(16) glm::mat4 projectionMatrix;
		alignas(16) glm::vec4 cameraPosition;
		alignas(4) float ambientIntensity;
		alignas(4) float reflectionIntensity;
	};

    struct CameraUbo {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::mat4 viewProj;
        alignas(16) glm::vec4 cameraPosition;
		alignas(16) glm::vec4 clipPlanes; // x=near, y=far, z=far-near, w=near*far
    };


}
