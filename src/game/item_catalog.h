#pragma once
#include <string>
#include <vector>
#include "ecs/components.h"

struct ItemDef {
    ItemId      id          = ItemId::Count;
    std::string name;
    std::string description;
    std::string type;       // "powerup","gear","crafting","consumable","lore","salvage"
    uint32_t    maxStack    = 1;
    MeshId      meshId      = MeshId::Cube;
    glm::vec3   color       {1.0f, 1.0f, 1.0f};
    bool        loaded      = false;

    // Effect fields — zero/empty when not applicable
    float       duration        = 0.0f;
    float       dPower          = 0.0f;
    float       dTopSpeed       = 0.0f;
    float       dHandling       = 0.0f;
    float       dEfficiency     = 0.0f;
    float       shieldAmount    = 0.0f;
    float       fuelRestore     = 0.0f;
    std::string upgradeTarget;           // "engine", "suspension", "gear"
    int         quantityRequired= 0;
    std::string gearSlot;               // "wheels", "suspension", "engine"
    uint8_t     tier            = 1;
    int         collectTarget   = 0;
    std::string reward;
    struct YieldEntry { ItemId id; uint32_t qty; };
    std::vector<YieldEntry> yields;
};

// Returns the global item def for a given id (nullptr if not loaded yet).
const ItemDef* getItemDef(ItemId id);

class ItemCatalog {
public:
    // Parse LOOT.json and populate the global table. Call once at startup.
    static bool loadFromFile(const char* path);
};
