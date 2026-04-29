#include "item_catalog.h"
#include <fstream>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <SDL.h>

using json = nlohmann::json;

static ItemDef kItemDefs[static_cast<uint32_t>(ItemId::Count)];

const ItemDef* getItemDef(ItemId id) {
    uint32_t idx = static_cast<uint32_t>(id);
    if (idx >= static_cast<uint32_t>(ItemId::Count)) return nullptr;
    return kItemDefs[idx].loaded ? &kItemDefs[idx] : nullptr;
}

static const std::unordered_map<std::string, ItemId> kIdMap = {
    {"NitroBoost",        ItemId::NitroBoost},
    {"MagneticShield",    ItemId::MagneticShield},
    {"EnginePart",        ItemId::EnginePart},
    {"SteelCoil",         ItemId::SteelCoil},
    {"TurboWheels",       ItemId::TurboWheels},
    {"OffRoadSuspension", ItemId::OffRoadSuspension},
    {"FuelCanister",      ItemId::FuelCanister},
    {"OverchargeCell",    ItemId::OverchargeCell},
    {"RoadSignFragment",  ItemId::RoadSignFragment},
    {"AbandonedWreck",    ItemId::AbandonedWreck},
};

static MeshId meshIdFromString(const std::string& s) {
    if (s == "Sphere") return MeshId::Sphere;
    if (s == "Ground") return MeshId::Ground;
    return MeshId::Cube;
}

bool ItemCatalog::loadFromFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        SDL_Log("ItemCatalog: cannot open '%s'", path);
        return false;
    }

    json data = json::parse(f, nullptr, /*exceptions=*/false);
    if (data.is_discarded()) {
        SDL_Log("ItemCatalog: JSON parse error in '%s'", path);
        return false;
    }

    if (!data.contains("items") || !data["items"].is_array()) {
        SDL_Log("ItemCatalog: missing 'items' array in '%s'", path);
        return false;
    }

    int loaded = 0;
    for (const auto& entry : data["items"]) {
        std::string idStr = entry.value("id", std::string{});
        auto it = kIdMap.find(idStr);
        if (it == kIdMap.end()) {
            SDL_Log("ItemCatalog: unknown item id '%s', skipping", idStr.c_str());
            continue;
        }

        ItemDef& def = kItemDefs[static_cast<uint32_t>(it->second)];
        def.id          = it->second;
        def.name        = entry.value("name", idStr);
        def.description = entry.value("description", std::string{});
        def.type        = entry.value("type", std::string{});
        def.maxStack    = entry.value("maxStack", 1u);
        def.meshId      = meshIdFromString(entry.value("meshId", std::string{"Cube"}));

        if (entry.contains("color") && entry["color"].is_array() && entry["color"].size() >= 3) {
            const auto& c = entry["color"];
            def.color = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
        }

        if (entry.contains("effects") && entry["effects"].is_object()) {
            const auto& fx = entry["effects"];
            def.duration         = fx.value("duration",         0.0f);
            def.dPower           = fx.value("dPower",           0.0f);
            def.dTopSpeed        = fx.value("dTopSpeed",        0.0f);
            def.dHandling        = fx.value("dHandling",        0.0f);
            def.dEfficiency      = fx.value("dEfficiency",      0.0f);
            def.shieldAmount     = fx.value("shieldAmount",     0.0f);
            def.fuelRestore      = fx.value("fuelRestore",      0.0f);
            def.upgradeTarget    = fx.value("upgradeTarget",    std::string{});
            def.quantityRequired = fx.value("quantityRequired", 0);
            def.gearSlot         = fx.value("slot",             std::string{});
            def.tier             = static_cast<uint8_t>(fx.value("tier", 1));
            def.collectTarget    = fx.value("collectTarget",    0);
            def.reward           = fx.value("reward",           std::string{});

            if (fx.contains("yields") && fx["yields"].is_array()) {
                for (const auto& y : fx["yields"]) {
                    std::string yIdStr = y.value("id", std::string{});
                    auto yIt = kIdMap.find(yIdStr);
                    if (yIt != kIdMap.end())
                        def.yields.push_back({yIt->second, y.value("qty", 1u)});
                }
            }
        }

        def.loaded = true;
        ++loaded;
    }

    SDL_Log("ItemCatalog: loaded %d/%d items from '%s'",
            loaded, static_cast<int>(ItemId::Count), path);
    return loaded > 0;
}
