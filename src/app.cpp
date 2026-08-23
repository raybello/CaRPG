#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <limits>
#include <string>
#include "ecs/components.h"
#include "game/item_catalog.h"
#include "game/game_events.h"
#include "model_loader.h"
#include "physics/box3d_interop.h"

#include <SDL.h>
#if defined(__EMSCRIPTEN__)
#  include <emscripten.h>
#  include <emscripten/html5.h>
#  include <GLES3/gl3.h>
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

#if defined(__EMSCRIPTEN__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    const char* glslVersion = "#version 300 es";
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
    hud_.init(800, 600);

    if (!ItemCatalog::loadFromFile("LOOT.json"))
        std::fprintf(stderr, "WARNING: LOOT.json failed to load\n");

    return initGame();
}

namespace {

// Drop heights added on top of a spawn's resting position so items/crates
// visibly fall and bounce into place — a visible signal that physics is
// running, rather than just appearing at rest.
constexpr float kItemDropHeight = 5.0f;
constexpr float kBoxDropHeight  = 5.0f;

// Density that yields the given total mass for a uniform-density box —
// used so spawn* functions can keep specifying mass directly, matching the
// old RigidBody-based tuning values, instead of density.
float boxDensityFromMass(float mass, const glm::vec3& halfExtents) {
    float volume = 8.0f * halfExtents.x * halfExtents.y * halfExtents.z;
    return (volume > 1e-8f) ? mass / volume : 1.0f;
}

// box3d has no automatic per-entity cleanup, so a PhysicsBody's body is
// destroyed whenever its component is (entity destruction on item pickup,
// or App::shutdown()'s explicit registry_.clear<PhysicsBody>()) — otherwise
// the body and its shape would leak and keep colliding invisibly forever.
void onPhysicsBodyDestroyed(entt::registry& reg, entt::entity e) {
    const auto& pb = reg.get<PhysicsBody>(e);
    if (!B3_IS_NULL(pb.id)) b3DestroyBody(pb.id);
}

}  // namespace

