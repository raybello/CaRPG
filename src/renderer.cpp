#include "renderer.h"
#include "ecs/components.h"

#include <cstdio>
#include <cstdint>

// 24 vertices (position + normal) — one face's worth of normals per face
// gives flat shading when the same normal is shared across both triangles
// of a face. Index buffer of 36 indices.
struct Vertex { float px, py, pz, nx, ny, nz; };

static const Vertex kCubeVerts[] = {
    // +X face
    { 0.5f,-0.5f,-0.5f,  1, 0, 0}, { 0.5f, 0.5f,-0.5f,  1, 0, 0},
    { 0.5f, 0.5f, 0.5f,  1, 0, 0}, { 0.5f,-0.5f, 0.5f,  1, 0, 0},
    // -X face
    {-0.5f,-0.5f, 0.5f, -1, 0, 0}, {-0.5f, 0.5f, 0.5f, -1, 0, 0},
    {-0.5f, 0.5f,-0.5f, -1, 0, 0}, {-0.5f,-0.5f,-0.5f, -1, 0, 0},
    // +Y face
    {-0.5f, 0.5f,-0.5f,  0, 1, 0}, {-0.5f, 0.5f, 0.5f,  0, 1, 0},
    { 0.5f, 0.5f, 0.5f,  0, 1, 0}, { 0.5f, 0.5f,-0.5f,  0, 1, 0},
    // -Y face
    {-0.5f,-0.5f, 0.5f,  0,-1, 0}, {-0.5f,-0.5f,-0.5f,  0,-1, 0},
    { 0.5f,-0.5f,-0.5f,  0,-1, 0}, { 0.5f,-0.5f, 0.5f,  0,-1, 0},
    // +Z face
    {-0.5f,-0.5f, 0.5f,  0, 0, 1}, { 0.5f,-0.5f, 0.5f,  0, 0, 1},
    { 0.5f, 0.5f, 0.5f,  0, 0, 1}, {-0.5f, 0.5f, 0.5f,  0, 0, 1},
    // -Z face
    { 0.5f,-0.5f,-0.5f,  0, 0,-1}, {-0.5f,-0.5f,-0.5f,  0, 0,-1},
    {-0.5f, 0.5f,-0.5f,  0, 0,-1}, { 0.5f, 0.5f,-0.5f,  0, 0,-1},
};

static const uint16_t kCubeIdx[] = {
     0, 1, 2,  0, 2, 3,
     4, 5, 6,  4, 6, 7,
     8, 9,10,  8,10,11,
    12,13,14, 12,14,15,
    16,17,18, 16,18,19,
    20,21,22, 20,22,23,
};

// Unit quad flat on Y=0 with normal (0,1,0).  The model matrix in scene.cpp
// scales it to the ground's world-space size.
static const Vertex kGroundVerts[] = {
    {-0.5f, 0.0f, -0.5f,  0, 1, 0},
    {-0.5f, 0.0f,  0.5f,  0, 1, 0},
    { 0.5f, 0.0f,  0.5f,  0, 1, 0},
    { 0.5f, 0.0f, -0.5f,  0, 1, 0},
};
static const uint16_t kGroundIdx[] = { 0, 1, 2,  0, 2, 3 };

void Renderer::createGroundMesh() {
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
    glGenVertexArrays(1, &groundVao_);
    glBindVertexArray(groundVao_);
#endif
    glGenBuffers(1, &groundVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, groundVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kGroundVerts), kGroundVerts, GL_STATIC_DRAW);

    glGenBuffers(1, &groundIbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, groundIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kGroundIdx), kGroundIdx, GL_STATIC_DRAW);
    groundIndexCount_ = sizeof(kGroundIdx) / sizeof(kGroundIdx[0]);

#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
#endif
}

void Renderer::createCubeMesh() {
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
    glGenVertexArrays(1, &cubeVao_);
    glBindVertexArray(cubeVao_);
#endif
    glGenBuffers(1, &cubeVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVerts), kCubeVerts, GL_STATIC_DRAW);

    glGenBuffers(1, &cubeIbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeIdx), kCubeIdx, GL_STATIC_DRAW);
    cubeIndexCount_ = sizeof(kCubeIdx) / sizeof(kCubeIdx[0]);

#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
#endif
}

bool Renderer::init() {
    createGroundMesh();
    createCubeMesh();
    return true;
}

void Renderer::destroyFbo() {
    if (fbo_)      { glDeleteFramebuffers(1, &fbo_);      fbo_ = 0; }
    if (colorTex_) { glDeleteTextures(1, &colorTex_);     colorTex_ = 0; }
    if (depthRb_)  { glDeleteRenderbuffers(1, &depthRb_); depthRb_ = 0; }
}

void Renderer::resize(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == fbWidth_ && h == fbHeight_ && fbo_ != 0) return;

    destroyFbo();
    fbWidth_  = w;
    fbHeight_ = h;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTex_, 0);

    glGenRenderbuffers(1, &depthRb_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depthRb_);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "FBO incomplete: 0x%x\n", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

GLuint Renderer::render(const Scene& scene) {
    if (fbo_ == 0) return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, fbWidth_, fbHeight_);

    glClearColor(0.08f, 0.10f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Helper: bind a mesh, set attrib pointers, draw, unbind.
    auto drawMesh = [&](GLuint vao, GLuint vbo, GLuint ibo, GLsizei count) {
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
        glBindVertexArray(vao);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (const void*)(sizeof(float)*3));
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, 0);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
        glBindVertexArray(0);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    };

    // --- Ground plane (draw first; depth test will handle occlusion) --------
    if (scene.groundShader.ok && scene.groundShader.program != 0) {
        glUseProgram(scene.groundShader.program);
        scene.applyUniformsToGround(fbWidth_, fbHeight_);
        drawMesh(groundVao_, groundVbo_, groundIbo_, groundIndexCount_);
    }

    // --- Cube ---------------------------------------------------------------
    if (!scene.cubeShader.ok || scene.cubeShader.program == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return colorTex_;
    }
    glUseProgram(scene.cubeShader.program);
    scene.applyUniformsToCube(fbWidth_, fbHeight_);
    drawMesh(cubeVao_, cubeVbo_, cubeIbo_, cubeIndexCount_);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return colorTex_;
}

