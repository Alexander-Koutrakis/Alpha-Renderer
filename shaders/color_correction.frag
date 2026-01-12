#version 450

layout(binding = 0) uniform sampler2D uInput;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 color = texture(uInput, inUV).rgb;
    vec3 mapped = ACESFilm(color);
    outColor = vec4(mapped, 1.0);
}

