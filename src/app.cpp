#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include "ecs/components.h"
#include "game/item_catalog.h"
#include "game/game_events.h"

#include <SDL.h>
#if defined(__EMSCRIPTEN__)
#  include <emscripten.h>
#  include <emscripten/html5.h>
#  include <SDL_opengles2.h>
#endif

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "ImGuizmo.h"

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
App::App() = default;
App::~App() { shutdown(); }

bool App::initWindow() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,  8);

#if defined(IMGUI_IMPL_OPENGL_ES2) || defined(__EMSCRIPTEN__)
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

    SDL_WindowFlags flags = (SDL_WindowFlags)(
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
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
    SDL_GL_SetSwapInterval(1);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(window_, glctx_);
    ImGui_ImplOpenGL3_Init(glslVersion);

    return true;
}

bool App::initImGui() { return true; }

bool App::initScene() {
    if (!renderer_.init()) return false;
    if (!scene_.initDefaultShaders())
        std::fprintf(stderr, "WARNING: object shader failed on init\n");
    renderer_.resize(800, 600);

    if (!ItemCatalog::loadFromFile("LOOT.json"))
        std::fprintf(stderr, "WARNING: LOOT.json failed to load\n");

    return initGame();
}

bool App::initGame() {
    initGameEntities();
    connectEventListeners();
    return true;
}

