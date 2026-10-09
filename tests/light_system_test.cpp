#include "doctest.h"
#include "test_helpers.hpp"
#include "Systems/light_system.hpp"
#include "Systems/shadow_budget.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Rendering;
using ECS::DirectionalLight;
using ECS::ECSManager;
using ECS::PointLight;
using ECS::SpotLight;
using ECS::Transform;

// The private steps of LightSystem that need no GPU, reachable through the friend declaration in light_system.hpp.
namespace Systems {
struct LightSystemTestAccess {
    static void cascadeSplits(DirectionalLight& light, float nearClip, float farClip) {
        LightSystem::calculateCascadeSplits(light, nearClip, farClip);
    }
    static void updateDirectional(DirectionalLight& light, const Transform& transform, CameraData& camera) {
        LightSystem::updateDirectionalLight(light, transform, camera);
    }
    static void updateSpot(SpotLight& light) { LightSystem::updateSpotLight(light); }
    static void updatePoint(PointLight& light) { LightSystem::updatePointLight(light); }
    static void cullLights(CameraData& camera, LightData& lights) { LightSystem::frustumCullLights(camera, lights); }
    static void assignSlots(FrameContext& frame, const ShadowcastingData& data) {
        LightSystem::assignShadowSlots(frame, data);
    }
};
} // namespace Systems

using Systems::LightSystemTestAccess;

namespace {

constexpr float kPi = 3.14159265358979f;

CameraData makeCamera(glm::vec3 position, float fovRadians = glm::radians(60.0f), float aspect = 1.5f,
                      float nearPlane = 0.1f, float farPlane = 1000.0f) {
    CameraData camera;
    camera.position = position;
    camera.fov = fovRadians;
    camera.aspectRatio = aspect;
    camera.nearPlane = nearPlane;
    camera.farPlane = farPlane;
    camera.viewMatrix = glm::lookAtLH(position, position + glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
    camera.invViewMatrix = glm::inverse(camera.viewMatrix);
    camera.projectionMatrix = glm::perspectiveLH_ZO(fovRadians, aspect, nearPlane, farPlane);
    camera.projectionMatrix[1][1] *= -1.0f;
    camera.invProjectionMatrix = glm::inverse(camera.projectionMatrix);
    camera.viewProjectionMatrix = camera.projectionMatrix * camera.viewMatrix;
    camera.viewFrustum = Math::ViewFrustum::createFromViewProjection(camera.viewProjectionMatrix);
    return camera;
}

glm::vec3 ndc(const glm::mat4& viewProjection, glm::vec3 point) {
    const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
    return glm::vec3(clip) / clip.w;
}

bool isFinite(const glm::mat4& matrix) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(matrix[c][r])) {
                return false;
            }
        }
    }
    return true;
}

// A spot or point light with a name-free position, as the budget helper and slot assignment need.
SpotLight makeSpot(glm::vec3 position, bool casting, float range = 10.0f) {
    SpotLight light(ECS::INVALID_ENTITY_ID, 1.0f, range, 20.0f, 30.0f, glm::vec3(1.0f), casting);
    light.transform.position = position;
    return light;
}

// Owns an entity with a Transform and a DirectionalLight, and destroys it afterwards.
struct DirectionalEntity {
    ECS::EntityID id;
    DirectionalLight* light;
    explicit DirectionalEntity(bool withTransform = true) {
        auto& ecs = ECSManager::getInstance();
        id = ecs.createEntity();
        if (withTransform) {
            ecs.addComponent(id, Transform(id));
        }
        ecs.addComponent(id, DirectionalLight(id, 1.0f, glm::vec3(1.0f), glm::vec4(0, 0, 1, 0), true));
        light = ecs.getComponent<DirectionalLight>(id);
    }
    ~DirectionalEntity() { ECSManager::getInstance().destroyEntity(id); }
    DirectionalEntity(const DirectionalEntity&) = delete;
    DirectionalEntity& operator=(const DirectionalEntity&) = delete;
};

} // namespace

// ---------------------------------------------------------------- cascade splits

