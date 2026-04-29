#pragma once
#include <entt/entt.hpp>
#include "game/game_events.h"

class HUD {
public:
    void draw(entt::registry& reg, entt::entity player, int vpW, int vpH);

    void onLowFuel(const LowFuelEvent& e);
    void onFuelDepleted(const FuelDepletedEvent& e);

private:
    bool  fuelDepleted_     = false;
    bool  lowFuel_          = false;
    float warningFlash_     = 0.0f;  // countdown timer for flashing
};
