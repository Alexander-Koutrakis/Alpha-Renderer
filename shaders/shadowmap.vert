#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec2 outUV;

layout(push_constant) uniform PushConstants {
    vec4 lightPosRange;    
    uint lightMatrixIndex;       // Direct index into lightSpaceMatrices array
    uint modelMatrixOffset;      // Offset into model matrix buffer
    uint lightType;              // 0 = directional, 1 = spot, 2 = point
} push;

layout(set = 0, binding = 0) uniform ShadowUBO {
    mat4 lightSpaceMatrices[64];
} ubo;

layout(std430, set = 1, binding = 0) readonly buffer ModelMatrixBuffer {
    mat4 modelMatrices[];
} modelMatrixBuffer;


void main() {
    mat4 modelMatrix = modelMatrixBuffer.modelMatrices[gl_InstanceIndex + push.modelMatrixOffset];
    vec4 worldPos = modelMatrix * vec4(position, 1.0);  
    
    outWorldPos = worldPos.xyz;
    outUV = uv;
    
    gl_Position = ubo.lightSpaceMatrices[push.lightMatrixIndex] * worldPos;
}