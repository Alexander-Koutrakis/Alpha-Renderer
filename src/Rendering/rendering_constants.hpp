#pragma once
#include <cstdint>

namespace Rendering {
    constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 4;
    constexpr uint32_t MAX_SPOT_LIGHTS = 8;
    constexpr uint32_t MAX_POINT_LIGHTS = 8;
    constexpr uint32_t MAX_LIGHTS = 128;
    constexpr uint32_t BASE_INSTANCED_RENDERABLES = 2000;
    
    //Shadows
    constexpr uint32_t MAX_SHADOW_CASCADE_COUNT = 4;//changing this to other value means we have to change how cascadesplits are passed to the buffer since now its using a vec4
    constexpr float MAX_SHADOW_DISTANCE = 300.0f;
    constexpr float MAX_SHADOW_DISTANCE_SQR = MAX_SHADOW_DISTANCE * MAX_SHADOW_DISTANCE;
    // Shadow casters beyond this distance from camera are culled (1.5x margin for shadows cast into view)
    constexpr float MAX_SHADOW_CASTER_DISTANCE = MAX_SHADOW_DISTANCE * 1.5f;
    constexpr float MAX_SHADOW_CASTER_DISTANCE_SQR = MAX_SHADOW_CASTER_DISTANCE * MAX_SHADOW_CASTER_DISTANCE;
    constexpr uint32_t MAX_SHADOWCASTING_LIGHT_MATRICES = 64;//1 directional(4 Cascades) +8 spot + 48 point (8 pointlights with 6 for each side)
    constexpr uint32_t MAX_SHADOWCASTING_DIRECTIONAL = 128;
    constexpr uint32_t DIRECTIONAL_SHADOW_MAP_RES = 2048;
    constexpr uint32_t SPOT_SHADOW_MAP_RES = 1028;
    constexpr uint32_t POINT_SHADOW_MAP_RES = 512;
    

    constexpr uint32_t RC_CASCADE_COUNT = 6;      
    constexpr uint32_t RC_BASE_TILE_SIZE = 2;     // i=0 tile: 2x2 = 4 directions per probe
    constexpr uint32_t RC_PROBE_STRIDE0_PX = 2;   
    constexpr uint32_t RC_DEPTH_MIP_LEVELS = 0;
    constexpr float RC_BASE_INTERVAL_LENGTH = 0.08f;
    constexpr float RC_INTERVAL_OVERLAP_FRACTION = 0.15f;
}
