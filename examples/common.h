#pragma once
#include "EOS.h"
#include "utils.h"
#include "assimp/cimport.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "glm/fwd.hpp"
#include "glm/detail/type_quat.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/transform.hpp"

struct Vertex final
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct TextureHandles final
{
    EOS::Holder<EOS::TextureHandle> albedo;
    EOS::Holder<EOS::TextureHandle> normal;
    EOS::Holder<EOS::TextureHandle> metallicRoughness;
};

void LoadModel(const std::filesystem::path& modelPath, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, TextureHandles& handles , EOS::IContext* context)
{
    const aiScene* scene = aiImportFile(modelPath.string().c_str(),aiProcess_Triangulate | aiProcess_CalcTangentSpace);
    CHECK(scene && scene->HasMeshes(), "Could not load mesh: {}", modelPath.string());
    const aiMesh* mesh = scene->mMeshes[0];

    vertices.clear();
    vertices.reserve(mesh->mNumVertices);

    indices.clear();
    indices.reserve(mesh->mNumFaces * 3);

    for (unsigned int i{}; i != mesh->mNumVertices; ++i)
    {
        const aiVector3D v = mesh->mVertices[i];
        const aiVector3D uv = mesh->mTextureCoords[0][i];
        const aiVector3D n = mesh->mNormals[i];
        const aiVector3D t   = mesh->mTangents[i];
        vertices.emplace_back(glm::vec3(v.x, v.y, v.z), glm::vec3(n.x, n.y, n.z), glm::vec2(uv.x, 1 - uv.y));
    }

    for (unsigned int i{}; i != mesh->mNumFaces; ++i)
    {
        for (int j{}; j != 3; ++j)
        {
            indices.emplace_back(mesh->mFaces[i].mIndices[j]);
        }
    }

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
                aiReturn result = material->GetTexture(textureType, 0, &texturePath);

                if (result == aiReturn_SUCCESS)
                {
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
            }

            CHECK(false, "Could not find texture");
            return {};
        };

        handles.albedo = loadTextureOfType(aiTextureType_DIFFUSE);
        handles.normal = loadTextureOfType(aiTextureType_NORMALS);
        handles.metallicRoughness = loadTextureOfType(aiTextureType_GLTF_METALLIC_ROUGHNESS);
    }

    aiReleaseImport(scene);
}