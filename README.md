# carpg — 3D Car RPG

A 3D driving RPG built in C++20 with OpenGL, SDL2, Dear ImGui, and the
[EnTT](https://github.com/skypjack/entt) entity-component system. The player
drives a car through a world, collecting loot, managing fuel, equipping gear,
and using powerups — all rendered to an offscreen framebuffer displayed inside
an ImGui viewport. HUD stats are composited over the scene as a transparent
texture overlay. Builds for **native** desktop and **web** (Emscripten /
WebGL2) from one Makefile.

## Layout

```
.
├── Makefile
├── LOOT.json               # item definitions (loaded at runtime)
├── models/                 # GLTF/OBJ assets loaded via Assimp
├── src/
│   ├── main.cpp
│   ├── app.{h,cpp}         # SDL2 + ImGui bootstrap, game loop
│   ├── scene.{h,cpp}       # shader programs, world setup
│   ├── renderer.{h,cpp}    # offscreen FBO, mesh upload, draw calls
│   ├── shader.{h,cpp}      # GLSL compile/link helpers
│   ├── model_loader.{h,cpp}# Assimp GLTF/OBJ → SubMesh upload
│   ├── glm_math.h
│   ├── ecs/
│   │   └── components.h    # all ECS component types
│   ├── game/
│   │   ├── item_catalog.{h,cpp}   # parses LOOT.json into ItemDef structs
│   │   └── game_events.h
│   ├── systems/
│   │   ├── input_system.{h,cpp}
│   │   ├── physics_system.{h,cpp} # wheel suspension raycasts, vehicle dynamics
│   │   ├── rigid_body_system.{h,cpp} # SAT collision, impulse resolution
│   │   ├── fuel_system.{h,cpp}
│   │   ├── stat_system.{h,cpp}
│   │   ├── item_system.{h,cpp}
│   │   ├── camera_system.{h,cpp}
│   │   └── render_system.{h,cpp}
│   └── ui/
│       ├── hud_texture.{h,cpp}     # HUD rendered to GL texture, composited over viewport
│       └── inventory_panel.{h,cpp} # 20-slot grid, equip / use / salvage
└── lib/
    ├── entt/       # EnTT ECS (header-only)
    ├── imgui/      # Dear ImGui + SDL2/OpenGL3 backends
    ├── imguizmo/   # ImGuizmo transform gizmo
    ├── json/       # nlohmann/json (header-only)
    ├── glm/        # OpenGL Mathematics
    └── assimp/     # Assimp model loader (native only)
```

## Architecture

The game is structured around an ECS world (EnTT). Each frame the systems run
in order:

| System | Responsibility |
|---|---|
| `InputSystem` | Maps keyboard state → `CarInput` throttle/brake/steer/handbrake |
| `PhysicsSystem` | Wheel suspension raycasts, drive/brake forces, lateral grip, vehicle dynamics |
| `RigidBodySystem` | SAT box-box and box-plane collision detection, impulse resolution |
| `FuelSystem` | Drains `Fuel` based on throttle and efficiency; applies hazard drain when shieldless |
| `StatSystem` | Recalculates `DerivedCarStats` from base + equipped gear + active powerups |
| `ItemSystem` | World pickup, inventory add, consume, salvage, crafting combine |
| `CameraSystem` | Smoothly follows the player entity via `CameraFollow` lag parameter |
| `RenderSystem` | Draws all entities; handles both primitive meshes and Assimp `ModelMesh` |

**Components** (`src/ecs/components.h`): `Transform`, `RigidBody`,
`BoxCollider`, `PlaneCollider`, `CarVehicle`, `WheelState`, `Velocity`,
`CarInput`, `BaseCarStats`, `DerivedCarStats`, `Fuel`, `Inventory`,
`GearComponent`, `ActivePowerup`, `CameraFollow`, `CameraState`,
`DirectionalLight`, `MeshRef`, `Material`, `ModelMesh`, `SubMesh`, plus tag
types `PlayerTag` / `NpcTag` / `ItemWorldTag` / `ObstacleTag` / `PushableTag`.

## Physics

Vehicles use a 4-wheel raycast suspension model. Each wheel fires a ray
downward in world space; hits are tested against all `BoxCollider` and
`PlaneCollider` entities, including tilted OBBs (ramps). The spring force,
damping, drive torque, and lateral grip impulse are applied at the contact
point each substep.

General rigid bodies (crates, obstacles) go through SAT-based box-vs-box and
box-vs-plane contact generation with restitution and friction impulses.

## Scene obstacles

The world contains a mix of static and dynamic props:

| Type | Shape | Behaviour |
|---|---|---|
| Ramp | Tilted OBB | Static; car drives up/over |
| Flat-top platform | OBB | Static; car can drive on top |
| Speed bump | Cylinder (lying flat) | Static; low bump across the road |
| Jersey barrier | Tall OBB | Static; deflects the car |
| Crate | OBB | Pushable; fully dynamic rigid body |

## Item system

Items are defined in [`LOOT.json`](LOOT.json) and loaded into an `ItemCatalog`
at startup. The inventory holds 20 slots; items stack up to their `maxStack`.

| Category | Item | Effect |
|---|---|---|
| Powerup | Nitro Canister | +60 power, +10 top speed for 8 s |
| Powerup | Magnetic Shield | Blocks hazard fuel drain for 15 s |
| Crafting | Engine Part | 3× combine → engine upgrade (+15 power, +0.2 efficiency) |
| Crafting | Steel Coil | 2× combine → suspension upgrade (+0.3 handling) |
| Gear | Turbo Wheels (Tier 2) | +0.3 handling, +5 top speed (wheels slot) |
| Gear | Off-Road Suspension | +0.5 handling, −0.1 efficiency (suspension slot) |
| Consumable | Fuel Canister | Instantly restores 50 fuel |
| Consumable | Overcharge Cell | +80 power burst for 3 s |
| Lore | Ancient Road Sign | Collect 10 → unlock hidden zone |
| Salvage | Abandoned Wreck | Strip for 2 Engine Parts + 1 Steel Coil |

## Build & run

### Native

```sh
make            # → build/carpg
make run        # build & launch
```

Requires SDL2 and Assimp:
```sh
brew install sdl2 assimp pkg-config   # macOS
apt install libsdl2-dev libassimp-dev # Debian/Ubuntu
```

### Web (Emscripten)

Assimp is excluded from the web build (stub stubs are provided). Models are
not available in the web build.

```sh
source ~/emsdk/emsdk_env.sh
make web                    # → docs/index.{html,js,wasm}
make serve                  # build + serve at http://localhost:8000
```

The web build targets WebGL2 / OpenGL ES3.

## Controls

| Key | Action |
|---|---|
| `W` / `Up` | Throttle |
| `S` / `Down` | Brake / reverse |
| `A` / `D` | Steer left / right |
| `Space` | Handbrake |
| `Q` | Reset car to spawn position |
| `I` | Toggle inventory panel |
| `P` | Toggle inspector panel |
| `T` / `E` / `R` | Gizmo: translate / rotate / scale |

## HUD

The HUD is rendered into a CPU pixel buffer each frame using an 8×8 bitmap
font, then uploaded to a GL texture composited over the 3D viewport with
`ImDrawList::AddImage`. The texture background is fully transparent — only
the glyphs and fuel bar are opaque.

Displayed: current speed, top speed, power, fuel bar (color-coded green →
orange → red), low fuel warning (blinking), out-of-fuel notice, and a
full-screen GAME OVER overlay when fuel hits zero.

## Shader notes

Shaders are GLSL ES 300 when targeting WebGL2 and GLSL 130 on desktop.
The inspector panel's *Edit Shader Source...* popup lets you live-recompile
shaders for any scene entity.
