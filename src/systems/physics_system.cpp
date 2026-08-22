#include "physics_system.h"
#include "ecs/components.h"
#include "physics/box3d_interop.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

// =============================================================================
// Per-wheel raycast suspension — replaces the bicycle model.
//
// For each of the 4 wheels, once per fixed physics tick (see
// App::tickSystems):
//   a) Suspension spring:  springDir * (offset*K - vel*D)
//   b) Lateral grip:       cancel sideways velocity at the tire contact point
//   c) Drive / brake:      forward force through a power curve, or brake force
//
// All forces go through b3Body_ApplyForce so off-centre forces generate
// torques automatically (box3d computes them from the force's application
// point), producing natural weight transfer / roll.
// =============================================================================

namespace {

inline float safeF(float x, float fallback = 0.0f) {
    return std::isfinite(x) ? x : fallback;
}

}  // namespace

void PhysicsSystem::update(entt::registry& reg, float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);

    // Wheel raycasts must not hit the car's own chassis hull (see
    // kCategoryChassis) — everything else (ground, obstacles, pushable
    // crates) keeps box3d's all-bits default filter and stays hittable.
    b3QueryFilter wheelFilter = b3DefaultQueryFilter();
    wheelFilter.maskBits      = ~kCategoryChassis;

    auto view = reg.view<PlayerTag, Transform, PhysicsBody, CarInput, CarVehicle>();
    view.each([&](entt::entity e,
                  Transform& tf, const PhysicsBody& pb,
                  const CarInput& rawInput, CarVehicle& cv) {

        const b3WorldId world = b3Body_GetWorld(pb.id);

        // 1. Smooth inputs (exponential ramp toward raw target).
        const float kT = std::clamp(throttleResponse * dt, 0.0f, 1.0f);
        const float kS = std::clamp(steerResponse    * dt, 0.0f, 1.0f);

        float tgtThrottle = std::clamp(safeF(rawInput.throttle), 0.0f, 1.0f);
        float tgtBrake    = std::clamp(safeF(rawInput.brake),    0.0f, 1.0f);
        float tgtSteer    = std::clamp(safeF(rawInput.steer),   -1.0f, 1.0f);

        // Kill throttle if fuel is empty (component is optional).
        if (const auto* fuel = reg.try_get<Fuel>(e))
            if (fuel->depleted()) tgtThrottle = 0.0f;

        cv.appliedThrottle += (tgtThrottle - cv.appliedThrottle) * kT;
        cv.appliedSteer    += (tgtSteer    - cv.appliedSteer)    * kS;
        cv.appliedThrottle  = std::clamp(safeF(cv.appliedThrottle), 0.0f, 1.0f);
        cv.appliedSteer     = std::clamp(safeF(cv.appliedSteer),   -1.0f, 1.0f);

        const float topSpeed     = std::max(safeF(cv.topSpeed), 1.0f);
        const glm::vec3 carFwd   = tf.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 linVel   = fromB3(b3Body_GetLinearVelocity(pb.id));
        const float carSpeedFwd  = safeF(glm::dot(carFwd, linVel));

        // 2. Per-wheel suspension + grip + drive.
        for (int i = 0; i < CarVehicle::kWheelCount; ++i) {
            WheelState& wheel = cv.wheels[i];

            // World-space spring direction and wheel attachment position.
            const glm::vec3 worldUp       = tf.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 worldWheelPos = tf.position + tf.rotation * wheel.localOffset;

            // Ray from the top of suspension travel, firing downward.
            const float maxRayDist = wheel.suspensionRestDist + wheel.suspensionTravel;
            const glm::vec3 rayOrigin      = worldWheelPos + worldUp * wheel.suspensionTravel;
            const glm::vec3 rayTranslation = -worldUp * maxRayDist;

            b3RayResult hit = b3World_CastRayClosest(
                world, toB3(rayOrigin), toB3(rayTranslation), wheelFilter);

            wheel.grounded = hit.hit;
            if (!hit.hit) {
                wheel.compressionRatio = 0.0f;
                wheel.contactDist      = 0.0f;
                continue;
            }

            // fraction is 0..1 of the translation vector's length.
            const float distance = hit.fraction * maxRayDist;
            wheel.contactDist      = distance;
            wheel.compressionRatio = std::clamp(
                1.0f - (distance - wheel.suspensionRestDist) / wheel.suspensionTravel,
                0.0f, 1.0f);

            const glm::vec3 tireWorldVel =
                fromB3(b3Body_GetWorldPointVelocity(pb.id, toB3(worldWheelPos)));

            // ---- a) Suspension spring-damper force ----------------------
            const float offset     = wheel.suspensionRestDist - distance;
            const float velSpring  = glm::dot(worldUp, tireWorldVel);
            const float maxSpringF = wheel.springStrength * wheel.suspensionTravel * 2.0f;
            const float springForce = std::clamp(
                offset * wheel.springStrength - velSpring * wheel.springDamper,
                -maxSpringF, maxSpringF);
            b3Body_ApplyForce(pb.id, toB3(worldUp * springForce), toB3(worldWheelPos), true);

            // ---- b) Lateral grip force ----------------------------------
            // Steer the front wheels; rear wheels point straight.
            // Negate: positive appliedSteer = right, but angleAxis(+θ, Y) rotates -Z toward -X (left).
            const float steerAngle = wheel.isSteered ? -cv.appliedSteer * maxSteerAngleRad : 0.0f;
            const glm::quat steerRot   = glm::angleAxis(steerAngle, worldUp);
            const glm::vec3 wheelRight = steerRot * (tf.rotation * glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 wheelFwd   = steerRot * (tf.rotation * glm::vec3(0.0f, 0.0f, -1.0f));

            float gripMul = wheel.gripFactor;
            // Handbrake removes rear grip → oversteer / drift.
            if (rawInput.handbrake && !wheel.isSteered) gripMul = 0.0f;

            const float steeringVel  = glm::dot(wheelRight, tireWorldVel);
            const float desiredAccel = (-steeringVel * gripMul) / dt;
            b3Body_ApplyForce(pb.id,
                toB3(wheelRight * wheel.wheelMass * desiredAccel), toB3(worldWheelPos), true);

            // ---- c) Drive / brake force ---------------------------------
            if (!wheel.isDriven) continue;

            // Forward drive — power curve scales torque down as forward speed rises.
            // Only clamp on the positive (forward) side: going backward should
            // always allow full forward thrust so the car can recover from reverse.
            if (cv.appliedThrottle > 0.001f) {
                float normSpeed = std::clamp(carSpeedFwd / topSpeed, 0.0f, 1.0f);
                float torqueMul = std::max(0.0f, 1.0f - normSpeed);
                b3Body_ApplyForce(pb.id,
                    toB3(wheelFwd * cv.maxTorque * torqueMul * cv.appliedThrottle),
                    toB3(worldWheelPos), true);
            }

            // Service brake / reverse — apply backward force until max reverse speed.
            // Braking while forward, then seamlessly transitions to reverse once stopped.
            if (tgtBrake > 0.01f) {
                const float maxReverseSpeed = topSpeed * 0.4f;
                if (carSpeedFwd > -maxReverseSpeed) {
                    const float brakeTorque = cv.maxTorque * 0.8f * tgtBrake;
                    b3Body_ApplyForce(pb.id, toB3(-wheelFwd * brakeTorque), toB3(worldWheelPos), true);
                }
            }

            // Rolling resistance — always opposes forward motion (tire deformation drag).
            // ~400 N total across 4 wheels gives 0.27 m/s² deceleration at any speed.
            constexpr float kRolling = 100.0f;  // N per wheel
            if (std::fabs(carSpeedFwd) > 0.05f) {
                float dir = (carSpeedFwd > 0.0f) ? -1.0f : 1.0f;
                b3Body_ApplyForce(pb.id, toB3(wheelFwd * kRolling * dir), toB3(worldWheelPos), true);
            }

            // Engine braking when fully coasting — speed-proportional drag.
            // Uses -carSpeedFwd (not fabs) so force always opposes actual travel
            // direction: forward → backward force, backward → forward force.
            if (cv.appliedThrottle < 0.001f && tgtBrake < 0.01f) {
                constexpr float kEngineBrake = 2.5f;
                b3Body_ApplyForce(pb.id,
                    toB3(wheelFwd * (-carSpeedFwd) * kEngineBrake * wheel.wheelMass),
                    toB3(worldWheelPos), true);
            }
        }

        // 3. Sync Velocity component for HUD / camera.
        if (auto* vel = reg.try_get<Velocity>(e)) {
            vel->linear          = linVel;
            vel->appliedThrottle = cv.appliedThrottle;
            vel->appliedSteer    = cv.appliedSteer;
        }
    });
}
