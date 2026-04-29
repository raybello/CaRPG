#include "input_system.h"
#include "ecs/components.h"
#include <SDL.h>
#include <imgui.h>

void InputSystem::update(entt::registry& reg, const uint8_t* keys) {
    // Don't drive the car while typing in the shader editor
    if (ImGui::GetIO().WantTextInput) return;

    auto view = reg.view<PlayerTag, CarInput>();
    view.each([&](CarInput& input) {
        input.throttle  = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    ? 1.0f : 0.0f;
        input.brake     = (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  ? 1.0f : 0.0f;
        input.steer     = 0.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  input.steer -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) input.steer += 1.0f;
        input.handbrake = keys[SDL_SCANCODE_SPACE] != 0;
    });
}