// ---------------------------------------------------------------------------
// Entity creation
// ---------------------------------------------------------------------------
void App::initGameEntities() {
    // --- Player car ---
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
    {
        // Player car: kinematic rigid body. The car's bicycle controller
        // owns its motion; the rigid body just transfers momentum into
        // dynamic items it bumps into.
        BoxCollider bc; bc.halfExtents = glm::vec3(1.0f, 0.5f, 2.0f);
        registry_.emplace<BoxCollider>(playerEntity_, bc);

        RigidBody rb;
        rb.mass            = 1500.0f;     // arcade car mass
        rb.inverseMass     = 0.0f;        // kinematic → no impulse pushback
        rb.kinematic       = true;
        rb.useGravity      = false;
        rb.invInertiaLocal = glm::mat3(0.0f);
        rb.restitution     = 0.15f;
        rb.friction        = 0.7f;
        registry_.emplace<RigidBody>(playerEntity_, rb);
    }

    // --- Chase / free-look camera ---
    cameraEcsEntity_ = registry_.create();
    registry_.emplace<CameraTag>(cameraEcsEntity_);
    registry_.emplace<CameraState>(cameraEcsEntity_);
    registry_.emplace<CameraFollow>(cameraEcsEntity_, playerEntity_);

    // --- Ground ---
    auto ground = registry_.create();
    {
        Transform tf;
        tf.scale = glm::vec3(50.0f, 1.0f, 50.0f); // 50×50 world units
        registry_.emplace<Transform>(ground, tf);
    }
    registry_.emplace<MeshRef>(ground, MeshId::Ground);
    registry_.emplace<Material>(ground, scene_.groundShader.program,
                                glm::vec3(0.28f, 0.30f, 0.26f));
    {
        // Ground: infinite plane at y = 0, fixed body.
        PlaneCollider pc; pc.normal = glm::vec3(0.0f, 1.0f, 0.0f); pc.d = 0.0f;
        registry_.emplace<PlaneCollider>(ground, pc);
        RigidBody rb;
        rb.mass            = 0.0f;
        rb.inverseMass     = 0.0f;
        rb.fixed           = true;
        rb.useGravity      = false;
        rb.invInertiaLocal = glm::mat3(0.0f);
        rb.restitution     = 0.2f;
        rb.friction        = 0.8f;
        registry_.emplace<RigidBody>(ground, rb);
    }

    // --- Directional light — rendered as a colored sphere ---
    lightEntity_ = registry_.create();
    registry_.emplace<DirectionalLight>(lightEntity_);   // defaults: dir, white, pos (2,3,2)
    {
        Transform tf;
        tf.position = glm::vec3(2.0f, 3.0f, 2.0f);
        tf.scale    = glm::vec3(0.3f);
        registry_.emplace<Transform>(lightEntity_, tf);
    }
    registry_.emplace<MeshRef>(lightEntity_, MeshId::Sphere);
    // Color synced from DirectionalLight.color each frame in frame()
    registry_.emplace<Material>(lightEntity_, scene_.lightShader.program,
                                glm::vec3(1.0f, 1.0f, 1.0f));

    // --- World items ---
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
    Transform tf;
    // Spawn slightly above the ground so they drop and bounce on first frame
    // — visible signal that physics is running.
    tf.position = pos + glm::vec3(0.0f, 1.2f, 0.0f);
    tf.scale    = glm::vec3(0.4f);
    registry_.emplace<Transform>(ent, tf);
    MeshId mesh = def ? def->meshId : MeshId::Cube;
    registry_.emplace<MeshRef>(ent, mesh);
    glm::vec3 col = def ? def->color : glm::vec3(1.0f);
    registry_.emplace<Material>(ent, scene_.cubeShader.program, col);
    registry_.emplace<ItemComponent>(ent, id, 1u, def ? def->maxStack : 1u);

    // Dynamic rigid body. Half-extents follow the entity's scale so the
    // collider matches the rendered cube/sphere mesh (both meshes are
    // unit-sized, scaled by Transform).
    glm::vec3 he = tf.scale * 0.5f;
    BoxCollider bc; bc.halfExtents = he;
    registry_.emplace<BoxCollider>(ent, bc);

    RigidBody rb;
    rb.mass            = 1.0f;
    rb.inverseMass     = 1.0f / rb.mass;
    rb.invInertiaLocal = RigidBodySystem::boxInvInertia(rb.mass, he);
    rb.restitution     = 0.45f;
    rb.friction        = 0.55f;
    rb.linearDamping   = 0.20f;
    rb.angularDamping  = 0.40f;
    rb.useGravity      = true;
    registry_.emplace<RigidBody>(ent, rb);
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

// ---------------------------------------------------------------------------
// Per-frame systems
// ---------------------------------------------------------------------------
void App::tickSystems(float dt) {
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    inputSys_.update(registry_, keys);
    statSys_.update(registry_);
    physicsSys_.update(registry_, dt);

    // Bridge the kinematic car into the rigid body world: copy its current
    // world-space velocity onto its RigidBody so collisions transfer momentum
    // into items the car bumps. The car itself is kinematic (inverseMass=0)
    // so the rigid body system never moves it back.
    if (registry_.valid(playerEntity_) &&
        registry_.all_of<Velocity, RigidBody>(playerEntity_)) {
        const auto& v  = registry_.get<Velocity>(playerEntity_);
        auto&       rb = registry_.get<RigidBody>(playerEntity_);
        rb.linearVel  = v.linear;
        rb.angularVel = glm::vec3(0.0f, glm::radians(v.angularY), 0.0f);
    }

    rigidBodySys_.update(registry_, dt);

    fuelSys_.update(registry_, dispatcher_, dt);
    itemSys_.update(registry_, dispatcher_, dt);
    cameraSys_.update(registry_, dt, vpW_, vpH_);
    dispatcher_.update();
}

void App::drawGameUI() {
    inventoryPanel_.draw(registry_, playerEntity_, itemSys_, dispatcher_);
    hud_.draw(registry_, playerEntity_, vpW_, vpH_);
}

void App::syncShaderProgramsToRegistry() {
    registry_.view<MeshRef, Material>().each([&](const MeshRef& mr, Material& mat) {
        switch (mr.meshId) {
            case MeshId::Ground:
                mat.program = scene_.groundShader.ok ? scene_.groundShader.program : 0;
                break;
            case MeshId::Sphere:
                mat.program = scene_.lightShader.ok  ? scene_.lightShader.program  : 0;
                break;
            default:
                mat.program = scene_.cubeShader.ok   ? scene_.cubeShader.program   : 0;
                break;
        }
    });
}

// ---------------------------------------------------------------------------
// Entity helpers
// ---------------------------------------------------------------------------
ShaderSlot App::slotForEntity(entt::entity e) const {
    if (e == cameraEcsEntity_) return ShaderSlot::Camera;
    if (registry_.all_of<DirectionalLight>(e)) return ShaderSlot::Light;
    if (registry_.all_of<MeshRef>(e)) {
        if (registry_.get<MeshRef>(e).meshId == MeshId::Ground)
            return ShaderSlot::Ground;
    }
    return ShaderSlot::Object;
}

std::string App::entityDisplayName(entt::entity e) const {
    if (e == cameraEcsEntity_)
        return "Camera";
    if (registry_.all_of<PlayerTag>(e))
        return "Player";
    if (registry_.all_of<DirectionalLight>(e))
        return "Light";
    if (registry_.all_of<ItemWorldTag>(e)) {
        if (registry_.all_of<ItemComponent>(e)) {
            const auto& ic  = registry_.get<ItemComponent>(e);
            const ItemDef* def = getItemDef(ic.itemId);
            if (def) return std::string(def->name);
        }
        return "Item";
    }
    if (registry_.all_of<MeshRef>(e)) {
        if (registry_.get<MeshRef>(e).meshId == MeshId::Ground)
            return "Ground";
    }
    return "Entity";
}

void App::openShaderEditorFor(ShaderSlot slot) {
    editorTargetSlot_ = slot;
    ShaderBundle& sb  = scene_.shaderFor(slot);
    editorVertBuf_    = sb.vertSrc;
    editorFragBuf_    = sb.fragSrc;
    showShaderPopup_  = true;
}

void App::recompileEdited() {
    ShaderBundle& sb = scene_.shaderFor(editorTargetSlot_);
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
        sb.ok  = false;
        sb.log = log;
    }
    syncShaderProgramsToRegistry();
}

