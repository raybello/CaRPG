#pragma once

#include <array>
#include <string>
#include <vector>
#include "glm_math.h"
#include <entt/entt.hpp>

// Forward declarations for GL types used in Material and ModelMesh
typedef unsigned int GLuint;

// ---------------------------------------------------------------------------
// Tag components (zero-size markers)
// ---------------------------------------------------------------------------
struct PlayerTag   {};
struct NpcTag      {};
struct ItemWorldTag{};  // item lying in the world waiting to be picked up
struct CameraTag   {};  // marks the scene/editor camera entity

// ---------------------------------------------------------------------------
// Item IDs — must match LOOT.json "id" strings
// ---------------------------------------------------------------------------
enum class ItemId : uint32_t {
    NitroBoost = 0,
    MagneticShield,
    EnginePart,
    SteelCoil,
    TurboWheels,
    OffRoadSuspension,
    FuelCanister,
    OverchargeCell,
    RoadSignFragment,
    AbandonedWreck,
    Count
};

// ---------------------------------------------------------------------------
// Mesh IDs — maps to VAO/VBO slots in Renderer
// ---------------------------------------------------------------------------
enum class MeshId : uint32_t {
    Cube     = 0,
    Ground   = 1,
    CarBody  = 2,
    CarWheel = 3,
    Sphere   = 4,
    Count
};

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------
struct Transform {
    glm::vec3 position {0.0f, 0.0f, 0.0f};
    glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale    {1.0f, 1.0f, 1.0f};

    glm::mat4 toMatrix() const {
        glm::mat4 m = glm::mat4_cast(rotation);
        m[3] = glm::vec4(position, 1.0f);
        m = glm::scale(glm::mat4(1.0f), scale) * m;
        // Correct order: T * R * S
        glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
        return T * R * S;
    }
};

// ---------------------------------------------------------------------------
// Physics / kinematics
// ---------------------------------------------------------------------------
struct Velocity {
    glm::vec3 linear   {0.0f, 0.0f, 0.0f};
    float     angularY = 0.0f;  // deg/s yaw (display only)

    // Smoothed input state — owned/written by PhysicsSystem.
    // Lives here so multiple cars work later without a system-side singleton.
    float appliedThrottle = 0.0f;  // [-1, +1]
    float appliedSteer    = 0.0f;  // [-1, +1]
};

struct CarInput {
    float throttle  = 0.0f;   // [0, 1]
    float brake     = 0.0f;   // [0, 1]
    float steer     = 0.0f;   // [-1, 1]  negative = left
    bool  handbrake = false;
};

// ---------------------------------------------------------------------------
// Car stats
// ---------------------------------------------------------------------------
struct BaseCarStats {
    float handling   = 1.0f;    // steering responsiveness
    float power      = 100.0f;  // engine force
    float efficiency = 1.0f;    // fuel multiplier (lower = better)
    float topSpeed   = 30.0f;   // m/s
};

struct DerivedCarStats {
    float handling   = 1.0f;
    float power      = 100.0f;
    float efficiency = 1.0f;
    float topSpeed   = 30.0f;
    bool  dirty      = true;    // set true on gear change; StatSystem recalculates
};

// ---------------------------------------------------------------------------
// Fuel (the car's "life")
// ---------------------------------------------------------------------------
struct Fuel {
    float current           = 100.0f;
    float max               = 100.0f;
    float idleDrainPerSec   = 0.05f;   // ~33 min idle
    float accelDrainPerUnit = 0.35f;   // ~4 min at full throttle

    bool depleted() const { return current <= 0.0f; }
    float ratio()   const { return max > 0.0f ? current / max : 0.0f; }
};

