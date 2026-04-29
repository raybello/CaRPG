#pragma once
#include <entt/entt.hpp>
#include "game/game_events.h"

class StatSystem {
public:
    // Rebuilds DerivedCarStats for any entity where dirty == true.
    void update(entt::registry& reg);

    // Dispatcher listener — marks player stats dirty on gear changes.
    void onGearEquipped(const GearEquippedEvent& e);
    void onGearUnequipped(const GearUnequippedEvent& e);

private:
    entt::registry* reg_ = nullptr;
};
