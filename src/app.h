// SDL2 + ImGui + GL bootstrap, plus the immediate-mode panel UI.
#pragma once

#include <string>

// Forward declarations to keep this header light
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

    bool initWindow();   // creates SDL window + GL context
    bool initImGui();    // initializes ImGui + backends
    bool initScene();    // creates default shaders, FBO, cube mesh + ECS world

    // One frame.  Returns false when the user closes the window.
    bool frame();

    // Run the main loop (delegates to emscripten's loop on web).
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

    // Scene & rendering (preserved for shader hot-reload)
    Scene    scene_;
    Renderer renderer_;

    // Panel state (shader editor — preserved unchanged)
    bool     showPanel_         = true;
    bool     showShaderPopup_   = false;
    SceneItem selectedItem_     = SceneItem::Cube;
    std::string editorVertBuf_;
    std::string editorFragBuf_;
    SceneItem  editorTargetItem_ = SceneItem::Cube;

    // ECS core
    entt::registry   registry_;
    entt::dispatcher dispatcher_;
    entt::entity     playerEntity_ = entt::null;

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

    // Viewport size last seen (for HUD positioning)
    int vpW_ = 800;
    int vpH_ = 600;

    // Shader editor helpers (unchanged)
    void drawPanel();
    void drawShaderPopup();
    void openShaderEditorFor(SceneItem item);
    void recompileEdited();
    void uniformsForSelected();

    // ECS helpers
    bool initGame();
    void initGameEntities();
    void connectEventListeners();
    void tickSystems(float dt);
    void drawGameUI();
    void syncShaderProgramsToRegistry();

    // Spawn a world item at a position
    void spawnWorldItem(ItemId id, const glm::vec3& pos);

    // Static dispatcher for emscripten_set_main_loop_arg
    static void mainLoopThunk(void* userdata);
};
