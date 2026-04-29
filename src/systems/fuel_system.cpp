#include "fuel_system.h"
#include "ecs/components.h"
#include "game/game_events.h"
#include <algorithm>

void FuelSystem::update(entt::registry& reg, entt::dispatcher& disp, float dt) {
    auto view = reg.view<PlayerTag, Fuel, CarInput, DerivedCarStats>();
    view.each([&](entt::entity entity, Fuel& fuel, const CarInput& input,
                  const DerivedCarStats& stats) {
        if (fuel.depleted()) {
            if (!prevDepleted_) {
                disp.enqueue<FuelDepletedEvent>(entity);
                prevDepleted_ = true;
            }
            return;
        }
        prevDepleted_ = false;

        // Shield from a MagneticShield powerup blocks hazard drain (not base drain)
        float shieldFactor = 1.0f;
        if (auto* pu = reg.try_get<ActivePowerup>(entity))
            shieldFactor = std::max(0.0f, 1.0f - pu->shieldAmount);

        float drain = (fuel.idleDrainPerSec + input.throttle * fuel.accelDrainPerUnit)
                    * (1.0f / std::max(0.1f, stats.efficiency))
                    * shieldFactor
                    * dt;

        fuel.current = std::max(0.0f, fuel.current - drain);

        // Low fuel edge event
        bool isLow = fuel.ratio() < lowFuelThreshold;
        if (isLow && !prevLow_)
            disp.enqueue<LowFuelEvent>(entity, fuel.current);
        prevLow_ = isLow;
    });
}