TEST_CASE("cascade splits increase, start past the near plane and end at the shadow distance") {
    DirectionalLight light;
    LightSystemTestAccess::cascadeSplits(light, 0.1f, 1000.0f);
    float previous = 0.1f;
    for (uint32_t i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        CHECK_MESSAGE(light.cascadeSplits[i] > previous, "split ", i, " = ", light.cascadeSplits[i]);
        previous = light.cascadeSplits[i];
    }
    CHECK(light.cascadeSplits[MAX_SHADOW_CASCADE_COUNT - 1] == doctest::Approx(MAX_SHADOW_DISTANCE));
}

TEST_CASE("cascade splits stop at the camera far plane when it is nearer than the shadow distance") {
    DirectionalLight light;
    LightSystemTestAccess::cascadeSplits(light, 0.5f, 100.0f);
    CHECK(light.cascadeSplits[MAX_SHADOW_CASCADE_COUNT - 1] == doctest::Approx(100.0f));
    for (uint32_t i = 1; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        CHECK(light.cascadeSplits[i] > light.cascadeSplits[i - 1]);
    }
}

TEST_CASE("cascade splits blend the logarithmic and uniform schemes") {
    DirectionalLight light;
    const float nearClip = 0.1f;
    const float farClip = 200.0f;
    LightSystemTestAccess::cascadeSplits(light, nearClip, farClip);
    for (uint32_t i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        const float p = float(i + 1) / float(MAX_SHADOW_CASCADE_COUNT);
        const float logarithmic = nearClip * std::pow(farClip / nearClip, p);
        const float uniform = nearClip + (farClip - nearClip) * p;
        CHECK(light.cascadeSplits[i] >= std::min(logarithmic, uniform) - 1e-3f);
        CHECK(light.cascadeSplits[i] <= std::max(logarithmic, uniform) + 1e-3f);
    }
}

// ---------------------------------------------------------------- directional light matrices

TEST_CASE("each cascade matrix covers the part of the camera frustum it is responsible for") {
    DirectionalEntity entity;
    DirectionalLight& light = *entity.light;
    Transform lightTransform;
    lightTransform.rotation = glm::angleAxis(0.9f, glm::normalize(glm::vec3(1, 0.3f, 0))); // a slanted sun
    CameraData camera = makeCamera({3, 2, -5});

    LightSystemTestAccess::updateDirectional(light, lightTransform, camera);

    const glm::vec3 forward = Systems::TransformSystem::getForward(lightTransform);
    CHECK_VEC3(glm::vec3(light.direction), forward);

    const float tanHalfY = std::tan(camera.fov * 0.5f);
    const float tanHalfX = tanHalfY * camera.aspectRatio;
    for (uint32_t cascade = 0; cascade < MAX_SHADOW_CASCADE_COUNT; ++cascade) {
        REQUIRE(isFinite(light.viewProjectionMatrix[cascade]));
        const float nearZ = cascade == 0 ? camera.nearPlane : light.cascadeSplits[cascade - 1];
        const float farZ = light.cascadeSplits[cascade];
        // The eight corners of the camera frustum slice, in world space (camera looks down +Z).
        for (int corner = 0; corner < 8; ++corner) {
            const float z = (corner & 4) ? farZ : nearZ;
            const glm::vec3 local((corner & 1) ? tanHalfX * z : -tanHalfX * z,
                                  (corner & 2) ? tanHalfY * z : -tanHalfY * z, z);
            const glm::vec3 world = camera.position + local;
            const glm::vec3 inLightNdc = ndc(light.viewProjectionMatrix[cascade], world);
            CHECK_MESSAGE(std::abs(inLightNdc.x) <= 1.0f + 1e-3f, "cascade ", cascade, " corner ", corner, " x ",
                          inLightNdc.x);
            CHECK_MESSAGE(std::abs(inLightNdc.y) <= 1.0f + 1e-3f, "cascade ", cascade, " corner ", corner, " y ",
                          inLightNdc.y);
            CHECK_MESSAGE(inLightNdc.z >= -1e-3f, "cascade ", cascade, " corner ", corner, " z ", inLightNdc.z);
            CHECK_MESSAGE(inLightNdc.z <= 1.0f + 1e-3f, "cascade ", cascade, " corner ", corner, " z ", inLightNdc.z);
        }
    }
}

