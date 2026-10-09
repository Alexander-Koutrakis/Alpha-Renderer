#include "doctest.h"
#include "test_helpers.hpp"
#include "Systems/transform_system.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using ECS::Transform;
using Systems::TransformSystem;

namespace {

constexpr float kPi = 3.14159265358979f;

glm::vec3 transformPoint(const glm::mat4& matrix, glm::vec3 point) {
    return glm::vec3(matrix * glm::vec4(point, 1.0f));
}

} // namespace

TEST_CASE("updateTransform of a default transform gives identity matrices") {
    Transform transform;
    TransformSystem::updateTransform(transform);
    CHECK(transform.modelMatrix == glm::mat4(1.0f));
    CHECK(transform.normalMatrix == glm::mat4(1.0f));
}

TEST_CASE("the model matrix scales, then rotates, then translates") {
    Transform transform;
    transform.position = glm::vec3(10, 0, 0);
    transform.scale = glm::vec3(2, 1, 1);
    transform.rotation = glm::angleAxis(kPi / 2.0f, glm::vec3(0, 1, 0)); // +X turns to -Z
    TransformSystem::updateTransform(transform);

    // (1,0,0) -> scaled (2,0,0) -> turned (0,0,-2) -> moved (10,0,-2)
    CHECK_VEC3(transformPoint(transform.modelMatrix, glm::vec3(1, 0, 0)), glm::vec3(10, 0, -2));
    CHECK_VEC3(transformPoint(transform.modelMatrix, glm::vec3(0, 0, 0)), glm::vec3(10, 0, 0));
}

TEST_CASE("the normal matrix keeps normals perpendicular to the surface under non-uniform scale") {
    Transform transform;
    transform.scale = glm::vec3(4, 1, 0.5f);
    transform.rotation = glm::angleAxis(0.6f, glm::normalize(glm::vec3(1, 1, 0)));
    transform.position = glm::vec3(5, -3, 2);
    TransformSystem::updateTransform(transform);

    // A slanted plane through the origin: tangents t1 and t2, normal n = t1 x t2.
    const glm::vec3 t1 = glm::normalize(glm::vec3(1, 1, 0));
    const glm::vec3 t2 = glm::normalize(glm::vec3(0, 1, 1));
    const glm::vec3 n = glm::cross(t1, t2);

    const glm::vec3 worldT1 = glm::vec3(transform.modelMatrix * glm::vec4(t1, 0.0f));
    const glm::vec3 worldT2 = glm::vec3(transform.modelMatrix * glm::vec4(t2, 0.0f));
    const glm::vec3 worldN = glm::vec3(transform.normalMatrix * glm::vec4(n, 0.0f));

    CHECK(glm::dot(worldN, worldT1) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(glm::dot(worldN, worldT2) == doctest::Approx(0.0f).epsilon(1e-4));
    // Using the model matrix on the normal would break this: prove the check can fail.
    const glm::vec3 wrongN = glm::vec3(transform.modelMatrix * glm::vec4(n, 0.0f));
    CHECK(std::abs(glm::dot(wrongN, worldT1)) + std::abs(glm::dot(wrongN, worldT2)) > 0.1f);
}

TEST_CASE("the normal matrix has no translation and a unit w") {
    Transform transform;
    transform.position = glm::vec3(100, 200, 300);
    transform.scale = glm::vec3(2, 3, 4);
    TransformSystem::updateTransform(transform);
    CHECK_VEC3(glm::vec3(transform.normalMatrix[3]), glm::vec3(0, 0, 0));
    CHECK(transform.normalMatrix[3][3] == doctest::Approx(1.0f));
}

TEST_CASE("rotate turns the forward vector by the angle about the axis and refreshes the matrices") {
    Transform transform;
    TransformSystem::rotate(transform, kPi / 2.0f, glm::vec3(0, 1, 0));
    CHECK_VEC3(TransformSystem::getForward(transform), glm::vec3(1, 0, 0));
    CHECK_VEC3(TransformSystem::getRight(transform), glm::vec3(0, 0, -1));
    CHECK_VEC3(TransformSystem::getUp(transform), glm::vec3(0, 1, 0));
    CHECK_VEC3(transformPoint(transform.modelMatrix, glm::vec3(0, 0, 1)), glm::vec3(1, 0, 0));
}

TEST_CASE("rotate normalizes a non-unit axis and keeps the rotation a unit quaternion over many steps") {
    Transform transform;
    for (int i = 0; i < 5000; ++i) {
        TransformSystem::rotate(transform, 0.013f, glm::vec3(3, 4, 5)); // axis length is not 1
    }
    CHECK(glm::length(transform.rotation) == doctest::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("rotateRelative yaws about world up") {
    Transform transform;
    TransformSystem::rotateRelative(transform, kPi / 2.0f, 0.0f, 0.0f);
    CHECK_VEC3(TransformSystem::getForward(transform), glm::vec3(1, 0, 0));
}

TEST_CASE("rotateRelative pitches about world X") {
    Transform transform;
    TransformSystem::rotateRelative(transform, 0.0f, kPi / 2.0f, 0.0f);
    // A positive turn about +X takes +Z towards -Y.
    CHECK_VEC3(TransformSystem::getForward(transform), glm::vec3(0, -1, 0));
}

TEST_CASE("forward, right and up stay orthonormal after arbitrary rotations") {
    Transform transform;
    TransformSystem::rotate(transform, 0.9f, glm::vec3(1, 2, 3));
    TransformSystem::rotateRelative(transform, 0.3f, -0.7f, 1.1f);
    const glm::vec3 f = TransformSystem::getForward(transform);
    const glm::vec3 r = TransformSystem::getRight(transform);
    const glm::vec3 u = TransformSystem::getUp(transform);
    CHECK(glm::length(f) == doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(glm::dot(f, r) == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(glm::dot(f, u) == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(glm::dot(r, u) == doctest::Approx(0.0f).epsilon(1e-5));
    // Left-handed basis: with the right-handed cross product, right x up points along forward.
    CHECK_VEC3(glm::cross(r, u), f);
}

TEST_CASE("Euler setters round-trip") {
    Transform transform;
    TransformSystem::setRotationEuler(transform, glm::vec3(0.2f, 0.4f, 0.1f));
    CHECK_VEC3(TransformSystem::getRotationEuler(transform), glm::vec3(0.2f, 0.4f, 0.1f));
}
