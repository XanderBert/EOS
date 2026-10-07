#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "EOS.h"
#include "renderGraphFile.h"
#include ".generated/eos/scene.h"   // MeshVertex, MeshInstance, Scene

// Scenes from glTF files on the GPU, in the layout eos.scene gives shaders, and the gltfScene graph node that loads one:
//
//     passes:
//       Sponza:   { type: gltfScene, path: sponza/Sponza.gltf }
//       Geometry: { type: gbuffer }
//     edges:
//       - Sponza.scene -> Geometry.scene
namespace EOS
{
    /**
     * @brief A glTF scene on the GPU: every mesh in one vertex and one index buffer, an instance per mesh placed in the
     *        scene (sorted opaque, alpha-tested, blended), the materials with their textures, the indirect draws of
     *        [DrawScene] passes, and a TLAS over the instances on devices that build acceleration structures.
     */
    class GltfScene final
    {
    public:
        /**
         * @brief Loads the default scene of a glTF file (every node with a mesh when it has none). Textures are
         *        compressed (BC5 for normal maps, BC7 otherwise) and cached in .cache/compressed_textures under the
         *        working directory.
         * @return nullptr, after reporting why, when the file cannot be loaded.
         */
        [[nodiscard]] static std::unique_ptr<GltfScene> Load(IContext* context, const std::filesystem::path& path);

        ~GltfScene();
        DELETE_COPY_MOVE(GltfScene)

        // eos.scene's Scene: what passes read through a Scene*.
        [[nodiscard]] BufferHandle GetSceneBuffer() const { return SceneBuffer; }
        [[nodiscard]] const SceneDrawData& GetDrawData() const { return DrawData; }

        // As on the GPU.
        [[nodiscard]] std::span<const MeshInstance> GetInstances() const { return Instances; }
        [[nodiscard]] std::span<const StandardMaterialData> GetMaterials() const { return Materials; }

        // Invalid on devices without acceleration structures.
        [[nodiscard]] AccelStructHandle GetTLAS() const { return TLAS; }

    private:
        GltfScene() = default;

        std::vector<MeshInstance> Instances;
        std::vector<StandardMaterialData> Materials;
        SceneDrawData DrawData;

        std::vector<TextureHolder> Textures;
        SamplerHolder MaterialSampler;
        BufferHolder VertexBuffer;
        BufferHolder IndexBuffer;
        BufferHolder InstanceBuffer;
        BufferHolder MaterialBuffer;
        BufferHolder IndirectBuffer;
        BufferHolder SceneBuffer;

        BufferHolder BLASTransformBuffer;
        BufferHolder TLASInstanceBuffer;
        std::vector<AccelStructHolder> BLASes;
        AccelStructHolder TLAS;
    };

    /**
     * @brief Registers the gltfScene pass type: a node with a `path` property (a glTF file, relative to assetDirectory)
     *        and a `scene` output. A scene is loaded the first time a graph file uses its path and stays loaded while
     *        graph files keep using it, across reloads.
     */
    void RegisterGltfScenePass(PassRegistry& registry, IContext* context, std::filesystem::path assetDirectory);
}
