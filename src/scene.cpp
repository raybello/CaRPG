#include "scene.h"

#include <cstdio>

const char* sceneItemName(SceneItem s) {
    switch (s) {
        case SceneItem::Ground: return "Ground";
        case SceneItem::Cube:   return "Cube";
        case SceneItem::Camera: return "Camera";
        case SceneItem::Light:  return "Light";
        default:                return "?";
    }
}

ShaderBundle& Scene::shaderFor(SceneItem s) {
    switch (s) {
        case SceneItem::Ground: return groundShader;
        case SceneItem::Cube:   return cubeShader;
        case SceneItem::Camera: return cameraShader;
        case SceneItem::Light:  return lightShader;
        default:                return cubeShader;
    }
}

// ---------------------------------------------------------------------------
// Default shaders.
//
// Two preambles depending on whether we're targeting WebGL 1 / GLES 2
// (Emscripten build) or desktop GL 3.0 core. The body is identical.
// ---------------------------------------------------------------------------
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
static const char* kAttr    = "attribute";
static const char* kVarOut  = "varying";
static const char* kVarIn   = "varying";
static const char* kFragOut = ""; // gl_FragColor
#else
static const char* kAttr    = "in";
static const char* kVarOut  = "out";
static const char* kVarIn   = "in";
static const char* kFragOut = "out vec4 FragColor;\n";
#endif
// kVersion is resolved at runtime after the GL context exists.
#define kVersion defaultGlslVersion()

static std::string defaultCubeVert() {
    std::string s = kVersion;
    s += kAttr; s += " vec3 aPos;\n";
    s += kAttr; s += " vec3 aNormal;\n";
    s += "uniform mat4 uModel;\n";
    s += "uniform mat4 uView;\n";
    s += "uniform mat4 uProj;\n";
    s += kVarOut; s += " vec3 vNormal;\n";
    s += kVarOut; s += " vec3 vWorldPos;\n";
    s += "void main() {\n";
    s += "    vec4 wp = uModel * vec4(aPos, 1.0);\n";
    s += "    vWorldPos = wp.xyz;\n";
    s += "    vNormal   = mat3(uModel) * aNormal;\n";
    s += "    gl_Position = uProj * uView * wp;\n";
    s += "}\n";
    return s;
}

static std::string defaultCubeFrag() {
    std::string s = kVersion;
    s += kVarIn; s += " vec3 vNormal;\n";
    s += kVarIn; s += " vec3 vWorldPos;\n";
    s += "uniform vec3 uLightDir;\n";
    s += "uniform vec3 uLightPos;\n";
    s += "uniform vec3 uLightColor;\n";
    s += "uniform float uIsDirectional;\n";
    s += "uniform float uAmbientOn;\n";
    s += "uniform vec3 uMaterialColor;\n";
    s += "uniform vec3 uCameraPos;\n";
    s += kFragOut;
    s += "void main() {\n";
    s += "    vec3 N = normalize(vNormal);\n";
    s += "    vec3 L;\n";
    s += "    if (uIsDirectional > 0.5) {\n";
    s += "        L = normalize(-uLightDir);\n";
    s += "    } else {\n";
    s += "        L = normalize(uLightPos - vWorldPos);\n";
    s += "    }\n";
    s += "    float diff = max(dot(N, L), 0.0);\n";
    s += "    vec3 V = normalize(uCameraPos - vWorldPos);\n";
    s += "    vec3 H = normalize(L + V);\n";
    s += "    float spec = pow(max(dot(N, H), 0.0), 32.0);\n";
    s += "    vec3 ambient = uAmbientOn > 0.5 ? 0.15 * uMaterialColor : vec3(0.0);\n";
    s += "    vec3 color = ambient + uMaterialColor * uLightColor * diff + 0.4 * uLightColor * spec;\n";
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
    s += "    gl_FragColor = vec4(color, 1.0);\n";
#else
    s += "    FragColor = vec4(color, 1.0);\n";
#endif
    s += "}\n";
    return s;
}

// The camera and light items don't actually rasterize geometry, but the
// brief asks that "depending which item is selected you can view and
// modify the vertex and fragment shader used when rendering that item".
// We populate them with simple, self-documenting placeholder programs
// that the user can experiment with — they will compile but, by design,
// the renderer only invokes the cube shader for drawing.
static std::string placeholderVert(const char* what) {
    std::string s = kVersion;
    s += kAttr; s += " vec3 aPos;\n";
    s += "uniform mat4 uModel;\n";
    s += "uniform mat4 uView;\n";
    s += "uniform mat4 uProj;\n";
    s += "void main() {\n";
    s += "    // ";
    s += what;
    s += " — vertex shader (not bound to drawing in this build)\n";
    s += "    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);\n";
    s += "}\n";
    return s;
}

static std::string placeholderFrag(const char* what) {
    std::string s = kVersion;
    s += kFragOut;
    s += "void main() {\n";
    s += "    // ";
    s += what;
    s += " — fragment shader\n";
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
    s += "    gl_FragColor = vec4(1.0, 0.5, 0.2, 1.0);\n";
#else
    s += "    FragColor = vec4(1.0, 0.5, 0.2, 1.0);\n";
#endif
    s += "}\n";
    return s;
}

