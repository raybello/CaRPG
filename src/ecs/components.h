#pragma once

#include <array>
#include <string>
#include <vector>
#include "glm_math.h"
#include <entt/entt.hpp>

// Forward declarations for GL types used in Material
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
    float idleDrainPerSec   = 0.5f;
    float accelDrainPerUnit = 2.0f;

    bool depleted() const { return current <= 0.0f; }
    float ratio()   const { return max > 0.0f ? current / max : 0.0f; }
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
