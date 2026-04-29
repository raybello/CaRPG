#pragma once
#include <entt/entt.hpp>

class PhysicsSystem {
public:
    void update(entt::registry& reg, float dt);

    float steerSensitivity = 160.0f;  // deg/s at full steer
    float brakeSensitivity = 200.0f;
    float drag             = 1.5f;    // rolling resistance per second
};