bool App::initGame() {
    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.gravity    = toB3(glm::vec3(0.0f, -9.81f, 0.0f));
    b3World_ = b3CreateWorld(&worldDef);
    registry_.on_destroy<PhysicsBody>().connect<&onPhysicsBodyDestroyed>();

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
    // Attempt to load the Porsche GLTF model; fall back to cube if missing.
    {
        ModelMesh mm = loadModel("models/2014_porsche_911_turbo_991/scene.gltf");
        if (!mm.submeshes.empty()) {
            mm.modelShaderProgram = scene_.modelShader.program;
            mm.visible            = true;
            // The Porsche model is exported with its hood facing +Z; rotate 180° around Y
            // so it aligns with the physics body's -Z forward.
            mm.rotOffset = glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            registry_.emplace<ModelMesh>(playerEntity_, std::move(mm));
        } else {
            registry_.emplace<MeshRef>(playerEntity_, MeshId::CarBody);
            registry_.emplace<Material>(playerEntity_, scene_.cubeShader.program,
                                        glm::vec3(0.8f, 0.15f, 0.1f));
        }
    }
    registry_.emplace<Inventory>(playerEntity_);
    {
        // Player car: fully dynamic body. Suspension spring forces (added in
        // PhysicsSystem) hold it up against gravity.
        const glm::vec3 he(1.0f, 0.5f, 2.0f);

        // Spawn slightly above ground so suspension settles naturally.
        glm::vec3 spawnPos(0.0f, 0.6f, 0.0f);
        registry_.get<Transform>(playerEntity_).position = spawnPos;

        b3BodyDef bodyDef      = b3DefaultBodyDef();
        bodyDef.type           = b3_dynamicBody;
        bodyDef.position       = toB3(spawnPos);
        bodyDef.linearDamping  = 0.02f;
        bodyDef.angularDamping = 0.85f;   // damp roll/pitch but allow them
        b3BodyId bodyId = b3CreateBody(b3World_, &bodyDef);

        b3ShapeDef shapeDef             = b3DefaultShapeDef();
        shapeDef.density                = boxDensityFromMass(1500.0f, he);
        shapeDef.baseMaterial.restitution = 0.05f;
        shapeDef.baseMaterial.friction    = 0.8f;
        // Tagged so the wheel suspension raycasts (PhysicsSystem) can
        // exclude the chassis's own hull; collides with everything else
        // normally since maskBits stays at its all-bits default.
        shapeDef.filter.categoryBits     = kCategoryChassis;
        b3BoxHull hull = b3MakeBoxHull(he.x, he.y, he.z);
        b3ShapeId shapeId = b3CreateHullShape(bodyId, &shapeDef, &hull.base);

        registry_.emplace<BoxCollider>(playerEntity_, BoxCollider{he});
        registry_.emplace<PhysicsBody>(playerEntity_, PhysicsBody{bodyId, shapeId});

        // 4-wheel suspension layout (car-local space):
        //   [0]=front-left  [1]=front-right  [2]=rear-left  [3]=rear-right
        CarVehicle cv;
        cv.topSpeed  = 30.0f;
        cv.maxTorque = 4000.0f;
        auto setWheel = [&](int i, float x, float y, float z, bool steered) {
            cv.wheels[i].localOffset        = glm::vec3(x, y, z);
            cv.wheels[i].isSteered          = steered;
            cv.wheels[i].isDriven           = true;
            cv.wheels[i].suspensionRestDist = 0.5f;
            cv.wheels[i].suspensionTravel   = 0.35f;
            cv.wheels[i].springStrength     = 38000.0f;
            cv.wheels[i].springDamper       = 3800.0f;
            cv.wheels[i].gripFactor         = 0.75f;
            cv.wheels[i].wheelMass          = 20.0f;
            cv.wheels[i].radius             = 0.35f;
        };
        setWheel(0, -0.95f, -0.5f, -1.7f, true);   // front-left  (steered)
        setWheel(1,  0.95f, -0.5f, -1.7f, true);   // front-right (steered)
        setWheel(2, -0.95f, -0.5f,  1.7f, false);  // rear-left
        setWheel(3,  0.95f, -0.5f,  1.7f, false);  // rear-right
        registry_.emplace<CarVehicle>(playerEntity_, cv);
    }

    // --- Chase / free-look camera ---
    cameraEcsEntity_ = registry_.create();
    registry_.emplace<CameraTag>(cameraEcsEntity_);
    registry_.emplace<CameraState>(cameraEcsEntity_);
    registry_.emplace<CameraFollow>(cameraEcsEntity_, playerEntity_);
    {
        // Give the camera a Transform so the gizmo can manipulate it.
        // CameraSystem syncs cam.position→Transform each frame when follow is active;
        // when follow is disabled the Transform acts as the authoritative position.
        Transform camTf;
        camTf.position = glm::vec3(0.0f, 5.0f, 15.0f);
        registry_.emplace<Transform>(cameraEcsEntity_, camTf);
    }

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
        // box3d has no infinite-plane primitive, so the ground is a large,
        // thin static box hull positioned so its top face sits at y = 0 —
        // buried well below the thin visual ground quad, which is unaffected.
        const glm::vec3 he(50.0f, 10.0f, 50.0f);
        const glm::vec3 pos(0.0f, -10.0f, 0.0f);

        b3BodyDef bodyDef = b3DefaultBodyDef();
        bodyDef.type      = b3_staticBody;
        bodyDef.position  = toB3(pos);
        b3BodyId bodyId = b3CreateBody(b3World_, &bodyDef);

        b3ShapeDef shapeDef             = b3DefaultShapeDef();
        shapeDef.baseMaterial.restitution = 0.2f;
        shapeDef.baseMaterial.friction    = 0.8f;
        b3BoxHull hull = b3MakeBoxHull(he.x, he.y, he.z);
        b3ShapeId shapeId = b3CreateHullShape(bodyId, &shapeDef, &hull.base);

        registry_.emplace<PhysicsBody>(ground, PhysicsBody{bodyId, shapeId});
    }

    // --- Directional light — rendered as a colored sphere ---
    lightEntity_ = registry_.create();
    registry_.emplace<DirectionalLight>(lightEntity_);   // defaults: dir, white, pos (2,3,2)
    {
        Transform tf;
        tf.position = glm::vec3(2.0f, 3.0f, 2.0f);
        tf.rotation = glm::quat(glm::vec3(-45.0f, 45.0f, 0.0f)); // point toward world origin
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

    spawnObstaclesAndProps();
}

