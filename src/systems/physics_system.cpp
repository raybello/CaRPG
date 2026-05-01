#include "physics_system.h"
#include "ecs/components.h"
#include <algorithm>
#include <cmath>

// =============================================================================
// Force-based car model
//
// PhysicsSystem is now purely a "force applier":
//   • Reads CarInput and smooth-ramps applied throttle/steer
//   • Applies longitudinal drive force, drag, and lateral-grip force to
//     RigidBody.forceAccum — RigidBodySystem integrates these each frame
//   • Sets RigidBody.angularVel.y directly to the bicycle-model yaw rate
//     so RigidBodySystem's quaternion integrator handles steering rotation
//   • Enforces the Y = 0 ground plane constraint on the dynamic car body
//     (cleared when the gizmo is repositioning the player)
//   • Syncs the Velocity component from RigidBody for HUD / camera use
// =============================================================================

namespace {

inline bool finiteV(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline bool finiteQ(const glm::quat& q) {
    return std::isfinite(q.w) && std::isfinite(q.x) &&
           std::isfinite(q.y) && std::isfinite(q.z);
}
inline float safeF(float x, float fallback = 0.0f) {
    return std::isfinite(x) ? x : fallback;
}
inline glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    if (!finiteV(v)) return fallback;
    float len = glm::length(v);
    return (len > 1e-4f) ? v / len : fallback;
}

// Extract yaw from any quaternion and return a clean pure-Y rotation.
inline glm::quat extractYaw(const glm::quat& q) {
    if (!finiteQ(q)) return glm::quat(1, 0, 0, 0);
    float yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z),
                           1.0f - 2.0f * (q.y * q.y + q.x * q.x));
    if (!std::isfinite(yaw)) yaw = 0.0f;
    return glm::angleAxis(yaw, glm::vec3(0, 1, 0));
}

}  // namespace

