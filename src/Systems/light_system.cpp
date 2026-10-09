#include "light_system.hpp"
#include "Engine/log.hpp"
#include "Rendering/Resources/material.hpp"
#include "Scene/scene.hpp"
#include "Systems/bounding_box_system.hpp"
#include "Systems/shadow_budget.hpp"
#include <vector>
#include <limits>
#include <algorithm>
#include <glm/gtx/norm.hpp>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <unordered_set>

using namespace Rendering;
using namespace ECS;
using namespace Math;

namespace Systems {

void LightSystem::updateDirectionalLight(DirectionalLight& directionalLight, const Transform& transform,
                                         CameraData& cameraData) {
    directionalLight.direction = glm::vec4(TransformSystem::getForward(transform), 0.0f);
    // Calculate the cascade splits first
    calculateCascadeSplits(directionalLight, cameraData.nearPlane, cameraData.farPlane);

    // Then calculate the view-projection matrices
    calculateCascadeViewProjections(directionalLight, cameraData);
}

void LightSystem::updateSpotLight(SpotLight& spotLight) {
    auto transform = spotLight.transform;
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

    // Handle the case where light direction is parallel to up vector
    if (std::abs(glm::dot(glm::normalize(TransformSystem::getForward(transform)), up)) > 0.9999f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    glm::mat4 viewMatrix =
        glm::lookAtLH(transform.position, transform.position + TransformSystem::getForward(transform), up);

    glm::mat4 projectionMatrix =
        glm::perspectiveLH_ZO(glm::radians(spotLight.outerCutoff * 0.5f) * 2.0f, 1.0f, 0.1f, spotLight.range);

    projectionMatrix[1][1] *= -1.0f;
    spotLight.viewProjectionMatrix = projectionMatrix * viewMatrix;
}

void LightSystem::updatePointLight(PointLight& pointLight) {
    auto transform = pointLight.transform;

    // Standard cubemap face order: +X, -X, +Y, -Y, +Z, -Z
    std::array<glm::vec3, 6> directions = {
        glm::vec3(1.0f, 0.0f, 0.0f),  // +X
        glm::vec3(-1.0f, 0.0f, 0.0f), // -X
        glm::vec3(0.0f, 1.0f, 0.0f),  // +Y
        glm::vec3(0.0f, -1.0f, 0.0f), // -Y
        glm::vec3(0.0f, 0.0f, 1.0f),  // +Z
        glm::vec3(0.0f, 0.0f, -1.0f)  // -Z
    };

    // Up vectors for each direction
    // These are chosen to avoid gimbal lock
    std::array<glm::vec3, 6> upVectors = {
        glm::vec3(0.0f, 1.0f, 0.0f),  // For +X: Use world up
        glm::vec3(0.0f, 1.0f, 0.0f),  // For -X: Use world up
        glm::vec3(0.0f, 0.0f, -1.0f), // For +Y: Use world back
        glm::vec3(0.0f, 0.0f, 1.0f),  // For -Y: Use world front
        glm::vec3(0.0f, 1.0f, 0.0f),  // For +Z: Use world up
        glm::vec3(0.0f, 1.0f, 0.0f)   // For -Z: Use world up
    };

    glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(90.0f), 1.0f, 0.1f, pointLight.range);
    proj[1][1] *= -1;

    for (int i = 0; i < 6; i++) {
        glm::mat4 view = glm::lookAtLH(transform.position, transform.position + directions[i], upVectors[i]);
        pointLight.viewProjectionMatrix[i] = proj * view;
    }
}

void LightSystem::calculateCascadeSplits(DirectionalLight& directionalLight, float nearClip, float farClip) {
    const float shadowFarPlane = std::min(farClip, Rendering::MAX_SHADOW_DISTANCE);
    float clipRange = shadowFarPlane - nearClip;

    // Lambda factor controls the blend between logarithmic and uniform distribution
    // Lower values (0.5-0.7) = more uniform = better distant cascade coverage
    // Higher values (0.8-0.9) = more logarithmic = better near cascade detail
    const float lambda = 0.7f;

    for (uint32_t i = 0; i < MAX_SHADOW_CASCADE_COUNT; i++) {
        float p = (i + 1) / static_cast<float>(MAX_SHADOW_CASCADE_COUNT);
        float log = nearClip * std::pow(shadowFarPlane / nearClip, p);
        float uniform = nearClip + clipRange * p;
        float d = lambda * (log - uniform) + uniform;
        directionalLight.cascadeSplits[i] = d;
    }
}

void LightSystem::calculateCascadeViewProjections(DirectionalLight& dirLight, CameraData& cameraData) {
    const glm::vec3 lightDir = glm::normalize(glm::vec3(dirLight.direction));

    // Choose an initial up vector that avoids gimbal lock
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 sideUp = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tempUp = (std::abs(glm::dot(lightDir, worldUp)) > 0.95f) ? sideUp : worldUp;

    // Build an ORTHONORMAL basis for light space
    // This is critical - using non-orthonormal basis causes distortion in later cascades
    const glm::vec3 right = glm::normalize(glm::cross(tempUp, lightDir));
    const glm::vec3 up = glm::normalize(glm::cross(lightDir, right)); // Recompute up to ensure orthogonality

    const glm::mat4 invView = cameraData.invViewMatrix;

    // NDC cube corners are compile-time constants
    static const glm::vec4 ndcCorners[8] = {{-1, -1, 0, 1}, {1, -1, 0, 1}, {-1, 1, 0, 1}, {1, 1, 0, 1},
                                            {-1, -1, 1, 1}, {1, -1, 1, 1}, {-1, 1, 1, 1}, {1, 1, 1, 1}};

    // Shadow map resolution for texel snapping
    constexpr float shadowMapResolution = static_cast<float>(Rendering::DIRECTIONAL_SHADOW_MAP_RES);

    // Extent multiplier: padding to ensure all shadow casters are captured
    constexpr float extentMultiplier = 1.10f;

    for (uint32_t i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        const float cascadeNear = (i == 0) ? cameraData.nearPlane : dirLight.cascadeSplits[i - 1];
        const float cascadeFar = dirLight.cascadeSplits[i];

        glm::mat4 proj = glm::perspectiveLH_ZO(cameraData.fov, cameraData.aspectRatio, cascadeNear, cascadeFar);
        proj[1][1] *= -1.0f;

        const glm::mat4 invProj = glm::inverse(proj);
        const glm::mat4 invViewProj = invView * invProj;

        // Transform frustum corners to world space
        glm::vec3 frustumCorners[8];
        glm::vec3 center(0.0f);
        for (int j = 0; j < 8; ++j) {
            glm::vec4 corner = invViewProj * ndcCorners[j];
            frustumCorners[j] = glm::vec3(corner) / corner.w;
            center += frustumCorners[j];
        }
        center *= 1.0f / 8.0f;

        // Calculate a stable radius - use the maximum distance from center to any corner
        float radius = 0.0f;
        for (int j = 0; j < 8; ++j) {
            float dist = glm::length(frustumCorners[j] - center);
            radius = std::max(radius, dist);
        }

        // Create ORTHONORMAL rotation matrix from light space to world space
        // right, up, and lightDir are all orthogonal to each other
        glm::mat3 lightToWorldRotation;
        lightToWorldRotation[0] = right;     // Light space X -> World space right
        lightToWorldRotation[1] = up;        // Light space Y -> World space up (orthogonalized)
        lightToWorldRotation[2] = -lightDir; // Light space Z -> World space forward (negated for view)

        // World to light space rotation (transpose of orthonormal matrix = its inverse)
        glm::mat3 worldToLightRotation = glm::transpose(lightToWorldRotation);

        // Transform frustum corners to light space using ONLY rotation (no translation)
        glm::vec3 lightSpace[8];
        for (int j = 0; j < 8; ++j) {
            lightSpace[j] = worldToLightRotation * frustumCorners[j];
        }

        AABB lightSpaceAABB(lightSpace);
        const float extendX = lightSpaceAABB.extents.x * extentMultiplier;
        const float extendY = lightSpaceAABB.extents.y * extentMultiplier;

        // Calculate light-space texel size for snapping
        const float lightTexelSizeX = (extendX * 2.0f) / shadowMapResolution;
        const float lightTexelSizeY = (extendY * 2.0f) / shadowMapResolution;

        // Transform center to light space
        glm::vec3 lightSpaceCenter = worldToLightRotation * center;

        // Snap the light-space center to texel boundaries
        glm::vec3 snappedLightSpaceCenter = lightSpaceCenter;
        snappedLightSpaceCenter.x = std::round(lightSpaceCenter.x / lightTexelSizeX) * lightTexelSizeX;
        snappedLightSpaceCenter.y = std::round(lightSpaceCenter.y / lightTexelSizeY) * lightTexelSizeY;

        // Transform the snapped center back to world space
        glm::vec3 snappedWorldCenter = lightToWorldRotation * snappedLightSpaceCenter;

        // Create the light view matrix using the same orthonormal basis we used for snapping
        // This ensures consistency between snapping and the final view matrix
        const glm::vec3 stableLightPos = snappedWorldCenter - lightDir * radius * 1.0f;
        glm::mat4 lightView = glm::lookAtLH(stableLightPos, snappedWorldCenter, up);

        // Recalculate light-space bounds with the final view matrix
        for (int j = 0; j < 8; ++j) {
            lightSpace[j] = glm::vec3(lightView * glm::vec4(frustumCorners[j], 1.0f));
        }

        AABB finalLightSpaceAABB(lightSpace);
        const float finalExtendX = finalLightSpaceAABB.extents.x * extentMultiplier;
        const float finalExtendY = finalLightSpaceAABB.extents.y * extentMultiplier;

        // Center the ortho projection on the middle of the slice's light-space bounds. The mean of the corners (what
        // the view looks at) is not that middle, and the 10% padding does not cover the difference on a slanted sun.
        glm::vec3 finalLightSpaceCenter = finalLightSpaceAABB.center;

        // Recalculate texel size with final extents and apply final snapping
        const float finalTexelSizeX = (finalExtendX * 2.0f) / shadowMapResolution;
        const float finalTexelSizeY = (finalExtendY * 2.0f) / shadowMapResolution;
        finalLightSpaceCenter.x = std::round(finalLightSpaceCenter.x / finalTexelSizeX) * finalTexelSizeX;
        finalLightSpaceCenter.y = std::round(finalLightSpaceCenter.y / finalTexelSizeY) * finalTexelSizeY;

        // Create orthographic projection centered on snapped position
        glm::mat4 ortho = glm::orthoLH_ZO(
            finalLightSpaceCenter.x - finalExtendX, finalLightSpaceCenter.x + finalExtendX,
            finalLightSpaceCenter.y - finalExtendY, finalLightSpaceCenter.y + finalExtendY, 0.0f, radius * 5.0f);
        // Flip Y for Vulkan clip space: the scale and the translation (the ortho is no longer centered on y = 0).
        ortho[1][1] *= -1.0f;
        ortho[3][1] *= -1.0f;

        dirLight.viewProjectionMatrix[i] = ortho * lightView;
    }
}

void LightSystem::updateSceneLightBuffer(FrameContext& frameContext) {
    SceneLightingUbo ubo{};
    ubo.viewMatrix = frameContext.cameraData.viewMatrix;
    ubo.projectionMatrix = frameContext.cameraData.projectionMatrix;

    // Set Enviroment settings
    Scene::EnvironmentLighting envLighting = Scene::Scene::getInstance().getEnvironmentLighting();
    ubo.cameraPosition = glm::vec4(frameContext.cameraData.position, 1.0f);
    ubo.reflectionIntensity = envLighting.reflectionIntensity;
    ubo.ambientIntensity = envLighting.ambientIntensity;

    frameContext.sceneLightingBuffer->writeToBuffer(&ubo);
}

void LightSystem::updateLightArrayBuffer(FrameContext& frameContext, LightData& lightData) {
    UnifiedLightBuffer unifiedBuffer{};
    uint32_t lightIndex = 0;

    // Convert directional lights to unified format
    for (auto* dirLightPtr : lightData.directionalLights) {
        if (lightIndex >= MAX_LIGHTS)
            break;

        DirectionalLight& dirLight = *dirLightPtr;
        Rendering::Light& light = unifiedBuffer.lights[lightIndex];

        // Unity approach: For directional lights, position.w = 0.0
        // This makes lightVector = direction, distanceSqr = 1.0 (normalized direction)
        light.positionAndData = glm::vec4(-dirLight.direction.x, -dirLight.direction.y, -dirLight.direction.z, 0.0f);
        light.colorAndIntensity = glm::vec4(dirLight.color, dirLight.intensity);
        light.directionAndRange = glm::vec4(dirLight.direction.x, dirLight.direction.y, dirLight.direction.z, 0.0f);

        // Directional lights: encode data so both attenuations return 1.0
        // Distance attenuation: set x=0 so UnityDistanceAttenuation returns 1.0
        // Angle attenuation: set z=0, w=1 so SdotL*0 + 1 = 1, then 1*1 = 1
        light.attenuationParams = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

        light.lightType = 0; // Directional
        const auto slot = frameContext.directionalShadowSlots.find(dirLightPtr);
        const bool hasShadow = dirLight.isCastingShadows && slot != frameContext.directionalShadowSlots.end();
        light.isCastingShadow = hasShadow ? 1 : 0;
        light.lightMatrixOffset = hasShadow ? slot->second.matrixBase : 0; // first of its cascade matrices
        light.shadowmapIndex = hasShadow ? slot->second.slot : 0; // one sampler2DArray per light, cascades are layers
        light.shadowStrength = dirLight.shadowStrength;

        lightIndex++;
    }

    // Convert spot lights to unified format
    for (auto* spotLightPtr : lightData.spotLights) {
        if (lightIndex >= MAX_LIGHTS)
            break;

        SpotLight& spotLight = *spotLightPtr;
        Rendering::Light& light = unifiedBuffer.lights[lightIndex];

        // Spot lights: position.w = 1.0 for proper distance calculation
        light.positionAndData = glm::vec4(spotLight.transform.position, 1.0f);
        light.colorAndIntensity = glm::vec4(spotLight.color, spotLight.intensity);
        light.directionAndRange = glm::vec4(TransformSystem::getForward(spotLight.transform), spotLight.range);

        float cosInner = glm::cos(glm::radians(spotLight.innerCutoff * 0.5f));
        float cosOuter = glm::cos(glm::radians(spotLight.outerCutoff * 0.5f));

        float invAngleRange = 1.0f / glm::max(cosInner - cosOuter, 0.001f);
        float angleScale = invAngleRange;
        float angleOffset = -cosOuter * invAngleRange;

        float rangeSqr = spotLight.range * spotLight.range;
        float invRangeSqr = 1.0f / glm::max(rangeSqr, 0.0001f);

        light.attenuationParams = glm::vec4(invRangeSqr, // x: smooth quadratic fade
                                            1.0f,        // y: always 1.0
                                            angleScale,  // z: spot angle scale
                                            angleOffset  // w: spot angle offset
        );

        light.lightType = 1; // Spot
        const auto slot = frameContext.spotShadowSlots.find(spotLightPtr);
        const bool hasShadow = spotLight.isCastingShadows && slot != frameContext.spotShadowSlots.end();
        light.isCastingShadow = hasShadow ? 1 : 0;
        light.lightMatrixOffset = hasShadow ? slot->second.matrixBase : 0;
        light.shadowmapIndex = hasShadow ? slot->second.slot : 0;
        light.shadowStrength = spotLight.shadowStrength;
        lightIndex++;
    }

    // Convert point lights to unified format
    for (auto* pointLightPtr : lightData.pointLights) {
        if (lightIndex >= MAX_LIGHTS)
            break;

        PointLight& pointLight = *pointLightPtr;
        Rendering::Light& light = unifiedBuffer.lights[lightIndex];

        // Point lights: position.w = 1.0 for proper distance calculation
        light.positionAndData = glm::vec4(pointLight.transform.position, 1.0f);
        light.colorAndIntensity = glm::vec4(pointLight.color, pointLight.intensity);
        light.directionAndRange = glm::vec4(0.0f, 0.0f, 0.0f, pointLight.range); // No primary direction

        float rangeSqr = pointLight.range * pointLight.range;
        float invRangeSqr = 1.0f / glm::max(rangeSqr, 0.0001f);

        light.attenuationParams = glm::vec4(invRangeSqr, // x: used for smooth quadratic range fade
                                            1.0f,        // y: always 1.0 in URP
                                            0.0f,        // z: angleScale (unused for point lights)
                                            1.0f         // w: angleOffset (unused, returns 1)
        );

        light.lightType = 2; // Point
        const auto slot = frameContext.pointShadowSlots.find(pointLightPtr);
        const bool hasShadow = pointLight.isCastingShadows && slot != frameContext.pointShadowSlots.end();
        light.isCastingShadow = hasShadow ? 1 : 0;
        light.lightMatrixOffset = hasShadow ? slot->second.matrixBase : 0; // first of its 6 face matrices
        light.shadowmapIndex = hasShadow ? slot->second.slot : 0;
        light.shadowStrength = pointLight.shadowStrength;
        lightIndex++;
    }

    unifiedBuffer.lightCount = lightIndex;
    frameContext.lightArrayUniformBuffer->writeToBuffer(&unifiedBuffer);
}

void LightSystem::frustumCullLights(CameraData& cameraData, LightData& lightData) {
    auto& ecsManager = ECSManager::getInstance();
    auto& scene = Scene::Scene::getInstance();
    auto potentialLights = scene.getVisibleLights(cameraData.viewFrustum);

    ecsManager.forEachComponent<DirectionalLight>([&](DirectionalLight& directionalLight) {
        auto* transform = ecsManager.getComponent<ECS::Transform>(directionalLight.owner);
        if (transform) {
            // The shaders index fixed-size shadow map and cascade-split arrays by directional light.
            if (lightData.directionalLights.size() >= MAX_DIRECTIONAL_LIGHTS) {
                throw std::runtime_error("Scene has more than " + std::to_string(MAX_DIRECTIONAL_LIGHTS) +
                                         " directional lights, which is the renderer's limit");
            }
            updateDirectionalLight(directionalLight, *transform, cameraData);
            lightData.directionalLights.push_back(&directionalLight);
        }
    });

    for (auto lightPtr : potentialLights) {
        ECS::Light& light = *lightPtr;
        if (light.type == LightType::SPOT_LIGHT) {
            SpotLight& spotLight = static_cast<SpotLight&>(light);
            updateSpotLight(spotLight);
            lightData.spotLights.push_back(&spotLight);
        } else {
            PointLight& pointLight = static_cast<PointLight&>(light);
            updatePointLight(pointLight);
            lightData.pointLights.push_back(&pointLight);
        }
    }
}

void LightSystem::lightFrustumCullShadowCasters(LightData& lightData, ShadowcastingData& shadowcastingData,
                                                const CameraData& cameraData) {
    auto& scene = Scene::Scene::getInstance();

    // Directional lights always cast shadows (they affect the entire scene); a light is a caster once it has shadow data.
    for (auto lightPtr : lightData.directionalLights) {
        DirectionalLight& directionalLight = *lightPtr;
        if (directionalLight.isCastingShadows) {
            processDirectionalLightShadowCasters(directionalLight, shadowcastingData, scene, cameraData);
            if (shadowcastingData.directionalShadowcastingKeyMapByCascade.count(lightPtr) > 0) {
                shadowcastingData.directionalCasters.push_back(lightPtr);
            }
        }
    }

    processNearestCasters(lightData.spotLights, cameraData.position, MAX_SPOT_LIGHTS, "spot",
                          shadowcastingData.spotCasters, [&](SpotLight& light) {
                              processSpotLightShadowCasters(light, shadowcastingData, scene, cameraData.position);
                              return shadowcastingData.spotShadowcastingKeyMap.count(&light) > 0;
                          });

    processNearestCasters(lightData.pointLights, cameraData.position, MAX_POINT_LIGHTS, "point",
                          shadowcastingData.pointCasters, [&](PointLight& light) {
                              processPointLightShadowCasters(light, shadowcastingData, scene, cameraData.position);
                              return shadowcastingData.pointShadowcastingKeyMapByFace.count(&light) > 0;
                          });
}

void LightSystem::assignShadowSlots(FrameContext& frameContext, const ShadowcastingData& shadowcastingData) {
    frameContext.directionalShadowSlots.clear();
    frameContext.spotShadowSlots.clear();
    frameContext.pointShadowSlots.clear();

    uint32_t matrixBase = 0;
    uint32_t slot = 0;
    for (auto* light : shadowcastingData.directionalCasters) {
        frameContext.directionalShadowSlots[light] = {slot++, matrixBase};
        matrixBase += MAX_SHADOW_CASCADE_COUNT;
    }
    slot = 0;
    for (auto* light : shadowcastingData.spotCasters) {
        frameContext.spotShadowSlots[light] = {slot++, matrixBase};
        matrixBase += 1;
    }
    slot = 0;
    for (auto* light : shadowcastingData.pointCasters) {
        frameContext.pointShadowSlots[light] = {slot++, matrixBase};
        matrixBase += 6;
    }
}

void LightSystem::processDirectionalLightShadowCasters(DirectionalLight& directionalLight,
                                                       ShadowcastingData& shadowcastingData, Scene::Scene& scene,
                                                       const CameraData& cameraData) {
    // Maximum distance from camera for shadow casters (precomputed constant)
    const float maxShadowCasterDistanceSqr = Rendering::MAX_SHADOW_CASTER_DISTANCE_SQR;
    for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADE_COUNT; cascadeIndex++) {
        std::unordered_set<MeshMaterialSubmeshKey> uniqueKeys;

        float paddedCascadeFar = 0;
        if (cascadeIndex != 0) {
            const float cascadeNear = directionalLight.cascadeSplits[cascadeIndex - 1];
            const float cascadeFar = directionalLight.cascadeSplits[cascadeIndex];
            const float cascadeLength = cascadeFar - cascadeNear;
            constexpr float cascadeBlendFraction = 0.2f;
            const float depthOverlap = cascadeLength * cascadeBlendFraction;
            paddedCascadeFar = std::min(cascadeFar + depthOverlap, Rendering::MAX_SHADOW_DISTANCE);
        }

        ViewFrustum lightFrustum =
            ViewFrustum::createFromViewProjection(directionalLight.viewProjectionMatrix[cascadeIndex]);
        std::vector<Renderable*> visibleObjects = scene.getVisibleRenderers(lightFrustum);

        for (const auto& renderable : visibleObjects) {
            if (cascadeIndex != 0) {
                // Compute world-space bounds for this renderable and test against cascade depth
                AABB worldBounds{};
                BoundingBoxSystem::getWorldBounds(worldBounds, renderable->meshRenderer.mesh->getLocalBounds(),
                                                  renderable->transform.modelMatrix);

                if (!BoundingBoxSystem::overlapsViewDepthRange(worldBounds, cameraData.viewMatrix, 0.0f,
                                                               paddedCascadeFar)) {
                    continue;
                }
            }

            if (cascadeIndex == MAX_SHADOW_CASCADE_COUNT - 1) {
                glm::vec3 objectPos = glm::vec3(renderable->transform.modelMatrix[3]);
                float distanceSqr = glm::dot(objectPos - cameraData.position, objectPos - cameraData.position);
                if (distanceSqr > maxShadowCasterDistanceSqr) {
                    continue;
                }
            }

            uint32_t submeshCount = renderable->meshRenderer.materials.size();
            Mesh* mesh = renderable->meshRenderer.mesh;
            for (uint32_t submeshIndex = 0; submeshIndex < submeshCount; submeshIndex++) {
                Material* material = renderable->meshRenderer.materials[submeshIndex];
                TransparencyType transparencyType = material->getTransparencyType();
                // Skip transparent materials - only opaque objects should cast shadows
                if (transparencyType != Rendering::TransparencyType::TYPE_OPAQUE &&
                    transparencyType != Rendering::TransparencyType::TYPE_MASK) {
                    continue;
                }

                MeshMaterialSubmeshKey key{mesh, material, submeshIndex};
                shadowcastingData.directionalShadowModelsByCascade[&directionalLight][cascadeIndex][key].push_back(
                    renderable->transform.modelMatrix);

                if (uniqueKeys.find(key) == uniqueKeys.end()) {
                    shadowcastingData.directionalShadowcastingKeyMapByCascade[&directionalLight][cascadeIndex]
                        .push_back(key);
                    uniqueKeys.insert(key);
                }
            }
        }
    }
}