TEST_CASE("a sun straight overhead gives finite cascade matrices") {
    DirectionalEntity entity;
    DirectionalLight& light = *entity.light;
    Transform lightTransform;
    lightTransform.rotation = glm::angleAxis(kPi / 2.0f, glm::vec3(1, 0, 0)); // forward is -Y: parallel to world up
    CameraData camera = makeCamera({0, 5, 0});
    LightSystemTestAccess::updateDirectional(light, lightTransform, camera);
    for (uint32_t cascade = 0; cascade < MAX_SHADOW_CASCADE_COUNT; ++cascade) {
        CHECK_MESSAGE(isFinite(light.viewProjectionMatrix[cascade]), "cascade ", cascade);
    }
}

TEST_CASE("cascade matrices do not move when the camera moves by less than a shadow texel") {
    // Texel snapping keeps shadow edges from shimmering: nudging the camera sideways by a tiny amount must not
    // change the cascade translation, because the light-space center snaps to the texel grid.
    DirectionalEntity entity;
    DirectionalLight& light = *entity.light;
    Transform lightTransform;
    lightTransform.rotation = glm::angleAxis(0.9f, glm::normalize(glm::vec3(1, 0.3f, 0)));

    CameraData base = makeCamera({0, 2, 0});
    LightSystemTestAccess::updateDirectional(light, lightTransform, base);
    const auto before = light.viewProjectionMatrix;

    CameraData nudged = makeCamera({0.0005f, 2, 0});
    LightSystemTestAccess::updateDirectional(light, lightTransform, nudged);
    for (uint32_t cascade = 0; cascade < MAX_SHADOW_CASCADE_COUNT; ++cascade) {
        const glm::mat4 diff = light.viewProjectionMatrix[cascade] - before[cascade];
        float largest = 0.0f;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                largest = std::max(largest, std::abs(diff[c][r]));
            }
        }
        CHECK_MESSAGE(largest < 1e-3f, "cascade ", cascade, " changed by ", largest);
    }
}

// ---------------------------------------------------------------- spot and point matrices

TEST_CASE("a spot light's matrix puts a point on its axis at the center of its shadow map") {
    const glm::quat rotations[] = {glm::quat(1, 0, 0, 0), glm::angleAxis(1.1f, glm::vec3(0, 1, 0)),
                                   glm::angleAxis(kPi / 2.0f, glm::vec3(1, 0, 0)),   // straight down
                                   glm::angleAxis(-kPi / 2.0f, glm::vec3(1, 0, 0))}; // straight up
    for (const glm::quat& rotation : rotations) {
        SpotLight light = makeSpot({4, 5, 6}, true, 20.0f);
        light.transform.rotation = rotation;
        LightSystemTestAccess::updateSpot(light);
        REQUIRE(isFinite(light.viewProjectionMatrix));
        const glm::vec3 axisPoint =
            light.transform.position + Systems::TransformSystem::getForward(light.transform) * 7.0f;
        const glm::vec3 inNdc = ndc(light.viewProjectionMatrix, axisPoint);
        CHECK_VEC3(glm::vec3(inNdc.x, inNdc.y, 0.0f), glm::vec3(0, 0, 0));
        CHECK(inNdc.z > 0.0f);
        CHECK(inNdc.z < 1.0f);
    }
}

TEST_CASE("a spot light's shadow map covers its outer cone") {
    SpotLight light = makeSpot({0, 0, 0}, true, 20.0f);
    LightSystemTestAccess::updateSpot(light);
    // outerCutoff is the full cone angle (the light buffer uses cos(outerCutoff / 2)). A point on the cone surface at
    // 10 units, just inside the half angle, must land inside the map.
    const float angle = glm::radians(light.outerCutoff * 0.5f * 0.95f);
    const glm::vec3 onCone(std::sin(angle) * 10.0f, 0.0f, std::cos(angle) * 10.0f);
    const glm::vec3 inNdc = ndc(light.viewProjectionMatrix, onCone);
    CHECK(std::abs(inNdc.x) <= 1.0f);
    CHECK(std::abs(inNdc.y) <= 1.0f);
}

