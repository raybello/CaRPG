#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include "ecs/components.h"
#include "game/item_catalog.h"
#include "game/game_events.h"

#include <SDL.h>
#if defined(__EMSCRIPTEN__)
#  include <emscripten.h>
#  include <emscripten/html5.h>
#  include <SDL_opengles2.h>
#endif
// GL symbols are available transitively through app.h → scene.h → shader.h.

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
App::App() = default;

App::~App() {
    shutdown();
}

bool App::initWindow() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    const char* glslVersion = "#version 100";
#elif defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    const char* glslVersion = "#version 150";
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    const char* glslVersion = "#version 130";
#endif

    SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    window_ = SDL_CreateWindow(title_.c_str(),
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               winW_, winH_, flags);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    glctx_ = SDL_GL_CreateContext(window_);
    if (!glctx_) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, glctx_);
#if !defined(__EMSCRIPTEN__)
    SDL_GL_SetSwapInterval(1); // vsync
#endif

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window_, glctx_);
    ImGui_ImplOpenGL3_Init(glslVersion);

    return true;
}

bool App::initImGui() {
    // ImGui already initialized inside initWindow() — this exists so the
    // main entry point can keep the conventional 3-step init.
    return true;
}

bool App::initScene() {
    if (!renderer_.init()) return false;
    if (!scene_.initDefaultShaders()) {
        std::fprintf(stderr, "WARNING: cube shader failed to compile on init\n");
    }
    renderer_.resize(800, 600);

    // Load item definitions from LOOT.json
    if (!ItemCatalog::loadFromFile("LOOT.json"))
        std::fprintf(stderr, "WARNING: LOOT.json failed to load — items unavailable\n");

    return initGame();
}

bool App::initGame() {
    initGameEntities();
    connectEventListeners();
    return true;
}

void App::initGameEntities() {
    // Player car entity
    playerEntity_ = registry_.create();
    registry_.emplace<PlayerTag>(playerEntity_);
    registry_.emplace<Transform>(playerEntity_);
    registry_.emplace<Velocity>(playerEntity_);
    registry_.emplace<CarInput>(playerEntity_);
    registry_.emplace<BaseCarStats>(playerEntity_);
    registry_.emplace<DerivedCarStats>(playerEntity_);
    registry_.emplace<Fuel>(playerEntity_);
    registry_.emplace<MeshRef>(playerEntity_, MeshId::CarBody);
    registry_.emplace<Material>(playerEntity_, scene_.cubeShader.program,
                                glm::vec3(0.8f, 0.15f, 0.1f));
    registry_.emplace<Inventory>(playerEntity_);

    // Chase camera entity
    auto camEnt = registry_.create();
    registry_.emplace<CameraState>(camEnt);
    registry_.emplace<CameraFollow>(camEnt, playerEntity_);

    // Ground entity
    auto ground = registry_.create();
    {
        Transform tf;
        tf.scale = glm::vec3(scene_.ground.halfSize * 2.0f, 1.0f, scene_.ground.halfSize * 2.0f);
        registry_.emplace<Transform>(ground, tf);
    }
    registry_.emplace<MeshRef>(ground, MeshId::Ground);
    registry_.emplace<Material>(ground, scene_.groundShader.program, scene_.ground.color);

    // Directional light entity
    auto lightEnt = registry_.create();
    registry_.emplace<DirectionalLight>(lightEnt,
        scene_.light.direction, scene_.light.color, scene_.light.position, scene_.light.ambient);

    // Spawn a handful of world items for testing
    spawnWorldItem(ItemId::NitroBoost,       glm::vec3( 5.0f, 0.0f,  0.0f));
    spawnWorldItem(ItemId::FuelCanister,     glm::vec3(-8.0f, 0.0f,  3.0f));
    spawnWorldItem(ItemId::TurboWheels,      glm::vec3( 0.0f, 0.0f,-10.0f));
    spawnWorldItem(ItemId::EnginePart,       glm::vec3( 4.0f, 0.0f,  6.0f));
    spawnWorldItem(ItemId::RoadSignFragment, glm::vec3(-4.0f, 0.0f, -5.0f));
    spawnWorldItem(ItemId::OverchargeCell,   glm::vec3( 9.0f, 0.0f, -3.0f));
}

