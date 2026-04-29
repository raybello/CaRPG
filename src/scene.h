// Scene: editor camera state, per-slot shader bundles, and helpers.
// All renderable entity data (lights, mesh transforms, materials) now lives in
// the ECS registry. This file only retains what the renderer and panel need
// that doesn't belong to a specific entity.
#pragma once

#include <string>
#include "glm_math.h"
#include "shader.h"

// ---------------------------------------------------------------------------
// Editor camera (drives the viewport; position is updated by CameraSystem)
// ---------------------------------------------------------------------------
struct CameraUniforms {
    float     fovDeg = 60.0f;
    glm::vec3 position {0.0f, 5.0f, 15.0f};
    glm::vec3 rotationDeg {0.0f, 0.0f, 0.0f};  // fallback only; normally overridden by ECS cam
};

// Per-item shader bundle (text + last successful program + log)
struct ShaderBundle {
    std::string vertSrc;
    std::string fragSrc;
    std::string log;
    GLuint      program = 0;
    bool        ok      = false;
};

// Shader slot — which bundle is being viewed/edited in the panel.
// Not used for entity enumeration; only for the shader editor.
enum class ShaderSlot {
    Ground = 0,
    Object,   // player, items, any generic mesh
    Camera,
    Light,
    Count
};

struct Scene {
    CameraUniforms camera;

    ShaderBundle groundShader;
    ShaderBundle cubeShader;    // "Object" slot
    ShaderBundle cameraShader;
    ShaderBundle lightShader;

    // Initialize default shaders + initial compile.
    bool initDefaultShaders();

    // Delete all GL shader programs. Call before destroying the GL context.
    void destroy();

    // Build view + projection matrices from the editor camera uniforms.
    // Used by the renderer as a fallback when no ECS CameraState exists.
    void buildViewProj(int w, int h, glm::mat4& outView, glm::mat4& outProj) const;

    ShaderBundle& shaderFor(ShaderSlot s);
};
