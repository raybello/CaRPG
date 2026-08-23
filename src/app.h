// SDL2 + ImGui + GL bootstrap, plus the immediate-mode panel UI.
#pragma once

#include <string>

struct SDL_Window;
typedef void* SDL_GLContext;

#include "scene.h"
#include "renderer.h"
#include <entt/entt.hpp>
#include <box3d/box3d.h>
#include "systems/input_system.h"
#include "systems/physics_system.h"
#include "systems/fuel_system.h"
#include "systems/stat_system.h"
#include "systems/item_system.h"
#include "systems/camera_system.h"
#include "systems/render_system.h"
#include "ui/hud_texture.h"
#include "ui/inventory_panel.h"
#include "imgui.h"
#include "ImGuizmo.h"

class App {
public:
    App();
    ~App();

    bool initWindow();
    bool initImGui();
    bool initScene();

    bool frame();
    void run();
    void shutdown();

private:
    // SDL / GL state
    SDL_Window*   window_  = nullptr;
    SDL_GLContext glctx_   = nullptr;
    std::string   title_   = "CarRPG";
    int           winW_    = 1280;
    int           winH_    = 720;
    bool          running_ = true;

    // Scene & rendering
    Scene    scene_;
    Renderer renderer_;

    // Panel UI state
    bool      showPanel_       = true;
    bool      showShaderPopup_ = false;

    ImGuizmo::OPERATION gizmoOperation_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      gizmoMode_      = ImGuizmo::WORLD;
    bool                useGizmoSnap_   = false;

    // Entity selected in the inspector panel (entt::null = none)
    entt::entity selectedEntity_ = entt::null;

    // Shader editor — which slot is being edited
    ShaderSlot  editorTargetSlot_ = ShaderSlot::Object;
    std::string editorVertBuf_;
    std::string editorFragBuf_;

    // ECS core
    entt::registry   registry_;
    entt::dispatcher dispatcher_;
    entt::entity     playerEntity_   = entt::null;
    entt::entity     lightEntity_    = entt::null;
    entt::entity     cameraEcsEntity_= entt::null;

    // Systems
    InputSystem    inputSys_;
    PhysicsSystem  physicsSys_;
    FuelSystem     fuelSys_;
    StatSystem     statSys_;
    ItemSystem     itemSys_;
    CameraSystem   cameraSys_;
    RenderSystem   renderSys_;

    // UI
    HudTexture     hud_;
    InventoryPanel inventoryPanel_;
    bool           gameRunning_ = true;

    // box3d world — the sole owner of simulated body state; Transform is a
    // read-only mirror refreshed each fixed tick (see syncTransformsFromBox3D).
    b3WorldId b3World_          = b3_nullWorldId;
    float     physicsAccumulator_      = 0.0f;
    int       physicsSubSteps_         = 4;         // b3World_Step subStepCount (solver quality)
    float     physicsFixedStep_        = 1.0f / 60.0f;
    float     physicsContactHertz_     = 30.0f;     // mirrors b3DefaultWorldDef's defaults —
    float     physicsContactDampingRatio_ = 10.0f;  // there is no live getter for these, only
    float     physicsContactSpeed_     = 3.0f;      // b3World_SetContactTuning, so the UI owns them

    // Viewport size
    int vpW_ = 800;
    int vpH_ = 600;

    // Screen-space rect of the rendered viewport image (used to position
    // the gizmo overlay so it tracks the visible image, not the window).
    float vpScreenX_ = 0.0f;
    float vpScreenY_ = 0.0f;

    // Panel helpers
    void drawPanel();
    void drawGizmo();
    void drawShaderPopup();

    // Return the shader slot for a given entity
    ShaderSlot slotForEntity(entt::entity e) const;
    void openShaderEditorFor(ShaderSlot slot);
    void recompileEdited();

    // ECS helpers
    bool initGame();
    void initGameEntities();
    void connectEventListeners();
    void tickSystems(float dt);
    void syncTransformsFromBox3D();
    void drawGameUI();
    void syncShaderProgramsToRegistry();
    void spawnWorldItem(ItemId id, const glm::vec3& pos);
    void spawnObstaclesAndProps();
    entt::entity spawnStaticBox(const glm::vec3& pos, const glm::vec3& halfExtents,
                                const glm::quat& rot,  const glm::vec3& color,
                                MeshId meshId = MeshId::Cube);
    entt::entity spawnPushable (const glm::vec3& pos, const glm::vec3& halfExtents,
                                float mass,             const glm::vec3& color);

    // Entity display name (for the panel list)
    std::string entityDisplayName(entt::entity e) const;

    // Viewport mouse-pick: returns the closest entity under the given
    // viewport-relative pixel coordinate, or entt::null if nothing hit.
    entt::entity pickEntity(float vpX, float vpY) const;

    static void mainLoopThunk(void* userdata);
};
