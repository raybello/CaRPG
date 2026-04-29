// Scene data: a single cube, a camera, and a single light.
// Each item holds its own editable vertex+fragment shader source, the
// last-built program (if any), the last compile log, and a small struct
// of uniform values that the panel UI binds widgets to.
#pragma once

#include <string>
#include "glm_math.h"
#include "shader.h"

// ---------------------------------------------------------------------------
// Ground plane
// ---------------------------------------------------------------------------
struct GroundUniforms {
    float     halfSize = 10.0f;                    // world-space half-extent
    glm::vec3 color    {0.28f, 0.30f, 0.26f};     // earthy gray-green
};

// ---------------------------------------------------------------------------
// Cube
// ---------------------------------------------------------------------------
struct CubeUniforms {
    glm::vec3 position    {0.0f, 0.5f, 0.0f};     // sits on the ground plane
    glm::vec3 rotationDeg {25.0f, 35.0f, 0.0f};
    glm::vec3 scale       {1.0f, 1.0f, 1.0f};
    glm::vec3 lightingColor {1.0f, 0.85f, 0.7f};  // material color
};

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
struct CameraUniforms {
    float     fovDeg = 60.0f;
    glm::vec3 position    {0.0f, 1.5f, 4.0f};
    glm::vec3 rotationDeg {0.0f, 0.0f, 0.0f};
};

// ---------------------------------------------------------------------------
// Light (single)
// ---------------------------------------------------------------------------
struct LightUniforms {
    glm::vec3 position   {2.0f, 3.0f, 2.0f};
    glm::vec3 direction  {-0.4f, -0.8f, -0.4f}; // points *away* from light
    glm::vec3 color      {1.0f, 1.0f, 1.0f};
    bool      directional = true;   // directional light vs point light
    bool      ambient     = true;   // contribute an ambient term
};

// Per-item shader bundle (text + last successful program + log)
struct ShaderBundle {
    std::string vertSrc;
    std::string fragSrc;
    std::string log;        // error/info text to display in the editor
    GLuint      program = 0;
    bool        ok      = false;
};

enum class SceneItem {
    Ground = 0,
    Cube,
    Camera,
    Light,
    Count
};

const char* sceneItemName(SceneItem s);

struct Scene {
    GroundUniforms ground;
    CubeUniforms   cube;
    CameraUniforms camera;
    LightUniforms  light;

    ShaderBundle groundShader;
    ShaderBundle cubeShader;
    ShaderBundle cameraShader;
    ShaderBundle lightShader;

    // Initialize default shaders + initial compile.  Returns false if the
    // initial cube shader failed to build.
    bool initDefaultShaders();

    // Delete all GL shader programs.  Call before destroying the GL context.
    // Idempotent — destructor calls it too but becomes a no-op afterwards.
    void destroy();

    // Build view + projection matrices from the current camera uniforms.
    void buildViewProj(int w, int h, glm::mat4& outView, glm::mat4& outProj) const;

    // Push uniforms to the currently-bound program.
    void applyUniformsToGround(int viewportW, int viewportH) const;
    void applyUniformsToCube  (int viewportW, int viewportH) const;

    ShaderBundle& shaderFor(SceneItem s);
};