void App::spawnWorldItem(ItemId id, const glm::vec3& pos) {
    const ItemDef* def = getItemDef(id);
    auto ent = registry_.create();
    registry_.emplace<ItemWorldTag>(ent);
    Transform tf; tf.position = pos; tf.scale = glm::vec3(0.4f);
    registry_.emplace<Transform>(ent, tf);
    MeshId mesh = def ? def->meshId : MeshId::Cube;
    registry_.emplace<MeshRef>(ent, mesh);
    glm::vec3 col = def ? def->color : glm::vec3(1.0f);
    registry_.emplace<Material>(ent, scene_.cubeShader.program, col);
    registry_.emplace<ItemComponent>(ent, id, 1u,
                                     def ? def->maxStack : 1u);
}

void App::connectEventListeners() {
    dispatcher_.sink<GearEquippedEvent>()
        .connect<&StatSystem::onGearEquipped>(statSys_);
    dispatcher_.sink<GearUnequippedEvent>()
        .connect<&StatSystem::onGearUnequipped>(statSys_);
    dispatcher_.sink<LowFuelEvent>()
        .connect<&HUD::onLowFuel>(hud_);
    dispatcher_.sink<FuelDepletedEvent>()
        .connect<&HUD::onFuelDepleted>(hud_);
    dispatcher_.sink<ItemPickedUpEvent>()
        .connect<&InventoryPanel::onItemPickedUp>(inventoryPanel_);
    dispatcher_.sink<GearEquippedEvent>()
        .connect<&InventoryPanel::onGearEquipped>(inventoryPanel_);
}

void App::tickSystems(float dt) {
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    inputSys_.update(registry_, keys);
    statSys_.update(registry_);
    physicsSys_.update(registry_, dt);
    fuelSys_.update(registry_, dispatcher_, dt);
    itemSys_.update(registry_, dispatcher_, dt);
    cameraSys_.update(registry_, dt, vpW_, vpH_);
    dispatcher_.update();
}

void App::drawGameUI() {
    hud_.draw(registry_, playerEntity_, vpW_, vpH_);
    inventoryPanel_.draw(registry_, playerEntity_, itemSys_, dispatcher_);
}

void App::syncShaderProgramsToRegistry() {
    registry_.view<MeshRef, Material>().each([&](const MeshRef& mr, Material& mat) {
        switch (mr.meshId) {
            case MeshId::Ground:
                mat.program = scene_.groundShader.ok ? scene_.groundShader.program : 0;
                break;
            case MeshId::Cube:
            case MeshId::CarBody:
            case MeshId::CarWheel:
            case MeshId::Sphere:
            default:
                mat.program = scene_.cubeShader.ok ? scene_.cubeShader.program : 0;
                break;
        }
    });
}

// ---------------------------------------------------------------------------
// Editor helpers
// ---------------------------------------------------------------------------
void App::openShaderEditorFor(SceneItem item) {
    editorTargetItem_ = item;
    ShaderBundle& sb  = scene_.shaderFor(item);
    editorVertBuf_    = sb.vertSrc;
    editorFragBuf_    = sb.fragSrc;
    showShaderPopup_  = true;
}

void App::recompileEdited() {
    ShaderBundle& sb = scene_.shaderFor(editorTargetItem_);
    // ImGui writes directly into the std::string's internal buffer but never
    // updates size().  Resize to strlen so the copy below carries only the
    // actual text, not stale bytes (which would embed a null mid-source and
    // cause the shader compiler to report "premature EOF").
    editorVertBuf_.resize(strlen(editorVertBuf_.data()));
    editorFragBuf_.resize(strlen(editorFragBuf_.data()));
    sb.vertSrc = editorVertBuf_;
    sb.fragSrc = editorFragBuf_;

    GLuint newProg = 0;
    std::string log;
    bool ok = compileShaderProgram(sb.vertSrc, sb.fragSrc, newProg, log);
    if (ok) {
        if (sb.program) glDeleteProgram(sb.program);
        sb.program = newProg;
        sb.ok      = true;
        sb.log     = "Compiled OK\n";
    } else {
        sb.ok      = false;
        sb.log     = log;
    }
    // Keep ECS Material components in sync with the newly compiled programs
    syncShaderProgramsToRegistry();
}

