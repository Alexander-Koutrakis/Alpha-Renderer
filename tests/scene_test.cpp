#include "doctest.h"
#include "test_helpers.hpp"
#include "Scene/scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <set>
#include <vector>

using ECS::PointLight;
using ECS::SpotLight;
using Math::AABB;
using Math::ViewFrustum;

// Scene::addRenderer and updateRenderer need a Mesh, which needs a Vulkan device, so only lights and the
// renderer-free queries are covered here.

namespace {

constexpr float kPi = 3.14159265358979f;

// Camera at the origin looking down +Z, built the way CameraSystem builds its matrices.
ViewFrustum originCamera(float farPlane = 100.0f) {
    glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(60.0f), 1.0f, 0.1f, farPlane);
    proj[1][1] *= -1.0f;
    const glm::mat4 view = glm::lookAtLH(glm::vec3(0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
    return ViewFrustum::createFromViewProjection(proj * view);
}

PointLight pointLightAt(glm::vec3 position, float range) {
    PointLight light(ECS::INVALID_ENTITY_ID, 1.0f, range);
    light.transform.position = position;
    return light;
}

SpotLight spotLightAt(glm::vec3 position, glm::quat rotation, float range, float outerDegrees = 30.0f) {
    SpotLight light(ECS::INVALID_ENTITY_ID, 1.0f, range, outerDegrees * 0.7f, outerDegrees);
    light.transform.position = position;
    light.transform.rotation = rotation;
    return light;
}

std::set<const ECS::Light*> asSet(const std::vector<ECS::Light*>& lights) {
    return {lights.begin(), lights.end()};
}

// Removes the lights from the Scene singleton when a test ends, even if an assertion throws.
struct SceneLights {
    std::vector<ECS::Light*> added;
    void add(ECS::Light& light) {
        Scene::Scene::getInstance().addLight(light);
        added.push_back(&light);
    }
    ~SceneLights() {
        for (ECS::Light* light : added) {
            Scene::Scene::getInstance().removeLight(*light);
        }
    }
};

} // namespace

TEST_CASE("a light is visible only while it is in the scene") {
    auto& scene = Scene::Scene::getInstance();
    PointLight light = pointLightAt({0, 0, 20}, 2.0f);
    const ViewFrustum camera = originCamera();

    CHECK(scene.getVisibleLights(camera).empty());
    scene.addLight(light);
    CHECK(asSet(scene.getVisibleLights(camera)) == std::set<const ECS::Light*>{&light});
    scene.removeLight(light);
    CHECK(scene.getVisibleLights(camera).empty());
    scene.removeLight(light); // removing twice is harmless
}

TEST_CASE("visible lights are those whose influence reaches the frustum") {
    auto& scene = Scene::Scene::getInstance();
    PointLight ahead = pointLightAt({0, 0, 30}, 3.0f);
    PointLight behind = pointLightAt({0, 0, -30}, 3.0f);
    PointLight farSide = pointLightAt({200, 0, 30}, 3.0f);
    PointLight beyondFar = pointLightAt({0, 0, 500}, 3.0f);
    PointLight bigBehind = pointLightAt({0, 0, -5}, 20.0f); // range reaches into the view
    SceneLights lights;
    for (PointLight* light : {&ahead, &behind, &farSide, &beyondFar, &bigBehind}) {
        lights.add(*light);
    }

    const auto visible = asSet(scene.getVisibleLights(originCamera()));
    CHECK(visible.count(&ahead) == 1);
    CHECK(visible.count(&behind) == 0);
    CHECK(visible.count(&farSide) == 0);
    CHECK(visible.count(&beyondFar) == 0);
    CHECK(visible.count(&bigBehind) == 1);
}

TEST_CASE("updateLight moves a light in the scene and adds one that is not there yet") {
    auto& scene = Scene::Scene::getInstance();
    PointLight light = pointLightAt({0, 0, -30}, 3.0f);
    SceneLights lights;
    lights.add(light);
    CHECK(scene.getVisibleLights(originCamera()).empty());

    light.transform.position = {0, 0, 30};
    scene.updateLight(light);
    CHECK(asSet(scene.getVisibleLights(originCamera())) == std::set<const ECS::Light*>{&light});

    PointLight late = pointLightAt({1, 0, 30}, 1.0f);
    lights.added.push_back(&late); // the fixture removes it
    scene.updateLight(late);
    CHECK(scene.getVisibleLights(originCamera()).size() == 2);
}

TEST_CASE("getIntersectingLights finds a light by its bounds") {
    auto& scene = Scene::Scene::getInstance();
    PointLight light = pointLightAt({10, 10, 10}, 2.0f);
    SceneLights lights;
    lights.add(light);

    AABB nearby{};
    nearby.center = {11, 10, 10};
    nearby.extents = glm::vec3(0.5f);
    AABB faraway{};
    faraway.center = {-50, -50, -50};
    faraway.extents = glm::vec3(1.0f);
    CHECK(asSet(scene.getIntersectingLights(nearby)).count(&light) == 1);
    CHECK(asSet(scene.getIntersectingLights(faraway)).count(&light) == 0);
}

TEST_CASE("a spot light's bounds follow its direction and range") {
    auto& scene = Scene::Scene::getInstance();
    // Rotating +Z by 90 degrees about +Y gives +X: a cone from the origin out to x = 20.
    SpotLight light = spotLightAt({0, 0, 0}, glm::angleAxis(kPi / 2.0f, glm::vec3(0, 1, 0)), 20.0f);
    SceneLights lights;
    lights.add(light);

    AABB atTip{};
    atTip.center = {0, 0, 0};
    atTip.extents = glm::vec3(0.5f);
    AABB atBase{};
    atBase.center = {19, 0, 0};
    atBase.extents = glm::vec3(0.5f);
    AABB behind{};
    behind.center = {-10, 0, 0};
    behind.extents = glm::vec3(0.5f);
    CHECK(asSet(scene.getIntersectingLights(atTip)).count(&light) == 1);
    CHECK(asSet(scene.getIntersectingLights(atBase)).count(&light) == 1);
    CHECK(asSet(scene.getIntersectingLights(behind)).count(&light) == 0);
}