TEST_CASE("each point light face looks along its axis, and the six faces tile the directions") {
    PointLight light(ECS::INVALID_ENTITY_ID, 1.0f, 15.0f, glm::vec3(1.0f), true);
    light.transform.position = {2, -3, 4};
    LightSystemTestAccess::updatePoint(light);

    const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int face = 0; face < 6; ++face) {
        REQUIRE(isFinite(light.viewProjectionMatrix[face]));
        const glm::vec3 inNdc = ndc(light.viewProjectionMatrix[face], light.transform.position + axes[face] * 5.0f);
        CHECK_MESSAGE(std::abs(inNdc.x) < 1e-4f, "face ", face);
        CHECK_MESSAGE(std::abs(inNdc.y) < 1e-4f, "face ", face);
        CHECK_MESSAGE((inNdc.z > 0.0f && inNdc.z < 1.0f), "face ", face, " depth ", inNdc.z);
    }

    // Every direction must be covered by at least one face (a 90 degree frustum per face leaves no gap).
    uint32_t state = 7u;
    auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return float((state >> 8) & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
    };
    int tested = 0;
    for (int i = 0; i < 300; ++i) {
        const glm::vec3 direction(next(), next(), next());
        if (glm::length(direction) < 0.1f) {
            continue;
        }
        ++tested;
        const glm::vec3 point = light.transform.position + glm::normalize(direction) * 6.0f;
        bool covered = false;
        for (int face = 0; face < 6; ++face) {
            const glm::vec3 inNdc = ndc(light.viewProjectionMatrix[face], point);
            covered = covered || (std::abs(inNdc.x) <= 1.0001f && std::abs(inNdc.y) <= 1.0001f && inNdc.z >= 0.0f &&
                                  inNdc.z <= 1.0f);
        }
        CHECK_MESSAGE(covered, "direction (", direction.x, ", ", direction.y, ", ", direction.z, ")");
    }
    CHECK(tested > 250);
}

// ---------------------------------------------------------------- frustum cull of lights

TEST_CASE("frustumCullLights collects directional lights that have a transform") {
    DirectionalEntity first;
    DirectionalEntity second;
    DirectionalEntity noTransform(false);
    CameraData camera = makeCamera({0, 0, 0});
    LightData lights;
    LightSystemTestAccess::cullLights(camera, lights);
    REQUIRE(lights.directionalLights.size() == 2);
    CHECK(std::count(lights.directionalLights.begin(), lights.directionalLights.end(), first.light) == 1);
    CHECK(std::count(lights.directionalLights.begin(), lights.directionalLights.end(), second.light) == 1);
    CHECK(std::count(lights.directionalLights.begin(), lights.directionalLights.end(), noTransform.light) == 0);
}

TEST_CASE("frustumCullLights accepts exactly MAX_DIRECTIONAL_LIGHTS and throws on one more") {
    std::vector<std::unique_ptr<DirectionalEntity>> entities;
    for (uint32_t i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i) {
        entities.push_back(std::make_unique<DirectionalEntity>());
    }
    CameraData camera = makeCamera({0, 0, 0});
    {
        LightData lights;
        CHECK_NOTHROW(LightSystemTestAccess::cullLights(camera, lights));
        CHECK(lights.directionalLights.size() == MAX_DIRECTIONAL_LIGHTS);
    }
    entities.push_back(std::make_unique<DirectionalEntity>());
    LightData lights;
    CHECK_THROWS_AS(LightSystemTestAccess::cullLights(camera, lights), std::runtime_error);
}

TEST_CASE("frustumCullLights sorts scene lights by type, keeps only visible ones and refreshes their matrices") {
    auto& scene = Scene::Scene::getInstance();
    SpotLight visibleSpot = makeSpot({0, 0, 20}, true);
    SpotLight hiddenSpot = makeSpot({0, 0, -400}, true);
    PointLight visiblePoint(ECS::INVALID_ENTITY_ID, 1.0f, 4.0f, glm::vec3(1.0f), true);
    visiblePoint.transform.position = {2, 0, 30};
    PointLight hiddenPoint(ECS::INVALID_ENTITY_ID, 1.0f, 4.0f, glm::vec3(1.0f), true);
    hiddenPoint.transform.position = {900, 0, 30};
    ECS::Light* all[] = {&visibleSpot, &hiddenSpot, &visiblePoint, &hiddenPoint};
    for (ECS::Light* light : all) {
        scene.addLight(*light);
    }

    CameraData camera = makeCamera({0, 0, 0});
    LightData lights;
    LightSystemTestAccess::cullLights(camera, lights);

    for (ECS::Light* light : all) {
        scene.removeLight(*light);
    }

    CHECK(lights.spotLights == std::vector<SpotLight*>{&visibleSpot});
    CHECK(lights.pointLights == std::vector<PointLight*>{&visiblePoint});
    CHECK(visibleSpot.viewProjectionMatrix != glm::mat4(1.0f)); // refreshed from the identity default
    CHECK(visiblePoint.viewProjectionMatrix[0] != glm::mat4(0.0f));
}

