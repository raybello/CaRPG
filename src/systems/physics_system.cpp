#include "physics_system.h"
#include "ecs/components.h"
#include <algorithm>
#include <cmath>

// =============================================================================
// Kinematic bicycle car model — robust arcade physics
// =============================================================================
//
// State, per car (Player):
//   tf.position     world (x, 0, z)
//   tf.rotation     pure-Y quaternion (yaw only — enforced each tick)
//   vel.linear      world-space velocity
//   vel.appliedThrottle, vel.appliedSteer  smoothed input state
//
// Each tick decomposes vel.linear into (vFwd, vLat) along the car's local
// axes, applies forces in the car frame, then recomposes back to world.
//
// Robustness rules followed throughout:
//   • Every divisor is guarded ≥ 1.0 or ≥ ε
//   • Every decay is `max(0, 1 - k·dt)` so it can't go negative
//   • Every input/output is `isfinite`-checked and falls back to a sane value
//   • Every accel/velocity is hard-clamped — no input combo can blow up
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

void PhysicsSystem::update(entt::registry& reg, float dt) {
    // ---- 1) Validate dt -------------------------------------------------
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;     // hard cap for first-frame / pause hitches

    auto view = reg.view<PlayerTag, Transform, Velocity, CarInput, DerivedCarStats, Fuel>();
    view.each([&](Transform& tf, Velocity& vel, const CarInput& rawInput,
                  const DerivedCarStats& stats, const Fuel& fuel) {

        // ---- 2) Sanitize state -----------------------------------------
        if (!finiteV(tf.position)) tf.position = glm::vec3(0.0f);
        if (!finiteV(vel.linear))  vel.linear  = glm::vec3(0.0f);
        tf.rotation = extractYaw(tf.rotation);   // also enforces pure-Y
        tf.position.y = 0.0f;

        vel.appliedThrottle = std::clamp(safeF(vel.appliedThrottle), -1.0f, 1.0f);
        vel.appliedSteer    = std::clamp(safeF(vel.appliedSteer),    -1.0f, 1.0f);

        // ---- 3) Sanitize inputs ----------------------------------------
        float tgtThrottle = std::clamp(safeF(rawInput.throttle), -1.0f, 1.0f);
        float tgtSteer    = std::clamp(safeF(rawInput.steer),    -1.0f, 1.0f);
        float tgtBrake    = std::clamp(safeF(rawInput.brake),     0.0f, 1.0f);
        if (fuel.depleted()) tgtThrottle = 0.0f;

        // ---- 4) Smooth applied inputs ----------------------------------
        float kT = std::clamp(throttleResponse * dt, 0.0f, 1.0f);
        float kS = std::clamp(steerResponse    * dt, 0.0f, 1.0f);
        vel.appliedThrottle += (tgtThrottle - vel.appliedThrottle) * kT;
        vel.appliedSteer    += (tgtSteer    - vel.appliedSteer)    * kS;
        vel.appliedThrottle  = std::clamp(safeF(vel.appliedThrottle), -1.0f, 1.0f);
        vel.appliedSteer     = std::clamp(safeF(vel.appliedSteer),    -1.0f, 1.0f);

        // ---- 5) Decompose velocity along car-local axes ----------------
        glm::vec3 forward = tf.rotation * glm::vec3(0, 0, -1);
        glm::vec3 right   = tf.rotation * glm::vec3(1, 0,  0);
        forward.y = 0.0f; right.y = 0.0f;
        forward = safeNormalize(forward, glm::vec3(0, 0, -1));
        right   = safeNormalize(right,   glm::vec3(1, 0,  0));

        float vFwd = safeF(glm::dot(vel.linear, forward));
        float vLat = safeF(glm::dot(vel.linear, right));

        // ---- 6) Longitudinal acceleration ------------------------------
        float topSpd  = std::max(safeF(stats.topSpeed, 30.0f), 1.0f);
        float power   = std::max(safeF(stats.power, 100.0f),   1.0f);
        float handle  = std::clamp(safeF(stats.handling, 1.0f), 0.1f, 5.0f);
        float vMaxFwd = topSpd;
        float vMaxRev = std::max(topSpd * reverseSpeedMul, 1.0f);

        float t = vel.appliedThrottle;
        float accel = 0.0f;

        if (t > 0.01f) {
            // Forward drive: headroom shrinks as we approach top forward speed
            float head = std::max(0.0f, 1.0f - vFwd / vMaxFwd);
            accel += t * power * powerScale * head;
        } else if (t < -0.01f) {
            // Reverse drive: headroom shrinks as we approach top reverse speed
            float head = std::max(0.0f, 1.0f - (-vFwd) / vMaxRev);
            accel += t * power * powerScale * reversePowerMul * head;
            // Note: t is negative, so accel becomes negative (pushes -Z forward = backward).
        } else {
            // Coasting — engine braking
            accel -= engineBrake * vFwd;
        }

        // Brake (Shift) — opposes current motion direction
        if (tgtBrake > 0.01f) {
            float bForce = brakeSensitivity * tgtBrake;
            if      (vFwd >  0.05f) accel -= bForce;
            else if (vFwd < -0.05f) accel += bForce;
        }

        accel = std::clamp(safeF(accel), -maxAccel, maxAccel);

        // ---- 7) Integrate vFwd, apply rolling drag ---------------------
        vFwd = safeF(vFwd + accel * dt);
        float fwdDrag = drag + (rawInput.handbrake ? handbrakeDrag : 0.0f);
        vFwd *= std::max(0.0f, 1.0f - fwdDrag * dt);
        vFwd  = std::clamp(safeF(vFwd), -vMaxRev, vMaxFwd);

        // ---- 8) Lateral grip (drift decay) -----------------------------
        float lat = lateralFriction * (rawInput.handbrake ? handbrakeGripMul : 1.0f);
        vLat *= std::max(0.0f, 1.0f - lat * dt);
        vLat  = std::clamp(safeF(vLat), -vMaxFwd, vMaxFwd);

        // ---- 9) Bicycle steering: yaw rate = (v / L) · tan(δ) ----------
        float delta   = vel.appliedSteer * maxSteerAngleRad;       // wheel angle
        float L       = std::max(wheelbase, 0.5f);
        float vForBike = (vFwd >= 0.0f) ? vFwd : vFwd * 0.7f;     // softer reverse
        float yawRate = -(vForBike / L) * std::tan(delta) * handle;

        // Low-speed pivot blend — lets the car rotate at rest while steering.
        // At |v| ≥ pivotMinSpeed, blend = 0 (pure bicycle).
        // At v = 0, blend = 1 (pure pivot).
        if (std::abs(vel.appliedSteer) > 0.01f) {
            float blend = std::clamp(1.0f - std::abs(vFwd) / std::max(pivotMinSpeed, 0.1f),
                                     0.0f, 1.0f);
            float pivotSign = (vFwd >= 0.0f) ? 1.0f : -1.0f;
            float pivot = -vel.appliedSteer * pivotYawRate * handle * pivotSign;
            yawRate = yawRate * (1.0f - blend) + pivot * blend;
        }

        yawRate = safeF(yawRate);
        // Hard sanity clamp on yaw rate (rad/s)
        yawRate = std::clamp(yawRate, -6.0f, 6.0f);
        float yawDelta = yawRate * dt;

        // ---- 10) Apply rotation ----------------------------------------
        if (std::isfinite(yawDelta) && std::abs(yawDelta) > 1e-6f) {
            glm::quat rot = glm::angleAxis(yawDelta, glm::vec3(0, 1, 0));
            glm::quat next = rot * tf.rotation;
            if (finiteQ(next)) {
                tf.rotation = glm::normalize(next);
            }
        }

        // Re-extract basis after rotation update
        forward = tf.rotation * glm::vec3(0, 0, -1);
        right   = tf.rotation * glm::vec3(1, 0,  0);
        forward.y = 0.0f; right.y = 0.0f;
        forward = safeNormalize(forward, glm::vec3(0, 0, -1));
        right   = safeNormalize(right,   glm::vec3(1, 0,  0));

        // ---- 11) Recompose & clamp world velocity ----------------------
        vel.linear = forward * vFwd + right * vLat;
        if (!finiteV(vel.linear)) vel.linear = glm::vec3(0.0f);

        float curSpd = glm::length(vel.linear);
        float vmaxAbs = std::max(vMaxFwd, vMaxRev);
        if (std::isfinite(curSpd) && curSpd > vmaxAbs && curSpd > 1e-6f) {
            vel.linear *= (vmaxAbs / curSpd);
        }
        vel.angularY = glm::degrees(yawRate);

        // ---- 12) Integrate position ------------------------------------
        tf.position += vel.linear * dt;
        tf.position.y = 0.0f;
        if (!finiteV(tf.position)) tf.position = glm::vec3(0.0f);
    });
}
