#include "doctest.h"
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

// The Radiance Cascades direction mapping is shader code (shaders/common/rc_direction.glsl). It is written in the subset
// GLSL and glm share, so this file compiles the real shader source and tests it on the CPU.
namespace rc_shader {
using namespace glm;
#include "shaders/common/rc_direction.glsl"
} // namespace rc_shader

// Tests marked should_fail pin a known defect from the radiance cascade review (F1, F2 in RadianceCascadeFixPlan.md).
// They pass today because the defect makes them fail. When the fix lands the test starts failing the other way:
// remove should_fail then, so the test guards the fix.

namespace {

constexpr float kPi = 3.14159265358979f;

float angleDegrees(const glm::vec3& a, const glm::vec3& b) {
    return glm::degrees(std::acos(glm::clamp(glm::dot(a, b), -1.0f, 1.0f)));
}

glm::vec2 texelUV(int x, int y, int tileSize) {
    return (glm::vec2(float(x), float(y)) + 0.5f) / float(tileSize);
}

// Direction of a tile texel in the probe's local frame (z = normal), exactly as rc_build_cascade writes it.
glm::vec3 localTexelDir(int x, int y, int tileSize) {
    return rc_shader::hemisphereDirFromUV(texelUV(x, y, tileSize), rc_shader::computeHemisphereShrink(tileSize));
}

} // namespace

TEST_CASE("RC probe basis is orthonormal and right-handed around any normal") {
    const glm::vec3 normals[] = {{0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {1, 0, 0}, {0.2f, 0.3f, 0.9f}, {0.001f, 0.0f, 1.0f}};
    for (const glm::vec3& n : normals) {
        const glm::vec3 normal = glm::normalize(n);
        const glm::mat3 basis = rc_shader::buildProbeBasis(normal);
        INFO("normal " << normal.x << ", " << normal.y << ", " << normal.z);
        CHECK(glm::length(basis[0]) == doctest::Approx(1.0f).epsilon(1e-4));
        CHECK(glm::length(basis[1]) == doctest::Approx(1.0f).epsilon(1e-4));
        CHECK(glm::dot(basis[0], basis[1]) == doctest::Approx(0.0f).epsilon(1e-4));
        CHECK(glm::dot(basis[0], basis[2]) == doctest::Approx(0.0f).epsilon(1e-4));
        CHECK(glm::dot(glm::cross(basis[0], basis[1]), basis[2]) == doctest::Approx(1.0f).epsilon(1e-4));
        CHECK(glm::dot(basis[2], normal) == doctest::Approx(1.0f).epsilon(1e-4));
    }
}

TEST_CASE("RC merge: a near texel reads exactly its own 2x2 children in the far tile") {
    for (int nearTile : {2, 4, 8, 16}) {
        for (int y = 0; y < nearTile; ++y) {
            for (int x = 0; x < nearTile; ++x) {
                const auto map = rc_shader::mapNearTexelToFarTile(glm::ivec2(x, y), nearTile);
                INFO("nearTile " << nearTile << " texel " << x << "," << y);
                CHECK(map.base == glm::ivec2(2 * x, 2 * y));
                CHECK(map.frac == glm::vec2(0.5f));
            }
        }
    }
}

// F1: the octahedral map covers the whole [0,1]^2 tile, but only the diamond |x|+|y| <= 1 is the upper hemisphere. The
// corners are folded onto the horizon (z = 0), so a large part of each tile holds grazing rays that the resolve never reads.
TEST_CASE("RC F1: no tile texel is a horizon ray" * doctest::should_fail()) {
    for (int tileSize : {2, 4, 8, 16, 32}) {
        float minZ = 1.0f;
        for (int y = 0; y < tileSize; ++y) {
            for (int x = 0; x < tileSize; ++x) {
                minZ = std::min(minZ, localTexelDir(x, y, tileSize).z);
            }
        }
        INFO("tileSize " << tileSize << " lowest texel z (cos of angle to normal) " << minZ);
        CHECK(minZ > 0.05f);
    }
}

// F1: cascade 0 (2x2 tile) has only four rays. They must spread over the hemisphere: for uniformly distributed directions
// the mean cosine to the normal is 0.5. The window is wide on purpose (four samples), the current value is about 0.16.
TEST_CASE("RC F1: cascade 0 rays are not all grazing" * doctest::should_fail()) {
    float meanCos = 0.0f;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            meanCos += localTexelDir(x, y, 2).z / 4.0f;
        }
    }
    INFO("mean cosine of the four cascade 0 rays: " << meanCos << " (uniform hemisphere: 0.5)");
    CHECK(meanCos > 0.35f);
    CHECK(meanCos < 0.75f);
}