void App::spawnObstaclesAndProps() {
    // Common material colours
    const glm::vec3 kYellow    {0.90f, 0.78f, 0.08f};
    const glm::vec3 kConcrete  {0.72f, 0.67f, 0.60f};
    const glm::vec3 kDarkGray  {0.42f, 0.42f, 0.42f};
    const glm::vec3 kMidGray   {0.58f, 0.58f, 0.58f};
    const glm::vec3 kBrown     {0.55f, 0.35f, 0.15f};
    const glm::vec3 kDarkBrown {0.35f, 0.22f, 0.08f};
    const glm::vec3 kBrownGray {0.50f, 0.42f, 0.35f};

    const glm::quat kIdent  {1.0f, 0.0f, 0.0f, 0.0f};
    // Ascending ramp: low end faces the car (+Z body side), rises toward -Z.
    // With R_x(+15°): bottom +Z corner is at y=0 when center.y≈1.07.
    const glm::quat kRampUp = glm::angleAxis(glm::radians( 15.0f), glm::vec3(1,0,0));

    // ---------------------------------------------------------------------------
    // Speed bumps — three yellow cylinders lying across the road (axis = X).
    // The cylinder mesh is Y-axis; R_z(90°) rotates it onto X.
    // Body half-extents: Y = half-length (2.5 m), X/Z = radius (0.14 m).
    // Center y = radius so the bottom just touches the ground plane.
    // ---------------------------------------------------------------------------
    const glm::quat kBumpRot = glm::angleAxis(glm::radians(90.0f), glm::vec3(0,0,1));
    const glm::vec3 kBumpHE  = {0.14f, 2.5f, 0.14f};
    spawnStaticBox({-2.0f, 0.14f,  -4.0f}, kBumpHE, kBumpRot, kYellow, MeshId::Cylinder);
    spawnStaticBox({ 2.0f, 0.14f,  -7.0f}, kBumpHE, kBumpRot, kYellow, MeshId::Cylinder);
    spawnStaticBox({-1.0f, 0.14f, -11.0f}, kBumpHE, kBumpRot, kYellow, MeshId::Cylinder);

    // ---------------------------------------------------------------------------
    // Jersey barriers — a pair flanking the ramp corridor
    // ---------------------------------------------------------------------------
    spawnStaticBox({-5.0f, 0.80f, -13.5f}, {0.30f, 0.80f, 2.5f}, kIdent, kConcrete);
    spawnStaticBox({ 5.0f, 0.80f, -13.5f}, {0.30f, 0.80f, 2.5f}, kIdent, kConcrete);

    // ---------------------------------------------------------------------------
    // Main ramp + flat-top platform + back wall
    //
    // Geometry (R_x +15°, he = {3, 0.3, 3}):
    //   center.y = 1.07 so the low (+Z) edge touches y = 0
    //   top of the ramp (-Z edge) reaches y ≈ 2.14
    // ---------------------------------------------------------------------------
    spawnStaticBox({0.0f, 1.07f, -17.5f}, {3.0f, 0.30f, 3.0f}, kRampUp, kDarkGray);

    // Flat platform — top face flush with the ramp peak (~2.14 m)
    spawnStaticBox({0.0f, 1.89f, -23.0f}, {3.5f, 0.25f, 2.5f}, kIdent, kMidGray);

    // Short wall at the platform's far edge — stops the car from rolling off
    spawnStaticBox({0.0f, 2.24f, -25.6f}, {3.5f, 0.35f, 0.25f}, kIdent, kConcrete);

    // ---------------------------------------------------------------------------
    // Side ramp (right of centre, no flat top) — same 15° pitch, stand-alone
    // ---------------------------------------------------------------------------
    spawnStaticBox({14.0f, 1.07f, -5.0f}, {2.5f, 0.30f, 3.0f}, kRampUp, kDarkGray);

    // ---------------------------------------------------------------------------
    // Pushable crates — small / medium / large, scattered around the arena
    // ---------------------------------------------------------------------------
    // Small (40 kg)
    spawnPushable({ 7.0f, 0.40f,  3.0f}, {0.40f, 0.40f, 0.40f},  40.0f, kBrown);
    spawnPushable({-7.0f, 0.40f,  2.0f}, {0.40f, 0.40f, 0.40f},  40.0f, kBrown);
    spawnPushable({ 5.0f, 0.40f, -2.5f}, {0.40f, 0.40f, 0.40f},  40.0f, kBrown);

    // Medium (100 kg)
    spawnPushable({10.0f, 0.60f, -3.0f}, {0.60f, 0.60f, 0.60f}, 100.0f, kDarkBrown);
    spawnPushable({-9.0f, 0.60f,  5.0f}, {0.60f, 0.60f, 0.60f}, 100.0f, kDarkBrown);

    // Large (250 kg)
    spawnPushable({ 2.0f, 0.50f,  6.0f}, {0.80f, 0.50f, 0.90f}, 250.0f, kBrownGray);
    spawnPushable({-3.0f, 0.50f,  8.0f}, {0.80f, 0.50f, 0.90f}, 250.0f, kBrownGray);
}

void App::spawnWorldItem(ItemId id, const glm::vec3& pos) {
    const ItemDef* def = getItemDef(id);
    auto ent = registry_.create();
    registry_.emplace<ItemWorldTag>(ent);
    Transform tf;
    // Spawn well above the ground so they drop and bounce into place
    // — visible signal that physics is running.
    tf.position = pos + glm::vec3(0.0f, kItemDropHeight, 0.0f);
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
    registry_.emplace<BoxCollider>(ent, BoxCollider{he});

    b3BodyDef bodyDef      = b3DefaultBodyDef();
    bodyDef.type           = b3_dynamicBody;
    bodyDef.position       = toB3(tf.position);
    bodyDef.linearDamping  = 0.20f;
    bodyDef.angularDamping = 0.40f;
    b3BodyId bodyId = b3CreateBody(b3World_, &bodyDef);

    b3ShapeDef shapeDef             = b3DefaultShapeDef();
    shapeDef.density                = boxDensityFromMass(1.0f, he);
    shapeDef.baseMaterial.restitution = 0.45f;
    shapeDef.baseMaterial.friction    = 0.55f;
    b3BoxHull hull = b3MakeBoxHull(he.x, he.y, he.z);
    b3ShapeId shapeId = b3CreateHullShape(bodyId, &shapeDef, &hull.base);

    registry_.emplace<PhysicsBody>(ent, PhysicsBody{bodyId, shapeId});
}

