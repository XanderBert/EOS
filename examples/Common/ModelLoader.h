#pragma once

#include "EOS.h"
#include "utils.h"
#include "texturePipeline.h"
#include "glm/fwd.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

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
    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    ~Scene()
    {
        Cleanup();
    }

    void Cleanup()
    {
        for (auto& texture : textureHandles)
        {
            if (texture.albedo.Valid()) texture.albedo.Reset();
            if (texture.metallicRoughness.Valid()) texture.metallicRoughness.Reset();
            if (texture.normal.Valid()) texture.normal.Reset();
        }
    }

    std::vector<VertexInformation> vertices;
    std::vector<uint32_t>          indices;
    std::vector<MeshEntry>         meshes;
    std::vector<TextureHandles>    textureHandles;
};

inline glm::mat4 FastGltfMatrixToGlm(const fastgltf::math::fmat4x4& m)
{
    return glm::make_mat4x4(&m.col(0)[0]);
}

inline std::optional<std::filesystem::path> ResolveTexturePath(const fastgltf::Asset& asset,
                                                               const std::filesystem::path& modelDirectory,
                                                               const std::optional<std::size_t> textureIndex)
{
    if (!textureIndex.has_value() || textureIndex.value() >= asset.textures.size())
    {
        return std::nullopt;
    }

    const fastgltf::Texture& texture = asset.textures[textureIndex.value()];
    auto imageIndex = texture.imageIndex;
    if (!imageIndex.has_value() && texture.basisuImageIndex.has_value())
    {
        imageIndex = texture.basisuImageIndex;
    }

    if (!imageIndex.has_value() || imageIndex.value() >= asset.images.size())
    {
        return std::nullopt;
    }

    const fastgltf::Image& image = asset.images[imageIndex.value()];
    if (const auto* uriSource = std::get_if<fastgltf::sources::URI>(&image.data))
    {
        if (!uriSource->uri.isLocalPath())
        {
            return std::nullopt;
        }

        const std::string relativePath(uriSource->uri.path().begin(), uriSource->uri.path().end());
        if (relativePath.empty())
        {
            return std::nullopt;
        }

        return modelDirectory / relativePath;
    }

    return std::nullopt;
}

inline EOS::Holder<EOS::TextureHandle> LoadGltfTexture(const fastgltf::Asset& asset,
                                                       const std::filesystem::path& modelDirectory,
                                                       const std::optional<std::size_t> textureIndex,
                                                       const EOS::Compression compression,
                                                       EOS::IContext* context)
{
    const auto texturePath = ResolveTexturePath(asset, modelDirectory, textureIndex);
    if (!texturePath.has_value())
    {
        return {};
    }

    return EOS::TexturePipeline::LoadTexture({
        .filePath = texturePath.value(),
        .compression = compression,
        .context = context,
    });
}

