#include "physics_system.h"
#include "ecs/components.h"

void PhysicsSystem::update(entt::registry& reg, float dt) {
    auto view = reg.view<PlayerTag, Transform, Velocity, CarInput, DerivedCarStats, Fuel>();
    view.each([&](Transform& tf, Velocity& vel, const CarInput& input,
                  const DerivedCarStats& stats, const Fuel& fuel) {
        // Blocked when out of fuel
        float effectiveThrottle = fuel.depleted() ? 0.0f : input.throttle;

        // Forward direction in world space (car points along -Z in local space)
        glm::vec3 forward = tf.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        forward.y = 0.0f;
        if (glm::length(forward) > 0.001f) forward = glm::normalize(forward);

        float speed = glm::dot(vel.linear, forward);

        // Steering: D = right (+steer) should yaw right (negative Y angle in OpenGL)
        // Use sqrt of speed fraction so steering is strong at low speed too
        float speedFraction = stats.topSpeed > 0.0f ? std::abs(speed) / stats.topSpeed : 0.0f;
        float steerCurve    = std::sqrt(speedFraction + 0.15f);  // non-zero at rest
        float yawDelta = -input.steer * stats.handling * steerSensitivity * steerCurve * dt;

        // Acceleration / braking
        float accel = effectiveThrottle * stats.power * (1.0f - speedFraction);
        float brake = input.brake * brakeSensitivity;

        vel.linear += forward * (accel - brake) * dt;

        // Rolling resistance
        vel.linear *= std::max(0.0f, 1.0f - drag * dt);

        // Speed cap
        float curSpeed = glm::length(vel.linear);
        if (curSpeed > stats.topSpeed)
            vel.linear = glm::normalize(vel.linear) * stats.topSpeed;

        // Handbrake: extra drag
        if (input.handbrake)
            vel.linear *= std::max(0.0f, 1.0f - 8.0f * dt);

        vel.angularY = yawDelta / dt;  // store as deg/s for rendering

        // Integrate
        tf.position += vel.linear * dt;
        tf.position.y = 0.0f;  // keep on ground

        if (std::abs(yawDelta) > 0.001f) {
            glm::quat yawRot = glm::angleAxis(glm::radians(yawDelta), glm::vec3(0,1,0));
            tf.rotation = glm::normalize(yawRot * tf.rotation);
        }
    });
}
