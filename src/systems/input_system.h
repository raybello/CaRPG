#pragma once
#include <entt/entt.hpp>
#include <SDL.h>
#include <memory>
#include <algorithm>
#include "ecs/components.h"

// ---------------------------------------------------------------------------
// InputProvider — device-agnostic input contract
//
// Implement this to add keyboard, gamepad, VR, or replay input. Swap the
// active provider via InputSystem::setProvider() at runtime.
// All values must be clamped to their declared ranges before storing.
// ---------------------------------------------------------------------------
struct InputProvider {
    virtual ~InputProvider() = default;
    virtual void poll(CarInput& out) = 0;
};

// ---------------------------------------------------------------------------
// KeyboardInputProvider — maps SDL keyboard state to CarInput
//
//   W / Up Arrow    → throttle  1.0   (forward drive)
//   S / Down Arrow  → brake     1.0   (braking / reverse)
//   A / Left Arrow  → steer    -1.0   (left)
//   D / Right Arrow → steer    +1.0   (right)
//   Space           → handbrake true
// ---------------------------------------------------------------------------
struct KeyboardInputProvider : InputProvider {
    const uint8_t* keys = nullptr;  // set by InputSystem before poll()

    void poll(CarInput& out) override {
        if (!keys) return;
        out.throttle  = (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    ? 1.0f : 0.0f;
        out.brake     = (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])   ? 1.0f : 0.0f;
        float s = 0.0f;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  s -= 1.0f;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) s += 1.0f;
        out.steer     = std::clamp(s, -1.0f, 1.0f);
        out.handbrake = keys[SDL_SCANCODE_SPACE] != 0;
    }
};

// ---------------------------------------------------------------------------
// GamepadInputProvider — SDL GameController mapping (stub)
//
//   Right trigger → throttle   Left trigger → brake
//   Left stick X  → steer      A button     → handbrake
// ---------------------------------------------------------------------------
struct GamepadInputProvider : InputProvider {
    SDL_GameController* controller = nullptr;

    void poll(CarInput& out) override {
        if (!controller) return;
        constexpr float kAxisScale = 1.0f / 32767.0f;
        out.throttle  = std::clamp(
            SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) * kAxisScale,
            0.0f, 1.0f);
        out.brake     = std::clamp(
            SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) * kAxisScale,
            0.0f, 1.0f);
        out.steer     = std::clamp(
            SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) * kAxisScale,
            -1.0f, 1.0f);
        out.handbrake = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) != 0;
    }
};

// ---------------------------------------------------------------------------
// InputSystem — polls the active provider and writes CarInput to the registry
// ---------------------------------------------------------------------------
class InputSystem {
public:
    InputSystem();

    // Replace the active provider (default: KeyboardInputProvider).
    void setProvider(std::unique_ptr<InputProvider> p) { provider_ = std::move(p); }

    // keys = SDL_GetKeyboardState(nullptr). Passed to keyboard provider each frame.
    void update(entt::registry& reg, const uint8_t* keys);

private:
    std::unique_ptr<InputProvider> provider_;
};