// ---------------------------------------------------------------------------
// UI: side panel
// ---------------------------------------------------------------------------
void App::drawPanel() {
    if (!showPanel_) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(360.0f, vp->WorkSize.y));
    ImGui::Begin("Inspector (press 'p' to toggle)", &showPanel_,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse);

    // ---- Scene list ------------------------------------------------
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < (int)SceneItem::Count; ++i) {
            const bool selected = (int)selectedItem_ == i;
            if (ImGui::Selectable(sceneItemName((SceneItem)i), selected))
                selectedItem_ = (SceneItem)i;
        }
    }

    ImGui::Separator();

    // ---- Per-item uniforms ----------------------------------------
    if (ImGui::CollapsingHeader("Uniforms", ImGuiTreeNodeFlags_DefaultOpen)) {
        switch (selectedItem_) {
            case SceneItem::Ground: {
                ImGui::TextDisabled("Appearance");
                ImGui::ColorEdit3("Color",    &scene_.ground.color.x);
                ImGui::SliderFloat("Half Size", &scene_.ground.halfSize, 1.0f, 50.0f);
                break;
            }
            case SceneItem::Cube: {
                ImGui::TextDisabled("Transform");
                ImGui::SliderFloat3("Position", &scene_.cube.position.x, -5.0f, 5.0f);
                ImGui::SliderFloat3("Rotation (deg)", &scene_.cube.rotationDeg.x, -180.0f, 180.0f);
                ImGui::SliderFloat3("Scale",    &scene_.cube.scale.x,     0.05f, 5.0f);
                ImGui::Separator();
                ImGui::TextDisabled("Material");
                ImGui::ColorEdit3("Lighting Color", &scene_.cube.lightingColor.x);
                break;
            }
            case SceneItem::Camera: {
                ImGui::SliderFloat ("FOV (deg)",   &scene_.camera.fovDeg, 10.0f, 120.0f);
                ImGui::SliderFloat3("Position",    &scene_.camera.position.x, -10.0f, 10.0f);
                ImGui::SliderFloat3("Rotation",    &scene_.camera.rotationDeg.x, -180.0f, 180.0f);
                break;
            }
            case SceneItem::Light: {
                ImGui::Checkbox("Directional",  &scene_.light.directional);
                ImGui::SameLine();
                ImGui::Checkbox("Ambient",      &scene_.light.ambient);
                ImGui::SliderFloat3("Direction", &scene_.light.direction.x, -1.0f, 1.0f);
                ImGui::SliderFloat3("Position",  &scene_.light.position.x,  -10.0f, 10.0f);
                ImGui::ColorEdit3 ("Color",      &scene_.light.color.x);
                break;
            }
            default: break;
        }
    }

    ImGui::Separator();

    // ---- Shader editor entry --------------------------------------
    if (ImGui::CollapsingHeader("Shader", ImGuiTreeNodeFlags_DefaultOpen)) {
        ShaderBundle& sb = scene_.shaderFor(selectedItem_);
        ImGui::Text("Status: %s", sb.ok ? "OK" : "ERROR");
        if (!sb.ok && !sb.log.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.4f, 0.4f, 1));
            ImGui::TextWrapped("%s", sb.log.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::Button("Edit Shader Source...")) {
            openShaderEditorFor(selectedItem_);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(opens popup)");
    }

    ImGui::Separator();
    ImGui::TextDisabled("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::End();
}

