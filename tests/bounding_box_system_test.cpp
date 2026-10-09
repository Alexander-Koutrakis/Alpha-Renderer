#include "doctest.h"
#include "test_helpers.hpp"
#include "Systems/bounding_box_system.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

using Math::AABB;
using Systems::BoundingBoxSystem;

namespace {

constexpr float kPi = 3.14159265358979f;

AABB box(glm::vec3 center, glm::vec3 extents) {
    AABB result{};
    result.center = center;
    result.extents = extents;
    return result;
}

} // namespace

// ---------------------------------------------------------------- getWorldBounds

TEST_CASE("getWorldBounds under identity is the local box") {
    AABB world{};
    BoundingBoxSystem::getWorldBounds(world, box({1, 2, 3}, {4, 5, 6}), glm::mat4(1.0f));
    CHECK_VEC3(world.center, glm::vec3(1, 2, 3));
    CHECK_VEC3(world.extents, glm::vec3(4, 5, 6));
}

TEST_CASE("getWorldBounds applies scale and translation") {
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, -5));
    transform = glm::scale(transform, glm::vec3(2, 1, 3));
    AABB world{};
    BoundingBoxSystem::getWorldBounds(world, box({1, 1, 1}, {1, 2, 1}), transform);
    CHECK_VEC3(world.center, glm::vec3(12, 1, -2));
    CHECK_VEC3(world.extents, glm::vec3(2, 2, 3));
}

TEST_CASE("getWorldBounds swaps extents under a quarter turn") {
    const glm::mat4 turnY = glm::rotate(glm::mat4(1.0f), kPi / 2.0f, glm::vec3(0, 1, 0));
    AABB world{};
    BoundingBoxSystem::getWorldBounds(world, box({0, 0, 0}, {1, 2, 3}), turnY);
    CHECK_VEC3(world.extents, glm::vec3(3, 2, 1));
}

TEST_CASE("getWorldBounds of a box turned 45 degrees is the rotated diamond's bounds") {
    // A 2x2 square rotated 45 degrees about Z reaches sqrt(2) along x and y.
    const glm::mat4 turnZ = glm::rotate(glm::mat4(1.0f), kPi / 4.0f, glm::vec3(0, 0, 1));
    AABB world{};
    BoundingBoxSystem::getWorldBounds(world, box({0, 0, 0}, {1, 1, 1}), turnZ);
    CHECK_VEC3(world.extents, glm::vec3(std::sqrt(2.0f), std::sqrt(2.0f), 1.0f));
}

TEST_CASE("getWorldBounds contains every transformed point of the local box and touches all six sides") {
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(3, -2, 7));
    transform = glm::rotate(transform, 0.7f, glm::normalize(glm::vec3(1, 2, 3)));
    transform = glm::scale(transform, glm::vec3(1.5f, 0.5f, 2.0f));
    const AABB local = box({0.5f, -1, 2}, {1, 2, 0.5f});

    AABB world{};
    BoundingBoxSystem::getWorldBounds(world, local, transform);
    const glm::vec3 worldMin = BoundingBoxSystem::getMin(world);
    const glm::vec3 worldMax = BoundingBoxSystem::getMax(world);

    glm::vec3 seenMin(1e9f);
    glm::vec3 seenMax(-1e9f);
    const int steps = 6;
    for (int ix = 0; ix <= steps; ++ix) {
        for (int iy = 0; iy <= steps; ++iy) {
            for (int iz = 0; iz <= steps; ++iz) {
                const glm::vec3 t(float(ix) / steps, float(iy) / steps, float(iz) / steps);
                const glm::vec3 point = BoundingBoxSystem::getMin(local) + t * BoundingBoxSystem::getSize(local);
                const glm::vec3 moved = glm::vec3(transform * glm::vec4(point, 1.0f));
                seenMin = glm::min(seenMin, moved);
                seenMax = glm::max(seenMax, moved);
            }
        }
    }
    // The sample grid includes the eight corners, so the sampled bounds equal the exact bounds.
    CHECK_VEC3(worldMin, seenMin);
    CHECK_VEC3(worldMax, seenMax);
}

// ---------------------------------------------------------------- min, max, size, contains

TEST_CASE("getMin, getMax and getSize agree with center and extents") {
    const AABB bounds = box({1, 2, 3}, {0.5f, 1, 2});
    CHECK_VEC3(BoundingBoxSystem::getMin(bounds), glm::vec3(0.5f, 1, 1));
    CHECK_VEC3(BoundingBoxSystem::getMax(bounds), glm::vec3(1.5f, 3, 5));
    CHECK_VEC3(BoundingBoxSystem::getSize(bounds), glm::vec3(1, 2, 4));
}

TEST_CASE("Contains includes the faces and rejects points just outside") {
    const AABB bounds = box({0, 0, 0}, {1, 1, 1});
    CHECK(BoundingBoxSystem::Contains(bounds, {0, 0, 0}));
    CHECK(BoundingBoxSystem::Contains(bounds, {1, 1, 1}));
    CHECK(BoundingBoxSystem::Contains(bounds, {-1, 0.5f, 0}));
    CHECK_FALSE(BoundingBoxSystem::Contains(bounds, {1.001f, 0, 0}));
    CHECK_FALSE(BoundingBoxSystem::Contains(bounds, {0, -1.001f, 0}));
    CHECK_FALSE(BoundingBoxSystem::Contains(bounds, {0, 0, 1.001f}));
}