// ---------------------------------------------------------------------------
// Rigid body physics (concepts adapted from research/physics3D)
// ---------------------------------------------------------------------------
//
// State for a 3D rigid body. Linear and angular velocities live here;
// orientation/position live in Transform. Inertia tensor is stored in body
// frame (invInertiaLocal); the rigid body system rotates it into world space
// each frame as needed.
//
// Three "classes" of body, governed by two flags:
//   fixed=true          → infinite mass, never moves (ground, walls)
//   kinematic=true      → infinite mass for impulse purposes, but its
//                         linearVel is still read so it transfers momentum
//                         into dynamic bodies (e.g. the car shoving items)
//   else                → fully dynamic: gravity + impulses move it
//
// fixed and kinematic both result in inverseMass = 0 and invInertiaLocal = 0.
struct RigidBody {
    float     mass            = 1.0f;
    float     inverseMass     = 1.0f;
    float     restitution     = 0.4f;
    float     friction        = 0.5f;
    glm::mat3 invInertiaLocal {0.0f};     // body-frame inverse inertia tensor
    glm::vec3 linearVel       {0.0f};
    glm::vec3 angularVel      {0.0f};     // world-space ω (rad/s)
    glm::vec3 forceAccum      {0.0f};
    glm::vec3 torqueAccum     {0.0f};
    bool      fixed           = false;
    bool      kinematic       = false;
    bool      useGravity      = true;
    float     linearDamping   = 0.05f;    // per-second
    float     angularDamping  = 0.30f;
};

// Oriented bounding box collider. Half-extents in the entity's local space;
// the world-space OBB is reconstructed from Transform each step.
struct BoxCollider {
    glm::vec3 halfExtents {0.5f, 0.5f, 0.5f};
    // When true, this collider is excluded from box-vs-plane (ground) contact
    // generation. Used on the player car so the suspension spring owns the
    // vertical axis instead of fighting the SAT ground impulse.
    bool skipGroundCollision = false;
};

// Infinite plane collider in world space. Stored as { n, d } with n·x = d.
// The ground uses this to avoid pretending a 100m × 100m sliver-thin OBB is
// meaningful for SAT.
struct PlaneCollider {
    glm::vec3 normal {0.0f, 1.0f, 0.0f};
    float     d      = 0.0f;
};

// ---------------------------------------------------------------------------
// Per-wheel suspension state — embedded in CarVehicle (4 per car).
// ---------------------------------------------------------------------------
struct WheelState {
    // ---- Configuration (set once at entity creation) ----------------------
    glm::vec3 localOffset       {0.0f};   // wheel attachment point in car-local space
    bool      isSteered         = false;  // front wheels turn with steering input
    bool      isDriven          = true;   // receives drive/brake torque

    float suspensionRestDist    = 0.5f;   // natural spring length (m)
    float suspensionTravel      = 0.35f;  // max compression beyond rest length (m)
    float springStrength        = 38000.0f; // N/m
    float springDamper          = 3800.0f;  // N·s/m
    float gripFactor            = 0.75f;  // [0..1] lateral grip multiplier
    float wheelMass             = 20.0f;  // kg — used for lateral impulse scaling
    float radius                = 0.35f;  // m — visual / clearance reference

    // ---- Runtime state (written by PhysicsSystem each frame) --------------
    bool  grounded              = false;
    float compressionRatio      = 0.0f;  // 0 = fully extended, 1 = fully compressed
    float contactDist           = 0.0f;  // last successful raycast distance
};

// ---------------------------------------------------------------------------
// CarVehicle — top-level vehicle component; holds the 4-wheel suspension state
// and smoothed drive inputs. Added to the player entity alongside RigidBody.
// ---------------------------------------------------------------------------
struct CarVehicle {
    static constexpr int kWheelCount = 4;
    // Layout: [0]=front-left, [1]=front-right, [2]=rear-left, [3]=rear-right
    WheelState wheels[kWheelCount];

    float topSpeed   = 30.0f;    // m/s — used to normalise speed for power curve
    float maxTorque  = 4000.0f;  // N — peak drive force per driven wheel

