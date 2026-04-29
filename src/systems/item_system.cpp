#include "item_system.h"
#include "ecs/components.h"
#include "game/game_events.h"
#include "game/item_catalog.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// Inventory helpers
// ---------------------------------------------------------------------------
bool ItemSystem::addToInventory(entt::registry& reg, entt::entity player,
                                 ItemId id, uint32_t qty) {
    auto* inv = reg.try_get<Inventory>(player);
    if (!inv) return false;
    const ItemDef* def = getItemDef(id);
    uint32_t maxStack = def ? def->maxStack : 1;

    // Try to stack onto existing slot
    int slot = inv->findSlot(id);
    if (slot >= 0) {
        uint32_t space = maxStack - inv->slots[slot].qty;
        uint32_t add   = std::min(qty, space);
        inv->slots[slot].qty += add;
        qty -= add;
    }
    // Fill into empty slots for remainder
    while (qty > 0) {
        int empty = inv->firstEmpty();
        if (empty < 0) return false;  // inventory full
        uint32_t add = std::min(qty, maxStack);
        inv->slots[empty] = {id, add};
        qty -= add;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Proximity pickup
// ---------------------------------------------------------------------------
void ItemSystem::update(entt::registry& reg, entt::dispatcher& disp, float dt) {
    // Tick active powerups
    auto puView = reg.view<PlayerTag, ActivePowerup>();
    puView.each([&](entt::entity entity, ActivePowerup& pu) {
        pu.durationRemaining -= dt;
        if (pu.durationRemaining <= 0.0f) {
            disp.enqueue<PowerupExpiredEvent>(entity, pu.sourceItem);
            // Mark stats dirty after powerup expires
            if (auto* d = reg.try_get<DerivedCarStats>(entity))
                d->dirty = true;
            reg.remove<ActivePowerup>(entity);
        }
    });

    // Auto-pickup world items in range of the player
    auto playerView = reg.view<PlayerTag, Transform, Inventory>();
    playerView.each([&](entt::entity player, const Transform& playerTf, Inventory&) {
        std::vector<entt::entity> toPickup;
        auto worldView = reg.view<ItemWorldTag, Transform, ItemComponent>();
        worldView.each([&](entt::entity item, const Transform& itemTf, const ItemComponent& ic) {
            float dist = glm::length(playerTf.position - itemTf.position);
            if (dist <= pickupRadius)
                toPickup.push_back(item);
        });

        for (entt::entity item : toPickup) {
            if (!reg.valid(item)) continue;
            const auto& ic = reg.get<ItemComponent>(item);
            if (addToInventory(reg, player, ic.itemId, ic.stackSize)) {
                disp.enqueue<ItemPickedUpEvent>(player, item, ic.itemId, ic.stackSize);
                reg.destroy(item);
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Use item from inventory slot
// ---------------------------------------------------------------------------
void ItemSystem::useItem(entt::registry& reg, entt::dispatcher& disp,
                          entt::entity player, int slot) {
    auto* inv = reg.try_get<Inventory>(player);
    if (!inv || slot < 0 || slot >= Inventory::kSlots) return;
    if (inv->slots[slot].qty == 0 || inv->slots[slot].id == ItemId::Count) return;

    ItemId id = inv->slots[slot].id;
    const ItemDef* def = getItemDef(id);
    if (!def) return;

    if (def->type == "consumable") {
        if (def->fuelRestore > 0.0f) {
            if (auto* fuel = reg.try_get<Fuel>(player))
                fuel->current = std::min(fuel->max, fuel->current + def->fuelRestore);
        } else if (def->duration > 0.0f) {
            // Short-duration powerup (e.g. OverchargeCell)
            auto& pu = reg.emplace_or_replace<ActivePowerup>(player);
            pu.sourceItem        = id;
            pu.durationRemaining = def->duration;
            pu.dPower            = def->dPower;
            pu.dTopSpeed         = def->dTopSpeed;
            pu.shieldAmount      = 0.0f;
            if (auto* d = reg.try_get<DerivedCarStats>(player)) d->dirty = true;
            disp.enqueue<PowerupActivatedEvent>(player, id, def->duration);
        }
        disp.enqueue<ItemConsumedEvent>(player, id);
    } else if (def->type == "powerup") {
        auto& pu = reg.emplace_or_replace<ActivePowerup>(player);
        pu.sourceItem        = id;
        pu.durationRemaining = def->duration;
        pu.dPower            = def->dPower;
        pu.dTopSpeed         = def->dTopSpeed;
        pu.shieldAmount      = def->shieldAmount;
        if (auto* d = reg.try_get<DerivedCarStats>(player)) d->dirty = true;
        disp.enqueue<PowerupActivatedEvent>(player, id, def->duration);
        disp.enqueue<ItemConsumedEvent>(player, id);
    } else if (def->type == "gear") {
        // Create a gear entity and equip it
        entt::entity gearEnt = reg.create();
        reg.emplace<GearComponent>(gearEnt,
            def->dHandling, def->dPower, def->dEfficiency, def->dTopSpeed, def->tier);
        reg.emplace<ItemComponent>(gearEnt, id, 1u, 1u);
        equipGear(reg, disp, player, gearEnt);
        disp.enqueue<ItemConsumedEvent>(player, id);
    } else if (def->type == "crafting") {
        // Check if enough stacked
        if (inv->slots[slot].qty >= (uint32_t)def->quantityRequired) {
            // Apply upgrade: directly modify BaseCarStats
            if (auto* base = reg.try_get<BaseCarStats>(player)) {
                base->power      += def->dPower;
                base->efficiency += def->dEfficiency;
                base->handling   += def->dHandling;
                base->topSpeed   += def->dTopSpeed;
            }
            if (auto* d = reg.try_get<DerivedCarStats>(player)) d->dirty = true;
            inv->slots[slot].qty -= def->quantityRequired;
            if (inv->slots[slot].qty == 0)
                inv->slots[slot] = {ItemId::Count, 0};
            return;  // no ItemConsumedEvent for crafting
        }
        return;  // not enough
    } else if (def->type == "salvage") {
        for (const auto& y : def->yields)
            addToInventory(reg, player, y.id, y.qty);
        disp.enqueue<SalvageEvent>(player, player);  // wreck = player entity as placeholder
        disp.enqueue<ItemConsumedEvent>(player, id);
    }

    // Decrement stack
    if (--inv->slots[slot].qty == 0)
        inv->slots[slot] = {ItemId::Count, 0};
}

// ---------------------------------------------------------------------------
// Equip gear
// ---------------------------------------------------------------------------
void ItemSystem::equipGear(entt::registry& reg, entt::dispatcher& disp,
                            entt::entity player, entt::entity gearEnt) {
    auto* inv = reg.try_get<Inventory>(player);
    if (!inv) return;
    const auto* ic = reg.try_get<ItemComponent>(gearEnt);
    if (!ic) return;
    const ItemDef* def = getItemDef(ic->itemId);
    if (!def) return;

    entt::entity* slot = nullptr;
    if (def->gearSlot == "wheels")     slot = &inv->equippedWheels;
    else if (def->gearSlot == "suspension") slot = &inv->equippedSuspension;
    else if (def->gearSlot == "engine")    slot = &inv->equippedEngine;
    if (!slot) return;

    entt::entity prev = *slot;
    if (prev != entt::null && reg.valid(prev))
        disp.enqueue<GearUnequippedEvent>(player, prev, ic->itemId);

    *slot = gearEnt;
    disp.enqueue<GearEquippedEvent>(player, gearEnt, prev, ic->itemId);
}
