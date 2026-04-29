#include "input_system.h"
#include "ecs/components.h"
#include <SDL.h>
#include <imgui.h>
#include <algorithm>

// InputSystem maps the keyboard to CarInput targets. It does not produce
// derived/smoothed values — physics owns smoothing and integration.
//
// Bindings:
//   W / Up        → throttle +1  (forward)
//   S / Down      → throttle -1  (reverse)
//   A / Left      → steer    -1  (left)
//   D / Right     → steer    +1  (right)
//   Shift (L/R)   → brake     1  (active brake; opposes motion)
//   Space         → handbrake true (cuts lateral grip → drift)
//
// Every value is clamped to its declared range before being stored.
void InputSystem::update(entt::registry& reg, const uint8_t* keys) {
    // Don't drive the car while typing in a UI text field.
    if (ImGui::GetIO().WantTextInput) {
        // Decay all targets to neutral so a keypress before/after a focus
        // change doesn't leave the car steering or accelerating.
        auto v = reg.view<PlayerTag, CarInput>();
        v.each([](CarInput& in) {
            in.throttle  = 0.0f;
            in.steer     = 0.0f;
            in.brake     = 0.0f;
            in.handbrake = false;
        });
        return;
    }
    if (!keys) return;

    auto view = reg.view<PlayerTag, CarInput>();
    view.each([&](CarInput& in) {
        // Throttle (signed)
        float t = 0.0f;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    t += 1.0f;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  t -= 1.0f;
        in.throttle = std::clamp(t, -1.0f, 1.0f);

        // Steer
        float s = 0.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  s -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) s += 1.0f;
        in.steer = std::clamp(s, -1.0f, 1.0f);

        // Brake (Shift)
        float b = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) ? 1.0f : 0.0f;
        in.brake = std::clamp(b, 0.0f, 1.0f);

        // Handbrake (Space)
        in.handbrake = keys[SDL_SCANCODE_SPACE] != 0;
    });
}
