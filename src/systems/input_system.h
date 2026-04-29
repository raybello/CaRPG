#pragma once
#include <entt/entt.hpp>
#include <cstdint>

class InputSystem {
public:
    // Translates keyboard state into CarInput on the player entity.
    // keys should be SDL_GetKeyboardState(nullptr).
    // Skips input when ImGui wants text (e.g., shader editor is open).
    void update(entt::registry& reg, const uint8_t* keys);
};