void LightSystem::processSpotLightShadowCasters(SpotLight& spotLight, ShadowcastingData& shadowcastingData,
                                                Scene::Scene& scene, const glm::vec3& cameraPosition) {
    const glm::vec3 lightPosition = spotLight.transform.position;
    const float lightRange = spotLight.range;

    // First check: Skip entire shadow generation if light's influence sphere
    // doesn't intersect the camera's shadow distance sphere
    // Note: This is conservative for spot lights (treats cone as sphere),
    // but more precise checks would require cone-sphere intersection
    float distanceToLight = glm::length(lightPosition - cameraPosition);
    float closestInfluenceDistance = distanceToLight - lightRange;

    if (closestInfluenceDistance > Rendering::MAX_SHADOW_DISTANCE) {
        return; // Light too far - skip shadow generation entirely
    }

    std::unordered_set<MeshMaterialSubmeshKey> uniqueKeys;

    // Use AABB intersection instead of ViewFrustum for consistency and to avoid frustum extraction issues
    ViewFrustum lightFrustum = ViewFrustum::createFromViewProjection(spotLight.viewProjectionMatrix);
    std::vector<Renderable*> visibleObjects = scene.getVisibleRenderers(lightFrustum);

    for (const auto& renderable : visibleObjects) {
        // Skip objects too far from camera to cast relevant shadows
        glm::vec3 objectPos = glm::vec3(renderable->transform.modelMatrix[3]);
        float distanceToCameraSqr = glm::dot(objectPos - cameraPosition, objectPos - cameraPosition);
        if (distanceToCameraSqr > Rendering::MAX_SHADOW_CASTER_DISTANCE_SQR) {
            continue;
        }

        uint32_t submeshCount = renderable->meshRenderer.materials.size();
        Mesh* mesh = renderable->meshRenderer.mesh;
        for (uint32_t i = 0; i < submeshCount; i++) {
            Material* material = renderable->meshRenderer.materials[i];
            TransparencyType transparencyType = material->getTransparencyType();
            // Skip transparent materials - only opaque objects should cast shadows
            if (transparencyType != Rendering::TransparencyType::TYPE_OPAQUE &&
                transparencyType != Rendering::TransparencyType::TYPE_MASK) {
                continue;
            }

            MeshMaterialSubmeshKey key{mesh, material, i};
            shadowcastingData.spotShadowModels[&spotLight][key].push_back(renderable->transform.modelMatrix);

            if (uniqueKeys.find(key) == uniqueKeys.end()) {
                shadowcastingData.spotShadowcastingKeyMap[&spotLight].push_back(key);
                uniqueKeys.insert(key);
            }
        }
    }
}

