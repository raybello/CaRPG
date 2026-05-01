#pragma once
#include <entt/entt.hpp>

// Kinematic bicycle car model.
// All parameters are tunable at runtime; defaults give a forgiving arcade
// feel with gradual ramps.
class PhysicsSystem {
public:
    void update(entt::registry& reg, float dt,
                entt::entity skipYConstraintFor = entt::null);

    // ---- Throttle / power ------------------------------------------------
    float powerScale       = 0.35f;   // global throttle→force scale
    float throttleResponse = 1.6f;    // 1/sec — exp ramp of applied throttle
    float maxAccel         = 60.0f;   // hard cap on |longitudinal accel|

    // ---- Brake / reverse -------------------------------------------------
    float brakeSensitivity = 180.0f;
    float reversePowerMul  = 0.55f;
    float reverseSpeedMul  = 0.45f;
    float engineBrake      = 1.2f;

    // ---- Friction --------------------------------------------------------
    float drag             = 1.4f;    // forward rolling resistance / sec
    float lateralFriction  = 8.0f;    // tire grip / sec (high = no slide)
    float handbrakeGripMul = 0.15f;   // multiplies lateralFriction
    float handbrakeDrag    = 3.0f;    // extra forward drag when handbraking

    // ---- Steering (bicycle model) ---------------------------------------
    float steerResponse    = 6.0f;    // 1/sec — exp ramp of applied steer
    float maxSteerAngleRad = 0.55f;   // ~31° — wheel max
    float wheelbase        = 2.5f;    // m — distance between axles
    float pivotMinSpeed    = 1.5f;    // below this, low-speed pivot blends in
    float pivotYawRate     = 1.4f;    // rad/s — pivot speed at rest
};