// ---------------------------------------------------------------------------
// UI: side panel (ECS entity list)
// ---------------------------------------------------------------------------
void App::drawPanel() {
    if (!showPanel_) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(360.0f, vp->WorkSize.y));
    ImGui::Begin("Inspector (P = toggle)", &showPanel_,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse);

    // ---- Scene entity list -----------------------------------------------
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {

        // Collect all entities that should appear in the list, in a stable order.
        // We walk the registry and bucket by category.
        std::vector<entt::entity> listed;

        // 1. Camera always first
        if (registry_.valid(cameraEcsEntity_))
            listed.push_back(cameraEcsEntity_);

        // 2. Player
        for (auto e : registry_.view<PlayerTag>())
            listed.push_back(e);

        // 3. Ground
        for (auto e : registry_.view<MeshRef>())
            if (registry_.get<MeshRef>(e).meshId == MeshId::Ground)
                listed.push_back(e);

        // 4. Light
        for (auto e : registry_.view<DirectionalLight>())
            if (!registry_.all_of<PlayerTag>(e))
                listed.push_back(e);

        // 5. World items
        for (auto e : registry_.view<ItemWorldTag>())
            listed.push_back(e);

        for (entt::entity e : listed) {
            const bool selected = (e == selectedEntity_);
            std::string name = entityDisplayName(e);
            ImGui::PushID((int)(uint32_t)e);
            if (ImGui::Selectable(name.c_str(), selected))
                selectedEntity_ = e;
            ImGui::PopID();
        }
    }

    ImGui::Separator();

    // ---- Per-entity uniforms --------------------------------------------
    if (ImGui::CollapsingHeader("Uniforms", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!registry_.valid(selectedEntity_)) {
            ImGui::TextDisabled("(select an entity above)");
        }
        else if (selectedEntity_ == cameraEcsEntity_) {
            // Camera — show live state
            ImGui::TextDisabled("Chase Camera");
            auto* cam = registry_.try_get<CameraState>(cameraEcsEntity_);
            if (cam) {
                ImGui::SliderFloat("FOV (deg)", &cam->fovDeg, 10.0f, 120.0f);
                ImGui::TextDisabled("Position (follows car)");
                ImGui::Text("  %.2f, %.2f, %.2f",
                            cam->position.x, cam->position.y, cam->position.z);
                ImGui::TextDisabled("Forward (car direction)");
                ImGui::Text("  %.2f, %.2f, %.2f",
                            cam->forward.x, cam->forward.y, cam->forward.z);
            }
            auto* follow = registry_.try_get<CameraFollow>(cameraEcsEntity_);
            if (follow) {
                ImGui::Separator();
                ImGui::TextDisabled("Follow offset");
                ImGui::SliderFloat("Height",   &follow->offset.y, 0.5f, 20.0f);
                ImGui::SliderFloat("Distance", &follow->offset.z, 1.0f, 30.0f);
                ImGui::SliderFloat("Lag",      &follow->lag,      1.0f, 20.0f);
            }
        }
        else if (registry_.all_of<DirectionalLight>(selectedEntity_)) {
            auto& dl = registry_.get<DirectionalLight>(selectedEntity_);

            // Light type selector
            ImGui::TextDisabled("Light Type");
            int typeIdx = (int)dl.type;
            ImGui::RadioButton("Ambient",     &typeIdx, 0); ImGui::SameLine();
            ImGui::RadioButton("Directional", &typeIdx, 1); ImGui::SameLine();
            ImGui::RadioButton("Spot",        &typeIdx, 2);
            dl.type = (LightType)typeIdx;

            ImGui::Separator();
            ImGui::ColorEdit3("Color",     &dl.color.x);
            ImGui::SliderFloat3("Position",&dl.position.x, -20.0f, 20.0f);

            if (dl.type == LightType::Directional || dl.type == LightType::Spot) {
                ImGui::SliderFloat3("Direction", &dl.direction.x, -1.0f, 1.0f);
            }
            if (dl.type == LightType::Spot) {
                ImGui::SliderFloat("Spot Cutoff (deg)", &dl.spotCutoff, 1.0f, 89.0f);
            }

            // Keep light Transform position in sync with DirectionalLight.position
            if (auto* tf = registry_.try_get<Transform>(selectedEntity_))
                tf->position = dl.position;
        }
        else {
            // Generic mesh entity: Transform + Material
            if (auto* tf = registry_.try_get<Transform>(selectedEntity_)) {
                ImGui::TextDisabled("Transform");

                // Gizmo operation selector
                if (ImGui::RadioButton("Translate", gizmoOperation_ == ImGuizmo::TRANSLATE))
                    gizmoOperation_ = ImGuizmo::TRANSLATE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Rotate",    gizmoOperation_ == ImGuizmo::ROTATE))
                    gizmoOperation_ = ImGuizmo::ROTATE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Scale",     gizmoOperation_ == ImGuizmo::SCALE))
                    gizmoOperation_ = ImGuizmo::SCALE;

                if (gizmoOperation_ != ImGuizmo::SCALE) {
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Local", gizmoMode_ == ImGuizmo::LOCAL))
                        gizmoMode_ = ImGuizmo::LOCAL;
                    ImGui::SameLine();
                    if (ImGui::RadioButton("World", gizmoMode_ == ImGuizmo::WORLD))
                        gizmoMode_ = ImGuizmo::WORLD;
                }

                ImGui::Checkbox("Snap", &useGizmoSnap_);

                ImGui::SliderFloat3("Position", &tf->position.x, -50.0f, 50.0f);

                // Convert quaternion → Euler for display, then write back
                glm::vec3 euler = glm::degrees(glm::eulerAngles(tf->rotation));
                if (ImGui::SliderFloat3("Rotation (deg)", &euler.x, -180.0f, 180.0f))
                    tf->rotation = glm::quat(glm::radians(euler));

                ImGui::SliderFloat3("Scale", &tf->scale.x, 0.01f, 20.0f);
            }
            if (auto* mat = registry_.try_get<Material>(selectedEntity_)) {
                ImGui::Separator();
                ImGui::TextDisabled("Material");
                ImGui::ColorEdit3("Color", &mat->color.x);
                ImGui::Checkbox("Visible", &mat->visible);
            }
            // Rigid body (live state + tunables)
            if (auto* rb = registry_.try_get<RigidBody>(selectedEntity_)) {
                ImGui::Separator();
                ImGui::TextDisabled("Rigid Body");
                const char* kind = rb->fixed     ? "fixed"
                                 : rb->kinematic ? "kinematic"
                                                 : "dynamic";
                ImGui::Text("Type: %s", kind);
                ImGui::Text("Mass: %.2f kg", rb->mass);
                ImGui::SliderFloat("Restitution", &rb->restitution, 0.0f, 1.0f);
                ImGui::SliderFloat("Friction",    &rb->friction,    0.0f, 2.0f);
                if (!rb->fixed && !rb->kinematic) {
                    ImGui::SliderFloat("Lin Damp", &rb->linearDamping,  0.0f, 5.0f);
                    ImGui::SliderFloat("Ang Damp", &rb->angularDamping, 0.0f, 5.0f);
                    ImGui::Checkbox("Gravity", &rb->useGravity);
                }
                ImGui::Text("v   %.2f, %.2f, %.2f",
                            rb->linearVel.x, rb->linearVel.y, rb->linearVel.z);
                ImGui::Text("ω   %.2f, %.2f, %.2f",
                            rb->angularVel.x, rb->angularVel.y, rb->angularVel.z);
                if (auto* bx = registry_.try_get<BoxCollider>(selectedEntity_)) {
                    ImGui::Text("Box he: %.2f, %.2f, %.2f",
                                bx->halfExtents.x, bx->halfExtents.y, bx->halfExtents.z);
                }
            }
            // Item info (read-only)
            if (registry_.all_of<ItemComponent>(selectedEntity_)) {
                const auto& ic  = registry_.get<ItemComponent>(selectedEntity_);
                const ItemDef* def = getItemDef(ic.itemId);
                ImGui::Separator();
                ImGui::TextDisabled("Item");
                ImGui::Text("Name:  %s", def ? def->name.c_str() : "?");
                ImGui::Text("Stack: %u / %u", ic.stackSize, ic.maxStack);
            }
        }
    }

    ImGui::Separator();

    // ---- Physics system settings ----------------------------------------
    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Throttle / Power");
        ImGui::SliderFloat("Power Scale",       &physicsSys_.powerScale,       0.01f, 2.0f);
        ImGui::SliderFloat("Throttle Response", &physicsSys_.throttleResponse, 0.1f,  10.0f);
        ImGui::SliderFloat("Max Accel",         &physicsSys_.maxAccel,         1.0f,  200.0f);

        ImGui::Separator();
        ImGui::TextDisabled("Brake / Reverse");
        ImGui::SliderFloat("Brake Sensitivity", &physicsSys_.brakeSensitivity, 10.0f,  500.0f);
        ImGui::SliderFloat("Reverse Power Mul", &physicsSys_.reversePowerMul,  0.1f,   2.0f);
        ImGui::SliderFloat("Reverse Speed Mul", &physicsSys_.reverseSpeedMul,  0.1f,   2.0f);
        ImGui::SliderFloat("Engine Brake",      &physicsSys_.engineBrake,      0.0f,   10.0f);

        ImGui::Separator();
        ImGui::TextDisabled("Friction");
        ImGui::SliderFloat("Drag",              &physicsSys_.drag,             0.0f,  10.0f);
        ImGui::SliderFloat("Lateral Friction",  &physicsSys_.lateralFriction,  0.1f,  50.0f);
        ImGui::SliderFloat("Handbrake Grip Mul",&physicsSys_.handbrakeGripMul, 0.01f,  1.0f);
        ImGui::SliderFloat("Handbrake Drag",    &physicsSys_.handbrakeDrag,    0.0f,  20.0f);

        ImGui::Separator();
        ImGui::TextDisabled("Steering");
        ImGui::SliderFloat("Steer Response",    &physicsSys_.steerResponse,    0.1f,  20.0f);
        ImGui::SliderFloat("Max Steer (deg)",   &physicsSys_.maxSteerAngleRad, 0.05f,  1.5f);
        ImGui::SliderFloat("Wheelbase",         &physicsSys_.wheelbase,        0.5f,  10.0f);
        ImGui::SliderFloat("Pivot Min Speed",   &physicsSys_.pivotMinSpeed,    0.1f,  10.0f);
        ImGui::SliderFloat("Pivot Yaw Rate",    &physicsSys_.pivotYawRate,     0.1f,   5.0f);
    }

    ImGui::Separator();

    // ---- Rigid body physics --------------------------------------------
    if (ImGui::CollapsingHeader("Rigid Body Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat3("Gravity",      &rigidBodySys_.gravity.x,       -30.0f, 30.0f);
        ImGui::SliderInt   ("Max Substeps", &rigidBodySys_.maxSubsteps,      1, 16);
        ImGui::SliderFloat ("Fixed Step",   &rigidBodySys_.fixedStep,        1.0f/240.0f, 1.0f/30.0f);
        ImGui::SliderFloat ("Pos Bias",     &rigidBodySys_.positionalBias,   0.0f, 1.0f);
        ImGui::SliderFloat ("Pen Slop",     &rigidBodySys_.penetrationSlop,  0.0f, 0.05f);
    }

    ImGui::Separator();

    // ---- Shader editor entry -------------------------------------------
    if (ImGui::CollapsingHeader("Shader", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (registry_.valid(selectedEntity_)) {
            ShaderSlot slot     = slotForEntity(selectedEntity_);
            ShaderBundle& sb    = scene_.shaderFor(slot);

            const char* slotNames[] = { "Ground", "Object", "Camera", "Light" };
            ImGui::TextDisabled("Slot: %s", slotNames[(int)slot]);
            ImGui::Text("Status: %s", sb.ok ? "OK" : "ERROR");
            if (!sb.ok && !sb.log.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.4f, 0.4f, 1));
                ImGui::TextWrapped("%s", sb.log.c_str());
                ImGui::PopStyleColor();
            }
            if (ImGui::Button("Edit Shader Source..."))
                openShaderEditorFor(slot);
            ImGui::SameLine();
            ImGui::TextDisabled("(opens popup)");
        } else {
            ImGui::TextDisabled("(select an entity above)");
        }
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
        const char* slotNames[] = { "Ground", "Object", "Camera", "Light" };
        ImGui::Text("Editing: %s shader", slotNames[(int)editorTargetSlot_]);
        ImGui::SameLine();
        if (ImGui::Button("Save & Recompile")) recompileEdited();
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            ShaderBundle& sb = scene_.shaderFor(editorTargetSlot_);
            editorVertBuf_   = sb.vertSrc;
            editorFragBuf_   = sb.fragSrc;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) showShaderPopup_ = false;

        ShaderBundle& sb = scene_.shaderFor(editorTargetSlot_);
        if (sb.ok) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1), "Compiled OK");
        } else {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Compile error:");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.6f, 1));
            ImGui::InputTextMultiline("##log", (char*)sb.log.c_str(),
                                      sb.log.size() + 1,
                                      ImVec2(-1, 60),
                                      ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();
        }

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float  halfH = avail.y * 0.5f - 12.0f;

        auto textEditor = [](const char* label, std::string& buf, float h) {
            buf.reserve(buf.size() + 4096);
            ImGui::InputTextMultiline(label, &buf[0], buf.capacity(),
                                      ImVec2(-1, h),
                                      ImGuiInputTextFlags_AllowTabInput,
                                      [](ImGuiInputTextCallbackData* d) -> int {
                                          auto* s = (std::string*)d->UserData;
                                          if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                              s->resize(d->BufTextLen);
                                              d->Buf = (char*)s->data();
                                          }
                                          return 0;
                                      }, &buf);
        };

        ImGui::TextDisabled("Vertex shader");
        textEditor("##vert", editorVertBuf_, halfH);
        ImGui::TextDisabled("Fragment shader");
        textEditor("##frag", editorFragBuf_, halfH);
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// UI: ImGuizmo for selected entity transform
// ---------------------------------------------------------------------------
void App::drawGizmo() {
    if (!registry_.valid(selectedEntity_)) return;
    auto* tf = registry_.try_get<Transform>(selectedEntity_);
    if (!tf) return;

    auto* cam = registry_.try_get<CameraState>(cameraEcsEntity_);
    if (!cam) return;

    // GLM and ImGuizmo both use column-major matrix layout, so we can pass
    // glm matrices directly via value_ptr — no transpose needed.
    glm::mat4 model = tf->toMatrix();
    float matrix[16];
    std::memcpy(matrix, glm::value_ptr(model), sizeof(matrix));

    // Anchor the gizmo overlay to the actual rendered image rectangle so it
    // sits on top of the visible viewport regardless of where ImGui placed it.
    ImGuizmo::SetRect(vpScreenX_, vpScreenY_, (float)vpW_, (float)vpH_);

    float snap[3] = {0.0f, 0.0f, 0.0f};
    if (useGizmoSnap_) {
        switch (gizmoOperation_) {
            case ImGuizmo::TRANSLATE: snap[0] = snap[1] = snap[2] = 0.5f; break;
            case ImGuizmo::ROTATE:    snap[0] = 15.0f; break;
            case ImGuizmo::SCALE:     snap[0] = 0.25f; break;
            default: break;
        }
    }

    bool used = ImGuizmo::Manipulate(glm::value_ptr(cam->view),
                                     glm::value_ptr(cam->proj),
                                     gizmoOperation_, gizmoMode_,
                                     matrix, nullptr,
                                     useGizmoSnap_ ? snap : nullptr);
    if (used) {
        // Decompose the manipulated matrix back into T/R/S. ImGuizmo's helper
        // returns Euler angles in degrees, which we convert back to a quat.
        float t[3], r[3], s[3];
        ImGuizmo::DecomposeMatrixToComponents(matrix, t, r, s);
        tf->position = glm::vec3(t[0], t[1], t[2]);
        tf->rotation = glm::quat(glm::radians(glm::vec3(r[0], r[1], r[2])));
        tf->scale    = glm::vec3(s[0], s[1], s[2]);
    }
}

