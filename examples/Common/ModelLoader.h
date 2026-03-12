#pragma once

#include "EOS.h"
#include "utils.h"
#include "glm/fwd.hpp"
#include "glm/detail/type_quat.hpp"
#include "glm/gtc/quaternion.hpp"

#include "assimp/cimport.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

// Convert Assimp row-major matrix to GLM column-major matrix
inline glm::mat4 AiMatrix4x4ToGlm(const aiMatrix4x4& m)
{
    return glm::mat4
    {
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    };
}

struct TextureHandles final
{
    EOS::Holder<EOS::TextureHandle> albedo;
    EOS::Holder<EOS::TextureHandle> normal;
    EOS::Holder<EOS::TextureHandle> metallicRoughness;
};

struct MeshEntry final
{
    uint32_t vertexOffset;          // offset into the vertex buffer
    uint32_t indexOffset;           // offset into the index buffer
    uint32_t indexCount;
    uint32_t drawDataIndex;         // index into the DrawData buffer

    uint32_t albedoTextureIdx               = 0;
    uint32_t normalTextureIdx               = 0;
    uint32_t metallicRoughnessTextureIdx    = 0;

    glm::mat4 transform{};
};

struct VertexInformation final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
    glm::vec4 color;
};

struct Scene final
{
    std::vector<VertexInformation> vertices;
    std::vector<uint32_t>          indices;
    std::vector<MeshEntry>         meshes;
    std::vector<TextureHandles>    textureHandles;
};

inline Scene LoadModel(const std::filesystem::path& modelPath, EOS::IContext* context)
{

    const aiScene* scene = aiImportFile(modelPath.string().c_str(), aiProcess_Triangulate | aiProcess_CalcTangentSpace);
    CHECK(scene && scene->HasMeshes(), "Could not load mesh: {}", modelPath.string());

    const uint32_t nMeshes = scene->mNumMeshes;

    // -----------------------------------------------------------------------
    // Step 1 – load geometry and textures once per unique  mesh
    // -----------------------------------------------------------------------
    struct MeshGeometry
    {
        uint32_t vertexOffset;
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t albedoIdx              = 0;
        uint32_t normalIdx              = 0;
        uint32_t metallicRoughnessIdx   = 0;
    };

    Scene importedScene{};
    importedScene.textureHandles.resize(nMeshes);
    std::vector<MeshGeometry> geomCache(nMeshes);

    for (uint32_t meshIdx = 0; meshIdx < nMeshes; ++meshIdx)
    {
        const aiMesh* mesh   = scene->mMeshes[meshIdx];
        MeshGeometry& geom   = geomCache[meshIdx];
        TextureHandles& texH = importedScene.textureHandles[meshIdx];

        geom.vertexOffset = static_cast<uint32_t>(importedScene.vertices.size());
        geom.indexOffset  = static_cast<uint32_t>(importedScene.indices.size());

        const aiColor4D* colors = mesh->mColors[0];
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
            const aiVector3D v  = mesh->mVertices[i];
            const aiVector3D uv = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][i] : aiVector3D(0, 0, 0);
            const aiVector3D n  = mesh->mNormals[i];
            const aiVector3D t  = mesh->mTangents  ? mesh->mTangents[i] : aiVector3D(1, 0, 0);

            importedScene.vertices.emplace_back(VertexInformation{
                .position = glm::vec3(v.x, v.y, v.z),
                .normal   = glm::vec3(n.x, n.y, n.z),
                .uv       = glm::vec2(uv.x, 1.0f - uv.y),
                .tangent  = glm::vec4(t.x, t.y, t.z, 1.0f),
                .color    = colors ? glm::vec4(colors[i].r, colors[i].g, colors[i].b, colors[i].a) : glm::vec4(1.0f),
            });
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
            for (int j = 0; j < 3; ++j)
                importedScene.indices.emplace_back(mesh->mFaces[i].mIndices[j]);

        geom.indexCount = static_cast<uint32_t>(importedScene.indices.size()) - geom.indexOffset;

        // Load textures for this material
        if (scene->mNumMaterials > 0 && mesh->mMaterialIndex < scene->mNumMaterials)
        {
            const aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

            auto loadTex = [&](aiTextureType type) -> EOS::Holder<EOS::TextureHandle>
            {
                if (material->GetTextureCount(type) == 0) return {};
                aiString texPath;
                if (material->GetTexture(type, 0, &texPath) != aiReturn_SUCCESS) return {};
                std::filesystem::path fullPath = modelPath.parent_path() / texPath.C_Str();
                return EOS::LoadTexture(
                {
                    .filePath    = fullPath,
                    .compression = (type == aiTextureType_NORMALS) ? EOS::Compression::BC5 : EOS::Compression::BC7,
                    .context     = context,
                });
            };

            texH.albedo             = loadTex(aiTextureType_DIFFUSE);
            texH.normal             = loadTex(aiTextureType_NORMALS);
            texH.metallicRoughness  = loadTex(aiTextureType_GLTF_METALLIC_ROUGHNESS);

            geom.albedoIdx             = texH.albedo.Index();
            geom.normalIdx             = texH.normal.Index();
            geom.metallicRoughnessIdx  = texH.metallicRoughness.Index();
        }
    }

    // -----------------------------------------------------------------------
    // Step 2 – traverse the node hierarchy, emit one MeshEntry per instance
    // -----------------------------------------------------------------------
    std::function<void(const aiNode*, const glm::mat4&)> traverseInstances;
    traverseInstances = [&](const aiNode* node, const glm::mat4& parentTransform)
    {
        const glm::mat4 worldTransform = parentTransform * AiMatrix4x4ToGlm(node->mTransformation);

        for (uint32_t i = 0; i < node->mNumMeshes; ++i)
        {
            const uint32_t    meshIdx = node->mMeshes[i];
            const MeshGeometry& geom  = geomCache[meshIdx];

            importedScene.meshes.push_back(MeshEntry
            {
                .vertexOffset               = geom.vertexOffset,
                .indexOffset                = geom.indexOffset,
                .indexCount                 = geom.indexCount,
                .drawDataIndex              = 0,
                .albedoTextureIdx           = geom.albedoIdx,
                .normalTextureIdx           = geom.normalIdx,
                .metallicRoughnessTextureIdx = geom.metallicRoughnessIdx,
                .transform                  = worldTransform,
            });
        }

        for (uint32_t i = 0; i < node->mNumChildren; ++i)
            traverseInstances(node->mChildren[i], worldTransform);
    };

    traverseInstances(scene->mRootNode, glm::mat4(1.0f));

    aiReleaseImport(scene);
    return importedScene;
}