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
    return rc_shader::hemisphereDirFromUV(texelUV(x, y, tileSize));
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

// F1 (fixed): the old octahedral map folded the corners of the tile onto the horizon (z = 0), so about half of each large
// tile held grazing rays that the resolve never read. With the hemi-octahedral map the outermost texel centre is half a
// texel inside the horizon, so its cosine to the normal is about 1/tileSize.
TEST_CASE("RC F1: no tile texel is a horizon ray") {
    for (int tileSize : {2, 4, 8, 16, 32}) {
        float minZ = 1.0f;
        for (int y = 0; y < tileSize; ++y) {
            for (int x = 0; x < tileSize; ++x) {
                minZ = std::min(minZ, localTexelDir(x, y, tileSize).z);
            }
        }
        INFO("tileSize " << tileSize << " lowest texel z (cos of angle to normal) " << minZ);
        CHECK(minZ >= 0.9f / float(tileSize));
    }
}

// F1: cascade 0 (2x2 tile) has only four rays. They must spread over the hemisphere: for uniformly distributed directions
// the mean cosine to the normal is 0.5. The window is wide on purpose (four samples), it was 0.155 with the old map.
TEST_CASE("RC F1: cascade 0 rays are not all grazing") {
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
TEST_CASE("RC F1: every tile texel direction maps back to its own texel") {
    for (int tileSize : {2, 4, 8, 16}) {
        int lost = 0;
        for (int y = 0; y < tileSize; ++y) {
            for (int x = 0; x < tileSize; ++x) {
                const glm::vec2 uv = rc_shader::hemisphereUVFromDir(localTexelDir(x, y, tileSize));
                const glm::vec2 expected = texelUV(x, y, tileSize);
                if (glm::any(glm::greaterThan(glm::abs(uv - expected), glm::vec2(1e-4f)))) {
                    ++lost;
                }
            }
        }
        INFO("tileSize " << tileSize << ": " << lost << " of " << tileSize * tileSize << " texels do not round-trip");
        CHECK(lost == 0);
    }
}

namespace {

// For every texel of a near tile, the world direction it represents versus the world direction at the far-tile position the
// merge reads for it. Returns the worst angle over the texels well inside the far probe's hemisphere (cosine to its normal
// above 0.5; closer to the horizon the lookup clamps to the outermost texel centre on purpose), and counts the texels
// below its horizon.
struct MergeError {
    float worstDegrees = 0.0f;
    int visible = 0;
    int hidden = 0;
};

MergeError mergeError(int nearTile, const glm::vec3& nearNormal, const glm::vec3& farNormal) {
    const int farTile = nearTile * 2;
    const glm::mat3 nearBasis = rc_shader::buildProbeBasis(nearNormal);
    const glm::mat3 farBasis = rc_shader::buildProbeBasis(farNormal);
    MergeError result;
    for (int y = 0; y < nearTile; ++y) {
        for (int x = 0; x < nearTile; ++x) {
            const glm::vec3 nearDir = nearBasis * localTexelDir(x, y, nearTile);
            const auto lookup = rc_shader::lookupFarTile(nearDir, farBasis, farTile);
            if (lookup.localZ <= 0.0f) {
                ++result.hidden;
                CHECK(rc_shader::probeHorizonWeight(lookup.localZ) < 0.5f);
            }
            if (lookup.localZ <= 0.5f) {
                continue;
            }
            ++result.visible;
            const glm::vec2 farUV = (glm::vec2(lookup.base) + lookup.frac + 0.5f) / float(farTile);
            const glm::vec3 farDir = farBasis * rc_shader::hemisphereDirFromUV(farUV);
            result.worstDegrees = std::max(result.worstDegrees, angleDegrees(nearDir, farDir));
        }
    }
    return result;
}

} // namespace

// F2 (fixed): the merge used to index the far tile by texel coordinate. A far probe with a different normal has a different
// tangent frame, so the same texel is another world direction (172 to 179 degrees off for a 45 degree normal change).
// The far position is now found from the world direction in the far probe's own frame.
TEST_CASE("RC F2: probes with different normals merge the matching direction") {
    const glm::vec3 nearNormal(0.0f, 0.0f, 1.0f);
    const glm::vec3 tiltedNormals[] = {
        {0.0f, std::sin(kPi / 4.0f), std::cos(kPi / 4.0f)}, // 45 degrees away
        glm::normalize(glm::vec3(0.3f, -0.2f, 0.9f)),       // a mild tilt
        glm::normalize(glm::vec3(-0.7f, 0.1f, 0.7f)),       // another frame
    };
    for (const glm::vec3& farNormal : tiltedNormals) {
        for (int nearTile : {2, 4, 8, 16}) {
            const MergeError e = mergeError(nearTile, nearNormal, farNormal);
            INFO("nearTile " << nearTile << " farNormal " << farNormal.x << ", " << farNormal.y << ", " << farNormal.z
                             << ": worst direction error " << e.worstDegrees << " degrees over " << e.visible
                             << " checked texels (" << e.hidden << " below the far horizon)");
            CHECK(e.visible > 0);
            CHECK(e.worstDegrees < 0.5f);
        }
    }
}

TEST_CASE("RC F2: probes with the same normal read the same direction and see all of it") {
    const glm::vec3 normal = glm::normalize(glm::vec3(0.3f, -0.2f, 0.9f));
    for (int nearTile : {2, 4, 8, 16}) {
        const MergeError e = mergeError(nearTile, normal, normal);
        INFO("nearTile " << nearTile << ": worst direction error " << e.worstDegrees << " degrees");
        CHECK(e.hidden == 0);
        CHECK(e.worstDegrees < 0.5f);
    }
}

TEST_CASE("RC F2: a probe's horizon weight is 0 below its horizon and 1 well above it") {
    CHECK(rc_shader::probeHorizonWeight(-0.5f) == 0.0f);
    CHECK(rc_shader::probeHorizonWeight(0.5f) == 1.0f);
    CHECK(rc_shader::probeHorizonWeight(0.05f) > 0.0f);
    CHECK(rc_shader::probeHorizonWeight(0.05f) < 1.0f);
}

namespace {

// Irradiance at a surface with normal z of the radiance field L(direction), estimated the way rc_resolve_indirect does:
// one sample per cell of a cells x cells grid on the hemisphere map, weighted by cosine.
template <typename Field> float estimateIrradiance(int cells, Field radiance) {
    float sumLCos = 0.0f;
    float sumCos = 0.0f;
    for (int y = 0; y < cells; ++y) {
        for (int x = 0; x < cells; ++x) {
            const glm::vec3 dir =
                rc_shader::resolveSampleDirection(glm::ivec2(x, y), glm::ivec2(cells), glm::vec2(0.0f));
            sumLCos += radiance(dir) * dir.z;
            sumCos += dir.z;
        }
    }
    return rc_shader::irradianceFromSamples(glm::vec3(sumLCos), sumCos).x;
}

} // namespace

// Resolve quadrature: the hemisphere map has unequal texel solid angles, so weighting every sample by 2*PI/N over-estimates
// the irradiance of a uniform field (by 41% for the 2x2 grid the resolve uses: four rays at 45 degrees). A field of
// constant radiance 1 must give exactly PI.
TEST_CASE("RC resolve: a uniform radiance field has irradiance PI") {
    for (int cells : {2, 4}) {
        const float e = estimateIrradiance(cells, [](const glm::vec3&) { return 1.0f; });
        INFO(cells << "x" << cells << " samples: irradiance " << e << ", exact " << kPi);
        CHECK(e == doctest::Approx(kPi).epsilon(1e-4));
    }
}

// Four samples cannot integrate a directional field exactly, but a smooth one should land within about 10%. L = cos(theta)
// has the exact irradiance 2*PI/3; L = 1 + x/2 integrates to PI because the x term cancels by symmetry.
TEST_CASE("RC resolve: smooth radiance fields are integrated within 10%") {
    for (int cells : {2, 4}) {
        const float cosine = estimateIrradiance(cells, [](const glm::vec3& d) { return d.z; });
        const float tilted = estimateIrradiance(cells, [](const glm::vec3& d) { return 1.0f + 0.5f * d.x; });
        INFO(cells << "x" << cells << " samples: cos field " << cosine << " (exact " << 2.0f * kPi / 3.0f
                   << "), tilted field " << tilted << " (exact " << kPi << ")");
        CHECK(cosine == doctest::Approx(2.0f * kPi / 3.0f).epsilon(0.10));
        CHECK(tilted == doctest::Approx(kPi).epsilon(0.10));
    }
}
