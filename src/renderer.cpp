#include "renderer.h"
#include "ecs/components.h"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>

struct Vertex { float px, py, pz, nx, ny, nz; };

// ---------------------------------------------------------------------------
// Cube geometry (flat-shaded, 24 verts / 36 indices)
// ---------------------------------------------------------------------------
static const Vertex kCubeVerts[] = {
    // +X
    { 0.5f,-0.5f,-0.5f, 1,0,0}, { 0.5f, 0.5f,-0.5f, 1,0,0},
    { 0.5f, 0.5f, 0.5f, 1,0,0}, { 0.5f,-0.5f, 0.5f, 1,0,0},
    // -X
    {-0.5f,-0.5f, 0.5f,-1,0,0}, {-0.5f, 0.5f, 0.5f,-1,0,0},
    {-0.5f, 0.5f,-0.5f,-1,0,0}, {-0.5f,-0.5f,-0.5f,-1,0,0},
    // +Y
    {-0.5f, 0.5f,-0.5f, 0,1,0}, {-0.5f, 0.5f, 0.5f, 0,1,0},
    { 0.5f, 0.5f, 0.5f, 0,1,0}, { 0.5f, 0.5f,-0.5f, 0,1,0},
    // -Y
    {-0.5f,-0.5f, 0.5f, 0,-1,0}, {-0.5f,-0.5f,-0.5f, 0,-1,0},
    { 0.5f,-0.5f,-0.5f, 0,-1,0}, { 0.5f,-0.5f, 0.5f, 0,-1,0},
    // +Z
    {-0.5f,-0.5f, 0.5f, 0,0,1}, { 0.5f,-0.5f, 0.5f, 0,0,1},
    { 0.5f, 0.5f, 0.5f, 0,0,1}, {-0.5f, 0.5f, 0.5f, 0,0,1},
    // -Z
    { 0.5f,-0.5f,-0.5f, 0,0,-1}, {-0.5f,-0.5f,-0.5f, 0,0,-1},
    {-0.5f, 0.5f,-0.5f, 0,0,-1}, { 0.5f, 0.5f,-0.5f, 0,0,-1},
};
static const uint16_t kCubeIdx[] = {
     0, 1, 2,  0, 2, 3,
     4, 5, 6,  4, 6, 7,
     8, 9,10,  8,10,11,
    12,13,14, 12,14,15,
    16,17,18, 16,18,19,
    20,21,22, 20,22,23,
};

// ---------------------------------------------------------------------------
// Ground geometry (unit quad at Y=0)
// ---------------------------------------------------------------------------
static const Vertex kGroundVerts[] = {
    {-0.5f,0.0f,-0.5f, 0,1,0},
    {-0.5f,0.0f, 0.5f, 0,1,0},
    { 0.5f,0.0f, 0.5f, 0,1,0},
    { 0.5f,0.0f,-0.5f, 0,1,0},
};
static const uint16_t kGroundIdx[] = { 0,1,2, 0,2,3 };

// ---------------------------------------------------------------------------
// Mesh creation helpers
// ---------------------------------------------------------------------------
static void uploadMesh(const void* verts, GLsizeiptr vBytes,
                       const uint16_t* idx, GLsizeiptr iBytes,
                       GLuint& vao, GLuint& vbo, GLuint& ibo)
{
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
#endif
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vBytes, verts, GL_STATIC_DRAW);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, iBytes, idx, GL_STATIC_DRAW);

#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
#endif
}

void Renderer::createGroundMesh() {
    uploadMesh(kGroundVerts, sizeof(kGroundVerts),
               kGroundIdx,   sizeof(kGroundIdx),
               groundVao_, groundVbo_, groundIbo_);
    groundIndexCount_ = (GLsizei)(sizeof(kGroundIdx) / sizeof(kGroundIdx[0]));
}

void Renderer::createCubeMesh() {
    uploadMesh(kCubeVerts, sizeof(kCubeVerts),
               kCubeIdx,   sizeof(kCubeIdx),
               cubeVao_, cubeVbo_, cubeIbo_);
    cubeIndexCount_ = (GLsizei)(sizeof(kCubeIdx) / sizeof(kCubeIdx[0]));
}