inline Scene LoadModel(const std::filesystem::path& modelPath, EOS::IContext* context)
{
    CHECK(std::filesystem::exists(modelPath), "Could not load mesh: {}", modelPath.string());

    fastgltf::GltfFileStream modelStream(modelPath);
    CHECK(modelStream.isOpen(), "Could not open mesh: {}", modelPath.string());

    static constexpr auto supportedExtensions =
        fastgltf::Extensions::KHR_mesh_quantization |
        fastgltf::Extensions::KHR_texture_basisu;
    fastgltf::Parser parser(supportedExtensions);

    constexpr auto parserOptions =
        fastgltf::Options::DontRequireValidAssetMember |
        fastgltf::Options::LoadExternalBuffers |
        fastgltf::Options::GenerateMeshIndices;

    auto parsedAsset = parser.loadGltf(modelStream, modelPath.parent_path(), parserOptions, fastgltf::Category::All);
    CHECK(parsedAsset.error() == fastgltf::Error::None,
          "Could not parse mesh {}: {}",
          modelPath.string(),
          fastgltf::getErrorMessage(parsedAsset.error()));

    const fastgltf::Asset& asset = parsedAsset.get();
    CHECK(!asset.meshes.empty(), "Could not load mesh: {}", modelPath.string());

    struct PrimitiveGeometry
    {
        uint32_t vertexOffset;
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t albedoIdx              = 0;
        uint32_t normalIdx              = 0;
        uint32_t metallicRoughnessIdx   = 0;
    };

    Scene importedScene{};
    std::vector<PrimitiveGeometry> geomCache;
    geomCache.reserve(asset.meshes.size());

    std::vector<std::vector<uint32_t>> primitiveToGeometry(asset.meshes.size());

    for (std::size_t meshIdx = 0; meshIdx < asset.meshes.size(); ++meshIdx)
    {
        const fastgltf::Mesh& mesh = asset.meshes[meshIdx];
        primitiveToGeometry[meshIdx].resize(mesh.primitives.size(), std::numeric_limits<uint32_t>::max());

        for (std::size_t primitiveIdx = 0; primitiveIdx < mesh.primitives.size(); ++primitiveIdx)
        {
            const fastgltf::Primitive& primitive = mesh.primitives[primitiveIdx];

            if (primitive.type != fastgltf::PrimitiveType::Triangles)
            {
                continue;
            }

            const auto positionAttribute = primitive.findAttribute("POSITION");
            if (positionAttribute == primitive.attributes.end())
            {
                continue;
            }

            const fastgltf::Accessor& positionAccessor = asset.accessors[positionAttribute->accessorIndex];
            CHECK(positionAccessor.count > 0, "Mesh primitive has no vertices: {}", modelPath.string());

            PrimitiveGeometry geometry{};
            geometry.vertexOffset = static_cast<uint32_t>(importedScene.vertices.size());
            geometry.indexOffset = static_cast<uint32_t>(importedScene.indices.size());

            const std::size_t vertexCount = positionAccessor.count;
            std::vector<glm::vec3> positions(vertexCount, glm::vec3(0.0f));
            std::vector<glm::vec3> normals(vertexCount, glm::vec3(0.0f, 1.0f, 0.0f));
            std::vector<glm::vec2> uvs(vertexCount, glm::vec2(0.0f));
            std::vector<glm::vec4> tangents(vertexCount, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
            std::vector<glm::vec4> colors(vertexCount, glm::vec4(1.0f));

            fastgltf::copyFromAccessor<glm::vec3>(asset, positionAccessor, positions.data());

            if (const auto normalAttribute = primitive.findAttribute("NORMAL"); normalAttribute != primitive.attributes.end())
            {
                const fastgltf::Accessor& normalAccessor = asset.accessors[normalAttribute->accessorIndex];
                fastgltf::copyFromAccessor<glm::vec3>(asset, normalAccessor, normals.data());
            }

            if (const auto uvAttribute = primitive.findAttribute("TEXCOORD_0"); uvAttribute != primitive.attributes.end())
            {
                const fastgltf::Accessor& uvAccessor = asset.accessors[uvAttribute->accessorIndex];
                fastgltf::copyFromAccessor<glm::vec2>(asset, uvAccessor, uvs.data());
            }

            if (const auto tangentAttribute = primitive.findAttribute("TANGENT"); tangentAttribute != primitive.attributes.end())
            {
                const fastgltf::Accessor& tangentAccessor = asset.accessors[tangentAttribute->accessorIndex];
                fastgltf::copyFromAccessor<glm::vec4>(asset, tangentAccessor, tangents.data());
            }

            if (const auto colorAttribute = primitive.findAttribute("COLOR_0"); colorAttribute != primitive.attributes.end())
            {
                const fastgltf::Accessor& colorAccessor = asset.accessors[colorAttribute->accessorIndex];
                if (colorAccessor.type == fastgltf::AccessorType::Vec3)
                {
                    std::vector<glm::vec3> color3(vertexCount, glm::vec3(1.0f));
                    fastgltf::copyFromAccessor<glm::vec3>(asset, colorAccessor, color3.data());
                    for (std::size_t i = 0; i < vertexCount; ++i)
                    {
                        colors[i] = glm::vec4(color3[i], 1.0f);
                    }
                }
                else if (colorAccessor.type == fastgltf::AccessorType::Vec4)
                {
                    fastgltf::copyFromAccessor<glm::vec4>(asset, colorAccessor, colors.data());
                }
            }

            importedScene.vertices.reserve(importedScene.vertices.size() + vertexCount);
            for (std::size_t vertexIdx = 0; vertexIdx < vertexCount; ++vertexIdx)
            {
                importedScene.vertices.emplace_back(VertexInformation{
                    .position = positions[vertexIdx],
                    .normal = normals[vertexIdx],
                    .uv = uvs[vertexIdx],
                    .tangent = tangents[vertexIdx],
                    .color = colors[vertexIdx],
                });
            }

            std::vector<uint32_t> localIndices;
            if (primitive.indicesAccessor.has_value())
            {
                const fastgltf::Accessor& indexAccessor = asset.accessors[primitive.indicesAccessor.value()];
                localIndices.resize(indexAccessor.count);
                fastgltf::copyFromAccessor<uint32_t>(asset, indexAccessor, localIndices.data());
            }
            else
            {
                localIndices.resize(vertexCount);
                for (std::size_t i = 0; i < vertexCount; ++i)
                {
                    localIndices[i] = static_cast<uint32_t>(i);
                }
            }

            importedScene.indices.insert(importedScene.indices.end(), localIndices.begin(), localIndices.end());
            geometry.indexCount = static_cast<uint32_t>(localIndices.size());

            TextureHandles textureHandles{};
            if (primitive.materialIndex.has_value() && primitive.materialIndex.value() < asset.materials.size())
            {
                const fastgltf::Material& material = asset.materials[primitive.materialIndex.value()];

                textureHandles.albedo = LoadGltfTexture(
                    asset,
                    modelPath.parent_path(),
                    material.pbrData.baseColorTexture.has_value()
                        ? std::optional<std::size_t>(material.pbrData.baseColorTexture->textureIndex)
                        : std::nullopt,
                    EOS::Compression::BC7,
                    context);

                textureHandles.normal = LoadGltfTexture(
                    asset,
                    modelPath.parent_path(),
                    material.normalTexture.has_value()
                        ? std::optional<std::size_t>(material.normalTexture->textureIndex)
                        : std::nullopt,
                    EOS::Compression::BC5,
                    context);

                textureHandles.metallicRoughness = LoadGltfTexture(
                    asset,
                    modelPath.parent_path(),
                    material.pbrData.metallicRoughnessTexture.has_value()
                        ? std::optional<std::size_t>(material.pbrData.metallicRoughnessTexture->textureIndex)
                        : std::nullopt,
                    EOS::Compression::BC7,
                    context);

                geometry.albedoIdx = textureHandles.albedo.Index();
                geometry.normalIdx = textureHandles.normal.Index();
                geometry.metallicRoughnessIdx = textureHandles.metallicRoughness.Index();
            }

            const uint32_t geometryIndex = static_cast<uint32_t>(geomCache.size());
            geomCache.push_back(geometry);
            importedScene.textureHandles.push_back(std::move(textureHandles));
            primitiveToGeometry[meshIdx][primitiveIdx] = geometryIndex;
        }
    }

    auto emitMeshInstances = [&](const std::size_t meshIndex, const glm::mat4& worldTransform)
    {
        if (meshIndex >= asset.meshes.size())
        {
            return;
        }

        const fastgltf::Mesh& mesh = asset.meshes[meshIndex];
        for (std::size_t primitiveIdx = 0; primitiveIdx < mesh.primitives.size(); ++primitiveIdx)
        {
            const uint32_t geometryIndex = primitiveToGeometry[meshIndex][primitiveIdx];
            if (geometryIndex == std::numeric_limits<uint32_t>::max())
            {
                continue;
            }

            const PrimitiveGeometry& geometry = geomCache[geometryIndex];
            importedScene.meshes.push_back(MeshEntry{
                .vertexOffset = geometry.vertexOffset,
                .indexOffset = geometry.indexOffset,
                .indexCount = geometry.indexCount,
                .drawDataIndex = 0,
                .albedoTextureIdx = geometry.albedoIdx,
                .normalTextureIdx = geometry.normalIdx,
                .metallicRoughnessTextureIdx = geometry.metallicRoughnessIdx,
                .transform = worldTransform,
            });
        }
    };

    if (!asset.scenes.empty())
    {
        const std::size_t sceneIndex = asset.defaultScene.value_or(0);
        if (sceneIndex < asset.scenes.size())
        {
            fastgltf::iterateSceneNodes(asset, sceneIndex, fastgltf::math::fmat4x4(),
                [&](const fastgltf::Node& node, const fastgltf::math::fmat4x4& transform)
            {
                if (node.meshIndex.has_value())
                {
                    emitMeshInstances(node.meshIndex.value(), FastGltfMatrixToGlm(transform));
                }
            });
        }
    }

    if (importedScene.meshes.empty())
    {
        for (const fastgltf::Node& node : asset.nodes)
        {
            if (node.meshIndex.has_value())
            {
                emitMeshInstances(node.meshIndex.value(), FastGltfMatrixToGlm(fastgltf::getTransformMatrix(node)));
            }
        }
    }

    return importedScene;
}