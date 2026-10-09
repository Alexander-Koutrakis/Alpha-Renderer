#pragma once

#include "Engine/log.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace Systems {

// Processes the shadow-casting lights nearest to the camera first and stops at `budget` lights that ended up with
// shadow data. Lights beyond the budget keep their lighting but get no shadow; that is logged when the count changes.
template <typename LightT, typename ProcessFn>
void processNearestCasters(const std::vector<LightT*>& lights, const glm::vec3& cameraPosition, size_t budget,
                           const char* kind, std::vector<LightT*>& casters, ProcessFn process) {
    std::vector<LightT*> candidates;
    for (LightT* light : lights) {
        if (light->isCastingShadows) {
            candidates.push_back(light);
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [&cameraPosition](const LightT* a, const LightT* b) {
        return glm::distance2(a->transform.position, cameraPosition) <
               glm::distance2(b->transform.position, cameraPosition);
    });

    size_t dropped = 0;
    for (LightT* light : candidates) {
        if (casters.size() >= budget) {
            ++dropped;
            continue;
        }
        if (process(*light)) {
            casters.push_back(light);
        }
    }

    static size_t lastReported = 0;
    if (dropped != lastReported) {
        if (dropped > 0) {
            Log::warn(dropped, " ", kind, " light(s) cast shadows beyond the budget of ", budget, "; the nearest ",
                      budget, " keep their shadows, the others are lit without one");
        }
        lastReported = dropped;
    }
}

} // namespace Systems
