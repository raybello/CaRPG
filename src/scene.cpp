#include "scene.h"

#include <cstdio>

ShaderBundle& Scene::shaderFor(ShaderSlot s) {
    switch (s) {
        case ShaderSlot::Ground:  return groundShader;
        case ShaderSlot::Object:  return cubeShader;
        case ShaderSlot::Camera:  return cameraShader;
        case ShaderSlot::Light:   return lightShader;
        default:                  return cubeShader;
    }
}

// ---------------------------------------------------------------------------
// Default shaders — GL ES 2 / WebGL vs desktop GL 3 core preambles
// ---------------------------------------------------------------------------
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
static const char* kAttr    = "attribute";
static const char* kVarOut  = "varying";
static const char* kVarIn   = "varying";
static const char* kFragOut = "";
#else
static const char* kAttr    = "in";
static const char* kVarOut  = "out";
static const char* kVarIn   = "in";
static const char* kFragOut = "out vec4 FragColor;\n";
#endif
#define kVersion defaultGlslVersion()

// ---------------------------------------------------------------------------
// Object / cube shader  (Blinn-Phong, supports Ambient / Directional / Spot)
// uLightType: 0 = ambient only, 1 = directional, 2 = spot
// ---------------------------------------------------------------------------
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
    s += "uniform vec3  uLightDir;\n";
    s += "uniform vec3  uLightPos;\n";
    s += "uniform vec3  uLightColor;\n";
    s += "uniform float uLightType;\n";   // 0=ambient, 1=directional, 2=spot
    s += "uniform float uSpotCutoff;\n";  // cos(half-angle) for spot
    s += "uniform vec3  uMaterialColor;\n";
    s += "uniform vec3  uCameraPos;\n";
    s += kFragOut;
    s += "void main() {\n";
    s += "    vec3 N = normalize(vNormal);\n";
    s += "    vec3 V = normalize(uCameraPos - vWorldPos);\n";
    s += "    vec3 ambient = 0.15 * uMaterialColor;\n";
    s += "    vec3 color = vec3(0.0);\n";
    s += "    if (uLightType < 0.5) {\n";
    s += "        // Ambient only\n";
    s += "        color = ambient;\n";
    s += "    } else if (uLightType < 1.5) {\n";
    s += "        // Directional\n";
    s += "        vec3 L = normalize(-uLightDir);\n";
    s += "        float diff = max(dot(N, L), 0.0);\n";
    s += "        vec3 H = normalize(L + V);\n";
    s += "        float spec = pow(max(dot(N, H), 0.0), 32.0);\n";
    s += "        color = ambient + uMaterialColor * uLightColor * diff\n";
    s += "              + 0.4 * uLightColor * spec;\n";
    s += "    } else {\n";
    s += "        // Spot\n";
    s += "        vec3 L = normalize(uLightPos - vWorldPos);\n";
    s += "        float angle = dot(normalize(-uLightDir), L);\n";
    s += "        if (angle > uSpotCutoff) {\n";
    s += "            float diff = max(dot(N, L), 0.0);\n";
    s += "            vec3 H = normalize(L + V);\n";
    s += "            float spec = pow(max(dot(N, H), 0.0), 32.0);\n";
    s += "            float att = (angle - uSpotCutoff) / (1.0 - uSpotCutoff);\n";
    s += "            color = ambient + att * (uMaterialColor * uLightColor * diff\n";
    s += "                  + 0.4 * uLightColor * spec);\n";
    s += "        } else {\n";
    s += "            color = ambient;\n";
    s += "        }\n";
    s += "    }\n";
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
    s += "    gl_FragColor = vec4(color, 1.0);\n";
#else
    s += "    FragColor = vec4(color, 1.0);\n";
#endif
    s += "}\n";
    return s;
}

// Ground uses the same Blinn-Phong shader as objects.
static std::string defaultGroundVert() { return defaultCubeVert(); }
static std::string defaultGroundFrag() { return defaultCubeFrag(); }

// ---------------------------------------------------------------------------
// Emissive / unlit shader — used for light-sphere visualization.
// The sphere always renders at its exact material color with no shading.
// ---------------------------------------------------------------------------
static std::string emissiveVert() {
    std::string s = kVersion;
    s += kAttr; s += " vec3 aPos;\n";
    s += kAttr; s += " vec3 aNormal;\n";  // bound but not used — keep attribute layout consistent
    s += "uniform mat4 uModel;\n";
    s += "uniform mat4 uView;\n";
    s += "uniform mat4 uProj;\n";
    s += "void main() {\n";
    s += "    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);\n";
    s += "}\n";
    return s;
}

static std::string emissiveFrag() {
    std::string s = kVersion;
    s += "uniform vec3 uMaterialColor;\n";
    s += kFragOut;
    s += "void main() {\n";
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
    s += "    gl_FragColor = vec4(uMaterialColor, 1.0);\n";
#else
    s += "    FragColor = vec4(uMaterialColor, 1.0);\n";
#endif
    s += "}\n";
    return s;
}

