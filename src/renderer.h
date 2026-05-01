// Off-screen renderer: FBO + ECS-driven draw loop.
// Supports Cube, Ground, and Sphere mesh IDs.
#pragma once

#include "scene.h"
#include <entt/entt.hpp>

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool init();

    // Release all GL resources. Idempotent — destructor calls it too.
    void destroy();

    // Resize the offscreen target if needed. Idempotent.
    void resize(int width, int height);

    // ECS-driven render: queries registry for Transform/MeshRef/Material/CameraState/DirectionalLight.
    GLuint render(entt::registry& reg, const Scene& scene);

    int width()  const { return fbWidth_;  }
    int height() const { return fbHeight_; }
    GLuint colorTexture() const { return colorTex_; }

private:
    GLuint fbo_           = 0;
    GLuint colorTex_      = 0;
    GLuint depthRb_       = 0;
    int    fbWidth_       = 0;
    int    fbHeight_      = 0;

    // Ground plane mesh
    GLuint  groundVao_        = 0;
    GLuint  groundVbo_        = 0;
    GLuint  groundIbo_        = 0;
    GLsizei groundIndexCount_ = 0;

    // Cube / generic box mesh
    GLuint  cubeVao_          = 0;
    GLuint  cubeVbo_          = 0;
    GLuint  cubeIbo_          = 0;
    GLsizei cubeIndexCount_   = 0;

    // UV Sphere mesh (used for light visualization)
    GLuint  sphereVao_        = 0;
    GLuint  sphereVbo_        = 0;
    GLuint  sphereIbo_        = 0;
    GLsizei sphereIndexCount_ = 0;

    // Cylinder mesh (axis along Y, radius 0.5, height 1.0)
    GLuint  cylinderVao_        = 0;
    GLuint  cylinderVbo_        = 0;
    GLuint  cylinderIbo_        = 0;
    GLsizei cylinderIndexCount_ = 0;

    GLuint  fallbackTex_        = 0;  // 1×1 white texture bound when submesh has no diffuse

    void createGroundMesh();
    void createCubeMesh();
    void createSphereMesh();
    void createCylinderMesh();
    void destroyFbo();
};
