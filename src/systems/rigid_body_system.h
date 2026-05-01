#pragma once
#include <entt/entt.hpp>
#include "glm_math.h"

// Rigid body system — gravity, integration, OBB/Plane collision detection,
// impulse-based response. Concepts adapted from research/physics3D.
class RigidBodySystem {
public:
    void update(entt::registry& reg, float dt);

    // ---- Tunables (exposed in inspector) --------------------------------
    glm::vec3 gravity = {0.0f, -9.81f, 0.0f};

    // Sub-stepping for stability — splits a frame's dt into N fixed steps.
    int   maxSubsteps   = 4;
    float fixedStep     = 1.0f / 120.0f;

    // Baumgarte-style positional correction parameters for contacts.
    float penetrationSlop  = 0.005f;   // tolerated overlap before correction
    float positionalBias   = 0.4f;     // 0 = no correction, 1 = full snap

    // Helper used by app.cpp when constructing rigid bodies — compute the
    // body-frame inverse inertia tensor of a uniform-density box.
    static glm::mat3 boxInvInertia(float mass, const glm::vec3& halfExtents);
};
