#include "input_system.h"
#include <imgui.h>

InputSystem::InputSystem()
    : provider_(std::make_unique<KeyboardInputProvider>())
{}

void InputSystem::update(entt::registry& reg, const uint8_t* keys) {
    // Zero all inputs while a UI text field has focus so typing doesn't steer.
    if (ImGui::GetIO().WantTextInput) {
        reg.view<PlayerTag, CarInput>().each([](CarInput& in) { in = CarInput{}; });
        return;
    }
    if (!provider_ || !keys) return;

    // Forward fresh key state to the keyboard provider if that's what's active.
    if (auto* kb = dynamic_cast<KeyboardInputProvider*>(provider_.get()))
        kb->keys = keys;

    CarInput fresh{};
    provider_->poll(fresh);
    reg.view<PlayerTag, CarInput>().each([&](CarInput& in) { in = fresh; });
}