// F1: the resolve reads a direction by inverting the build mapping. Every texel the build writes must be found again.
TEST_CASE("RC F1: every tile texel direction maps back to its own texel" * doctest::should_fail()) {
    for (int tileSize : {2, 4, 8, 16}) {
        const float shrink = rc_shader::computeHemisphereShrink(tileSize);
        int lost = 0;
        for (int y = 0; y < tileSize; ++y) {
            for (int x = 0; x < tileSize; ++x) {
                const glm::vec2 uv = rc_shader::hemisphereUVFromDir(localTexelDir(x, y, tileSize), shrink);
                const glm::vec2 expected = texelUV(x, y, tileSize);
                if (glm::any(glm::greaterThan(glm::abs(uv - expected), glm::vec2(0.5f / float(tileSize))))) {
                    ++lost;
                }
            }
        }
        INFO("tileSize " << tileSize << ": " << lost << " of " << tileSize * tileSize << " texels do not round-trip");
        CHECK(lost == 0);
    }
}

namespace {

// World direction a near texel represents versus the world direction of the far-tile sample the merge reads for it.
// Returns the worst angle over the near tile.
float worstMergeAngleDegrees(int nearTile, const glm::vec3& nearNormal, const glm::vec3& farNormal) {
    const int farTile = nearTile * 2;
    const glm::mat3 nearBasis = rc_shader::buildProbeBasis(nearNormal);
    const glm::mat3 farBasis = rc_shader::buildProbeBasis(farNormal);
    float worst = 0.0f;
    for (int y = 0; y < nearTile; ++y) {
        for (int x = 0; x < nearTile; ++x) {
            const glm::vec3 nearDir = nearBasis * localTexelDir(x, y, nearTile);
            const auto map = rc_shader::mapNearTexelToFarTile(glm::ivec2(x, y), nearTile);
            const glm::vec2 farUV = (glm::vec2(map.base) + map.frac + 0.5f) / float(farTile);
            const glm::vec3 farDir =
                farBasis * rc_shader::hemisphereDirFromUV(farUV, rc_shader::computeHemisphereShrink(farTile));
            worst = std::max(worst, angleDegrees(nearDir, farDir));
        }
    }
    return worst;
}

} // namespace

// Pitch of one far texel across the hemisphere: 180 degrees over the tile width.
TEST_CASE("RC merge: probes with the same normal merge the matching direction") {
    const glm::vec3 normal = glm::normalize(glm::vec3(0.3f, -0.2f, 0.9f));
    for (int nearTile : {4, 8, 16}) {
        const float pitch = 180.0f / float(nearTile * 2);
        const float worst = worstMergeAngleDegrees(nearTile, normal, normal);
        INFO("nearTile " << nearTile << ": worst direction error " << worst << " degrees, far texel pitch " << pitch);
        CHECK(worst <= pitch);
    }
}

// F2: the merge indexes the far tile by texel coordinate. A far probe with a different normal has a different tangent
// frame, so the same texel is another world direction and the merged radiance points the wrong way.
TEST_CASE("RC F2: probes with different normals merge the matching direction" * doctest::should_fail()) {
    const glm::vec3 nearNormal(0.0f, 0.0f, 1.0f);
    const glm::vec3 farNormal(0.0f, std::sin(kPi / 4.0f), std::cos(kPi / 4.0f)); // 45 degrees away
    for (int nearTile : {4, 8, 16}) {
        const float pitch = 180.0f / float(nearTile * 2);
        const float worst = worstMergeAngleDegrees(nearTile, nearNormal, farNormal);
        INFO("nearTile " << nearTile << ": worst direction error " << worst << " degrees, far texel pitch " << pitch);
        CHECK(worst <= pitch);
    }
}