// ---------------------------------------------------------------- shadow slot assignment

TEST_CASE("assignShadowSlots gives each caster a slot and a matrix range, packed by type") {
    std::array<DirectionalLight, 2> directional;
    std::array<SpotLight, 3> spot;
    std::array<PointLight, 2> point;
    ShadowcastingData data;
    for (auto& light : directional) {
        data.directionalCasters.push_back(&light);
    }
    for (auto& light : spot) {
        data.spotCasters.push_back(&light);
    }
    for (auto& light : point) {
        data.pointCasters.push_back(&light);
    }

    auto frame = std::make_unique<FrameContext>();
    LightSystemTestAccess::assignSlots(*frame, data);

    // Directional lights take MAX_SHADOW_CASCADE_COUNT matrices each, spot lights one, point lights six.
    CHECK(frame->directionalShadowSlots.at(&directional[0]).slot == 0);
    CHECK(frame->directionalShadowSlots.at(&directional[0]).matrixBase == 0);
    CHECK(frame->directionalShadowSlots.at(&directional[1]).slot == 1);
    CHECK(frame->directionalShadowSlots.at(&directional[1]).matrixBase == MAX_SHADOW_CASCADE_COUNT);

    const uint32_t spotStart = 2 * MAX_SHADOW_CASCADE_COUNT;
    for (uint32_t i = 0; i < 3; ++i) {
        CHECK(frame->spotShadowSlots.at(&spot[i]).slot == i);
        CHECK(frame->spotShadowSlots.at(&spot[i]).matrixBase == spotStart + i);
    }
    const uint32_t pointStart = spotStart + 3;
    for (uint32_t i = 0; i < 2; ++i) {
        CHECK(frame->pointShadowSlots.at(&point[i]).slot == i);
        CHECK(frame->pointShadowSlots.at(&point[i]).matrixBase == pointStart + 6 * i);
    }
    CHECK(frame->directionalShadowSlots.size() == 2);
    CHECK(frame->spotShadowSlots.size() == 3);
    CHECK(frame->pointShadowSlots.size() == 2);
}

TEST_CASE("the matrices of a full set of shadow casters exactly fill the light matrix buffer") {
    std::array<DirectionalLight, MAX_DIRECTIONAL_LIGHTS> directional;
    std::array<SpotLight, MAX_SPOT_LIGHTS> spot;
    std::array<PointLight, MAX_POINT_LIGHTS> point;
    ShadowcastingData data;
    for (auto& light : directional) {
        data.directionalCasters.push_back(&light);
    }
    for (auto& light : spot) {
        data.spotCasters.push_back(&light);
    }
    for (auto& light : point) {
        data.pointCasters.push_back(&light);
    }
    auto frame = std::make_unique<FrameContext>();
    LightSystemTestAccess::assignSlots(*frame, data);

    const ShadowSlot lastPoint = frame->pointShadowSlots.at(&point.back());
    CHECK(lastPoint.slot == MAX_POINT_LIGHTS - 1);
    CHECK(lastPoint.matrixBase + 6 == MAX_SHADOWCASTING_LIGHT_MATRICES);
}

TEST_CASE("assignShadowSlots forgets the previous frame's casters") {
    DirectionalLight directional;
    SpotLight spot;
    PointLight point;
    ShadowcastingData first;
    first.directionalCasters.push_back(&directional);
    first.spotCasters.push_back(&spot);
    first.pointCasters.push_back(&point);
    auto frame = std::make_unique<FrameContext>();
    LightSystemTestAccess::assignSlots(*frame, first);

    ShadowcastingData second;
    second.spotCasters.push_back(&spot);
    LightSystemTestAccess::assignSlots(*frame, second);
    CHECK(frame->directionalShadowSlots.empty());
    CHECK(frame->pointShadowSlots.empty());
    REQUIRE(frame->spotShadowSlots.size() == 1);
    CHECK(frame->spotShadowSlots.at(&spot).matrixBase == 0);
}