void LightSystem::processPointLightShadowCasters(PointLight& pointLight, ShadowcastingData& shadowcastingData,
                                                 Scene::Scene& scene, const glm::vec3& cameraPosition) {
    const glm::vec3 lightPosition = pointLight.transform.position;
    const float lightRange = pointLight.range;

    // First check: Skip entire shadow generation if light's influence sphere
    // doesn't intersect the camera's shadow distance sphere
    float distanceToLight = glm::length(lightPosition - cameraPosition);
    float closestInfluenceDistance = distanceToLight - lightRange;

    if (closestInfluenceDistance > Rendering::MAX_SHADOW_DISTANCE) {
        return; // Light too far - skip shadow generation entirely
    }

    for (int face = 0; face < 6; face++) {
        ViewFrustum faceFrustum = ViewFrustum::createFromViewProjection(pointLight.viewProjectionMatrix[face]);
        std::unordered_set<MeshMaterialSubmeshKey> uniqueKeys;
        std::vector<Renderable*> faceVisibleObjects = scene.getVisibleRenderers(faceFrustum);
        for (const auto& renderable : faceVisibleObjects) {
            // Skip objects too far from camera to cast relevant shadows
            glm::vec3 objectPos = glm::vec3(renderable->transform.modelMatrix[3]);
            float distanceToCameraSqr = glm::dot(objectPos - cameraPosition, objectPos - cameraPosition);
            if (distanceToCameraSqr > Rendering::MAX_SHADOW_CASTER_DISTANCE_SQR) {
                continue;
            }

            uint32_t submeshCount = renderable->meshRenderer.materials.size();
            Mesh* mesh = renderable->meshRenderer.mesh;
            for (uint32_t submeshIndex = 0; submeshIndex < submeshCount; submeshIndex++) {
                Material* material = renderable->meshRenderer.materials[submeshIndex];
                TransparencyType transparencyType = material->getTransparencyType();
                // Skip transparent materials - only opaque objects should cast shadows
                if (transparencyType != Rendering::TransparencyType::TYPE_OPAQUE &&
                    transparencyType != Rendering::TransparencyType::TYPE_MASK) {
                    continue;
                }

                MeshMaterialSubmeshKey key{mesh, material, submeshIndex};
                shadowcastingData.pointShadowModelsByFace[&pointLight][face][key].push_back(
                    renderable->transform.modelMatrix);

                if (uniqueKeys.find(key) == uniqueKeys.end()) {
                    shadowcastingData.pointShadowcastingKeyMapByFace[&pointLight][face].push_back(key);
                    uniqueKeys.insert(key);
                }
            }
        }
    }
}

