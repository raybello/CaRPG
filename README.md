# Web3D — tiny shader playground

A minimal C++/OpenGL + ImGui scene viewer. A single cube is rendered to an
off-screen framebuffer that's shown inside an ImGui window. A side panel
(toggle with **`p`**) lets you select Cube / Camera / Light, tweak their
uniforms, and pop open a shader editor that **recompiles live** when you
hit *Save & Recompile*.

The project builds for **native** desktop and **web** (Emscripten / WebGL)
from one Makefile.

## Layout

```
.
├── Makefile        # native + emscripten in one file
├── src/            # all the application code
│   ├── main.cpp
│   ├── app.{h,cpp}     # SDL2 + ImGui bootstrap, panel UI, shader editor
│   ├── scene.{h,cpp}   # Cube / Camera / Light + per-item shaders & uniforms
│   ├── renderer.{h,cpp}# off-screen FBO + cube draw call
│   ├── shader.{h,cpp}  # compile / link helper
│   └── math.h          # tiny vec3 / mat4 / perspective / lookAt
└── lib/imgui/      # vendored Dear ImGui (sdl2 + opengl3 backends)
```

## Build & run

### Native

```sh
make            # → build/web3d
make run        # build & launch
```

Requires a working SDL2 (`brew install sdl2` on macOS,
`apt install libsdl2-dev` on Debian/Ubuntu) and a C++17 compiler.

### Web (Emscripten)

```sh
source ~/emsdk/emsdk_env.sh   # or wherever emsdk lives
make web                      # → web/index.{html,js,wasm}
make serve                    # build + serve at http://localhost:8000
```

If `em++` isn't available the build will fall back to telling you so.

## Controls

* **`p`** — show / hide the inspector panel
* Drag the panel + viewport windows to rearrange
* Inspector → *Edit Shader Source...* opens a popup with the vertex +
  fragment GLSL for the selected scene item; *Save & Recompile* builds
  the new program and the viewport updates immediately on success or
  shows the compiler error otherwise.

## Notes

* Shaders are written in GLSL ES 100 when targeting the web (WebGL 1) and
  GLSL 130 (`#version 130`) on desktop. Default sources are built for
  whichever target is active so a fresh build "just works".
* The cube uses a Blinn-Phong-ish shading model that consumes
  `uMaterialColor`, `uLightColor`, `uLightDir`, `uLightPos`,
  `uIsDirectional`, `uAmbientOn`, `uCameraPos`, `uModel`, `uView`,
  `uProj`. You can rip these out in the shader editor and replace with
  whatever you want — uniforms that the new program does not declare are
  silently ignored.