    // Smoothed inputs written by PhysicsSystem each substep.
    float appliedThrottle = 0.0f;  // [0, +1]
    float appliedSteer    = 0.0f;  // [-1, +1]
};

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
struct MeshRef {
    MeshId meshId = MeshId::Cube;
};

struct Material {
    GLuint    program = 0;
    glm::vec3 color   {1.0f, 1.0f, 1.0f};
    bool      visible = true;
};

// One draw call worth of GPU data from a loaded GLTF/OBJ model.
// Indices are GL_UNSIGNED_INT. diffuseTex=0 means use diffuseColor instead.
struct SubMesh {
    GLuint   vao        = 0;
    GLuint   vbo        = 0;
    GLuint   ibo        = 0;
    uint32_t indexCount = 0;
    GLuint   diffuseTex = 0;       // 0 = no texture, use diffuseColor
    glm::vec4 diffuseColor {1.0f, 1.0f, 1.0f, 1.0f};
};

// Collection of SubMeshes that make up an imported 3D model.
// Replaces MeshRef+Material on entities that have been loaded via assimp.
struct ModelMesh {
    std::vector<SubMesh> submeshes;
    GLuint    modelShaderProgram = 0;  // shader that handles UV + texture uniforms
    bool      visible            = true;
    glm::quat rotOffset          = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // baked into model matrix
};

// ---------------------------------------------------------------------------
// Item / gear / inventory
// ---------------------------------------------------------------------------
struct ItemComponent {
    ItemId   itemId    = ItemId::Count;
    uint32_t stackSize = 1;
    uint32_t maxStack  = 1;
};

struct GearComponent {
    float   dHandling   = 0.0f;
    float   dPower      = 0.0f;
    float   dEfficiency = 0.0f;
    float   dTopSpeed   = 0.0f;
    uint8_t tier        = 1;
};

struct ActivePowerup {
    ItemId sourceItem             = ItemId::Count;
    float  durationRemaining      = 0.0f;
    float  dPower                 = 0.0f;
    float  dTopSpeed              = 0.0f;
    float  shieldAmount           = 0.0f;  // MagneticShield: blocks hazard drain
};

struct Inventory {
    static constexpr int kSlots = 20;
    struct Slot {
        ItemId   id  = ItemId::Count;
        uint32_t qty = 0;
    };
    std::array<Slot, kSlots> slots{};
    entt::entity equippedWheels     = entt::null;
    entt::entity equippedSuspension = entt::null;
    entt::entity equippedEngine     = entt::null;

    // Returns slot index holding itemId, or -1
    int findSlot(ItemId id) const {
        for (int i = 0; i < kSlots; ++i)
            if (slots[i].id == id) return i;
        return -1;
    }
    // Returns first empty slot index, or -1
    int firstEmpty() const {
        for (int i = 0; i < kSlots; ++i)
            if (slots[i].id == ItemId::Count) return i;
        return -1;
    }
};

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
struct CameraFollow {
    entt::entity target    = entt::null;
    glm::vec3    offset    {0.0f, 4.0f, 10.0f};
    float        lag       = 5.0f;
    bool         enabled   = true;   // false = free-look / gizmo override
};

struct CameraState {
    glm::vec3 position {0.0f, 5.0f, 15.0f};
    glm::vec3 forward  {0.0f, 0.0f, -1.0f};
    float     fovDeg   = 60.0f;
    glm::mat4 view     {1.0f};
    glm::mat4 proj     {1.0f};
};

// ---------------------------------------------------------------------------
// Light
// ---------------------------------------------------------------------------
enum class LightType { Ambient = 0, Directional = 1, Spot = 2 };

struct DirectionalLight {
    glm::vec3 direction  {-0.4f, -0.8f, -0.4f};
    glm::vec3 color      {1.0f,  1.0f,  1.0f};
    glm::vec3 position   {2.0f,  3.0f,  2.0f};
    bool      ambientOn  = true;
    LightType type       = LightType::Directional;
    float     spotCutoff = 30.0f;  // half-angle in degrees for spot lights
};