void LightSystem::updateCascadeSplitsBuffer(FrameContext& frameContext) {
    DirectionalLightCascadesBuffer cascadeBuffer{};

    // Indexed by shadow slot, which is what the shaders read from the light's shadowmapIndex. Splits are stored as a
    // vec4 (one float per cascade).
    for (const auto& [dirLight, shadowSlot] : frameContext.directionalShadowSlots) {
        cascadeBuffer.cascadeSplits[shadowSlot.slot] =
            glm::vec4(dirLight->cascadeSplits[0], dirLight->cascadeSplits[1], dirLight->cascadeSplits[2],
                      dirLight->cascadeSplits[3]);
    }

    frameContext.cascadeSplitsBuffer->writeToBuffer(&cascadeBuffer);
}

void LightSystem::updateShadowLightMatrixBuffer(FrameContext& frameContext) {
    auto* data = static_cast<char*>(frameContext.lightMatrixBuffer->getMappedMemory());
    constexpr size_t mat4Size = sizeof(glm::mat4);

    for (const auto& [light, shadowSlot] : frameContext.directionalShadowSlots) {
        memcpy(data + shadowSlot.matrixBase * mat4Size, light->viewProjectionMatrix.data(),
               mat4Size * MAX_SHADOW_CASCADE_COUNT);
    }
    for (const auto& [light, shadowSlot] : frameContext.spotShadowSlots) {
        memcpy(data + shadowSlot.matrixBase * mat4Size, &light->viewProjectionMatrix, mat4Size);
    }
    for (const auto& [light, shadowSlot] : frameContext.pointShadowSlots) {
        memcpy(data + shadowSlot.matrixBase * mat4Size, light->viewProjectionMatrix.data(), mat4Size * 6);
    }
}