// ---------------------------------------------------------------- shadow budget

TEST_CASE("the shadow budget keeps the nearest casters and skips lights that do not cast") {
    std::vector<SpotLight> storage = {makeSpot({0, 0, 50}, true), makeSpot({0, 0, 10}, true),
                                      makeSpot({0, 0, 5}, false), makeSpot({0, 0, 30}, true),
                                      makeSpot({0, 0, 20}, true)};
    std::vector<SpotLight*> lights;
    for (auto& light : storage) {
        lights.push_back(&light);
    }
    std::vector<SpotLight*> casters;
    std::vector<SpotLight*> processed;
    Systems::processNearestCasters(lights, glm::vec3(0), 3, "spot", casters, [&](SpotLight& light) {
        processed.push_back(&light);
        return true;
    });

    // z = 10, 20, 30 are the three nearest casters; z = 5 does not cast and z = 50 is over budget.
    REQUIRE(casters.size() == 3);
    CHECK(casters[0] == &storage[1]);
    CHECK(casters[1] == &storage[4]);
    CHECK(casters[2] == &storage[3]);
    CHECK(processed == casters); // an over-budget light is never processed
}

TEST_CASE("a light that ends up with no shadow data does not use up the budget") {
    std::vector<SpotLight> storage = {makeSpot({0, 0, 10}, true), makeSpot({0, 0, 20}, true),
                                      makeSpot({0, 0, 30}, true)};
    std::vector<SpotLight*> lights = {&storage[0], &storage[1], &storage[2]};
    std::vector<SpotLight*> casters;
    Systems::processNearestCasters(lights, glm::vec3(0), 2, "spot", casters,
                                   [&](SpotLight& light) { return &light != &storage[0]; }); // nearest has no data
    REQUIRE(casters.size() == 2);
    CHECK(casters[0] == &storage[1]);
    CHECK(casters[1] == &storage[2]);
}

TEST_CASE("the shadow budget gives the same casters whatever order the lights are listed in") {
    // The listing order of the scene's lights must not decide which lights get shadows.
    std::vector<SpotLight> storage;
    for (int i = 0; i < 12; ++i) {
        storage.push_back(makeSpot({float(i * 7 % 12), 0, 20.0f + float(i)}, true));
    }
    auto select = [&](const std::vector<int>& order) {
        std::vector<SpotLight*> lights;
        for (int index : order) {
            lights.push_back(&storage[index]);
        }
        std::vector<SpotLight*> casters;
        Systems::processNearestCasters(lights, glm::vec3(0), 5, "spot", casters, [](SpotLight&) { return true; });
        std::sort(casters.begin(), casters.end());
        return casters;
    };
    std::vector<int> order(12);
    for (int i = 0; i < 12; ++i) {
        order[i] = i;
    }
    const auto reference = select(order);
    REQUIRE(reference.size() == 5);
    std::reverse(order.begin(), order.end());
    CHECK(select(order) == reference);
    std::rotate(order.begin(), order.begin() + 5, order.end());
    CHECK(select(order) == reference);
}

TEST_CASE("equally distant casters keep their listing order") {
    std::vector<SpotLight> storage = {makeSpot({0, 0, 10}, true), makeSpot({0, 0, -10}, true),
                                      makeSpot({10, 0, 0}, true)};
    std::vector<SpotLight*> lights = {&storage[2], &storage[0], &storage[1]};
    std::vector<SpotLight*> casters;
    Systems::processNearestCasters(lights, glm::vec3(0), 2, "spot", casters, [](SpotLight&) { return true; });
    REQUIRE(casters.size() == 2);
    CHECK(casters[0] == &storage[2]);
    CHECK(casters[1] == &storage[0]);
}

TEST_CASE("casters already listed count against the budget") {
    std::vector<SpotLight> storage = {makeSpot({0, 0, 10}, true), makeSpot({0, 0, 20}, true)};
    std::vector<SpotLight*> lights = {&storage[0], &storage[1]};
    SpotLight alreadyThere;
    std::vector<SpotLight*> casters = {&alreadyThere};
    Systems::processNearestCasters(lights, glm::vec3(0), 2, "spot", casters, [](SpotLight&) { return true; });
    REQUIRE(casters.size() == 2);
    CHECK(casters[1] == &storage[0]);
}