// ---------------------------------------------------------------------------
// Main frame
// ---------------------------------------------------------------------------
bool App::frame() {
    static Uint64 lastTick = SDL_GetPerformanceCounter();
    Uint64 now = SDL_GetPerformanceCounter();
    float  dt  = (float)(now - lastTick) / (float)SDL_GetPerformanceFrequency();
    dt = std::min(dt, 0.05f);
    lastTick = now;

    // --- Event loop ---
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        if (e.type == SDL_QUIT) running_ = false;
        if (e.type == SDL_WINDOWEVENT &&
            e.window.event == SDL_WINDOWEVENT_CLOSE &&
            e.window.windowID == SDL_GetWindowID(window_))
            running_ = false;

        if (e.type == SDL_KEYDOWN && !ImGui::GetIO().WantTextInput) {
            if (e.key.keysym.sym == SDLK_p)
                showPanel_ = !showPanel_;
            if (e.key.keysym.sym == SDLK_i)
                inventoryPanel_.visible = !inventoryPanel_.visible;
            if (e.key.keysym.sym == SDLK_t)
                gizmoOperation_ = ImGuizmo::TRANSLATE;
            if (e.key.keysym.sym == SDLK_e)
                gizmoOperation_ = ImGuizmo::ROTATE;
            if (e.key.keysym.sym == SDLK_r)
                gizmoOperation_ = ImGuizmo::SCALE;
            if (e.key.keysym.sym == SDLK_s)
                useGizmoSnap_ = !useGizmoSnap_;
        }
    }

    // --- Tick game ---
    if (gameRunning_ && !ImGuizmo::IsUsing()) tickSystems(dt);

    // --- Sync light sphere color from its DirectionalLight component ---
    if (registry_.valid(lightEntity_)) {
        auto& dl  = registry_.get<DirectionalLight>(lightEntity_);
        auto& mat = registry_.get<Material>(lightEntity_);
        mat.color = dl.color;
    }

    // --- ImGui frame ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // Fullscreen background viewport
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

        GLuint tex = renderSys_.render(registry_, renderer_, scene_);
        // Capture the image's screen-space position so the gizmo overlay
        // can be placed exactly on top of the rendered viewport.
        ImVec2 imgPos = ImGui::GetCursorScreenPos();
        vpScreenX_ = imgPos.x;
        vpScreenY_ = imgPos.y;
        ImGui::Image((ImTextureID)(intptr_t)tex,
                     ImVec2((float)vpW_, (float)vpH_),
                     ImVec2(0, 1), ImVec2(1, 0));
        ImGuizmo::SetAlternativeWindow(ImGui::GetCurrentContext() ? ImGui::FindWindowByName("Viewport##bg") : nullptr);
        ImGui::End();
    }

    drawPanel();
    drawGizmo();
    drawShaderPopup();
    drawGameUI();

    // --- Render ---
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
    static_cast<App*>(userdata)->frame();
}

void App::run() {
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(&App::mainLoopThunk, this, 0, 1);
#else
    while (running_) {
        if (!frame()) break;
    }
#endif
}

void App::shutdown() {
    if (glctx_) {
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
