#include "scene.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "logger.h"
#include "texturePipeline.h"

namespace EOS
{
    namespace
    {
        [[nodiscard]] glm::mat4 ToGlm(const fastgltf::math::fmat4x4& matrix)
        {
            return glm::make_mat4x4(&matrix.col(0)[0]);
        }

        // A mesh primitive's geometry in the scene's buffers.
        struct Geometry final
        {
            uint32_t FirstIndex = 0;
            uint32_t IndexCount = 0;
            uint32_t VertexOffset = 0;
            uint32_t Material = 0;
        };

        // Reads the meshes, materials and textures of a glTF asset into CPU arrays, loading each texture once.
        class GltfReader final
        {
        public:
            GltfReader(const fastgltf::Asset& asset, std::filesystem::path directory, IContext* context, std::vector<TextureHolder>& textures)
            : Asset(asset), Directory(std::move(directory)), Context(context), Textures(textures)
            {
            }

            std::vector<MeshVertex> Vertices;
            std::vector<uint32_t> Indices;
            std::vector<StandardMaterialData> Materials;
            std::vector<std::vector<uint32_t>> MeshGeometries;  // per mesh, per primitive: into Geometries, or kNoGeometry
            std::vector<Geometry> Geometries;

            static constexpr uint32_t kNoGeometry = std::numeric_limits<uint32_t>::max();

            void ReadMaterials()
            {
                Materials.reserve(Asset.materials.size() + 1);
                for (const fastgltf::Material& material : Asset.materials)
                {
                    StandardMaterialData& data = Materials.emplace_back();
                    const auto& baseColor = material.pbrData.baseColorFactor;
                    data.baseColorFactor = glm::vec4(baseColor[0], baseColor[1], baseColor[2], baseColor[3]);
                    data.emissiveFactor = glm::vec3(material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2]) * material.emissiveStrength;
                    data.metallicFactor = material.pbrData.metallicFactor;
                    data.roughnessFactor = material.pbrData.roughnessFactor;
                    data.normalScale = material.normalTexture.has_value() ? material.normalTexture->scale : 1.0f;
                    data.occlusionStrength = material.occlusionTexture.has_value() ? material.occlusionTexture->strength : 1.0f;
                    data.alphaCutoff = material.alphaCutoff;
                    data.alphaMode = material.alphaMode == fastgltf::AlphaMode::Mask  ? AlphaMode::Mask
                                   : material.alphaMode == fastgltf::AlphaMode::Blend ? AlphaMode::Blend
                                                                                      : AlphaMode::Opaque;

                    data.baseColorTexture = LoadTexture(material.pbrData.baseColorTexture, Compression::BC7);
                    data.metallicRoughnessTexture = LoadTexture(material.pbrData.metallicRoughnessTexture, Compression::BC7);
                    data.normalTexture = LoadTexture(material.normalTexture, Compression::BC5);
                    data.emissiveTexture = LoadTexture(material.emissiveTexture, Compression::BC7);
                    data.occlusionTexture = LoadTexture(material.occlusionTexture, Compression::BC7);
                }
            }

            // glTF's default material, for primitives without one. The C++ struct only has Slang's literal defaults, so
            // the base color (a vector) is set here.
            [[nodiscard]] uint32_t DefaultMaterial()
            {
                if (DefaultMaterialIndex == kNoGeometry)
                {
                    DefaultMaterialIndex = static_cast<uint32_t>(Materials.size());
                    Materials.push_back({.baseColorFactor = glm::vec4(1.0f)});
                }
                return DefaultMaterialIndex;
            }

            void ReadMeshes()
            {
                MeshGeometries.resize(Asset.meshes.size());
                for (size_t meshIndex = 0; meshIndex < Asset.meshes.size(); ++meshIndex)
                {
                    const fastgltf::Mesh& mesh = Asset.meshes[meshIndex];
                    MeshGeometries[meshIndex].assign(mesh.primitives.size(), kNoGeometry);
                    for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
                    {
                        MeshGeometries[meshIndex][primitiveIndex] = ReadPrimitive(mesh.primitives[primitiveIndex]);
                    }
                }
            }

