#pragma once
#include <entt/entt.hpp>
#include "game/game_events.h"

class ItemSystem;

class InventoryPanel {
public:
    void draw(entt::registry& reg, entt::entity player, ItemSystem& itemSys,
              entt::dispatcher& disp);

    void onItemPickedUp(const ItemPickedUpEvent& e);
    void onGearEquipped(const GearEquippedEvent& e);

    bool visible = false;  // toggled with 'i' key

private:
    bool dirty_ = true;
};
