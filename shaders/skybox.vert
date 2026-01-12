
#version 450

layout(set = 0, binding = 0) uniform CameraUbo {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPosition;
} cameraUbo;

layout(location = 0) out vec3 outTexCoord;

vec2 positions[6] = vec2[](
        vec2(-1.0, -1.0),  // bottom left
        vec2(1.0, -1.0),   // bottom right
        vec2(-1.0, 1.0),   // top left
        vec2(-1.0, 1.0),   // top left
        vec2(1.0, -1.0),   // bottom right
        vec2(1.0, 1.0)     // top right
    );
    
// We'll generate a fullscreen quad's vertices directly in the shader
// No need for vertex input attributes
void main() {
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 1.0, 1.0);
    vec4 clipPos = vec4(pos, 1.0, 1.0);
    vec4 viewPos = inverse(cameraUbo.proj) * clipPos;
    viewPos /= viewPos.w;

    mat3 rotView = mat3(
        cameraUbo.view[0].xyz,
        cameraUbo.view[1].xyz, 
        cameraUbo.view[2].xyz
    );
    
    mat3 invRotView = inverse(rotView);
    outTexCoord = invRotView * viewPos.xyz;
}

