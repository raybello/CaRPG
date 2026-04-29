#include "render_system.h"
#include "ecs/components.h"

GLuint RenderSystem::render(entt::registry& reg, Renderer& renderer, const Scene& scene) {
    return renderer.render(reg, scene);
}