void Renderer::createSphereMesh() {
    const int stacks = 16, slices = 16;
    std::vector<Vertex>   verts;
    std::vector<uint16_t> indices;
    verts.reserve((stacks + 1) * (slices + 1));

    for (int i = 0; i <= stacks; ++i) {
        float phi = (float)M_PI * i / stacks;       // 0 … π (top → bottom)
        float sp  = std::sin(phi), cp = std::cos(phi);
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * (float)M_PI * j / slices;
            float st = std::sin(theta), ct = std::cos(theta);
            float nx = sp * ct, ny = cp, nz = sp * st;
            verts.push_back({ 0.5f*nx, 0.5f*ny, 0.5f*nz, nx, ny, nz });
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint16_t a = (uint16_t)(i * (slices + 1) + j);
            uint16_t b = (uint16_t)(a + slices + 1);
            indices.push_back(a);   indices.push_back(b);   indices.push_back(a + 1);
            indices.push_back(b);   indices.push_back(b + 1); indices.push_back(a + 1);
        }
    }

    uploadMesh(verts.data(),   (GLsizeiptr)(verts.size()   * sizeof(Vertex)),
               indices.data(), (GLsizeiptr)(indices.size() * sizeof(uint16_t)),
               sphereVao_, sphereVbo_, sphereIbo_);
    sphereIndexCount_ = (GLsizei)indices.size();
}

void Renderer::createCylinderMesh() {
    const int   slices = 24;
    const float r = 0.5f;
    std::vector<Vertex>   verts;
    std::vector<uint16_t> indices;

    // Side band — two rings of vertices with outward normals.
    for (int i = 0; i <= slices; ++i) {
        float theta = 2.0f * (float)M_PI * i / slices;
        float ct = std::cos(theta), st = std::sin(theta);
        verts.push_back({ r*ct,  0.5f, r*st,  ct, 0.0f, st });
        verts.push_back({ r*ct, -0.5f, r*st,  ct, 0.0f, st });
    }
    for (int i = 0; i < slices; ++i) {
        uint16_t a = (uint16_t)(i * 2);
        indices.push_back(a);     indices.push_back(a+1); indices.push_back(a+2);
        indices.push_back(a+1);   indices.push_back(a+3); indices.push_back(a+2);
    }

    // Top cap
    auto addCap = [&](float y, float ny) {
        uint16_t center = (uint16_t)verts.size();
        verts.push_back({ 0.0f, y, 0.0f, 0.0f, ny, 0.0f });
        uint16_t ring = (uint16_t)verts.size();
        for (int i = 0; i <= slices; ++i) {
            float theta = 2.0f * (float)M_PI * i / slices;
            verts.push_back({ r*std::cos(theta), y, r*std::sin(theta), 0.0f, ny, 0.0f });
        }
        for (int i = 0; i < slices; ++i) {
            if (ny > 0.0f) {
                indices.push_back(center);
                indices.push_back((uint16_t)(ring + i));
                indices.push_back((uint16_t)(ring + i + 1));
            } else {
                indices.push_back(center);
                indices.push_back((uint16_t)(ring + i + 1));
                indices.push_back((uint16_t)(ring + i));
            }
        }
    };
    addCap( 0.5f,  1.0f);
    addCap(-0.5f, -1.0f);

    uploadMesh(verts.data(),   (GLsizeiptr)(verts.size()   * sizeof(Vertex)),
               indices.data(), (GLsizeiptr)(indices.size() * sizeof(uint16_t)),
               cylinderVao_, cylinderVbo_, cylinderIbo_);
    cylinderIndexCount_ = (GLsizei)indices.size();
}

