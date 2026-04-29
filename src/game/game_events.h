#pragma once
#include <entt/entt.hpp>
#include "ecs/components.h"

struct ItemPickedUpEvent {
    entt::entity player;
    entt::entity worldItem;
    ItemId       itemId;
    uint32_t     quantity;
};

struct GearEquippedEvent {
    entt::entity player;
    entt::entity gear;
    entt::entity previousGear;
    ItemId       itemId;
};

struct GearUnequippedEvent {
    entt::entity player;
    entt::entity gear;
    ItemId       itemId;
};

struct PowerupActivatedEvent {
    entt::entity player;
    ItemId       itemId;
    float        duration;
};

struct PowerupExpiredEvent {
    entt::entity player;
    ItemId       itemId;
};

struct LowFuelEvent {
    entt::entity player;
    float        currentFuel;
};

struct FuelDepletedEvent {
    entt::entity player;
};

struct ItemConsumedEvent {
    entt::entity player;
    ItemId       itemId;
};

struct SalvageEvent {
    entt::entity player;
    entt::entity wreck;
};

struct ZoneUnlockedEvent {
    entt::entity player;
    std::string  reward;
};