        private:
            const fastgltf::Asset& Asset;
            std::filesystem::path Directory;
            IContext* Context;
            std::vector<TextureHolder>& Textures;
            std::map<std::pair<size_t, Compression>, DescriptorHandle> LoadedTextures;
            uint32_t DefaultMaterialIndex = kNoGeometry;

            [[nodiscard]] std::optional<std::filesystem::path> ImagePath(size_t textureIndex) const
            {
                if (textureIndex >= Asset.textures.size()) return std::nullopt;

                const fastgltf::Texture& texture = Asset.textures[textureIndex];
                const std::optional<size_t> imageIndex = texture.imageIndex.has_value() ? texture.imageIndex : texture.basisuImageIndex;
                if (!imageIndex.has_value() || imageIndex.value() >= Asset.images.size()) return std::nullopt;

                const auto* uri = std::get_if<fastgltf::sources::URI>(&Asset.images[imageIndex.value()].data);
                if (!uri || !uri->uri.isLocalPath()) return std::nullopt;

                const std::string relativePath(uri->uri.path().begin(), uri->uri.path().end());
                if (relativePath.empty()) return std::nullopt;
                return Directory / relativePath;
            }

            template<typename TextureInfo>
            [[nodiscard]] DescriptorHandle LoadTexture(const std::optional<TextureInfo>& info, Compression compression)
            {
                if (!info.has_value()) return {};

                const std::pair key{info->textureIndex, compression};
                if (const auto loaded = LoadedTextures.find(key); loaded != LoadedTextures.end()) return loaded->second;

                DescriptorHandle handle{};
                if (const std::optional<std::filesystem::path> path = ImagePath(info->textureIndex))
                {
                    TextureHolder texture = TexturePipeline::LoadTexture(
                    {
                        .InputFilePath = path.value(),
                        .OutputFilePath = std::filesystem::current_path() / ".cache" / "compressed_textures",
                        .TextureCompression = compression,
                        .Context = Context,
                    });
                    if (texture.Valid())
                    {
                        handle = DescriptorHandle(texture);
                        Textures.push_back(std::move(texture));
                    }
                }

                LoadedTextures.emplace(key, handle);
                return handle;
            }

            // A triangle primitive with positions, into the vertex and index arrays; kNoGeometry for anything else.
            [[nodiscard]] uint32_t ReadPrimitive(const fastgltf::Primitive& primitive)
            {
                if (primitive.type != fastgltf::PrimitiveType::Triangles) return kNoGeometry;

                const auto position = primitive.findAttribute("POSITION");
                if (position == primitive.attributes.end()) return kNoGeometry;

                const fastgltf::Accessor& positions = Asset.accessors[position->accessorIndex];
                const size_t vertexCount = positions.count;
                if (vertexCount == 0) return kNoGeometry;

                Geometry geometry
                {
                    .FirstIndex = static_cast<uint32_t>(Indices.size()),
                    .VertexOffset = static_cast<uint32_t>(Vertices.size()),
                };

                Vertices.resize(Vertices.size() + vertexCount, MeshVertex{.normal = glm::vec3(0.0f, 1.0f, 0.0f), .tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)});
                MeshVertex* vertices = Vertices.data() + geometry.VertexOffset;

                fastgltf::iterateAccessorWithIndex<glm::vec3>(Asset, positions, [vertices](glm::vec3 value, size_t i) { vertices[i].position = value; });
                if (const auto normal = primitive.findAttribute("NORMAL"); normal != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(Asset, Asset.accessors[normal->accessorIndex], [vertices](glm::vec3 value, size_t i) { vertices[i].normal = value; });
                }
                if (const auto uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(Asset, Asset.accessors[uv->accessorIndex], [vertices](glm::vec2 value, size_t i) { vertices[i].uv = value; });
                }
                if (const auto tangent = primitive.findAttribute("TANGENT"); tangent != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(Asset, Asset.accessors[tangent->accessorIndex], [vertices](glm::vec4 value, size_t i) { vertices[i].tangent = value; });
                }

