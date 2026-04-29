#pragma once
#include <entt/entt.hpp>

class CameraSystem {
public:
    void update(entt::registry& reg, float dt, int vpW, int vpH);
};