// Ground uses the same Blinn-Phong vertex/fragment shader as the cube so
// users can edit it independently with the same uniform conventions.
// The only difference from the cube shader is the default material color.
static std::string defaultGroundVert() { return defaultCubeVert(); }
static std::string defaultGroundFrag() { return defaultCubeFrag(); }

void Scene::destroy() {
    auto cleanup = [](ShaderBundle& sb) {
        if (sb.program) { glDeleteProgram(sb.program); sb.program = 0; sb.ok = false; }
    };
    cleanup(groundShader);
    cleanup(cubeShader);
    cleanup(cameraShader);
    cleanup(lightShader);
}

bool Scene::initDefaultShaders() {
    groundShader.vertSrc = defaultGroundVert();
    groundShader.fragSrc = defaultGroundFrag();
    cubeShader.vertSrc   = defaultCubeVert();
    cubeShader.fragSrc   = defaultCubeFrag();
    cameraShader.vertSrc = placeholderVert("Camera");
    cameraShader.fragSrc = placeholderFrag("Camera");
    lightShader.vertSrc  = placeholderVert("Light");
    lightShader.fragSrc  = placeholderFrag("Light");

    auto compile = [](ShaderBundle& sb) {
        bool ok = compileShaderProgram(sb.vertSrc, sb.fragSrc, sb.program, sb.log);
        sb.ok = ok;
        return ok;
    };

    compile(groundShader);
    bool okCube = compile(cubeShader);
    compile(cameraShader);
    compile(lightShader);

    if (!okCube)
        std::fprintf(stderr, "Cube shader failed:\n%s\n", cubeShader.log.c_str());
    return okCube;
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------
void Scene::buildViewProj(int w, int h, glm::mat4& outView, glm::mat4& outProj) const {
    glm::mat4 camRot = glm::mat4(1.0f);
    camRot = glm::rotate(camRot, glm::radians(camera.rotationDeg.z), glm::vec3(0,0,1));
    camRot = glm::rotate(camRot, glm::radians(camera.rotationDeg.y), glm::vec3(0,1,0));
    camRot = glm::rotate(camRot, glm::radians(camera.rotationDeg.x), glm::vec3(1,0,0));
    glm::vec3 forward = glm::vec3(camRot * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));

    outView = glm::lookAt(camera.position,
                          camera.position + forward,
                          glm::vec3(0.0f, 1.0f, 0.0f));
    float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
    outProj = glm::perspective(glm::radians(camera.fovDeg), aspect, 0.1f, 200.0f);
}

// Push MVP + lighting uniforms to whatever program is currently bound.
static void pushUniforms(GLuint p,
                         const glm::mat4& model,
                         const glm::mat4& view,
                         const glm::mat4& proj,
                         const glm::vec3& matColor,
                         const LightUniforms& light,
                         const glm::vec3& cameraPos) {
    auto setMat4 = [&](const char* n, const glm::mat4& m) {
        GLint l = glGetUniformLocation(p, n);
        if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, glm::value_ptr(m));
    };
    auto setVec3 = [&](const char* n, const glm::vec3& v) {
        GLint l = glGetUniformLocation(p, n);
        if (l >= 0) glUniform3f(l, v.x, v.y, v.z);
    };
    auto setFloat = [&](const char* n, float v) {
        GLint l = glGetUniformLocation(p, n);
        if (l >= 0) glUniform1f(l, v);
    };

    setMat4("uModel", model);
    setMat4("uView",  view);
    setMat4("uProj",  proj);
    setVec3 ("uMaterialColor",  matColor);
    setVec3 ("uLightDir",       light.direction);
    setVec3 ("uLightPos",       light.position);
    setVec3 ("uLightColor",     light.color);
    setFloat("uIsDirectional",  light.directional ? 1.0f : 0.0f);
    setFloat("uAmbientOn",      light.ambient     ? 1.0f : 0.0f);
    setVec3 ("uCameraPos",      cameraPos);
}

// ---------------------------------------------------------------------------
// Per-object uniform upload
// ---------------------------------------------------------------------------
void Scene::applyUniformsToGround(int viewportW, int viewportH) const {
    if (!groundShader.ok || groundShader.program == 0) return;

    // Model: scale a unit quad to (2*halfSize) × (2*halfSize) at Y = 0.
    float s = ground.halfSize * 2.0f;
    glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(s, 1.0f, s));

    glm::mat4 view, proj;
    buildViewProj(viewportW, viewportH, view, proj);
    pushUniforms(groundShader.program, model, view, proj,
                 ground.color, light, camera.position);
}

void Scene::applyUniformsToCube(int viewportW, int viewportH) const {
    if (!cubeShader.ok || cubeShader.program == 0) return;

    glm::mat4 model = glm::translate(glm::mat4(1.0f), cube.position);
    model = glm::rotate(model, glm::radians(cube.rotationDeg.z), glm::vec3(0,0,1));
    model = glm::rotate(model, glm::radians(cube.rotationDeg.y), glm::vec3(0,1,0));
    model = glm::rotate(model, glm::radians(cube.rotationDeg.x), glm::vec3(1,0,0));
    model = glm::scale(model, cube.scale);

    glm::mat4 view, proj;
    buildViewProj(viewportW, viewportH, view, proj);
    pushUniforms(cubeShader.program, model, view, proj,
                 cube.lightingColor, light, camera.position);
}
