#pragma once
// Collision component: AABB bounds for collision detection.
#include <glm/glm.hpp>

struct Collision {
    glm::vec3 min; // Local min corner
    glm::vec3 max; // Local max corner
    bool isStatic = false; // Static or dynamic object

    Collision(glm::vec3 min_ = {-0.5f, -0.5f, -0.5f}, glm::vec3 max_ = {0.5f, 0.5f, 0.5f}, bool static_ = false)
        : min(min_), max(max_), isStatic(static_) {}
};