entt::entity App::spawnStaticBox(const glm::vec3& pos, const glm::vec3& halfExtents,
                                  const glm::quat& rot, const glm::vec3& color,
                                  MeshId meshId) {
    auto ent = registry_.create();
    registry_.emplace<ObstacleTag>(ent);

    Transform tf;
    tf.position = pos;
    tf.rotation = rot;
    tf.scale    = halfExtents * 2.0f;
    registry_.emplace<Transform>(ent, tf);

    registry_.emplace<MeshRef>(ent, meshId);
    registry_.emplace<Material>(ent, scene_.cubeShader.program, color);

    registry_.emplace<BoxCollider>(ent, BoxCollider{halfExtents});

    b3BodyDef bodyDef = b3DefaultBodyDef();
    bodyDef.type      = b3_staticBody;
    bodyDef.position  = toB3(pos);
    bodyDef.rotation  = toB3(rot);
    b3BodyId bodyId = b3CreateBody(b3World_, &bodyDef);

    b3ShapeDef shapeDef             = b3DefaultShapeDef();
    shapeDef.baseMaterial.restitution = 0.25f;
    shapeDef.baseMaterial.friction    = 0.75f;
    b3BoxHull hull = b3MakeBoxHull(halfExtents.x, halfExtents.y, halfExtents.z);
    b3ShapeId shapeId = b3CreateHullShape(bodyId, &shapeDef, &hull.base);

    registry_.emplace<PhysicsBody>(ent, PhysicsBody{bodyId, shapeId});
    return ent;
}

entt::entity App::spawnPushable(const glm::vec3& pos, const glm::vec3& halfExtents,
                                 float mass, const glm::vec3& color) {
    auto ent = registry_.create();
    registry_.emplace<PushableTag>(ent);

    // Spawn above the resting position so crates visibly fall and bounce
    // into place — a visible signal that physics is running.
    const glm::vec3 spawnPos = pos + glm::vec3(0.0f, kBoxDropHeight, 0.0f);

    Transform tf;
    tf.position = spawnPos;
    tf.scale    = halfExtents * 2.0f;
    registry_.emplace<Transform>(ent, tf);

    registry_.emplace<MeshRef>(ent, MeshId::Cube);
    registry_.emplace<Material>(ent, scene_.cubeShader.program, color);

    registry_.emplace<BoxCollider>(ent, BoxCollider{halfExtents});

    b3BodyDef bodyDef      = b3DefaultBodyDef();
    bodyDef.type           = b3_dynamicBody;
    bodyDef.position       = toB3(spawnPos);
    bodyDef.linearDamping  = 0.35f;
    bodyDef.angularDamping = 0.45f;
    b3BodyId bodyId = b3CreateBody(b3World_, &bodyDef);

    b3ShapeDef shapeDef             = b3DefaultShapeDef();
    shapeDef.density                = boxDensityFromMass(mass, halfExtents);
    shapeDef.baseMaterial.restitution = 0.3f;
    shapeDef.baseMaterial.friction    = 0.65f;
    b3BoxHull hull = b3MakeBoxHull(halfExtents.x, halfExtents.y, halfExtents.z);
    b3ShapeId shapeId = b3CreateHullShape(bodyId, &shapeDef, &hull.base);

    registry_.emplace<PhysicsBody>(ent, PhysicsBody{bodyId, shapeId});
    return ent;
}

void App::connectEventListeners() {
    dispatcher_.sink<GearEquippedEvent>()
        .connect<&StatSystem::onGearEquipped>(statSys_);
    dispatcher_.sink<GearUnequippedEvent>()
        .connect<&StatSystem::onGearUnequipped>(statSys_);
    dispatcher_.sink<LowFuelEvent>()
        .connect<&HudTexture::onLowFuel>(hud_);
    dispatcher_.sink<FuelDepletedEvent>()
        .connect<&HudTexture::onFuelDepleted>(hud_);
    dispatcher_.sink<ItemPickedUpEvent>()
        .connect<&InventoryPanel::onItemPickedUp>(inventoryPanel_);
    dispatcher_.sink<GearEquippedEvent>()
        .connect<&InventoryPanel::onGearEquipped>(inventoryPanel_);
}

