#pragma once

#include <algorithm>
#include <iterator>
#include <vector>

#include "EOS.h"
#include "ModelLoader.h"

// Shared helpers for example setup.

template <typename VertexT>
inline std::vector<VertexT> BuildVerticesFromScene(const Scene& scene)
{
    std::vector<VertexT> vertices;
    vertices.reserve(scene.vertices.size());
    for (const VertexInformation& vertexInfo : scene.vertices)
    {
        vertices.push_back(VertexT{
            vertexInfo.position,
            vertexInfo.normal,
            vertexInfo.uv,
            vertexInfo.tangent
        });
    }
    return vertices;
}

// Orders the meshes so every opaque mesh comes before every alpha-tested (glTF MASK) one, and returns how many are
// opaque. Call it before building any per-mesh buffer. Depth-only passes can then draw the opaque range with a
// fragment shader that never discards, which keeps the GPU's early depth test, and pay for alpha testing only on the
// alpha-tested range.
inline uint32_t PartitionMeshesByAlphaTest(Scene& scene)
{
    const auto firstAlphaTested = std::stable_partition(scene.meshes.begin(), scene.meshes.end(), [](const MeshEntry& mesh)
    {
        return mesh.material.Alpha != EOS::AlphaMode::Mask;
    });

    return static_cast<uint32_t>(std::distance(scene.meshes.begin(), firstAlphaTested));
}

// DrawDataT needs a `material` (EOS::StandardMaterialData) and a `transform` member.
// materialSampler is the sampler every material reads its textures with.
template <typename DrawDataT>
inline std::vector<DrawDataT> BuildDrawDataFromScene(const Scene& scene, EOS::DescriptorHandle materialSampler)
{
    std::vector<DrawDataT> drawData;
    drawData.reserve(scene.meshes.size());
    for (const auto& mesh : scene.meshes)
    {
        DrawDataT& draw = drawData.emplace_back();
        draw.material = mesh.material;
        draw.material.Sampler = materialSampler;
        draw.transform = mesh.transform;
    }
    return drawData;
}

inline std::vector<EOS::DrawIndexedIndirectCommand> BuildIndirectCommands(const Scene& scene)
{
    std::vector<EOS::DrawIndexedIndirectCommand> indirectCmds;
    indirectCmds.reserve(scene.meshes.size());
    for (const auto& mesh : scene.meshes)
    {
        indirectCmds.emplace_back(EOS::DrawIndexedIndirectCommand
        {
            .indexCount    = mesh.indexCount,
            .instanceCount = 1,
            .firstIndex    = mesh.indexOffset,
            .vertexOffset  = static_cast<int32_t>(mesh.vertexOffset),
            .firstInstance = 0,
        });
    }
    return indirectCmds;
}
