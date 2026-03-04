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

struct TextureHandles final
{
    EOS::Holder<EOS::TextureHandle> albedo;
    EOS::Holder<EOS::TextureHandle> normal;
    EOS::Holder<EOS::TextureHandle> metallicRoughness;
};

struct MeshEntry final
{
    uint32_t vertexOffset;  // offset into the vertex buffer
    uint32_t indexOffset;   // offset into the index buffer
    uint32_t indexCount;
    uint32_t drawDataIndex; // index into the DrawData buffer
    TextureHandles textures;
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
    std::vector<uint32_t>  indices;
    std::vector<MeshEntry> meshes;
};

inline Scene LoadModel(const std::filesystem::path& modelPath, EOS::IContext* context)
{
    const aiScene* scene = aiImportFile(modelPath.string().c_str(),aiProcess_Triangulate | aiProcess_CalcTangentSpace | aiProcess_OptimizeMeshes);
    CHECK(scene && scene->HasMeshes(), "Could not load mesh: {}", modelPath.string());

    //Calculate vertices and indices
    const int nMeshes = scene->mNumMeshes;
    int numVertices{};
    int numIndices{};
    for (int i{}; i < nMeshes; ++i)
    {
        numVertices += scene->mMeshes[i]->mNumVertices;
        numIndices += scene->mMeshes[i]->mNumFaces * 3;
    }

    Scene importedScene{};
    importedScene.meshes.resize(nMeshes);
    importedScene.vertices.reserve(numVertices);
    importedScene.indices.reserve(numIndices);

    for (int meshIdx{}; meshIdx < nMeshes; ++meshIdx)
    {
        MeshEntry& meshEntry = importedScene.meshes[meshIdx];
        const aiMesh* mesh = scene->mMeshes[meshIdx];

        meshEntry.vertexOffset = importedScene.vertices.size();
        meshEntry.indexOffset = importedScene.indices.size();

        const aiColor4D* colors = mesh->mColors[0];

        for (unsigned int i{}; i != mesh->mNumVertices; ++i)
        {
            const aiVector3D v  = mesh->mVertices[i];
            const aiVector3D uv = mesh->mTextureCoords[0][i];
            const aiVector3D n  = mesh->mNormals[i];
            const aiVector3D t  = mesh->mTangents[i];

            VertexInformation vertex
            {
                .position       = glm::vec3(v.x, v.y, v.z),
                .normal         = glm::vec3(n.x, n.y, n.z),
                .uv             = glm::vec2(uv.x, 1 - uv.y),
                .tangent        = glm::vec4{t.x, t.y, t.z, 1.0f},
                .color          = colors ? glm::vec4(colors[i].r, colors[i].g, colors[i].b, colors[i].a) : glm::vec4(1),
            };

            importedScene.vertices.emplace_back(vertex);
        }



        for (unsigned int i{}; i != mesh->mNumFaces; ++i)
        {
            for (int j{}; j != 3; ++j)
            {
                importedScene.indices.emplace_back(mesh->mFaces[i].mIndices[j]);
            }
        }
        meshEntry.indexCount = importedScene.indices.size() - meshEntry.indexOffset;



        // Extract texture path from material
        if (scene->mNumMaterials > 0 && mesh->mMaterialIndex < scene->mNumMaterials)
        {
            const aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

            //Lambda for texture extraction
            auto loadTextureOfType = [&](aiTextureType textureType) -> EOS::Holder<EOS::TextureHandle>
            {
                if (material->GetTextureCount(textureType) > 0)
                {
                    aiString texturePath;
                    CHECK(material->GetTexture(textureType, 0, &texturePath) == aiReturn_SUCCESS, "Mesh has no {} Texture at path: {}", aiTextureTypeToString(textureType), texturePath.C_Str());

                    // Convert relative path to absolute if needed
                    std::filesystem::path fullTexturePath = modelPath.parent_path() / texturePath.C_Str();

                    // Load the texture
                    EOS::TextureLoadingDescription textureDescription
                    {
                        .filePath = fullTexturePath,
                        .compression = textureType == aiTextureType_NORMALS ? EOS::Compression::BC5 : EOS::Compression::BC7,
                        .context = context,
                    };

                    return EOS::LoadTexture(textureDescription);
                }
                return {};
            };

            meshEntry.textures.albedo = loadTextureOfType(aiTextureType_DIFFUSE);
            meshEntry.textures.normal = loadTextureOfType(aiTextureType_NORMALS);
            meshEntry.textures.metallicRoughness = loadTextureOfType(aiTextureType_GLTF_METALLIC_ROUGHNESS);
        }
    }

    aiReleaseImport(scene);
    return importedScene;
}