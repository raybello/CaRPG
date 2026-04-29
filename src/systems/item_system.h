#pragma once
#include <entt/entt.hpp>
#include "ecs/components.h"

class ItemSystem {
public:
    void update(entt::registry& reg, entt::dispatcher& disp, float dt);

    // Use item from inventory slot (called from UI)
    void useItem(entt::registry& reg, entt::dispatcher& disp,
                 entt::entity player, int inventorySlot);

    // Equip a gear entity into the correct slot
    void equipGear(entt::registry& reg, entt::dispatcher& disp,
                   entt::entity player, entt::entity gearEntity);

    // Add item to player inventory (returns false if full)
    static bool addToInventory(entt::registry& reg, entt::entity player,
                                ItemId id, uint32_t qty = 1);

    float pickupRadius = 2.5f;  // world-space proximity for auto-pickup
};
