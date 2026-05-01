#pragma once
#include "ecs/components.h"
#include <string>

// Load a GLTF/OBJ/FBX model from disk via Assimp.
// Returns a ModelMesh with GPU-uploaded VAOs ready for rendering.
// On failure returns an empty ModelMesh (submeshes.size() == 0).
// modelShaderProgram must be set by the caller after loading.
ModelMesh loadModel(const std::string& path);

// Free all GPU resources owned by a ModelMesh (VAOs, VBOs, IBOs, textures).
void destroyModelMesh(ModelMesh& mm);