bool Renderer::init() {
    createGroundMesh();
    createCubeMesh();
    createSphereMesh();
    createCylinderMesh();

    // 1×1 opaque white texture used as a safe fallback when a submesh has no diffuse texture.
    // Prevents the macOS GL validator from complaining about an unloaded sampler unit.
    static const uint8_t kWhite[4] = {255, 255, 255, 255};
    glGenTextures(1, &fallbackTex_);
    glBindTexture(GL_TEXTURE_2D, fallbackTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, kWhite);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

// ---------------------------------------------------------------------------
// FBO management
// ---------------------------------------------------------------------------
void Renderer::destroyFbo() {
    if (fbo_)      { glDeleteFramebuffers(1,  &fbo_);      fbo_      = 0; }
    if (colorTex_) { glDeleteTextures(1,      &colorTex_); colorTex_ = 0; }
    if (depthRb_)  { glDeleteRenderbuffers(1, &depthRb_);  depthRb_  = 0; }
}

void Renderer::resize(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w == fbWidth_ && h == fbHeight_ && fbo_ != 0) return;

    destroyFbo();
    fbWidth_ = w; fbHeight_ = h;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);

    glGenRenderbuffers(1, &depthRb_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "FBO incomplete\n");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

// ---------------------------------------------------------------------------
// Uniform helpers
// ---------------------------------------------------------------------------
static void pushUniformsEcs(GLuint p,
                             const glm::mat4& model,
                             const glm::mat4& view,
                             const glm::mat4& proj,
                             const glm::vec3& matColor,
                             const glm::vec3& lightDir,
                             const glm::vec3& lightPos,
                             const glm::vec3& lightColor,
                             float            lightType,   // 0=ambient,1=dir,2=spot
                             float            spotCutoff,  // cos(half-angle)
                             const glm::vec3& cameraPos)
{
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
    setFloat("uLightType",     lightType);
    setFloat("uSpotCutoff",    spotCutoff);
    setVec3 ("uCameraPos",     cameraPos);
}

