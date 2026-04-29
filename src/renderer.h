// Off-screen renderer: maintains an FBO sized to the viewport, draws the
// scene's cube into it, and exposes the resulting GL color texture so the
// app can show it inside an ImGui window.
#pragma once

#include "scene.h"
#include <entt/entt.hpp>

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool init();

    // Release all GL resources.  Safe to call before the GL context is
    // destroyed, and idempotent (destructor calls it too but is a no-op then).
    void destroy();

    // Resize the offscreen target if needed.  Idempotent.
    void resize(int width, int height);

    // ECS-driven render: queries registry for Transform/MeshRef/Material/CameraState/DirectionalLight.
    // Scene& is kept to stay compatible with shader hot-reload (Material.program is patched from it).
    GLuint render(entt::registry& reg, const Scene& scene);

    // Legacy single-scene render (shader editor fallback).
    GLuint render(const Scene& scene);

    int width()  const { return fbWidth_;  }
    int height() const { return fbHeight_; }
    GLuint colorTexture() const { return colorTex_; }

private:
    GLuint fbo_           = 0;
    GLuint colorTex_      = 0;
    GLuint depthRb_       = 0;
    int    fbWidth_       = 0;
    int    fbHeight_      = 0;

    // Ground plane (large flat quad, unit size scaled via model matrix)
    GLuint  groundVao_        = 0; // unused on GLES2/WebGL but always present
    GLuint  groundVbo_        = 0;
    GLuint  groundIbo_        = 0;
    GLsizei groundIndexCount_ = 0;

    // Cube
    GLuint  cubeVao_          = 0; // unused on GLES2/WebGL but always present
    GLuint  cubeVbo_          = 0;
    GLuint  cubeIbo_          = 0;
    GLsizei cubeIndexCount_   = 0;

    void createGroundMesh();
    void createCubeMesh();
    void destroyFbo();
};
