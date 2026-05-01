#pragma once
#include <entt/entt.hpp>
#include <functional>
#include "glm_math.h"
#include "ecs/components.h"

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

    // Called at the start of each substep before integration and collision.
    // Used by PhysicsSystem to re-apply suspension/drive forces each substep
    // so spring forces aren't zeroed out between substep integrations.
    using PreStepCallback = std::function<void(entt::registry&, float h)>;
    PreStepCallback preStepCb;

    // Helper used by app.cpp when constructing rigid bodies — compute the
    // body-frame inverse inertia tensor of a uniform-density box.
    static glm::mat3 boxInvInertia(float mass, const glm::vec3& halfExtents);
};

// ---------------------------------------------------------------------------
// RigidBody helper free functions
//
// These operate on a (RigidBody, Transform) pair and must be called before
// the substep integrator clears forceAccum/torqueAccum.
// ---------------------------------------------------------------------------

// World-space velocity of a point rigidly attached to the body.
//   v_p = v_cm + ω × (p - x_cm)
inline glm::vec3 rbGetPointVelocity(const RigidBody& rb, const Transform& tf,
                                    const glm::vec3& pointWorld) {
    return rb.linearVel + glm::cross(rb.angularVel, pointWorld - tf.position);
}

// Accumulate a force applied at a world-space point.
// Adds to forceAccum (linear) and torqueAccum (angular).
//   τ = (p - x_cm) × F
inline void rbAddForceAtPosition(RigidBody& rb, const Transform& tf,
                                 const glm::vec3& force, const glm::vec3& pointWorld) {
    rb.forceAccum  += force;
    rb.torqueAccum += glm::cross(pointWorld - tf.position, force);
}

// Accumulate a pure central force (no torque).
inline void rbAddForce(RigidBody& rb, const glm::vec3& force) {
    rb.forceAccum += force;
}

// Accumulate a pure torque.
inline void rbAddTorque(RigidBody& rb, const glm::vec3& torque) {
    rb.torqueAccum += torque;
}
