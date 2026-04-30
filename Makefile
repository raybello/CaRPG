# =====================================================================
# Web3D - tiny shader playground
# Single Makefile that builds either a native C++ binary OR a web build
# via Emscripten.
#
# Usage:
#   make                 # native build  ->  build/web3d
#   make run             # native build + run
#   make web             # emscripten build  ->  web/index.html
#   make serve           # emscripten build + python http.server
#   make clean           # clean both
#
# Configuration:
#   WEB=1                # force-build the web target via the default rule
# =====================================================================

# ---------- common config ----------
APP        := web3d
SRC_DIR    := src
IMGUI_DIR  := lib/imgui
BACKEND_DIR:= $(IMGUI_DIR)/backends
IMGUIZMO_DIR:= lib/imguizmo

# Source files (project + imgui)
APP_SRCS := \
    $(SRC_DIR)/main.cpp \
    $(SRC_DIR)/app.cpp \
    $(SRC_DIR)/scene.cpp \
    $(SRC_DIR)/shader.cpp \
    $(SRC_DIR)/renderer.cpp \
    $(SRC_DIR)/game/item_catalog.cpp \
    $(SRC_DIR)/systems/input_system.cpp \
    $(SRC_DIR)/systems/physics_system.cpp \
    $(SRC_DIR)/systems/fuel_system.cpp \
    $(SRC_DIR)/systems/stat_system.cpp \
    $(SRC_DIR)/systems/item_system.cpp \
    $(SRC_DIR)/systems/camera_system.cpp \
    $(SRC_DIR)/systems/render_system.cpp \
    $(SRC_DIR)/ui/hud.cpp \
    $(SRC_DIR)/ui/inventory_panel.cpp

IMGUI_SRCS := \
    $(IMGUI_DIR)/imgui.cpp \
    $(IMGUI_DIR)/imgui_demo.cpp \
    $(IMGUI_DIR)/imgui_draw.cpp \
    $(IMGUI_DIR)/imgui_tables.cpp \
    $(IMGUI_DIR)/imgui_widgets.cpp \
    $(BACKEND_DIR)/imgui_impl_sdl2.cpp \
    $(BACKEND_DIR)/imgui_impl_opengl3.cpp \
    $(IMGUIZMO_DIR)/ImGuizmo.cpp

ALL_SRCS := $(APP_SRCS) $(IMGUI_SRCS)

GLM_DIR    := lib/glm
ENTT_DIR   := lib/entt/single_include
JSON_DIR   := lib/json/single_include
INCLUDES := -I$(SRC_DIR) -I$(IMGUI_DIR) -I$(BACKEND_DIR) -I$(IMGUIZMO_DIR) -I$(GLM_DIR) -I$(ENTT_DIR) -I$(JSON_DIR)

# Shared compile flags (added to whichever toolchain is used)
COMMON_CXXFLAGS := -std=c++20 -Wall -Wformat -Wno-unused-function $(INCLUDES)

# ---------- native target ----------
NATIVE_BUILD_DIR := build
NATIVE_OBJ_DIR   := $(NATIVE_BUILD_DIR)/obj
NATIVE_BIN       := $(NATIVE_BUILD_DIR)/$(APP)

UNAME_S := $(shell uname -s)

NATIVE_CXX  := g++
NATIVE_CXXFLAGS := $(COMMON_CXXFLAGS) -O2 -g
NATIVE_LIBS :=

ifeq ($(UNAME_S),Linux)
    NATIVE_LIBS    += -lGL -ldl `sdl2-config --libs`
    NATIVE_CXXFLAGS += `sdl2-config --cflags`
endif
ifeq ($(UNAME_S),Darwin)
    NATIVE_LIBS    += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo `sdl2-config --libs`
    NATIVE_CXXFLAGS += `sdl2-config --cflags` -I/usr/local/include -I/opt/local/include -DGL_SILENCE_DEPRECATION
endif
ifeq ($(OS),Windows_NT)
    NATIVE_LIBS    += -lgdi32 -lopengl32 -limm32 `pkg-config --static --libs sdl2`
    NATIVE_CXXFLAGS += `pkg-config --cflags sdl2`
endif

# Map .cpp to .o under the build dir, mirroring directories so source names are unique
NATIVE_OBJS := $(patsubst %.cpp,$(NATIVE_OBJ_DIR)/%.o,$(ALL_SRCS))

# ---------- web target (emscripten) ----------
WEB_DIR        := web
WEB_OBJ_DIR    := $(WEB_DIR)/obj
WEB_OUT        := $(WEB_DIR)/index.html
WEB_CXX        := em++
WEB_EMS        := -s USE_SDL=2 -s DISABLE_EXCEPTION_CATCHING=1 -DIMGUI_IMPL_OPENGL_ES2
WEB_CXXFLAGS   := $(COMMON_CXXFLAGS) -Os $(WEB_EMS)
WEB_LDFLAGS    := -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s NO_EXIT_RUNTIME=0 \
                  -s ASSERTIONS=1 -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
                  $(WEB_EMS) \
                  --shell-file $(IMGUI_DIR)/examples/libs/emscripten/shell_minimal.html

WEB_OBJS := $(patsubst %.cpp,$(WEB_OBJ_DIR)/%.o,$(ALL_SRCS))

# ---------- top-level rules ----------
.PHONY: all native web run serve clean clean-native clean-web help dirs

all: native

native: $(NATIVE_BIN)

web: $(WEB_OUT)

run: native
	./$(NATIVE_BIN)

rerun: clean-native run

serve: web
	@echo "Serving $(WEB_DIR) at http://localhost:8000"
	cd $(WEB_DIR) && python3 -m http.server

# Generic per-cpp build rules (native)
$(NATIVE_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(NATIVE_CXX) $(NATIVE_CXXFLAGS) -c -o $@ $<

$(NATIVE_BIN): $(NATIVE_OBJS)
	@mkdir -p $(dir $@)
	$(NATIVE_CXX) $(NATIVE_CXXFLAGS) -o $@ $^ $(NATIVE_LIBS)
	@echo "============ Native build complete: $@ ============"

# Generic per-cpp build rules (web)
$(WEB_OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(WEB_CXX) $(WEB_CXXFLAGS) -c -o $@ $<

$(WEB_OUT): $(WEB_OBJS)
	@mkdir -p $(dir $@)
	$(WEB_CXX) -o $@ $(WEB_OBJS) $(WEB_LDFLAGS)
	@echo "============ Web build complete: $@ ============"

clean: clean-native clean-web

clean-native:
	rm -rf $(NATIVE_BUILD_DIR)

clean-web:
	rm -rf $(WEB_DIR)/obj $(WEB_DIR)/index.html $(WEB_DIR)/index.js $(WEB_DIR)/index.wasm $(WEB_DIR)/index.data

help:
	@echo "Targets:"
	@echo "  make            native build (default)"
	@echo "  make run        native build + run"
	@echo "  make web        emscripten build"
	@echo "  make serve      emscripten build + http server"
	@echo "  make clean      clean both"