// ---------------------------------------------------------------------------
// ECS render
// ---------------------------------------------------------------------------
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

    // Helper: bind buffers, set attrib pointers, draw, clean up.
    auto drawMeshBuffers = [&](GLuint vao, GLuint vbo, GLuint ibo, GLsizei count) {
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
        glBindVertexArray(vao);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (const void*)(sizeof(float) * 3));
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, 0);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
        glBindVertexArray(0);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    };

    // 1. Gather camera matrices from ECS (fallback to scene editor camera)
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

    // 2. Gather light data from ECS
    glm::vec3 lightDir   {-0.4f, -0.8f, -0.4f};
    glm::vec3 lightPos   { 2.0f,  3.0f,  2.0f};
    glm::vec3 lightColor { 1.0f,  1.0f,  1.0f};
    float     lightType  = 1.0f;   // directional by default
    float     spotCutoff = std::cos(glm::radians(30.0f));

    auto lightView = reg.view<DirectionalLight>();
    if (!lightView.empty()) {
        auto& dl   = reg.get<DirectionalLight>(lightView.front());
        lightDir   = dl.direction;
        lightPos   = dl.position;
        lightColor = dl.color;
        lightType  = (float)dl.type;
        spotCutoff = std::cos(glm::radians(dl.spotCutoff));
    }

    // 3. Draw all renderable ECS entities (cube/ground/sphere meshes)
    auto renderView = reg.view<Transform, MeshRef, Material>();
    renderView.each([&](const Transform& tf, const MeshRef& mr, const Material& mat) {
        if (!mat.visible || mat.program == 0) return;

        glUseProgram(mat.program);
        glm::mat4 model = tf.toMatrix();

        pushUniformsEcs(mat.program, model, view, proj,
                        mat.color,
                        lightDir, lightPos, lightColor,
                        lightType, spotCutoff,
                        camPos);

        switch (mr.meshId) {
            case MeshId::Ground:
                drawMeshBuffers(groundVao_, groundVbo_, groundIbo_, groundIndexCount_);
                break;
            case MeshId::Sphere:
                drawMeshBuffers(sphereVao_, sphereVbo_, sphereIbo_, sphereIndexCount_);
                break;
            case MeshId::Cylinder:
                drawMeshBuffers(cylinderVao_, cylinderVbo_, cylinderIbo_, cylinderIndexCount_);
                break;
            case MeshId::Cube:
            case MeshId::CarBody:
            case MeshId::CarWheel:
            default:
                drawMeshBuffers(cubeVao_, cubeVbo_, cubeIbo_, cubeIndexCount_);
                break;
        }
    });

    // 4. Draw assimp-loaded models (ModelMesh component)
    auto modelView = reg.view<Transform, ModelMesh>();
    modelView.each([&](const Transform& tf, const ModelMesh& mm) {
        if (!mm.visible || mm.modelShaderProgram == 0 || mm.submeshes.empty()) return;

        glUseProgram(mm.modelShaderProgram);
        glm::mat4 model = tf.toMatrix() * glm::mat4_cast(mm.rotOffset);

        // Set shared uniforms
        auto setMat4  = [&](const char* n, const glm::mat4& m) {
            GLint l = glGetUniformLocation(mm.modelShaderProgram, n);
            if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, glm::value_ptr(m));
        };
        auto setVec3  = [&](const char* n, const glm::vec3& v) {
            GLint l = glGetUniformLocation(mm.modelShaderProgram, n);
            if (l >= 0) glUniform3f(l, v.x, v.y, v.z);
        };
        auto setFloat = [&](const char* n, float v) {
            GLint l = glGetUniformLocation(mm.modelShaderProgram, n);
            if (l >= 0) glUniform1f(l, v);
        };
        auto setInt   = [&](const char* n, int v) {
            GLint l = glGetUniformLocation(mm.modelShaderProgram, n);
            if (l >= 0) glUniform1i(l, v);
        };

        setMat4 ("uModel",     model);
        setMat4 ("uView",      view);
        setMat4 ("uProj",      proj);
        setVec3 ("uLightDir",  lightDir);
        setVec3 ("uLightColor",lightColor);
        setFloat("uLightType", lightType);
        setVec3 ("uCameraPos", camPos);
        setInt  ("uDiffuseTex", 0);   // texture unit 0

        for (const auto& sm : mm.submeshes) {
            if (sm.indexCount == 0 || sm.vao == 0) continue;

            if (sm.diffuseTex != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sm.diffuseTex);
                setFloat("uHasTexture", 1.0f);
            } else {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, fallbackTex_);
                setFloat("uHasTexture", 0.0f);
                GLint lc = glGetUniformLocation(mm.modelShaderProgram, "uDiffuseColor");
                if (lc >= 0) glUniform4f(lc,
                    sm.diffuseColor.r, sm.diffuseColor.g,
                    sm.diffuseColor.b, sm.diffuseColor.a);
            }

            glBindVertexArray(sm.vao);
            glDrawElements(GL_TRIANGLES, (GLsizei)sm.indexCount, GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    });

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return colorTex_;
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------
void Renderer::destroy() {
    destroyFbo();

    if (groundVbo_) { glDeleteBuffers(1, &groundVbo_); groundVbo_ = 0; }
    if (groundIbo_) { glDeleteBuffers(1, &groundIbo_); groundIbo_ = 0; }
    if (cubeVbo_)   { glDeleteBuffers(1, &cubeVbo_);   cubeVbo_   = 0; }
    if (cubeIbo_)   { glDeleteBuffers(1, &cubeIbo_);   cubeIbo_   = 0; }
    if (sphereVbo_)    { glDeleteBuffers(1, &sphereVbo_);    sphereVbo_    = 0; }
    if (sphereIbo_)    { glDeleteBuffers(1, &sphereIbo_);    sphereIbo_    = 0; }
    if (cylinderVbo_)  { glDeleteBuffers(1, &cylinderVbo_);  cylinderVbo_  = 0; }
    if (cylinderIbo_)  { glDeleteBuffers(1, &cylinderIbo_);  cylinderIbo_  = 0; }

#if !defined(IMGUI_IMPL_OPENGL_ES2) && !defined(__EMSCRIPTEN__)
    if (groundVao_)   { glDeleteVertexArrays(1, &groundVao_);   groundVao_   = 0; }
    if (cubeVao_)     { glDeleteVertexArrays(1, &cubeVao_);     cubeVao_     = 0; }
    if (sphereVao_)   { glDeleteVertexArrays(1, &sphereVao_);   sphereVao_   = 0; }
    if (cylinderVao_) { glDeleteVertexArrays(1, &cylinderVao_); cylinderVao_ = 0; }
#endif
    if (fallbackTex_) { glDeleteTextures(1, &fallbackTex_); fallbackTex_ = 0; }
}

Renderer::~Renderer() {
    destroy();
}
