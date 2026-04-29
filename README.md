# rve — 3D Car RPG

A 3D driving RPG built in C++20 with OpenGL, SDL2, Dear ImGui, and the
[EnTT](https://github.com/skypjack/entt) entity-component system. The player
drives a car through a world, collecting loot, managing fuel, equipping gear,
and using powerups — all rendered to an offscreen framebuffer displayed inside
an ImGui viewport. Builds for **native** desktop and **web** (Emscripten /
WebGL) from one Makefile.

## Layout

```
.
├── Makefile
├── LOOT.json               # item definitions (loaded at runtime)
├── src/
│   ├── main.cpp
│   ├── app.{h,cpp}         # SDL2 + ImGui bootstrap
│   ├── scene.{h,cpp}       # world setup, entity spawning
│   ├── renderer.{h,cpp}    # offscreen FBO, mesh upload, draw calls
│   ├── shader.{h,cpp}      # GLSL compile/link helpers
│   ├── glm_math.h
│   ├── ecs/
│   │   ├── components.h    # all ECS component types
│   │   └── world.h
│   ├── game/
│   │   ├── item_catalog.{h,cpp}   # parses LOOT.json into ItemDef structs
│   │   └── game_events.h
│   ├── systems/
│   │   ├── input_system.{h,cpp}
│   │   ├── physics_system.{h,cpp}
│   │   ├── fuel_system.{h,cpp}
│   │   ├── stat_system.{h,cpp}
│   │   ├── item_system.{h,cpp}
│   │   ├── camera_system.{h,cpp}
│   │   └── render_system.{h,cpp}
│   └── ui/
│       ├── hud.{h,cpp}             # fuel bar, speed, active powerup timers
│       └── inventory_panel.{h,cpp} # 20-slot grid, equip / use / salvage
└── lib/
    ├── entt/       # EnTT ECS (header-only)
    ├── imgui/      # Dear ImGui + SDL2/OpenGL3 backends
    ├── json/       # nlohmann/json (header-only)
    └── glm/        # OpenGL Mathematics
```

## Architecture

The game is structured around an ECS world (EnTT). Each frame the systems run
in order:

| System | Responsibility |
|---|---|
| `InputSystem` | Maps keyboard state → `CarInput` throttle/brake/steer/handbrake |
| `PhysicsSystem` | Integrates `CarInput` + `DerivedCarStats` → `Velocity` / `Transform` |
| `FuelSystem` | Drains `Fuel` based on throttle and efficiency; applies hazard drain when shieldless |
| `StatSystem` | Recalculates `DerivedCarStats` from base + equipped gear + active powerups (dirty flag) |
| `ItemSystem` | Handles world pickup, inventory add, consume, salvage, crafting combine |
| `CameraSystem` | Smoothly follows the player entity via `CameraFollow` lag parameter |
| `RenderSystem` | Draws all `MeshRef` + `Material` entities; uploads view/proj from `CameraState` |

**Components** (`src/ecs/components.h`): `Transform`, `Velocity`, `CarInput`,
`BaseCarStats`, `DerivedCarStats`, `Fuel`, `Inventory`, `GearComponent`,
`ActivePowerup`, `CameraFollow`, `CameraState`, `DirectionalLight`, `MeshRef`,
`Material`, plus tag types `PlayerTag` / `NpcTag` / `ItemWorldTag`.

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
make            # → build/web3d
make run        # build & launch
```

Requires SDL2 (`brew install sdl2` on macOS, `apt install libsdl2-dev` on
Debian/Ubuntu) and a C++20-capable compiler.

### Web (Emscripten)

```sh
source ~/emsdk/emsdk_env.sh   # or wherever emsdk lives
make web                      # → web/index.{html,js,wasm}
make serve                    # build + serve at http://localhost:8000
```

## Controls

* **WASD / arrow keys** — throttle, brake, steer
* **`p`** — show / hide the ImGui inspector panel
* HUD shows fuel bar, current speed, and active powerup timers

## Shader notes

Shaders are written in GLSL ES 100 when targeting WebGL and GLSL 130
(`#version 130`) on desktop. Default sources are selected at build time so a
fresh build just works. The inspector panel's *Edit Shader Source...* popup
lets you edit and live-recompile shaders for any scene entity.
