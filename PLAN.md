# Plan: Integrate ImGuizmo for Entity Transform Manipulation

## Overview

Add ImGuizmo to provide visual 3D transform gizmos (translate/rotate/scale) for
any renderable entity with a `Transform` component. The gizmo is drawn over the
viewport and allows direct manipulation of an entity's position, rotation, and
scale.

## Changes

### 1. Makefile (lines 21-55)

- Add `lib/imguizmo/ImGuizmo.cpp` to `IMGUI_SRCS`
- Add `-Ilib/imguizmo` to `INCLUDES`

### 2. `src/app.h`

Add member variables after `showShaderPopup_` (~line 50):

```cpp
    // ImGuizmo state
    ImGuizmo::OPERATION gizmoOperation_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      gizmoMode_      = ImGuizmo::WORLD;
    bool                useGizmoSnap_   = false;
```

Add method declaration (~line 92):

```cpp
    void drawGizmo();
```

### 3. `src/app.cpp`

#### 3a. Add include (after line 21)

```cpp
#include "ImGuizmo.h"
```

#### 3b. In `frame()` — call `ImGuizmo::BeginFrame()` after `ImGui::NewFrame()` (~line 598)

#### 3c. In `frame()` — skip game tick when gizmo is active (~line 586)

Change:
```cpp
if (gameRunning_) tickSystems(dt);
```
To:
```cpp
if (gameRunning_ && !ImGuizmo::IsUsing()) tickSystems(dt);
```

#### 3d. In `frame()` — call `drawGizmo()` after `drawPanel()` (~line 629)

#### 3e. In `frame()` — add keyboard shortcuts (~line 580)

- `T` key → switch to TRANSLATE
- `E` key → switch to ROTATE
- `R` key → switch to SCALE
- `S` key → toggle snap

#### 3f. Implement `drawGizmo()` method at end of file

Logic:
1. Early return if `selectedEntity_` is `entt::null` or lacks `Transform`
2. Get view/projection matrices from `CameraState` on `cameraEcsEntity_`
3. Convert `Transform::toMatrix()` (glm column-major) → row-major `float[16]` via `glm::transpose()`
4. Call `ImGuizmo::SetRect(0, 0, vpW_, vpH_)`
5. Call `ImGuizmo::Manipulate(view, proj, operation, mode, matrix, ...)`
6. If Manipulate returns true (matrix changed), decompose back to
   position/rotation/scale and update the `Transform` component

#### 3g. In `drawPanel()` Uniforms section (~line 351)

When a Transform entity is selected, add gizmo controls **above** the existing
Transform sliders:

- Radio buttons: Translate / Rotate / Scale
- Toggle: Local / World mode
- Checkbox: Snap + snap value input (per operation, following README example)

## Matrix Conversion Approach

ImGuizmo expects row-major `float[16]`; glm uses column-major.

- Convert: `glm::transpose(Transform::toMatrix())` → `float matrix[16]`
- Pass `matrix` to `ImGuizmo::Manipulate()`
- After Manipulate, convert back by treating `float[16]` as a transposed
  `glm::mat4`, then extract position/rotation/scale

Note: `ImGuizmo::DecomposeMatrixToComponents` has known numerical stability
issues per ImGuizmo docs, but is acceptable for editor use. The direct matrix
from `Manipulate()` is the primary path; decompose is only for the InputFloat3
UI widgets.

## Tradeoffs

1. **Gizmo rendered over viewport**: Using `SetRect(0,0,vpW_,vpH_)` matches the
   viewport background window so the gizmo aligns with the 3D render. Simpler
   and matches the README example.

2. **Skip systems when gizmo active**: Prevents physics from overriding manual
   positioning. Car won't respond to input while manipulating — correct behavior
   for a transform editor.

3. **Any Transform entity**: Camera, lights, world items, and player all have
   `Transform` components, so all will work with the gizmo.