                // Indices stay relative to the primitive's first vertex; draws add VertexOffset.
                if (primitive.indicesAccessor.has_value())
                {
                    const fastgltf::Accessor& indices = Asset.accessors[primitive.indicesAccessor.value()];
                    Indices.resize(Indices.size() + indices.count);
                    fastgltf::copyFromAccessor<uint32_t>(Asset, indices, Indices.data() + geometry.FirstIndex);
                }
                else
                {
                    for (size_t i = 0; i < vertexCount; ++i) Indices.push_back(static_cast<uint32_t>(i));
                }
                geometry.IndexCount = static_cast<uint32_t>(Indices.size()) - geometry.FirstIndex;
                if (geometry.IndexCount < 3)
                {
                    Indices.resize(geometry.FirstIndex);
                    Vertices.resize(geometry.VertexOffset);
                    return kNoGeometry;
                }

                const bool hasMaterial = primitive.materialIndex.has_value() && primitive.materialIndex.value() < Asset.materials.size();
                geometry.Material = hasMaterial ? static_cast<uint32_t>(primitive.materialIndex.value()) : DefaultMaterial();

                Geometries.push_back(geometry);
                return static_cast<uint32_t>(Geometries.size() - 1);
            }
        };

        // BufferUsageFlags combined with |, which yields an int.
        [[nodiscard]] BufferUsageFlags Usage(uint32_t flags)
        {
            return static_cast<BufferUsageFlags>(flags);
        }
    }

    // -------------------------------------------------------------------------------------------------------------------
    // GltfScene

    std::unique_ptr<GltfScene> GltfScene::Load(IContext* context, const std::filesystem::path& path)
    {
        const std::string name = path.generic_string();
        fastgltf::GltfFileStream stream(path);
        if (!stream.isOpen())
        {
            Logger->error("Scene {} cannot be opened", name);
            return nullptr;
        }

        fastgltf::Parser parser(fastgltf::Extensions::KHR_mesh_quantization | fastgltf::Extensions::KHR_texture_basisu);
        constexpr auto options = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadExternalBuffers | fastgltf::Options::GenerateMeshIndices;
        auto parsed = parser.loadGltf(stream, path.parent_path(), options, fastgltf::Category::All);
        if (parsed.error() != fastgltf::Error::None)
        {
            Logger->error("Scene {} cannot be read: {}", name, fastgltf::getErrorMessage(parsed.error()));
            return nullptr;
        }
        const fastgltf::Asset& asset = parsed.get();

        std::unique_ptr<GltfScene> scene(new GltfScene());
        GltfReader reader(asset, path.parent_path(), context, scene->Textures);
        reader.ReadMaterials();
        reader.ReadMeshes();

        // An instance per primitive of every mesh a node places, in the default scene, or of every node without one;
        // with the geometry each places, for its BLAS.
        std::vector<MeshInstance>& instances = scene->Instances;
        std::vector<uint32_t> instanceGeometries;
        const auto place = [&](size_t meshIndex, const glm::mat4& transform)
        {
            if (meshIndex >= reader.MeshGeometries.size()) return;
            for (const uint32_t geometryIndex : reader.MeshGeometries[meshIndex])
            {
                if (geometryIndex == GltfReader::kNoGeometry) continue;
                const Geometry& geometry = reader.Geometries[geometryIndex];
                instances.push_back(
                {
                    .transform = transform,
                    .firstIndex = geometry.FirstIndex,
                    .indexCount = geometry.IndexCount,
                    .vertexOffset = static_cast<int32_t>(geometry.VertexOffset),
                    .materialIndex = geometry.Material,
                });
                instanceGeometries.push_back(geometryIndex);
            }
        };

        const size_t sceneIndex = asset.defaultScene.value_or(0);
        if (sceneIndex < asset.scenes.size())
        {
            fastgltf::iterateSceneNodes(asset, sceneIndex, fastgltf::math::fmat4x4(), [&](const fastgltf::Node& node, const fastgltf::math::fmat4x4& transform)
            {
                if (node.meshIndex.has_value()) place(node.meshIndex.value(), ToGlm(transform));
            });
        }
        if (instances.empty())
        {
            for (const fastgltf::Node& node : asset.nodes)
            {
                if (node.meshIndex.has_value()) place(node.meshIndex.value(), ToGlm(fastgltf::getTransformMatrix(node)));
            }
        }
        if (instances.empty() || reader.Vertices.empty())
        {
            Logger->error("Scene {} has no triangle meshes", name);
            return nullptr;
        }

        // Opaque, then alpha-tested, then blended: a [DrawScene] pass draws each run with the fragment shader for it.
        std::vector<StandardMaterialData>& materials = scene->Materials;
        materials = std::move(reader.Materials);
        {
            const auto alphaMode = [&](uint32_t instance) { return static_cast<uint32_t>(materials[instances[instance].materialIndex].alphaMode); };
            std::vector<uint32_t> order(instances.size());
            for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
            std::ranges::stable_sort(order, {}, alphaMode);

            std::vector<MeshInstance> sortedInstances;
            std::vector<uint32_t> sortedGeometries;
            sortedInstances.reserve(order.size());
            sortedGeometries.reserve(order.size());
            for (const uint32_t i : order)
            {
                sortedInstances.push_back(instances[i]);
                sortedGeometries.push_back(instanceGeometries[i]);
            }
            instances = std::move(sortedInstances);
            instanceGeometries = std::move(sortedGeometries);
        }

        SceneDrawData& draws = scene->DrawData;
        for (const MeshInstance& instance : instances) ++draws.InstanceCount[static_cast<uint32_t>(materials[instance.materialIndex].alphaMode)];
        draws.FirstInstance = {0, draws.InstanceCount[0], draws.InstanceCount[0] + draws.InstanceCount[1]};

        std::vector<DrawIndexedIndirectCommand> commands;
        commands.reserve(instances.size());
        for (uint32_t i = 0; i < instances.size(); ++i)
        {
            commands.push_back({.indexCount = instances[i].indexCount, .instanceCount = 1, .firstIndex = instances[i].firstIndex, .vertexOffset = instances[i].vertexOffset, .firstInstance = i});
        }

        scene->MaterialSampler = context->CreateSampler({.mipMap = SamplerMip::Linear, .mipLodMax = EOS_MAX_MIP_LEVELS, .maxAnisotropic = 0, .debugName = "Scene Material Sampler"});
        for (StandardMaterialData& material : materials) material.samplerState = scene->MaterialSampler;

        const bool accelerationStructures = context->SupportsAccelerationStructures();
        const uint8_t geometryInput = accelerationStructures ? BufferUsageFlags::AccelStructBuildInputReadOnly : 0;
        const std::vector<MeshVertex>& vertices = reader.Vertices;
        const std::vector<uint32_t>& indices = reader.Indices;

        scene->VertexBuffer = context->CreateBuffer({.Usage = Usage(BufferUsageFlags::StorageFlag | geometryInput), .Storage = StorageType::Device, .Size = sizeof(MeshVertex) * vertices.size(), .Data = vertices.data(), .DebugName = "Scene Vertices"});
        scene->IndexBuffer = context->CreateBuffer({.Usage = Usage(BufferUsageFlags::Index | BufferUsageFlags::StorageFlag | geometryInput), .Storage = StorageType::Device, .Size = sizeof(uint32_t) * indices.size(), .Data = indices.data(), .DebugName = "Scene Indices"});
        scene->InstanceBuffer = context->CreateBuffer({.Usage = BufferUsageFlags::StorageFlag, .Storage = StorageType::Device, .Size = sizeof(MeshInstance) * instances.size(), .Data = instances.data(), .DebugName = "Scene Instances"});
        scene->MaterialBuffer = context->CreateBuffer({.Usage = BufferUsageFlags::StorageFlag, .Storage = StorageType::Device, .Size = sizeof(StandardMaterialData) * materials.size(), .Data = materials.data(), .DebugName = "Scene Materials"});
        scene->IndirectBuffer = context->CreateBuffer({.Usage = BufferUsageFlags::Indirect, .Storage = StorageType::Device, .Size = sizeof(DrawIndexedIndirectCommand) * commands.size(), .Data = commands.data(), .DebugName = "Scene Draws"});
        draws.IndexBuffer = scene->IndexBuffer;
        draws.IndirectBuffer = scene->IndirectBuffer;

        // A BLAS per geometry an instance uses, and the TLAS over the instances, in instance order.
        if (accelerationStructures)
        {
            constexpr glm::mat3x4 identity{1.0f};
            scene->BLASTransformBuffer = context->CreateBuffer({.Usage = BufferUsageFlags::AccelStructBuildInputReadOnly, .Storage = StorageType::HostVisible, .Size = sizeof(identity), .Data = &identity, .DebugName = "Scene BLAS Transform"});

            std::vector<uint32_t> geometryBLAS(reader.Geometries.size(), GltfReader::kNoGeometry);
            std::vector<AccelStructInstance> tlasInstances;
            tlasInstances.reserve(instances.size());
            for (uint32_t i = 0; i < instances.size(); ++i)
            {
                const MeshInstance& instance = instances[i];
                const uint32_t geometryIndex = instanceGeometries[i];
                if (geometryBLAS[geometryIndex] == GltfReader::kNoGeometry)
                {
                    geometryBLAS[geometryIndex] = static_cast<uint32_t>(scene->BLASes.size());
                    scene->BLASes.push_back(context->CreateAccelerationStructure(
                    {
                        .Type = AccelerationStructureType::BLAS,
                        .GeometryType = AccelerationStructureGeometryType::Triangles,
                        .VertexFormatStructure = VertexFormat::Float3,
                        .VertexBuffer = scene->VertexBuffer,
                        .VertexStride = sizeof(MeshVertex),
                        .NumberOfVertices = static_cast<uint32_t>(vertices.size()),
                        .IndexBuffer = scene->IndexBuffer,
                        .TransformBuffer = scene->BLASTransformBuffer,
                        .BuildRange =
                        {
                            .PrimitiveCount = instance.indexCount / 3,
                            .PrimitiveOffset = instance.firstIndex * static_cast<uint32_t>(sizeof(uint32_t)),
                            .FirstVertex = static_cast<uint32_t>(instance.vertexOffset),
                        },
                        .BuildFlags = PreferFastTrace,
                        .DebugName = "Scene BLAS",
                    }));
                }

                const glm::mat4& m = instance.transform;
                tlasInstances.push_back(
                {
                    .Transform =
                    {
                        {m[0][0], m[1][0], m[2][0], m[3][0]},
                        {m[0][1], m[1][1], m[2][1], m[3][1]},
                        {m[0][2], m[1][2], m[2][2], m[3][2]},
                    },
                    .InstanceCustomIndex = i,
                    .Mask = 0xFF,
                    .Flags = TriangleFacingCullDisable,
                    .AccelerationStructureReference = context->GetGPUAddress(scene->BLASes[geometryBLAS[geometryIndex]]),
                });
            }

            scene->TLASInstanceBuffer = context->CreateBuffer({.Usage = BufferUsageFlags::AccelStructBuildInputReadOnly, .Storage = StorageType::HostVisible, .Size = sizeof(AccelStructInstance) * tlasInstances.size(), .Data = tlasInstances.data(), .DebugName = "Scene TLAS Instances"});
            scene->TLAS = context->CreateAccelerationStructure(
            {
                .Type = AccelerationStructureType::TLAS,
                .GeometryType = AccelerationStructureGeometryType::Instances,
                .InstancesBuffer = scene->TLASInstanceBuffer,
                .BuildRange = {.PrimitiveCount = static_cast<uint32_t>(tlasInstances.size())},
                .BuildFlags = PreferFastTrace,
                .DebugName = "Scene TLAS",
            });
        }

        const Scene header
        {
            .vertices = context->GetGPUAddress(scene->VertexBuffer),
            .indices = context->GetGPUAddress(scene->IndexBuffer),
            .instances = context->GetGPUAddress(scene->InstanceBuffer),
            .materials = context->GetGPUAddress(scene->MaterialBuffer),
            .tlas = scene->TLAS.Valid() ? DescriptorHandle(scene->TLAS) : DescriptorHandle{},
            .instanceCount = static_cast<uint32_t>(instances.size()),
            .materialCount = static_cast<uint32_t>(materials.size()),
        };
        scene->SceneBuffer = context->CreateBuffer({.Usage = BufferUsageFlags::StorageFlag, .Storage = StorageType::Device, .Size = sizeof(header), .Data = &header, .DebugName = "Scene"});

        Logger->info("Scene {}: {} instances ({} opaque, {} alpha-tested, {} blended), {} materials, {} textures, {} vertices, {} triangles{}",
                     name, instances.size(), draws.InstanceCount[0], draws.InstanceCount[1], draws.InstanceCount[2], materials.size(), scene->Textures.size(),
                     vertices.size(), indices.size() / 3, accelerationStructures ? fmt::format(", {} BLASes", scene->BLASes.size()) : std::string{});
        return scene;
    }

    GltfScene::~GltfScene() = default;

    // -------------------------------------------------------------------------------------------------------------------
    // The gltfScene pass type

    namespace
    {
        // The scenes graph files use, by path. A scene not used in the last loads of the others is released: a graph
        // file changed its path, or stopped using it. Counted in uses, not time, so a minimized window keeps them.
        class SceneCache final
        {
        public:
            struct Entry final
            {
                std::string Path;
                std::string DebugName;
                std::unique_ptr<GltfScene> Scene;   // nullptr when it could not be loaded
                uint64_t LastUsed = 0;
            };

            SceneCache(IContext* context, std::filesystem::path assetDirectory) : Context(context), AssetDirectory(std::move(assetDirectory)) {}

            // nullptr when the file cannot be loaded (reported once).
            [[nodiscard]] Entry* Get(std::string_view path)
            {
                const uint64_t now = ++Uses;
                std::erase_if(Entries, [now](const std::unique_ptr<Entry>& entry) { return now - entry->LastUsed > kReleaseAfter; });

                auto found = std::ranges::find(Entries, path, [](const std::unique_ptr<Entry>& entry) { return std::string_view(entry->Path); });
                if (found == Entries.end())
                {
                    auto entry = std::make_unique<Entry>();
                    entry->Path = path;
                    entry->DebugName = fmt::format("Scene {}", path);
                    entry->Scene = GltfScene::Load(Context, AssetDirectory / path);
                    Entries.push_back(std::move(entry));
                    found = Entries.end() - 1;
                }

                (*found)->LastUsed = now;
                return (*found)->Scene ? found->get() : nullptr;
            }

        private:
            static constexpr uint64_t kReleaseAfter = 120;
            uint64_t Uses = 0;

            IContext* Context;
            std::filesystem::path AssetDirectory;
            std::vector<std::unique_ptr<Entry>> Entries;    // in place: passes refer to them until the frame executes
        };
    }

    void RegisterGltfScene(PassRegistry& registry, IContext* context, std::filesystem::path assetDirectory)
    {
        auto cache = std::make_shared<SceneCache>(context, std::move(assetDirectory));
        registry.Register(
        {
            .Name = "gltfScene",
            .Pins = {EOS::Pin::Typed(EOS::Pin::BufferOutput("scene", 0), "Scene")},
            .Properties = {EOS::Property::String("path")},
            .Setup = [cache](PassSetup& setup)
            {
                const SceneCache::Entry* entry = cache->Get(setup.Data.String("path"));
                if (!entry) return;

                const GraphBuffer buffer = setup.Graph.ImportBuffer(entry->Scene->GetSceneBuffer(), entry->DebugName.c_str());
                setup.Output("scene", buffer, &entry->Scene->GetDrawData());
            },
        });
    }
}
