#pragma once

#include "core.hpp"

namespace Scene {

struct EnvironmentLighting {
    glm::vec3 ambientColor;
    float ambientIntensity;
    Rendering::Texture* skyboxTexture{nullptr};
    float reflectionIntensity;

    EnvironmentLighting(glm::vec3 ambientColor, float ambientIntensity, Rendering::Texture* skyboxTexture,
                        float reflectionIntensity)
        : ambientColor(ambientColor),
          ambientIntensity(ambientIntensity),
          skyboxTexture(skyboxTexture),
          reflectionIntensity(reflectionIntensity) {}
};

} // namespace Scene