void LightSystem::updateShadowModelMatrixBuffer(FrameContext& frameContext, ShadowcastingData& shadowcastingData) {
    VkDeviceSize modelBufferOffset = 0;
    uint32_t matrixOffset = 0;
    uint32_t mat4size = sizeof(glm::mat4);
    frameContext.directionalShadowcastingMaterialMap.clear();
    frameContext.spotShadowcastingMaterialMap.clear();
    frameContext.pointShadowcastingMaterialMapByFace.clear();

    for (auto& [lightPtr, cascadeKeys] : shadowcastingData.directionalShadowcastingKeyMapByCascade) {
        auto modelsByCascadeIt = shadowcastingData.directionalShadowModelsByCascade.find(lightPtr);
        if (modelsByCascadeIt == shadowcastingData.directionalShadowModelsByCascade.end()) {
            continue;
        }
        auto& cascadeModelMaps = modelsByCascadeIt->second;
        for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADE_COUNT; ++cascadeIndex) {
            for (auto& key : cascadeKeys[cascadeIndex]) {
                auto& cascadeModelMap = cascadeModelMaps[cascadeIndex];
                auto instancesIt = cascadeModelMap.find(key);
                if (instancesIt == cascadeModelMap.end()) {
                    continue;
                }
                auto& instances = instancesIt->second;
                uint32_t instancesSize = instances.size();

                // Prevent buffer overflow when many instances are duplicated across cascades.
                VkDeviceSize bytesNeeded = instancesSize * mat4size;
                VkDeviceSize bufferSize = frameContext.shadowModelMatrixBuffer->getBufferSize();
                if (modelBufferOffset + bytesNeeded > bufferSize) {
                    Log::error("Shadow model matrix buffer overflow for directional light cascade ", cascadeIndex,
                               " (needed ", (modelBufferOffset + bytesNeeded), " bytes, have ", bufferSize, ")");
                    continue;
                }

                frameContext.shadowModelMatrixBuffer->writeToBuffer(instances.data(), instancesSize * mat4size,
                                                                    modelBufferOffset);

                MaterialBatch materialBatch{};
                materialBatch.mesh = key.mesh;
                materialBatch.material = key.material;
                materialBatch.submeshIndex = key.submeshIndex;
                materialBatch.instanceCount = instancesSize;
                materialBatch.matrixOffset = matrixOffset;

                modelBufferOffset += instancesSize * mat4size;
                matrixOffset += instancesSize;

                frameContext.directionalShadowcastingMaterialMap[lightPtr][cascadeIndex].push_back(materialBatch);
            }
        }
    }

    for (auto& [lightPtr, meshKeys] : shadowcastingData.spotShadowcastingKeyMap) {
        auto modelsIt = shadowcastingData.spotShadowModels.find(lightPtr);
        if (modelsIt == shadowcastingData.spotShadowModels.end()) {
            continue;
        }
        auto& modelMap = modelsIt->second;
        for (auto& key : meshKeys) {
            auto instancesIt = modelMap.find(key);
            if (instancesIt == modelMap.end()) {
                continue;
            }
            auto& instances = instancesIt->second;
            uint32_t instancesSize = instances.size();

            VkDeviceSize bytesNeeded = instancesSize * mat4size;
            VkDeviceSize bufferSize = frameContext.shadowModelMatrixBuffer->getBufferSize();
            if (modelBufferOffset + bytesNeeded > bufferSize) {
                Log::error("Shadow model matrix buffer overflow for spot light (matrixOffset ", matrixOffset, ")");
                continue;
            }
            frameContext.shadowModelMatrixBuffer->writeToBuffer(instances.data(), instancesSize * mat4size,
                                                                modelBufferOffset);

            MaterialBatch materialBatch{};
            materialBatch.mesh = key.mesh;
            materialBatch.material = key.material;
            materialBatch.submeshIndex = key.submeshIndex;
            materialBatch.instanceCount = instancesSize;
            materialBatch.matrixOffset = matrixOffset;

            modelBufferOffset += instancesSize * mat4size;
            matrixOffset += instancesSize;

            frameContext.spotShadowcastingMaterialMap[lightPtr].push_back(materialBatch);
        }
    }

    for (auto& [lightPtr, meshKeys] : shadowcastingData.pointShadowcastingKeyMapByFace) {
        auto modelsByFaceIt = shadowcastingData.pointShadowModelsByFace.find(lightPtr);
        if (modelsByFaceIt == shadowcastingData.pointShadowModelsByFace.end()) {
            continue;
        }
        auto& faceModelMaps = modelsByFaceIt->second;
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            for (auto& key : meshKeys[faceIndex]) {
                auto& faceModelMap = faceModelMaps[faceIndex];
                auto instancesIt = faceModelMap.find(key);
                if (instancesIt == faceModelMap.end()) {
                    continue;
                }
                auto& instances = instancesIt->second;
                uint32_t instancesSize = instances.size();

                VkDeviceSize bytesNeeded = instancesSize * mat4size;
                VkDeviceSize bufferSize = frameContext.shadowModelMatrixBuffer->getBufferSize();
                if (modelBufferOffset + bytesNeeded > bufferSize) {
                    Log::error("Shadow model matrix buffer overflow for point light face ", faceIndex,
                               " (matrixOffset ", matrixOffset, ")");
                    continue;
                }
                frameContext.shadowModelMatrixBuffer->writeToBuffer(instances.data(), instancesSize * mat4size,
                                                                    modelBufferOffset);

                MaterialBatch materialBatch{};
                materialBatch.mesh = key.mesh;
                materialBatch.material = key.material;
                materialBatch.submeshIndex = key.submeshIndex;
                materialBatch.instanceCount = instancesSize;
                materialBatch.matrixOffset = matrixOffset;

                modelBufferOffset += instancesSize * mat4size;
                matrixOffset += instancesSize;

                frameContext.pointShadowcastingMaterialMapByFace[lightPtr][faceIndex].push_back(materialBatch);
            }
        }
    }
}
void LightSystem::updateFrameContext(FrameContext& frameContext) {
    LightData lightData{};
    ShadowcastingData shadowcastingData{};
    CameraData& cameraData = frameContext.cameraData;
    frustumCullLights(cameraData, lightData);
    lightFrustumCullShadowCasters(lightData, shadowcastingData, cameraData);
    assignShadowSlots(frameContext, shadowcastingData);
    updateLightArrayBuffer(frameContext, lightData);
    updateSceneLightBuffer(frameContext);
    updateCascadeSplitsBuffer(frameContext);
    updateShadowLightMatrixBuffer(frameContext);
    updateShadowModelMatrixBuffer(frameContext, shadowcastingData);
}

} // namespace Systems
