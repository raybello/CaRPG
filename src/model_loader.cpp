#include "model_loader.h"

#ifdef __EMSCRIPTEN__

ModelMesh loadModel(const std::string&) { return {}; }
void destroyModelMesh(ModelMesh& mm) { mm.submeshes.clear(); }

#else

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef __APPLE__
#  ifndef GL_SILENCE_DEPRECATION
#    define GL_SILENCE_DEPRECATION
#  endif
#  include <OpenGL/gl3.h>
#else
#  include <GL/glew.h>
#endif

#include <cstdio>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Vertex layout sent to GPU: position(3) + normal(3) + uv(2) = 8 floats
// Attribute slots: aPos=0, aNormal=1, aTexCoord=2
// ---------------------------------------------------------------------------
struct ModelVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u,  v;
};

// ---------------------------------------------------------------------------
// Texture cache — avoids loading the same PNG twice across sub-meshes.
// Keyed by resolved absolute path.
// ---------------------------------------------------------------------------
static std::unordered_map<std::string, GLuint> gTexCache;

static GLuint loadTexture(const std::string& path) {
    auto it = gTexCache.find(path);
    if (it != gTexCache.end()) return it->second;

    stbi_set_flip_vertically_on_load(true);
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) {
        std::fprintf(stderr, "[model_loader] stbi_load failed: %s\n", path.c_str());
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    gTexCache[path] = tex;
    return tex;
}

// ---------------------------------------------------------------------------
// Resolve a texture path relative to the model's directory.
// ---------------------------------------------------------------------------
static std::string resolveTexPath(const std::string& modelDir,
                                  const std::string& texPath) {
    if (texPath.empty()) return "";
    // Already absolute
    if (texPath[0] == '/') return texPath;
    return modelDir + "/" + texPath;
}

static std::string dirOf(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? "." : path.substr(0, pos);
}

// ---------------------------------------------------------------------------
// Upload one aiMesh to the GPU. Returns a filled SubMesh on success.
// ---------------------------------------------------------------------------
static SubMesh uploadMesh(const aiMesh* mesh,
                          const aiScene* scene,
                          const std::string& modelDir) {
    SubMesh sm;

    // --- Build vertex buffer ---
    std::vector<ModelVertex> verts;
    verts.reserve(mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        ModelVertex mv;
        mv.px = mesh->mVertices[i].x;
        mv.py = mesh->mVertices[i].y;
        mv.pz = mesh->mVertices[i].z;
        if (mesh->HasNormals()) {
            mv.nx = mesh->mNormals[i].x;
            mv.ny = mesh->mNormals[i].y;
            mv.nz = mesh->mNormals[i].z;
        } else {
            mv.nx = 0.0f; mv.ny = 1.0f; mv.nz = 0.0f;
        }
        if (mesh->HasTextureCoords(0)) {
            mv.u = mesh->mTextureCoords[0][i].x;
            mv.v = mesh->mTextureCoords[0][i].y;
        } else {
            mv.u = 0.0f; mv.v = 0.0f;
        }
        verts.push_back(mv);
    }

    // --- Build index buffer ---
    std::vector<uint32_t> indices;
    indices.reserve(mesh->mNumFaces * 3);
    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace& face = mesh->mFaces[f];
        if (face.mNumIndices != 3) continue;   // only triangles (triangulate flag set)
        indices.push_back(face.mIndices[0]);
        indices.push_back(face.mIndices[1]);
        indices.push_back(face.mIndices[2]);
    }
    if (indices.empty()) return sm;
    sm.indexCount = static_cast<uint32_t>(indices.size());

    // --- Upload to GPU ---
    glGenVertexArrays(1, &sm.vao);
    glGenBuffers(1, &sm.vbo);
    glGenBuffers(1, &sm.ibo);

    glBindVertexArray(sm.vao);

    glBindBuffer(GL_ARRAY_BUFFER, sm.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(ModelVertex)),
                 verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sm.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    // aPos (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                          reinterpret_cast<void*>(offsetof(ModelVertex, px)));
    // aNormal (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                          reinterpret_cast<void*>(offsetof(ModelVertex, nx)));
    // aTexCoord (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                          reinterpret_cast<void*>(offsetof(ModelVertex, u)));

    glBindVertexArray(0);

    // --- Load diffuse texture from material ---
    if (mesh->mMaterialIndex < scene->mNumMaterials) {
        const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

        // Try diffuse texture first
        if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            aiString texFile;
            mat->GetTexture(aiTextureType_DIFFUSE, 0, &texFile);
            std::string resolved = resolveTexPath(modelDir, texFile.C_Str());
            sm.diffuseTex = loadTexture(resolved);
        }

        // Fallback: read base color from material properties
        if (sm.diffuseTex == 0) {
            aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
            if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
                sm.diffuseColor = glm::vec4(color.r, color.g, color.b, color.a);
            }
        }
    }

    return sm;
}

// ---------------------------------------------------------------------------
// Traverse aiNode tree, collecting mesh indices
// ---------------------------------------------------------------------------
static void collectMeshes(const aiNode* node, const aiScene* scene,
                           const std::string& modelDir,
                           std::vector<SubMesh>& out) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        unsigned int meshIdx = node->mMeshes[i];
        if (meshIdx < scene->mNumMeshes) {
            SubMesh sm = uploadMesh(scene->mMeshes[meshIdx], scene, modelDir);
            if (sm.indexCount > 0) out.push_back(sm);
        }
    }
    for (unsigned int c = 0; c < node->mNumChildren; ++c)
        collectMeshes(node->mChildren[c], scene, modelDir, out);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
ModelMesh loadModel(const std::string& path) {
    ModelMesh result;

    Assimp::Importer importer;
    const unsigned int flags =
        aiProcess_Triangulate       |
        aiProcess_GenSmoothNormals  |
        aiProcess_FlipUVs           |
        aiProcess_CalcTangentSpace  |
        aiProcess_JoinIdenticalVertices;

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        std::fprintf(stderr, "[model_loader] Assimp error: %s\n", importer.GetErrorString());
        return result;
    }

    std::string modelDir = dirOf(path);
    collectMeshes(scene->mRootNode, scene, modelDir, result.submeshes);

    std::fprintf(stdout, "[model_loader] Loaded '%s': %zu submeshes\n",
                 path.c_str(), result.submeshes.size());
    return result;
}

void destroyModelMesh(ModelMesh& mm) {
    for (auto& sm : mm.submeshes) {
        if (sm.vao) { glDeleteVertexArrays(1, &sm.vao); sm.vao = 0; }
        if (sm.vbo) { glDeleteBuffers(1, &sm.vbo);      sm.vbo = 0; }
        if (sm.ibo) { glDeleteBuffers(1, &sm.ibo);      sm.ibo = 0; }
        // Textures are cached — don't delete here; they may be shared.
    }
    mm.submeshes.clear();
}

#endif // !__EMSCRIPTEN__
