#include "doctest.h"
#include "Math/view_frustum.hpp"

#include <glm/gtc/matrix_transform.hpp>

using Math::AABB;
using Math::ViewFrustum;
using Intersection = ViewFrustum::Intersection;

namespace {

constexpr float kFovY = glm::radians(90.0f); // tan(45 deg) = 1: half-width equals depth
constexpr float kNear = 1.0f;
constexpr float kFar = 100.0f;

AABB box(float x, float y, float z, float half = 1.0f) {
    return AABB{{x, y, z}, {half, half, half}};
}

// Camera at the origin looking down +z (the engine's perspective convention is left-handed, depth 0..1).
ViewFrustum originFrustum() {
    return ViewFrustum::createPerspective(kFovY, 1.0f, kNear, kFar, glm::mat4(1.0f));
}

} // namespace

TEST_CASE("perspective frustum classifies boxes along the view axis") {
    const ViewFrustum frustum = originFrustum();
    CHECK(frustum.testAABB(box(0, 0, 10)) == Intersection::INSIDE);
    CHECK(frustum.testAABB(box(0, 0, -10)) == Intersection::OUTSIDE); // behind the camera
    CHECK(frustum.testAABB(box(0, 0, 200)) == Intersection::OUTSIDE); // beyond the far plane
}

TEST_CASE("perspective frustum classifies boxes to the sides") {
    const ViewFrustum frustum = originFrustum();
    // At depth 10 the frustum is x, y in [-10, 10].
    CHECK(frustum.testAABB(box(50, 0, 10)) == Intersection::OUTSIDE);
    CHECK(frustum.testAABB(box(-50, 0, 10)) == Intersection::OUTSIDE);
    CHECK(frustum.testAABB(box(0, 50, 10)) == Intersection::OUTSIDE);
    CHECK(frustum.testAABB(box(0, -50, 10)) == Intersection::OUTSIDE);
    CHECK(frustum.testAABB(box(10, 0, 10)) == Intersection::INTERSECT);
    CHECK(frustum.testAABB(box(-10, 0, 10)) == Intersection::INTERSECT);
    CHECK(frustum.testAABB(box(0, 10, 10)) == Intersection::INTERSECT);
    CHECK(frustum.testAABB(box(0, -10, 10)) == Intersection::INTERSECT);
}

TEST_CASE("perspective frustum near and far planes intersect straddling boxes") {
    const ViewFrustum frustum = originFrustum();
    CHECK(frustum.testAABB(box(0, 0, kNear, 0.5f)) == Intersection::INTERSECT);
    CHECK(frustum.testAABB(box(0, 0, kFar, 2.0f)) == Intersection::INTERSECT);
    CHECK(frustum.testAABB(box(0, 0, kNear - 5.0f, 0.5f)) == Intersection::OUTSIDE);
}

TEST_CASE("a box that encloses the whole frustum intersects it") {
    const ViewFrustum frustum = originFrustum();
    CHECK(frustum.testAABB(box(0, 0, 0, 1000.0f)) == Intersection::INTERSECT);
}

TEST_CASE("perspective frustum follows the view matrix") {
    // Camera moved to z = 50: a box at z = 60 is 10 units ahead, a box at z = 10 is behind.
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -50.0f));
    const ViewFrustum frustum = ViewFrustum::createPerspective(kFovY, 1.0f, kNear, kFar, view);
    CHECK(frustum.testAABB(box(0, 0, 60)) == Intersection::INSIDE);
    CHECK(frustum.testAABB(box(0, 0, 10)) == Intersection::OUTSIDE);
}

TEST_CASE("aspect ratio widens the horizontal extent only") {
    const ViewFrustum frustum = ViewFrustum::createPerspective(kFovY, 2.0f, kNear, kFar, glm::mat4(1.0f));
    // At depth 10: x in [-20, 20], y in [-10, 10].
    CHECK(frustum.testAABB(box(15, 0, 10)) == Intersection::INSIDE);
    CHECK(frustum.testAABB(box(0, 15, 10)) == Intersection::OUTSIDE);
}

TEST_CASE("all perspective construction paths agree") {
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -2.0f, -15.0f));
    const glm::mat4 proj = glm::perspectiveLH_ZO(kFovY, 1.5f, kNear, kFar);

    const ViewFrustum fromParts = ViewFrustum::createPerspective(kFovY, 1.5f, kNear, kFar, view);
    const ViewFrustum fromMatrices = ViewFrustum::createPerspective(view, proj);
    const ViewFrustum fromViewProj = ViewFrustum::createFromViewProjection(proj * view);
    ViewFrustum updated;
    updated.update(proj * view);

    int mismatches = 0;
    int outside = 0;
    int inside = 0;
    for (float x = -60.0f; x <= 60.0f; x += 7.5f) {
        for (float y = -40.0f; y <= 40.0f; y += 8.0f) {
            for (float z = -20.0f; z <= 130.0f; z += 6.0f) {
                const AABB b = box(x, y, z, 1.5f);
                const Intersection expected = fromParts.testAABB(b);
                outside += expected == Intersection::OUTSIDE;
                inside += expected == Intersection::INSIDE;
                mismatches += fromMatrices.testAABB(b) != expected;
                mismatches += fromViewProj.testAABB(b) != expected;
                mismatches += updated.testAABB(b) != expected;
            }
        }
    }
    CHECK(mismatches == 0);
    // The sweep must exercise both sides of the frustum, or the comparison proves nothing.
    CHECK(outside > 0);
    CHECK(inside > 0);
}

TEST_CASE("orthographic frustum classifies boxes") {
    // Looks down -z from the origin: x, y in [-5, 5], depth 1..100.
    const ViewFrustum frustum =
        ViewFrustum::createOrthographic(-5.0f, 5.0f, -5.0f, 5.0f, 1.0f, 100.0f, glm::mat4(1.0f));
    CHECK(frustum.testAABB(box(0, 0, -10)) == Intersection::INSIDE);
    CHECK(frustum.testAABB(box(0, 0, 10)) == Intersection::OUTSIDE);
    CHECK(frustum.testAABB(box(0, 0, -200)) == Intersection::OUTSIDE);
    CHECK(frustum.testAABB(box(20, 0, -10)) == Intersection::OUTSIDE);
    CHECK(frustum.testAABB(box(0, 20, -10)) == Intersection::OUTSIDE);
    CHECK(frustum.testAABB(box(5, 0, -10)) == Intersection::INTERSECT);
    CHECK(frustum.testAABB(box(0, -5, -10)) == Intersection::INTERSECT);
}
