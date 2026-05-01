#include "physics_system.h"
#include "ecs/components.h"
#include "raycast.h"
#include "rigid_body_system.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

// =============================================================================
// Per-wheel raycast suspension — replaces the bicycle model.
//
// For each of the 4 wheels each substep:
//   a) Suspension spring:  springDir * (offset*K - vel*D)
//   b) Lateral grip:       cancel sideways velocity at the tire contact point
//   c) Drive / brake:      forward force through a power curve, or brake force
//
// All forces go through rbAddForceAtPosition so off-centre forces generate
// torques automatically, producing natural weight transfer / roll.
// =============================================================================

namespace {

inline float safeF(float x, float fallback = 0.0f) {
    return std::isfinite(x) ? x : fallback;
}

// Collidable geometry description for per-wheel raycasting.
// One entry per fixed entity that has a plane or box collider.
struct CollidableRef {
    const glm::vec3*  planeNormal = nullptr;   // non-null → plane collider
    float             planeD      = 0.0f;
    const glm::vec3*  boxCenter   = nullptr;   // non-null → box collider
    const glm::vec3*  boxHalfExt  = nullptr;
    const glm::quat*  boxRot      = nullptr;
};

}  // namespace

void PhysicsSystem::update(entt::registry& reg, float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);

    // --- Collect fixed collidable geometry (ground plane, fixed boxes) ---
    std::vector<CollidableRef> collidables;
    collidables.reserve(16);

    reg.view<Transform, RigidBody>().each(
        [&](entt::entity e, const Transform& tf, const RigidBody& rb) {
            if (!rb.fixed) return;
            CollidableRef cr;
            bool useful = false;
            if (const auto* pc = reg.try_get<PlaneCollider>(e)) {
                cr.planeNormal = &pc->normal;
                cr.planeD      = pc->d;
                useful = true;
            }
            if (const auto* bc = reg.try_get<BoxCollider>(e)) {
                cr.boxCenter   = &tf.position;
                cr.boxHalfExt  = &bc->halfExtents;
                cr.boxRot      = &tf.rotation;
                useful = true;
            }
            if (useful) collidables.push_back(cr);
        });

    // --- Per-vehicle update ----------------------------------------------
    auto view = reg.view<PlayerTag, Transform, RigidBody, CarInput, CarVehicle>();
    view.each([&](entt::entity e,
                  Transform& tf, RigidBody& rb,
                  const CarInput& rawInput, CarVehicle& cv) {

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

        const float topSpeed    = std::max(safeF(cv.topSpeed), 1.0f);
        const glm::vec3 carFwd  = tf.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        const float carSpeedFwd = safeF(glm::dot(carFwd, rb.linearVel));

        // 2. Per-wheel suspension + grip + drive.
        for (int i = 0; i < CarVehicle::kWheelCount; ++i) {
            WheelState& wheel = cv.wheels[i];

            // World-space spring direction and wheel attachment position.
            const glm::vec3 worldUp       = tf.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 worldWheelPos = tf.position + tf.rotation * wheel.localOffset;

            // Ray from the top of suspension travel, firing downward.
            const Ray   ray        { worldWheelPos + worldUp * wheel.suspensionTravel, -worldUp };
            const float maxRayDist = wheel.suspensionRestDist + wheel.suspensionTravel;

            // Find the closest hit among all fixed collidables.
            RaycastHit bestHit;
            for (const auto& cr : collidables) {
                RaycastHit h;
                if (cr.planeNormal)
                    h = raycastPlane(ray, *cr.planeNormal, cr.planeD);
                else if (cr.boxCenter)
                    h = raycastBox(ray, *cr.boxCenter, *cr.boxHalfExt, *cr.boxRot);
                if (h.hit && h.distance <= maxRayDist) {
                    if (!bestHit.hit || h.distance < bestHit.distance)
                        bestHit = h;
                }
            }

            wheel.grounded = bestHit.hit;
            if (!bestHit.hit) {
                wheel.compressionRatio = 0.0f;
                wheel.contactDist      = 0.0f;
                continue;
            }

            wheel.contactDist      = bestHit.distance;
            wheel.compressionRatio = std::clamp(
                1.0f - (bestHit.distance - wheel.suspensionRestDist) / wheel.suspensionTravel,
                0.0f, 1.0f);

            const glm::vec3 tireWorldVel = rbGetPointVelocity(rb, tf, worldWheelPos);

            // ---- a) Suspension spring-damper force ----------------------
            const float offset     = wheel.suspensionRestDist - bestHit.distance;
            const float velSpring  = glm::dot(worldUp, tireWorldVel);
            const float maxSpringF = wheel.springStrength * wheel.suspensionTravel * 2.0f;
            const float springForce = std::clamp(
                offset * wheel.springStrength - velSpring * wheel.springDamper,
                -maxSpringF, maxSpringF);
            rbAddForceAtPosition(rb, tf, worldUp * springForce, worldWheelPos);

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
            rbAddForceAtPosition(rb, tf,
                wheelRight * wheel.wheelMass * desiredAccel, worldWheelPos);

            // ---- c) Drive / brake force ---------------------------------
            if (!wheel.isDriven) continue;

            // Forward drive — power curve scales torque down as forward speed rises.
            // Only clamp on the positive (forward) side: going backward should
            // always allow full forward thrust so the car can recover from reverse.
            if (cv.appliedThrottle > 0.001f) {
                float normSpeed = std::clamp(carSpeedFwd / topSpeed, 0.0f, 1.0f);
                float torqueMul = std::max(0.0f, 1.0f - normSpeed);
                rbAddForceAtPosition(rb, tf,
                    wheelFwd * cv.maxTorque * torqueMul * cv.appliedThrottle,
                    worldWheelPos);
            }

            // Service brake / reverse — apply backward force until max reverse speed.
            // Braking while forward, then seamlessly transitions to reverse once stopped.
            if (tgtBrake > 0.01f) {
                const float maxReverseSpeed = topSpeed * 0.4f;
                if (carSpeedFwd > -maxReverseSpeed) {
                    const float brakeTorque = cv.maxTorque * 0.8f * tgtBrake;
                    rbAddForceAtPosition(rb, tf, -wheelFwd * brakeTorque, worldWheelPos);
                }
            }

            // Rolling resistance — always opposes forward motion (tire deformation drag).
            // ~400 N total across 4 wheels gives 0.27 m/s² deceleration at any speed.
            constexpr float kRolling = 100.0f;  // N per wheel
            if (std::fabs(carSpeedFwd) > 0.05f) {
                float dir = (carSpeedFwd > 0.0f) ? -1.0f : 1.0f;
                rbAddForceAtPosition(rb, tf, wheelFwd * kRolling * dir, worldWheelPos);
            }

            // Engine braking when fully coasting — speed-proportional drag.
            // Uses -carSpeedFwd (not fabs) so force always opposes actual travel
            // direction: forward → backward force, backward → forward force.
            if (cv.appliedThrottle < 0.001f && tgtBrake < 0.01f) {
                constexpr float kEngineBrake = 2.5f;
                rbAddForceAtPosition(rb, tf,
                    wheelFwd * (-carSpeedFwd) * kEngineBrake * wheel.wheelMass,
                    worldWheelPos);
            }
        }

        // 3. Sync Velocity component for HUD / camera.
        if (auto* vel = reg.try_get<Velocity>(e)) {
            vel->linear          = rb.linearVel;
            vel->appliedThrottle = cv.appliedThrottle;
            vel->appliedSteer    = cv.appliedSteer;
        }
    });
}
