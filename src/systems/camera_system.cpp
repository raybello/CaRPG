#include "camera_system.h"
#include "ecs/components.h"
#include <algorithm>

void CameraSystem::update(entt::registry& reg, float dt, int vpW, int vpH)
{
    auto camView = reg.view<CameraState, CameraFollow>();
    camView.each([&](CameraState& cam, const CameraFollow& follow) {
        if (follow.target == entt::null || !reg.valid(follow.target)) return;

        const auto* tf = reg.try_get<Transform>(follow.target);
        if (!tf) return;

        // Car's world-space forward direction
        glm::vec3 carForward = tf->rotation * glm::vec3(0.0f, 0.0f, -1.0f);

        // Position: fixed offset behind and above the car, rotates with it
        glm::vec3 desiredPos = tf->position
            - carForward * follow.offset.z
            + glm::vec3(0.0f, 1.0f, 0.0f) * follow.offset.y;

        float t = std::min(1.0f, follow.lag * dt);
        cam.position = glm::mix(cam.position, desiredPos, t);

        // View direction: always the car's forward (look slightly ahead of it)
        cam.forward = carForward;
        glm::vec3 lookAt = tf->position + carForward * 2.0f;
        cam.view = glm::lookAt(cam.position, lookAt, glm::vec3(0, 1, 0));

        float aspect = (vpH > 0) ? (float)vpW / (float)vpH : 1.0f;
        cam.proj = glm::perspective(glm::radians(cam.fovDeg), aspect, 0.1f, 500.0f);
    });
}
