#include "doctest.h"
#include "test_helpers.hpp"
#include "Math/AABB.hpp"

#include <glm/gtc/matrix_transform.hpp>

using Math::AABB;

TEST_CASE("AABB from center and extents keeps both") {
    const AABB box{{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
    CHECK_VEC3(box.center, glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK_VEC3(box.extents, glm::vec3(4.0f, 5.0f, 6.0f));
}

TEST_CASE("AABB default is an empty box at the origin") {
    const AABB box;
    CHECK_VEC3(box.center, glm::vec3(0.0f));
    CHECK_VEC3(box.extents, glm::vec3(0.0f));
}

TEST_CASE("AABB equality compares center and extents") {
    const AABB a{{0, 0, 0}, {1, 1, 1}};
    CHECK(a == AABB{{0, 0, 0}, {1, 1, 1}});
    CHECK_FALSE(a == AABB{{0, 0, 1}, {1, 1, 1}});
    CHECK_FALSE(a == AABB{{0, 0, 0}, {1, 1, 2}});
}

TEST_CASE("AABB from corners encloses all eight, in any order") {
    // A box spanning x [-1, 3], y [0, 4], z [2, 8], corners given in a scrambled order.
    const glm::vec3 corners[8] = {{3, 4, 8},  {-1, 0, 2}, {3, 0, 2}, {-1, 4, 2},
                                  {-1, 0, 8}, {3, 4, 2},  {3, 0, 8}, {-1, 4, 8}};
    const AABB box{corners};
    CHECK_VEC3(box.center, glm::vec3(1.0f, 2.0f, 5.0f));
    CHECK_VEC3(box.extents, glm::vec3(2.0f, 2.0f, 3.0f));
}

TEST_CASE("AABB from corners of a degenerate point has zero extents") {
    const glm::vec3 p{2.0f, -3.0f, 4.0f};
    const glm::vec3 corners[8] = {p, p, p, p, p, p, p, p};
    const AABB box{corners};
    CHECK_VEC3(box.center, p);
    CHECK_VEC3(box.extents, glm::vec3(0.0f));
}

TEST_CASE("combineAABBs of disjoint boxes spans both") {
    const AABB a{{0, 0, 0}, {1, 1, 1}};
    const AABB b{{4, 0, 0}, {1, 1, 1}};
    const AABB c = AABB::combineAABBs(a, b);
    CHECK_VEC3(c.center, glm::vec3(2.0f, 0.0f, 0.0f));
    CHECK_VEC3(c.extents, glm::vec3(3.0f, 1.0f, 1.0f));
}

TEST_CASE("combineAABBs with a contained box returns the outer box") {
    const AABB outer{{0, 0, 0}, {10, 10, 10}};
    const AABB inner{{1, 2, 3}, {1, 1, 1}};
    CHECK(AABB::combineAABBs(outer, inner) == outer);
    CHECK(AABB::combineAABBs(inner, outer) == outer);
}

TEST_CASE("combineAABBs is symmetric and idempotent") {
    const AABB a{{-3, 1, 0}, {2, 1, 4}};
    const AABB b{{5, -2, 1}, {1, 3, 1}};
    const AABB ab = AABB::combineAABBs(a, b);
    const AABB ba = AABB::combineAABBs(b, a);
    CHECK_VEC3(ab.center, ba.center);
    CHECK_VEC3(ab.extents, ba.extents);
    const AABB aa = AABB::combineAABBs(a, a);
    CHECK_VEC3(aa.center, a.center);
    CHECK_VEC3(aa.extents, a.extents);
}

TEST_CASE("fromViewProjection of an orthographic projection returns the view volume") {
    // Vulkan clip space: depth 0..1, so the volume is x [-2, 2], y [-3, 3], z [1, 11] in view space.
    const glm::mat4 proj = glm::orthoLH_ZO(-2.0f, 2.0f, -3.0f, 3.0f, 1.0f, 11.0f);
    const AABB box = AABB::fromViewProjection(proj);
    CHECK_VEC3(box.center, glm::vec3(0.0f, 0.0f, 6.0f));
    CHECK_VEC3(box.extents, glm::vec3(2.0f, 3.0f, 5.0f));
}

TEST_CASE("fromViewProjection of a perspective projection spans near to far") {
    const glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(90.0f), 1.0f, 1.0f, 9.0f);
    const AABB box = AABB::fromViewProjection(proj);
    // 90 degree fov: half-width equals depth, so the far plane (z = 9) gives x, y in [-9, 9].
    CHECK_VEC3(box.center, glm::vec3(0.0f, 0.0f, 5.0f));
    CHECK_VEC3(box.extents, glm::vec3(9.0f, 9.0f, 4.0f));
}

TEST_CASE("fromViewProjection follows the view transform") {
    const glm::mat4 proj = glm::orthoLH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 10.0f);
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -20.0f)); // camera at z = 20
    const AABB box = AABB::fromViewProjection(proj * view);
    CHECK_VEC3(box.center, glm::vec3(0.0f, 0.0f, 25.0f));
    CHECK_VEC3(box.extents, glm::vec3(1.0f, 1.0f, 5.0f));
}