// ---------------------------------------------------------------- encapsulate

TEST_CASE("encapsulate of one box is that box") {
    AABB result{};
    BoundingBoxSystem::encapsulate(result, {box({1, 2, 3}, {1, 1, 1})});
    CHECK_VEC3(result.center, glm::vec3(1, 2, 3));
    CHECK_VEC3(result.extents, glm::vec3(1, 1, 1));
}

TEST_CASE("encapsulate covers every input box and nothing more") {
    const std::vector<AABB> boxes = {box({0, 0, 0}, {1, 1, 1}), box({10, 0, 0}, {1, 2, 1}),
                                     box({-3, 4, -6}, {1, 1, 2})};
    AABB result{};
    BoundingBoxSystem::encapsulate(result, boxes);
    CHECK_VEC3(BoundingBoxSystem::getMin(result), glm::vec3(-4, -2, -8));
    CHECK_VEC3(BoundingBoxSystem::getMax(result), glm::vec3(11, 5, 1));
}

// ---------------------------------------------------------------- intersects

TEST_CASE("intersects: overlapping and touching boxes intersect, separated ones do not") {
    const AABB a = box({0, 0, 0}, {1, 1, 1});
    CHECK(BoundingBoxSystem::intersects(a, box({1.5f, 0, 0}, {1, 1, 1})));
    CHECK(BoundingBoxSystem::intersects(a, box({2, 0, 0}, {1, 1, 1}))); // touching faces
    CHECK_FALSE(BoundingBoxSystem::intersects(a, box({2.01f, 0, 0}, {1, 1, 1})));
    CHECK_FALSE(BoundingBoxSystem::intersects(a, box({0, 5, 0}, {1, 1, 1})));
    CHECK(BoundingBoxSystem::intersects(a, box({0, 0, 0}, {0.1f, 0.1f, 0.1f}))); // containment
}

TEST_CASE("intersects is symmetric over a sweep and matches an overlap test per axis") {
    const AABB a = box({0, 0, 0}, {2, 1, 3});
    int hits = 0;
    int misses = 0;
    for (float x = -6; x <= 6; x += 1.5f) {
        for (float y = -4; y <= 4; y += 1.5f) {
            for (float z = -8; z <= 8; z += 2.0f) {
                const AABB b = box({x, y, z}, {1, 1.5f, 2});
                const bool expected = std::abs(x) <= 2 + 1 && std::abs(y) <= 1 + 1.5f && std::abs(z) <= 3 + 2;
                CHECK(BoundingBoxSystem::intersects(a, b) == expected);
                CHECK(BoundingBoxSystem::intersects(b, a) == expected);
                (expected ? hits : misses)++;
            }
        }
    }
    CHECK(hits > 0);
    CHECK(misses > 0);
}

// ---------------------------------------------------------------- light bounds

TEST_CASE("calculatePointLightBounds is a cube of half-size range around the light") {
    AABB bounds{};
    BoundingBoxSystem::calculatePointLightBounds(bounds, {1, 2, 3}, 5.0f);
    CHECK_VEC3(bounds.center, glm::vec3(1, 2, 3));
    CHECK_VEC3(bounds.extents, glm::vec3(5, 5, 5));
}

// ---------------------------------------------------------------- overlapsViewDepthRange

TEST_CASE("overlapsViewDepthRange uses depth along the view direction") {
    const glm::mat4 view(1.0f);                    // left-handed, +Z forward: view z equals world z
    const AABB bounds = box({0, 0, 6}, {1, 1, 1}); // depth 5..7
    CHECK(BoundingBoxSystem::overlapsViewDepthRange(bounds, view, 6.0f, 10.0f));
    CHECK(BoundingBoxSystem::overlapsViewDepthRange(bounds, view, 0.0f, 5.0f));  // touches at 5
    CHECK(BoundingBoxSystem::overlapsViewDepthRange(bounds, view, 7.0f, 20.0f)); // touches at 7
    CHECK(BoundingBoxSystem::overlapsViewDepthRange(bounds, view, 5.5f, 6.5f));  // range inside the box
    CHECK_FALSE(BoundingBoxSystem::overlapsViewDepthRange(bounds, view, 0.0f, 4.99f));
    CHECK_FALSE(BoundingBoxSystem::overlapsViewDepthRange(bounds, view, 7.01f, 20.0f));
}

TEST_CASE("overlapsViewDepthRange follows a translated and turned camera") {
    // Camera at (0,0,10) looking along -Z (turned around): the box at z = 4..6 is 4..6 units in front of it.
    const glm::mat4 view = glm::lookAtLH(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const AABB bounds = box({0, 0, 5}, {1, 1, 1});
    CHECK(BoundingBoxSystem::overlapsViewDepthRange(bounds, view, 4.5f, 5.5f));
    CHECK_FALSE(BoundingBoxSystem::overlapsViewDepthRange(bounds, view, 0.0f, 3.9f));
    CHECK_FALSE(BoundingBoxSystem::overlapsViewDepthRange(bounds, view, 6.1f, 50.0f));
}