// ---------------------------------------------------------------------------
// Per-frame systems
// ---------------------------------------------------------------------------
void App::tickSystems(float dt) {
    // Input is always polled — decoupled from the gizmo gate so the car
    // responds immediately when the gizmo is released.
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    inputSys_.update(registry_, keys);

    // Physics / simulation are still paused while the gizmo is being dragged
    // so it can reposition entities without fighting the simulation.
    if (ImGuizmo::IsUsing()) return;

    statSys_.update(registry_);

    // Fixed-tick accumulator: box3d wants exactly one b3World_Step per fixed
    // timeStep (subStepCount is an internal solver-quality knob, not a
    // repeat count), so suspension/drive forces are (re-)applied once per
    // tick immediately before stepping — not once per rendered frame.
    physicsAccumulator_ += dt;
    constexpr int kMaxTicksPerFrame = 4;  // spiral-of-death guard
    int ticks = 0;
    while (physicsAccumulator_ >= physicsFixedStep_ && ticks < kMaxTicksPerFrame) {
        physicsSys_.update(registry_, physicsFixedStep_);
        b3World_Step(b3World_, physicsFixedStep_, physicsSubSteps_);
        syncTransformsFromBox3D();
        physicsAccumulator_ -= physicsFixedStep_;
        ++ticks;
    }
    if (ticks == kMaxTicksPerFrame) physicsAccumulator_ = 0.0f;  // drop backlog rather than spiral

    fuelSys_.update(registry_, dispatcher_, dt);
    itemSys_.update(registry_, dispatcher_, dt);
    cameraSys_.update(registry_, dt, vpW_, vpH_);
    dispatcher_.update();
}

void App::syncTransformsFromBox3D() {
    // Static bodies never move and some (the ground) are deliberately offset
    // from their entity's visual Transform (see spawn code), so only mirror
    // bodies box3d can actually move.
    registry_.view<Transform, PhysicsBody>().each(
        [&](Transform& tf, const PhysicsBody& pb) {
            if (b3Body_GetType(pb.id) != b3_dynamicBody) return;
            tf.position = fromB3(b3Body_GetPosition(pb.id));
            tf.rotation = fromB3(b3Body_GetRotation(pb.id));
        });
}

