// Shared math constants and helpers. Include with GL_GOOGLE_include_directive.
#ifndef ALPHA_COMMON_MATH_GLSL
#define ALPHA_COMMON_MATH_GLSL

const float PI = 3.14159265359;
const float EPSILON = 0.0000001;

float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

#endif
