#pragma once

#include "core.hpp"

namespace Scene {

struct EnvironmentLighting {
    glm::vec3 ambientColor;
    float ambientIntensity;
    Texture* skyboxTexture{nullptr};
    float reflectionIntensity;
    
    EnvironmentLighting(glm::vec3 ambientColor, float ambientIntensity, Texture* skyboxTexture, float reflectionIntensity)
        : ambientColor(ambientColor), ambientIntensity(ambientIntensity), skyboxTexture(skyboxTexture), reflectionIntensity(reflectionIntensity) {}
};

}