// ---------------------------------------------------------------------------
// UI: shader editor popup
// ---------------------------------------------------------------------------
void App::drawShaderPopup() {
    if (!showShaderPopup_) return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Shader Editor", &showShaderPopup_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Editing %s shaders", sceneItemName(editorTargetItem_));
        ImGui::SameLine();
        if (ImGui::Button("Save & Recompile")) {
            recompileEdited();
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            ShaderBundle& sb = scene_.shaderFor(editorTargetItem_);
            editorVertBuf_   = sb.vertSrc;
            editorFragBuf_   = sb.fragSrc;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            showShaderPopup_ = false;
        }

        ShaderBundle& sb = scene_.shaderFor(editorTargetItem_);
        if (sb.ok) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Compiled OK");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Compile error:");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.6f, 1));
            ImGui::InputTextMultiline("##log",
                                      (char*)sb.log.c_str(),
                                      sb.log.size() + 1,
                                      ImVec2(-1, 60),
                                      ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();
        }

        // Two side-by-side text editors for vertex / fragment
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float halfH  = avail.y * 0.5f - 12.0f;

        ImGui::TextDisabled("Vertex shader");
        // Resize the std::string buffer to leave room for typing
        editorVertBuf_.reserve(editorVertBuf_.size() + 4096);
        ImGui::InputTextMultiline("##vert",
                                  &editorVertBuf_[0],
                                  editorVertBuf_.capacity(),
                                  ImVec2(-1, halfH),
                                  ImGuiInputTextFlags_AllowTabInput,
                                  [](ImGuiInputTextCallbackData* data) -> int {
                                      // Resize callback — keep std::string sized to actual content
                                      auto* str = static_cast<std::string*>(data->UserData);
                                      if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                          str->resize(data->BufTextLen);
                                          data->Buf = (char*)str->data();
                                      }
                                      return 0;
                                  },
                                  &editorVertBuf_);

        ImGui::TextDisabled("Fragment shader");
        editorFragBuf_.reserve(editorFragBuf_.size() + 4096);
        ImGui::InputTextMultiline("##frag",
                                  &editorFragBuf_[0],
                                  editorFragBuf_.capacity(),
                                  ImVec2(-1, halfH),
                                  ImGuiInputTextFlags_AllowTabInput,
                                  [](ImGuiInputTextCallbackData* data) -> int {
                                      auto* str = static_cast<std::string*>(data->UserData);
                                      if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                          str->resize(data->BufTextLen);
                                          data->Buf = (char*)str->data();
                                      }
                                      return 0;
                                  },
                                  &editorFragBuf_);
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// One frame
// ---------------------------------------------------------------------------
bool App::frame() {
    // Compute delta time
    static Uint64 lastTick = SDL_GetPerformanceCounter();
    Uint64 now = SDL_GetPerformanceCounter();
    float dt = (float)(now - lastTick) / (float)SDL_GetPerformanceFrequency();
    dt = std::min(dt, 0.05f);  // cap at 50 ms to avoid spiral of death
    lastTick = now;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        if (e.type == SDL_QUIT) running_ = false;
        if (e.type == SDL_WINDOWEVENT &&
            e.window.event == SDL_WINDOWEVENT_CLOSE &&
            e.window.windowID == SDL_GetWindowID(window_)) {
            running_ = false;
        }
        if (e.type == SDL_KEYDOWN && !ImGui::GetIO().WantTextInput) {
            if (e.key.keysym.sym == SDLK_p)
                showPanel_ = !showPanel_;
            if (e.key.keysym.sym == SDLK_i)
                inventoryPanel_.visible = !inventoryPanel_.visible;
        }
    }

    // Tick game systems
    if (gameRunning_) tickSystems(dt);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // ----- 3D viewport — locked fullscreen behind the inspector panel ------
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport##bg", nullptr,
                     ImGuiWindowFlags_NoTitleBar          |
                     ImGuiWindowFlags_NoResize            |
                     ImGuiWindowFlags_NoMove              |
                     ImGuiWindowFlags_NoScrollbar         |
                     ImGuiWindowFlags_NoScrollWithMouse   |
                     ImGuiWindowFlags_NoCollapse          |
                     ImGuiWindowFlags_NoSavedSettings     |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();

        ImVec2 sz = ImGui::GetContentRegionAvail();
        vpW_ = (int)sz.x; if (vpW_ < 1) vpW_ = 1;
        vpH_ = (int)sz.y; if (vpH_ < 1) vpH_ = 1;
        renderer_.resize(vpW_, vpH_);

        // ECS-driven render
        GLuint tex = renderSys_.render(registry_, renderer_, scene_);

        // V-flip: ImGui's UV origin is top-left; OpenGL FBO origin is bottom-left.
        ImGui::Image((ImTextureID)(intptr_t)tex,
                     ImVec2((float)vpW_, (float)vpH_),
                     ImVec2(0, 1), ImVec2(1, 0));
        ImGui::End();
    }

    drawPanel();
    drawShaderPopup();
    drawGameUI();

    // ----- Render --------------------------------------------------------
    ImGui::Render();
    int dispW, dispH;
    SDL_GL_GetDrawableSize(window_, &dispW, &dispH);
    glViewport(0, 0, dispW, dispH);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);

    return running_;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void App::mainLoopThunk(void* userdata) {
    auto* self = static_cast<App*>(userdata);
    self->frame();
}

void App::run() {
#if defined(__EMSCRIPTEN__)
    // The emscripten main loop owns the call stack — never returns.
    emscripten_set_main_loop_arg(&App::mainLoopThunk, this, 0, 1);
#else
    while (running_) {
        if (!frame()) break;
    }
#endif
}

void App::shutdown() {
    if (glctx_) {
        // Destroy GL resources while the context is still alive.
        // The member destructors run after ~App() returns, by which point
        // the context is gone — calling GL functions then causes a Bus Error.
        renderer_.destroy();
        scene_.destroy();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DeleteContext(glctx_);
        glctx_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}
