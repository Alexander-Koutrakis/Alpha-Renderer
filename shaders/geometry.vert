#version 450

// Vertex attributes (binding = 0)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;


layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 4) out vec3 worldTangent;
layout(location = 5) out vec3 worldBitangent;
layout(location = 6) out vec3 worldNormal;

layout(set = 0, binding = 0) uniform CameraUbo {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
} cameraUBO;

// Instance data buffer
layout(std430, set = 1, binding = 0) readonly buffer ModelMatrixBuffer {
    mat4 modelMatrices[];
} modelMatrixBuffer;

layout(std430, set = 1, binding = 1) readonly buffer NormalMatrixBuffer {
    mat4 normalMatrices[];
} normalMatrixBuffer;

layout(push_constant) uniform PushConstants {
    uint instanceOffset;
} pushConstants;

void main() {

    uint instanceIndex = gl_InstanceIndex + pushConstants.instanceOffset;
    mat4 modelMatrix = modelMatrixBuffer.modelMatrices[instanceIndex];
    mat4 normalMatrix = normalMatrixBuffer.normalMatrices[instanceIndex];
    
    vec4 worldPosition = modelMatrix * vec4(inPosition, 1.0);    
    gl_Position = cameraUBO.viewProjection * worldPosition;
    fragPosition = worldPosition.xyz;

    fragUV = inUV;
    
    worldNormal   = mat3(normalMatrix) * inNormal;
    worldTangent  = mat3(modelMatrix) * inTangent.xyz;
    worldBitangent = cross(worldTangent,worldNormal) * inTangent.w;
  
    fragNormal = worldNormal;

}