// ---------------------------------------------------------------------------
// Model shader — Blinn-Phong with optional diffuse texture.
// Vertex layout: aPos(0), aNormal(1), aTexCoord(2)
// uHasTexture=1 samples uDiffuseTex; =0 uses uDiffuseColor fallback.
// ---------------------------------------------------------------------------
static std::string modelVert() {
    std::string s = kVersion;
    s += kAttr; s += " vec3 aPos;\n";
    s += kAttr; s += " vec3 aNormal;\n";
    s += kAttr; s += " vec2 aTexCoord;\n";
    s += "uniform mat4 uModel;\n";
    s += "uniform mat4 uView;\n";
    s += "uniform mat4 uProj;\n";
    s += kVarOut; s += " vec3 vNormal;\n";
    s += kVarOut; s += " vec3 vWorldPos;\n";
    s += kVarOut; s += " vec2 vTexCoord;\n";
    s += "void main() {\n";
    s += "    vec4 wp = uModel * vec4(aPos, 1.0);\n";
    s += "    vWorldPos = wp.xyz;\n";
    s += "    vNormal   = mat3(uModel) * aNormal;\n";
    s += "    vTexCoord = aTexCoord;\n";
    s += "    gl_Position = uProj * uView * wp;\n";
    s += "}\n";
    return s;
}

static std::string modelFrag() {
    std::string s = kVersion;
    s += kVarIn; s += " vec3 vNormal;\n";
    s += kVarIn; s += " vec3 vWorldPos;\n";
    s += kVarIn; s += " vec2 vTexCoord;\n";
    s += "uniform sampler2D uDiffuseTex;\n";
    s += "uniform float     uHasTexture;\n";   // 1.0 = sample tex, 0.0 = use color
    s += "uniform vec4      uDiffuseColor;\n";
    s += "uniform vec3      uLightDir;\n";
    s += "uniform vec3      uLightColor;\n";
    s += "uniform float     uLightType;\n";
    s += "uniform vec3      uCameraPos;\n";
    s += kFragOut;
    s += "void main() {\n";
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
    s += "    vec4 base = (uHasTexture > 0.5) ? texture2D(uDiffuseTex, vTexCoord) : uDiffuseColor;\n";
#else
    s += "    vec4 base = (uHasTexture > 0.5) ? texture(uDiffuseTex, vTexCoord) : uDiffuseColor;\n";
#endif
    s += "    vec3 N = normalize(vNormal);\n";
    s += "    vec3 V = normalize(uCameraPos - vWorldPos);\n";
    s += "    vec3 ambient = 0.2 * base.rgb;\n";
    s += "    vec3 color = ambient;\n";
    s += "    if (uLightType > 0.5) {\n";
    s += "        vec3 L = normalize(-uLightDir);\n";
    s += "        float diff = max(dot(N, L), 0.0);\n";
    s += "        vec3 H = normalize(L + V);\n";
    s += "        float spec = pow(max(dot(N, H), 0.0), 32.0);\n";
    s += "        color = ambient + base.rgb * uLightColor * diff + 0.3 * uLightColor * spec;\n";
    s += "    }\n";
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
    s += "    gl_FragColor = vec4(color, base.a);\n";
#else
    s += "    FragColor = vec4(color, base.a);\n";
#endif
    s += "}\n";
    return s;
}

// Camera slot — placeholder, not used for drawing.
static std::string cameraPlaceholderVert() {
    std::string s = kVersion;
    s += kAttr; s += " vec3 aPos;\n";
    s += kAttr; s += " vec3 aNormal;\n";
    s += "uniform mat4 uModel;\n";
    s += "uniform mat4 uView;\n";
    s += "uniform mat4 uProj;\n";
    s += "void main() {\n";
    s += "    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);\n";
    s += "}\n";
    return s;
}

static std::string cameraPlaceholderFrag() {
    std::string s = kVersion;
    s += kFragOut;
    s += "void main() {\n";
#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
    s += "    gl_FragColor = vec4(0.2, 0.6, 1.0, 1.0);\n";
#else
    s += "    FragColor = vec4(0.2, 0.6, 1.0, 1.0);\n";
#endif
    s += "}\n";
    return s;
}

// ---------------------------------------------------------------------------

void Scene::destroy() {
    auto cleanup = [](ShaderBundle& sb) {
        if (sb.program) { glDeleteProgram(sb.program); sb.program = 0; sb.ok = false; }
    };
    cleanup(groundShader);
    cleanup(cubeShader);
    cleanup(cameraShader);
    cleanup(lightShader);
    cleanup(modelShader);
}

bool Scene::initDefaultShaders() {
    groundShader.vertSrc  = defaultGroundVert();
    groundShader.fragSrc  = defaultGroundFrag();
    cubeShader.vertSrc    = defaultCubeVert();
    cubeShader.fragSrc    = defaultCubeFrag();
    cameraShader.vertSrc  = cameraPlaceholderVert();
    cameraShader.fragSrc  = cameraPlaceholderFrag();
    lightShader.vertSrc   = emissiveVert();
    lightShader.fragSrc   = emissiveFrag();
    modelShader.vertSrc   = modelVert();
    modelShader.fragSrc   = modelFrag();

    auto compile = [](ShaderBundle& sb) {
        bool ok = compileShaderProgram(sb.vertSrc, sb.fragSrc, sb.program, sb.log);
        sb.ok = ok;
        return ok;
    };

    compile(groundShader);
    bool okCube = compile(cubeShader);
    compile(cameraShader);
    compile(lightShader);
    bool okModel = compile(modelShader);

    if (!okCube)
        std::fprintf(stderr, "Object shader failed:\n%s\n", cubeShader.log.c_str());
    if (!okModel)
        std::fprintf(stderr, "Model shader failed:\n%s\n", modelShader.log.c_str());
    return okCube && okModel;
}

// ---------------------------------------------------------------------------
// Fallback view/proj from editor camera uniforms
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
    outProj = glm::perspective(glm::radians(camera.fovDeg), aspect, 0.1f, 500.0f);
}
