#pragma once
#include <entt/entt.hpp>
#include "renderer.h"
#include "scene.h"

class RenderSystem {
public:
    GLuint render(entt::registry& reg, Renderer& renderer, const Scene& scene);
};