// ---------------------------------------------------------------------------
// ECS-driven render
// ---------------------------------------------------------------------------
static void pushUniformsEcs(GLuint p,
                             const glm::mat4& model,
                             const glm::mat4& view,
                             const glm::mat4& proj,
                             const glm::vec3& matColor,
                             const glm::vec3& lightDir,
                             const glm::vec3& lightPos,
                             const glm::vec3& lightColor,
                             bool  ambientOn,
                             const glm::vec3& cameraPos) {
    auto setMat4  = [&](const char* n, const glm::mat4& m) {
        GLint l = glGetUniformLocation(p, n);
        if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, glm::value_ptr(m));
    };
    auto setVec3  = [&](const char* n, const glm::vec3& v) {
        GLint l = glGetUniformLocation(p, n);
        if (l >= 0) glUniform3f(l, v.x, v.y, v.z);
    };
    auto setFloat = [&](const char* n, float v) {
        GLint l = glGetUniformLocation(p, n);
        if (l >= 0) glUniform1f(l, v);
    };

    setMat4 ("uModel",         model);
    setMat4 ("uView",          view);
    setMat4 ("uProj",          proj);
    setVec3 ("uMaterialColor", matColor);
    setVec3 ("uLightDir",      lightDir);
    setVec3 ("uLightPos",      lightPos);
    setVec3 ("uLightColor",    lightColor);
    setFloat("uIsDirectional", 1.0f);  // always directional in ECS render
    setFloat("uAmbientOn",     ambientOn ? 1.0f : 0.0f);
    setVec3 ("uCameraPos",     cameraPos);
}

GLuint Renderer::render(entt::registry& reg, const Scene& scene) {
    if (fbo_ == 0) return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, fbWidth_, fbHeight_);

    glClearColor(0.08f, 0.10f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    auto drawMesh = [&](GLuint vao, GLuint vbo, GLuint ibo, GLsizei count) {
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
        glBindVertexArray(vao);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (const void*)(sizeof(float)*3));
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, 0);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
        glBindVertexArray(0);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    };

    // 1. Gather camera matrices from ECS (fall back to scene camera if not ready)
    glm::mat4 view {1.0f}, proj {1.0f};
    glm::vec3 camPos {0.0f, 5.0f, 15.0f};
    auto camView = reg.view<CameraState>();
    if (!camView.empty()) {
        auto& cam = reg.get<CameraState>(camView.front());
        view   = cam.view;
        proj   = cam.proj;
        camPos = cam.position;
    } else {
        scene.buildViewProj(fbWidth_, fbHeight_, view, proj);
        camPos = scene.camera.position;
    }

    // 2. Gather light from ECS (fall back to scene light)
    glm::vec3 lightDir   {-0.4f, -0.8f, -0.4f};
    glm::vec3 lightPos   { 2.0f,  3.0f,  2.0f};
    glm::vec3 lightColor {1.0f,   1.0f,  1.0f};
    bool      ambientOn  = true;
    auto lightView = reg.view<DirectionalLight>();
    if (!lightView.empty()) {
        auto& dl = reg.get<DirectionalLight>(lightView.front());
        lightDir   = dl.direction;
        lightPos   = dl.position;
        lightColor = dl.color;
        ambientOn  = dl.ambientOn;
    }

    // 3. Draw all renderable ECS entities
    auto renderView = reg.view<Transform, MeshRef, Material>();
    renderView.each([&](const Transform& tf, const MeshRef& mr, const Material& mat) {
        if (!mat.visible || mat.program == 0) return;

        glUseProgram(mat.program);
        glm::mat4 model = tf.toMatrix();

        pushUniformsEcs(mat.program, model, view, proj,
                        mat.color, lightDir, lightPos, lightColor, ambientOn, camPos);

        switch (mr.meshId) {
            case MeshId::Ground:
                drawMesh(groundVao_, groundVbo_, groundIbo_, groundIndexCount_);
                break;
            case MeshId::Cube:
            case MeshId::CarBody:
            case MeshId::CarWheel:
            case MeshId::Sphere:
            default:
                drawMesh(cubeVao_, cubeVbo_, cubeIbo_, cubeIndexCount_);
                break;
        }
    });

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return colorTex_;
}

void Renderer::destroy() {
    destroyFbo();
    if (groundVbo_) { glDeleteBuffers(1, &groundVbo_); groundVbo_ = 0; }
    if (groundIbo_) { glDeleteBuffers(1, &groundIbo_); groundIbo_ = 0; }
    if (cubeVbo_)   { glDeleteBuffers(1, &cubeVbo_);   cubeVbo_   = 0; }
    if (cubeIbo_)   { glDeleteBuffers(1, &cubeIbo_);   cubeIbo_   = 0; }
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
    if (groundVao_) { glDeleteVertexArrays(1, &groundVao_); groundVao_ = 0; }
    if (cubeVao_)   { glDeleteVertexArrays(1, &cubeVao_);   cubeVao_   = 0; }
#endif
}

Renderer::~Renderer() {
    destroy(); // no-op if already called explicitly before context deletion
}