void App::drawGameUI() {
    inventoryPanel_.draw(registry_, playerEntity_, itemSys_, dispatcher_);
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
    if (registry_.all_of<ObstacleTag>(e)) return "Obstacle";
    if (registry_.all_of<PushableTag>(e)) return "Crate";
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
            auto* cam    = registry_.try_get<CameraState>(cameraEcsEntity_);
            auto* follow = registry_.try_get<CameraFollow>(cameraEcsEntity_);

            if (follow && !follow->enabled) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Free-look (gizmo override)");
                if (ImGui::Button("Resume Follow Car")) {
                    follow->enabled = true;
                    follow->target  = playerEntity_;
                }
                ImGui::Separator();
                // Show gizmo op selector (translate / rotate only)
                if (ImGui::RadioButton("Translate", gizmoOperation_ == ImGuizmo::TRANSLATE))
                    gizmoOperation_ = ImGuizmo::TRANSLATE;
                ImGui::SameLine();
                if (ImGui::RadioButton("Rotate", gizmoOperation_ == ImGuizmo::ROTATE))
                    gizmoOperation_ = ImGuizmo::ROTATE;
            } else {
                ImGui::TextDisabled("Chase Camera");
                ImGui::TextDisabled("Gizmo translate/rotate to enter free-look");
            }

            if (cam) {
                ImGui::SliderFloat("FOV (deg)", &cam->fovDeg, 10.0f, 120.0f);
                ImGui::TextDisabled("Position");
                ImGui::Text("  %.2f, %.2f, %.2f",
                            cam->position.x, cam->position.y, cam->position.z);
                ImGui::TextDisabled("Forward");
                ImGui::Text("  %.2f, %.2f, %.2f",
                            cam->forward.x, cam->forward.y, cam->forward.z);
            }
            if (follow && follow->enabled) {
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
            ImGui::ColorEdit3("Color", &dl.color.x);

            // Gizmo controls — translate/rotate only (same as generic entities)
            if (ImGui::RadioButton("Translate", gizmoOperation_ == ImGuizmo::TRANSLATE))
                gizmoOperation_ = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate", gizmoOperation_ == ImGuizmo::ROTATE))
                gizmoOperation_ = ImGuizmo::ROTATE;
            ImGui::SameLine();
            if (ImGui::RadioButton("Local", gizmoMode_ == ImGuizmo::LOCAL))
                gizmoMode_ = ImGuizmo::LOCAL;
            ImGui::SameLine();
            if (ImGui::RadioButton("World", gizmoMode_ == ImGuizmo::WORLD))
                gizmoMode_ = ImGuizmo::WORLD;
            ImGui::SameLine();
            ImGui::Checkbox("Snap", &useGizmoSnap_);

            // Transform is the source of truth for position; sync to DirectionalLight.
            if (auto* tf = registry_.try_get<Transform>(selectedEntity_)) {
                if (ImGui::SliderFloat3("Position", &tf->position.x, -50.0f, 50.0f))
                    dl.position = tf->position;
                else
                    dl.position = tf->position;  // keep in sync after gizmo moves
            }

            if (dl.type == LightType::Directional || dl.type == LightType::Spot) {
                ImGui::TextDisabled("Direction (driven by Rotate gizmo)");
                ImGui::Text("  %.2f, %.2f, %.2f",
                            dl.direction.x, dl.direction.y, dl.direction.z);
            }
            if (dl.type == LightType::Spot) {
                ImGui::SliderFloat("Spot Cutoff (deg)", &dl.spotCutoff, 1.0f, 89.0f);
            }
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
            // Physics body (live state + tunables, read straight from box3d —
            // there is no ECS-side cache to go stale).
            if (auto* pb = registry_.try_get<PhysicsBody>(selectedEntity_)) {
                ImGui::Separator();
                ImGui::TextDisabled("Physics Body");
                b3BodyType type = b3Body_GetType(pb->id);
                const char* kind = type == b3_staticBody    ? "static"
                                 : type == b3_kinematicBody ? "kinematic"
                                                             : "dynamic";
                ImGui::Text("Type: %s", kind);
                ImGui::Text("Mass: %.2f kg", b3Body_GetMass(pb->id));

                float restitution = b3Shape_GetRestitution(pb->shapeId);
                if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f))
                    b3Shape_SetRestitution(pb->shapeId, restitution);
                float friction = b3Shape_GetFriction(pb->shapeId);
                if (ImGui::SliderFloat("Friction", &friction, 0.0f, 2.0f))
                    b3Shape_SetFriction(pb->shapeId, friction);

                if (type == b3_dynamicBody) {
                    float linDamp = b3Body_GetLinearDamping(pb->id);
                    if (ImGui::SliderFloat("Lin Damp", &linDamp, 0.0f, 5.0f))
                        b3Body_SetLinearDamping(pb->id, linDamp);
                    float angDamp = b3Body_GetAngularDamping(pb->id);
                    if (ImGui::SliderFloat("Ang Damp", &angDamp, 0.0f, 5.0f))
                        b3Body_SetAngularDamping(pb->id, angDamp);
                    bool gravityOn = b3Body_GetGravityScale(pb->id) > 0.5f;
                    if (ImGui::Checkbox("Gravity", &gravityOn))
                        b3Body_SetGravityScale(pb->id, gravityOn ? 1.0f : 0.0f);
                }
                glm::vec3 v = fromB3(b3Body_GetLinearVelocity(pb->id));
                glm::vec3 w = fromB3(b3Body_GetAngularVelocity(pb->id));
                ImGui::Text("v   %.2f, %.2f, %.2f", v.x, v.y, v.z);
                ImGui::Text("ω   %.2f, %.2f, %.2f", w.x, w.y, w.z);
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
        ImGui::TextDisabled("Input Response");
        ImGui::SliderFloat("Throttle Response", &physicsSys_.throttleResponse, 0.1f, 10.0f);
        ImGui::SliderFloat("Steer Response",    &physicsSys_.steerResponse,    0.1f, 10.0f);
        ImGui::SliderFloat("Max Steer (deg)",   &physicsSys_.maxSteerAngleRad, 0.05f, 1.5f);

        // Per-vehicle suspension tuning (syncs all 4 wheels when changed).
        if (auto* cv = registry_.try_get<CarVehicle>(playerEntity_)) {
            ImGui::Separator();
            ImGui::TextDisabled("Vehicle");
            ImGui::SliderFloat("Top Speed (m/s)", &cv->topSpeed,  5.0f, 100.0f);
            ImGui::SliderFloat("Max Torque (N)",  &cv->maxTorque, 100.0f, 20000.0f);

            ImGui::Separator();
            ImGui::TextDisabled("Suspension (all wheels)");
            float springK    = cv->wheels[0].springStrength;
            float springD    = cv->wheels[0].springDamper;
            float grip       = cv->wheels[0].gripFactor;
            float restDist   = cv->wheels[0].suspensionRestDist;
            bool changed = false;
            changed |= ImGui::SliderFloat("Spring K (N/m)",  &springK,  1000.0f, 100000.0f);
            changed |= ImGui::SliderFloat("Spring Damp",     &springD,  100.0f,  20000.0f);
            changed |= ImGui::SliderFloat("Grip Factor",     &grip,     0.0f,    2.0f);
            changed |= ImGui::SliderFloat("Rest Length (m)", &restDist, 0.1f,    1.5f);
            if (changed) {
                for (auto& w : cv->wheels) {
                    w.springStrength     = springK;
                    w.springDamper       = springD;
                    w.gripFactor         = grip;
                    w.suspensionRestDist = restDist;
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Wheel Debug");
            const char* labels[] = {"FL", "FR", "RL", "RR"};
            for (int i = 0; i < CarVehicle::kWheelCount; ++i) {
                const auto& w = cv->wheels[i];
                ImGui::Text("%s: %s  comp=%.2f  dist=%.3f",
                    labels[i],
                    w.grounded ? "grounded" : "air    ",
                    w.compressionRatio,
                    w.contactDist);
            }
        }
    }

    ImGui::Separator();

    // ---- World physics (box3d) -------------------------------------------
    if (ImGui::CollapsingHeader("World Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        glm::vec3 gravity = fromB3(b3World_GetGravity(b3World_));
        if (ImGui::SliderFloat3("Gravity", &gravity.x, -30.0f, 30.0f))
            b3World_SetGravity(b3World_, toB3(gravity));
        ImGui::SliderInt  ("Sub-Steps (solver quality)", &physicsSubSteps_, 1, 16);
        ImGui::SliderFloat("Fixed Step",                 &physicsFixedStep_, 1.0f/240.0f, 1.0f/30.0f);

        // b3World_SetContactTuning has no matching getter, so the sliders
        // own these values and push them on change rather than reading back.
        ImGui::TextDisabled("Contact softness");
        bool tuningChanged = false;
        tuningChanged |= ImGui::SliderFloat("Contact Hertz",   &physicsContactHertz_,        1.0f, 60.0f);
        tuningChanged |= ImGui::SliderFloat("Contact Damping", &physicsContactDampingRatio_, 0.0f, 20.0f);
        tuningChanged |= ImGui::SliderFloat("Contact Speed",   &physicsContactSpeed_,        0.1f, 10.0f);
        if (tuningChanged)
            b3World_SetContactTuning(b3World_, physicsContactHertz_,
                                      physicsContactDampingRatio_, physicsContactSpeed_);
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
// Viewport mouse picking — ray-AABB intersection in world space
// ---------------------------------------------------------------------------
entt::entity App::pickEntity(float vpX, float vpY) const {
    auto* cam = registry_.try_get<CameraState>(cameraEcsEntity_);
    if (!cam || vpW_ < 1 || vpH_ < 1) return entt::null;

    // NDC in [-1,1] × [-1,1], Y flipped because viewport Y grows downward.
    float ndcX =  (2.0f * vpX / (float)vpW_) - 1.0f;
    float ndcY = -(2.0f * vpY / (float)vpH_) + 1.0f;

    glm::mat4 invVP = glm::inverse(cam->proj * cam->view);
    glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farH  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    nearH /= nearH.w;
    farH  /= farH.w;

    glm::vec3 rayOrig = glm::vec3(nearH);
    glm::vec3 rayDir  = glm::normalize(glm::vec3(farH) - rayOrig);

    // Ray-AABB slab test (world-space, axis-aligned).
    auto testAABB = [&](const glm::vec3& boxMin, const glm::vec3& boxMax, float& tHit) -> bool {
        glm::vec3 invD = 1.0f / rayDir;
        glm::vec3 t0   = (boxMin - rayOrig) * invD;
        glm::vec3 t1   = (boxMax - rayOrig) * invD;
        glm::vec3 tNear = glm::min(t0, t1);
        glm::vec3 tFar  = glm::max(t0, t1);
        float tMin = std::max({tNear.x, tNear.y, tNear.z});
        float tMax = std::min({tFar.x,  tFar.y,  tFar.z});
        if (tMax < tMin || tMax < 0.0f) return false;
        tHit = (tMin >= 0.0f) ? tMin : tMax;
        return true;
    };

    entt::entity closest = entt::null;
    float       bestT    = std::numeric_limits<float>::max();

    registry_.view<Transform, MeshRef>().each(
        [&](entt::entity e, const Transform& tf, const MeshRef& mr) {
            // The ground mesh has a 50×50 world-unit footprint; the camera
            // sits inside its XZ AABB so its slab entry appears very close.
            // Skip it — it's not a meaningful pick target.
            if (mr.meshId == MeshId::Ground) return;

            // Use BoxCollider half-extents when available; fall back to half-scale.
            glm::vec3 half;
            const auto* bc = registry_.try_get<BoxCollider>(e);
            if (bc) half = bc->halfExtents * glm::abs(tf.scale);
            else    half = glm::abs(tf.scale) * 0.5f;

            glm::vec3 boxMin = tf.position - half;
            glm::vec3 boxMax = tf.position + half;

            float t;
            if (testAABB(boxMin, boxMax, t) && t < bestT) {
                bestT   = t;
                closest = e;
            }
        });

    return closest;
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

    const bool isCameraSelected = (selectedEntity_ == cameraEcsEntity_);

    // For the camera, only allow translate and rotate (scale makes no sense).
    ImGuizmo::OPERATION op = gizmoOperation_;
    if (isCameraSelected && op == ImGuizmo::SCALE)
        op = ImGuizmo::TRANSLATE;

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
        switch (op) {
            case ImGuizmo::TRANSLATE: snap[0] = snap[1] = snap[2] = 0.5f; break;
            case ImGuizmo::ROTATE:    snap[0] = 15.0f; break;
            case ImGuizmo::SCALE:     snap[0] = 0.25f; break;
            default: break;
        }
    }

    bool used = ImGuizmo::Manipulate(glm::value_ptr(cam->view),
                                     glm::value_ptr(cam->proj),
                                     op, gizmoMode_,
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

        if (isCameraSelected) {
            // Sync camera state directly so the view matrix updates this frame.
            // Also detach from the car so CameraSystem doesn't fight the gizmo.
            cam->position = tf->position;
            cam->forward  = glm::normalize(tf->rotation * glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 lookAt = cam->position + cam->forward;
            cam->view = glm::lookAt(cam->position, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));

            auto* follow = registry_.try_get<CameraFollow>(cameraEcsEntity_);
            if (follow) follow->enabled = false;
        } else if (auto* dl = registry_.try_get<DirectionalLight>(selectedEntity_)) {
            // Keep DirectionalLight.position in sync with the gizmo-moved Transform.
            dl->position = tf->position;
        }

        // Push the edit into box3d as a teleport, and zero out momentum —
        // otherwise the next syncTransformsFromBox3D() overwrites the gizmo's
        // edit with box3d's stale pose, and any carried-over velocity could
        // produce a violent contact-resolution spike on the next step.
        if (auto* pb = registry_.try_get<PhysicsBody>(selectedEntity_)) {
            b3Body_SetTransform(pb->id, toB3(tf->position), toB3(tf->rotation));
            b3Body_SetLinearVelocity(pb->id, b3Vec3{0.0f, 0.0f, 0.0f});
            b3Body_SetAngularVelocity(pb->id, b3Vec3{0.0f, 0.0f, 0.0f});
        }
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
            if (e.key.keysym.sym == SDLK_ESCAPE)
                selectedEntity_ = entt::null;
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
            if (e.key.keysym.sym == SDLK_q && registry_.valid(playerEntity_)) {
                auto& tf = registry_.get<Transform>(playerEntity_);
                tf.position = glm::vec3(0.0f, 0.6f, 0.0f);
                tf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                if (auto* pb = registry_.try_get<PhysicsBody>(playerEntity_)) {
                    b3Body_SetTransform(pb->id, toB3(tf.position), toB3(tf.rotation));
                    b3Body_SetLinearVelocity(pb->id, b3Vec3{0.0f, 0.0f, 0.0f});
                    b3Body_SetAngularVelocity(pb->id, b3Vec3{0.0f, 0.0f, 0.0f});
                }
                if (auto* vel = registry_.try_get<Velocity>(playerEntity_))
                    *vel = Velocity{};
            }
        }
    }

    // --- Tick game ---
    // tickSystems always runs when gameRunning; input is unconditionally
    // updated inside it while physics/camera are still gated on !IsUsing.
    if (gameRunning_) tickSystems(dt);
    hud_.update(registry_, playerEntity_, dt);

    // --- Sync light sphere: color from DirectionalLight, direction from rotation ---
    if (registry_.valid(lightEntity_)) {
        auto& dl  = registry_.get<DirectionalLight>(lightEntity_);
        auto& mat = registry_.get<Material>(lightEntity_);
        mat.color = dl.color;
        // Rotation drives direction: the light shines along the entity's -Z axis.
        if (auto* tf = registry_.try_get<Transform>(lightEntity_))
            dl.direction = glm::normalize(tf->rotation * glm::vec3(0.0f, 0.0f, -1.0f));
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
        hud_.resize(vpW_, vpH_);

        GLuint tex = renderSys_.render(registry_, renderer_, scene_);
        // Capture the image's screen-space position so the gizmo overlay
        // can be placed exactly on top of the rendered viewport.
        ImVec2 imgPos = ImGui::GetCursorScreenPos();
        vpScreenX_ = imgPos.x;
        vpScreenY_ = imgPos.y;
        ImGui::Image((ImTextureID)(intptr_t)tex,
                     ImVec2((float)vpW_, (float)vpH_),
                     ImVec2(0, 1), ImVec2(1, 0));

        // HUD overlay — alpha-blended, always on top of the 3D viewport
        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)(intptr_t)hud_.texture(),
            imgPos,
            ImVec2(imgPos.x + (float)vpW_, imgPos.y + (float)vpH_),
            ImVec2(0, 0), ImVec2(1, 1));

        // Mouse picking: left-click in the viewport selects the entity under
        // the cursor.  Skip when the gizmo is being manipulated.
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGuizmo::IsOver()) {
            ImVec2 mouse  = ImGui::GetMousePos();
            float  vpRelX = mouse.x - vpScreenX_;
            float  vpRelY = mouse.y - vpScreenY_;
            entt::entity hit = pickEntity(vpRelX, vpRelY);
            if (hit != entt::null)
                selectedEntity_ = hit;
        }

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
    if (!B3_IS_NULL(b3World_)) {
        // Destroy every box3d body via the on_destroy<PhysicsBody> hook
        // (registered in initGame()) before tearing down the world itself.
        registry_.clear<PhysicsBody>();
        b3DestroyWorld(b3World_);
        b3World_ = b3_nullWorldId;
    }
    if (glctx_) {
        // Free GPU resources owned by any ModelMesh components
        registry_.view<ModelMesh>().each([](ModelMesh& mm) { destroyModelMesh(mm); });
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
