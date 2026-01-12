#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 tangent;


layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;


layout(set = 0, binding = 0) uniform CameraUbo {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
} cameraUBO;

// Instance data buffer
layout(std430, set = 3, binding = 0) readonly buffer ModelMatrixBuffer {
    mat4 modelMatrices[];
} modelMatrixBuffer;

layout(std430, set = 3, binding = 1) readonly buffer NormalMatrixBuffer {
    mat4 normalMatrices[];
} normalMatrixBuffer;

layout(push_constant) uniform PushConstants {
    uint instanceOffset;
} pushConstants;

void main() {
    
    uint instanceIndex = gl_InstanceIndex + pushConstants.instanceOffset;
    mat4 modelMatrix = modelMatrixBuffer.modelMatrices[instanceIndex];
    mat4 normalMatrix = normalMatrixBuffer.normalMatrices[instanceIndex];

   vec4 worldPosition = modelMatrix * vec4(position, 1.0);    
    gl_Position = cameraUBO.viewProjection * worldPosition;
    fragPosition = worldPosition.xyz;
    fragUV = uv;

    // Transform TBN vectors to world space
    fragNormal = normalize(mat3(normalMatrix) * normal);
    fragTangent = normalize(mat3(normalMatrix) * tangent.xyz);
    fragBitangent = cross(fragNormal, fragTangent) * tangent.w;
} 