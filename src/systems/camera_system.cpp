#include "camera_system.h"
#include "ecs/components.h"

void CameraSystem::update(entt::registry& reg, float dt, int vpW, int vpH) {
    auto camView = reg.view<CameraState, CameraFollow>();
    camView.each([&](CameraState& cam, const CameraFollow& follow) {
        if (follow.target == entt::null || !reg.valid(follow.target)) return;

        const auto* tf = reg.try_get<Transform>(follow.target);
        if (!tf) return;

        // Desired camera position: behind and above the car in world space
        glm::vec3 carForward = tf->rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 desiredPos = tf->position
            - carForward * follow.offset.z
            + glm::vec3(0, 1, 0) * follow.offset.y;

        // Lerp toward desired position
        float t = std::min(1.0f, follow.lag * dt);
        cam.position = glm::mix(cam.position, desiredPos, t);

        // Look at slightly above the car's center
        glm::vec3 lookAt = tf->position + glm::vec3(0.0f, 0.5f, 0.0f);
        cam.forward = glm::normalize(lookAt - cam.position);

        // Build matrices
        cam.view = glm::lookAt(cam.position, lookAt, glm::vec3(0, 1, 0));
        float aspect = (vpH > 0) ? (float)vpW / (float)vpH : 1.0f;
        cam.proj = glm::perspective(glm::radians(cam.fovDeg), aspect, 0.1f, 500.0f);
    });
}
