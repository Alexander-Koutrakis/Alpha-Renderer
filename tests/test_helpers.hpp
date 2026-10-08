#pragma once

#include "doctest.h"
#include <glm/glm.hpp>

// Compares two vectors component-wise with a tolerance, printing both on failure.
inline bool nearlyEqual(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f) {
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(epsilon)));
}

#define CHECK_VEC3(actual, expected)                                                                                   \
    do {                                                                                                               \
        const glm::vec3 a_ = (actual);                                                                                 \
        const glm::vec3 e_ = (expected);                                                                               \
        CHECK_MESSAGE(nearlyEqual(a_, e_), #actual " = (", a_.x, ", ", a_.y, ", ", a_.z, "), expected (", e_.x, ", ",  \
                      e_.y, ", ", e_.z, ")");                                                                          \
    } while (0)
