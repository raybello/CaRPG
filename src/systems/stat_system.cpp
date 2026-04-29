#include "stat_system.h"
#include "ecs/components.h"
#include <algorithm>

void StatSystem::update(entt::registry& reg) {
    reg_ = &reg;
    auto view = reg.view<BaseCarStats, DerivedCarStats, Inventory>();
    view.each([&](entt::entity entity,
                  const BaseCarStats& base,
                  DerivedCarStats& derived,
                  const Inventory& inv) {
        if (!derived.dirty) return;

        // Step 1: copy base
        derived.handling   = base.handling;
        derived.power      = base.power;
        derived.efficiency = base.efficiency;
        derived.topSpeed   = base.topSpeed;

        // Step 2: add gear deltas
        auto applyGear = [&](entt::entity gearEnt) {
            if (gearEnt == entt::null || !reg.valid(gearEnt)) return;
            if (auto* g = reg.try_get<GearComponent>(gearEnt)) {
                derived.handling   += g->dHandling;
                derived.power      += g->dPower;
                derived.efficiency += g->dEfficiency;
                derived.topSpeed   += g->dTopSpeed;
            }
        };
        applyGear(inv.equippedWheels);
        applyGear(inv.equippedSuspension);
        applyGear(inv.equippedEngine);

        // Step 3: clamp base range
        derived.handling   = std::clamp(derived.handling,   0.1f, 5.0f);
        derived.power      = std::clamp(derived.power,       1.0f, 500.0f);
        derived.efficiency = std::clamp(derived.efficiency,  0.1f, 5.0f);
        derived.topSpeed   = std::clamp(derived.topSpeed,    5.0f, 100.0f);

        // Step 4: active powerup bonuses
        if (auto* pu = reg.try_get<ActivePowerup>(entity)) {
            derived.power    += pu->dPower;
            derived.topSpeed += pu->dTopSpeed;
        }

        // Step 5: final clamp
        derived.topSpeed = std::clamp(derived.topSpeed, 5.0f, 150.0f);

        derived.dirty = false;
    });
}

void StatSystem::onGearEquipped(const GearEquippedEvent& e) {
    if (!reg_) return;
    if (auto* d = reg_->try_get<DerivedCarStats>(e.player))
        d->dirty = true;
}

void StatSystem::onGearUnequipped(const GearUnequippedEvent& e) {
    if (!reg_) return;
    if (auto* d = reg_->try_get<DerivedCarStats>(e.player))
        d->dirty = true;
}
