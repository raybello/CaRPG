#pragma once
#include <entt/entt.hpp>

class FuelSystem {
public:
    void update(entt::registry& reg, entt::dispatcher& disp, float dt);

    float lowFuelThreshold = 0.25f;  // fire LowFuelEvent below this fuel ratio

private:
    bool prevDepleted_ = false;
    bool prevLow_      = false;
};
