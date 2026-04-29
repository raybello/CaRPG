// SDL2 + ImGui + GL bootstrap, plus the immediate-mode panel UI.
#pragma once

#include <string>

struct SDL_Window;
typedef void* SDL_GLContext;

#include "scene.h"
#include "renderer.h"
#include <entt/entt.hpp>
#include "systems/input_system.h"
#include "systems/physics_system.h"
#include "systems/fuel_system.h"
#include "systems/stat_system.h"
#include "systems/item_system.h"
#include "systems/camera_system.h"
#include "systems/render_system.h"
#include "ui/hud.h"
#include "ui/inventory_panel.h"

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
    HUD            hud_;
    InventoryPanel inventoryPanel_;
    bool           gameRunning_ = true;

    // Viewport size
    int vpW_ = 800;
    int vpH_ = 600;

    // Panel helpers
    void drawPanel();
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
    void drawGameUI();
    void syncShaderProgramsToRegistry();
    void spawnWorldItem(ItemId id, const glm::vec3& pos);

    // Entity display name (for the panel list)
    std::string entityDisplayName(entt::entity e) const;

    static void mainLoopThunk(void* userdata);
};
