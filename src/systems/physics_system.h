#pragma once
#include <entt/entt.hpp>

// Per-wheel raycast suspension system.
//
// Replaces the bicycle model. Once per fixed physics tick (see App::tickSystems):
//   1. Smooth CarInput → CarVehicle.appliedThrottle / appliedSteer
//   2. For each wheel: raycast downward (b3World_CastRayClosest) → suspension
//      spring, lateral grip, drive force
//   3. All forces accumulate via b3Body_ApplyForce → torques are computed by
//      box3d from the force's application point
//
// The car body is fully dynamic (gravity + suspension) — no Y-constraint needed.
class PhysicsSystem {
public:
    void update(entt::registry& reg, float dt);

    // ---- Input smoothing ------------------------------------------------
    float throttleResponse = 3.0f;   // 1/sec exponential ramp toward target
    float steerResponse    = 5.0f;

    // ---- Steering angle -------------------------------------------------
    float maxSteerAngleRad = 0.52f;  // ~30 degrees
};