void PhysicsSystem::update(entt::registry& reg, float dt,
                           entt::entity skipYConstraintFor) {
    // ---- 1) Validate dt -------------------------------------------------
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;

    auto view = reg.view<PlayerTag, Transform, Velocity, RigidBody,
                         CarInput, DerivedCarStats, Fuel>();
    view.each([&](entt::entity e, Transform& tf, Velocity& vel, RigidBody& rb,
                  const CarInput& rawInput,
                  const DerivedCarStats& stats, const Fuel& fuel) {

        // ---- 2) Sanitize state -----------------------------------------
        if (!finiteV(tf.position)) tf.position = glm::vec3(0.0f);
        if (!finiteV(rb.linearVel)) rb.linearVel = glm::vec3(0.0f);
        tf.rotation = extractYaw(tf.rotation);   // enforce pure-Y rotation

        const bool constrained = (e != skipYConstraintFor);
        if (constrained) {
            tf.position.y   = 0.0f;
            rb.linearVel.y  = 0.0f;
            rb.forceAccum.y = 0.0f;
        }
        // Prevent roll and pitch — the car stays flat
        rb.angularVel.x = 0.0f;
        rb.angularVel.z = 0.0f;

        // ---- 3) Smooth inputs ------------------------------------------
        vel.appliedThrottle = std::clamp(safeF(vel.appliedThrottle), -1.0f, 1.0f);
        vel.appliedSteer    = std::clamp(safeF(vel.appliedSteer),    -1.0f, 1.0f);

        float tgtThrottle = std::clamp(safeF(rawInput.throttle), -1.0f, 1.0f);
        float tgtSteer    = std::clamp(safeF(rawInput.steer),    -1.0f, 1.0f);
        float tgtBrake    = std::clamp(safeF(rawInput.brake),     0.0f, 1.0f);
        if (fuel.depleted()) tgtThrottle = 0.0f;

        float kT = std::clamp(throttleResponse * dt, 0.0f, 1.0f);
        float kS = std::clamp(steerResponse    * dt, 0.0f, 1.0f);
        vel.appliedThrottle += (tgtThrottle - vel.appliedThrottle) * kT;
        vel.appliedSteer    += (tgtSteer    - vel.appliedSteer)    * kS;
        vel.appliedThrottle  = std::clamp(safeF(vel.appliedThrottle), -1.0f, 1.0f);
        vel.appliedSteer     = std::clamp(safeF(vel.appliedSteer),    -1.0f, 1.0f);

        // ---- 4) Car-local basis and velocity components -----------------
        glm::vec3 forward = tf.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 right   = tf.rotation * glm::vec3(1.0f, 0.0f,  0.0f);
        forward.y = 0.0f; right.y = 0.0f;
        forward = safeNormalize(forward, glm::vec3(0.0f, 0.0f, -1.0f));
        right   = safeNormalize(right,   glm::vec3(1.0f, 0.0f,  0.0f));

        float vFwd = safeF(glm::dot(rb.linearVel, forward));
        float vLat = safeF(glm::dot(rb.linearVel, right));

        // ---- 5) Longitudinal force (drive + engine-brake + service-brake) --
        float topSpd = std::max(safeF(stats.topSpeed, 30.0f), 1.0f);
        float power  = std::max(safeF(stats.power,   100.0f), 1.0f);
        float handle = std::clamp(safeF(stats.handling, 1.0f), 0.1f, 5.0f);
        float vMaxFwd = topSpd;
        float vMaxRev = std::max(topSpd * reverseSpeedMul, 1.0f);

        float t = vel.appliedThrottle;
        float accel = 0.0f;

        if (t > 0.01f) {
            // Forward: headroom shrinks as we approach top speed
            float head = std::max(0.0f, 1.0f - vFwd / vMaxFwd);
            accel += t * power * powerScale * head;
        } else if (t < -0.01f) {
            // Reverse
            float head = std::max(0.0f, 1.0f - (-vFwd) / vMaxRev);
            accel += t * power * powerScale * reversePowerMul * head;
        } else {
            // Coast — engine braking
            accel -= engineBrake * vFwd;
        }

        if (tgtBrake > 0.01f) {
            float bForce = brakeSensitivity * tgtBrake;
            if      (vFwd >  0.05f) accel -= bForce;
            else if (vFwd < -0.05f) accel += bForce;
        }
        accel = std::clamp(safeF(accel), -maxAccel, maxAccel);

        // Accumulate as force on the rigid body (F = m·a)
        rb.forceAccum += forward * (accel * rb.mass);

        // ---- 6) Rolling drag + handbrake drag ---------------------------
        float fwdDrag = drag + (rawInput.handbrake ? handbrakeDrag : 0.0f);
        rb.forceAccum -= forward * (vFwd * fwdDrag * rb.mass);

        // ---- 7) Lateral grip force (cancels sideways sliding) -----------
        float latGrip = lateralFriction * (rawInput.handbrake ? handbrakeGripMul : 1.0f);
        // This force zeroes out vLat over ~(1/latGrip) seconds.
        rb.forceAccum -= right * (vLat * latGrip * rb.mass);

        // ---- 8) Bicycle-model steering → set angular velocity directly --
        float delta    = vel.appliedSteer * maxSteerAngleRad;
        float L        = std::max(wheelbase, 0.5f);
        float vForBike = (vFwd >= 0.0f) ? vFwd : vFwd * 0.7f;
        float yawRate  = -(vForBike / L) * std::tan(delta) * handle;

        if (std::abs(vel.appliedSteer) > 0.01f) {
            float blend = std::clamp(
                1.0f - std::abs(vFwd) / std::max(pivotMinSpeed, 0.1f), 0.0f, 1.0f);
            float pivotSign = (vFwd >= 0.0f) ? 1.0f : -1.0f;
            float pivot = -vel.appliedSteer * pivotYawRate * handle * pivotSign;
            yawRate = yawRate * (1.0f - blend) + pivot * blend;
        }
        yawRate = std::clamp(safeF(yawRate), -6.0f, 6.0f);

        // RigidBodySystem will integrate angularVel → quaternion rotation.
        rb.angularVel.y = yawRate;

        // ---- 9) Sync Velocity component (used by HUD + camera) ----------
        vel.linear   = rb.linearVel;
        vel.angularY = glm::degrees(yawRate);
    });
}
