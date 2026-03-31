#pragma once

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

template <typename DrawDataT>
inline std::vector<DrawDataT> BuildDrawDataFromScene(const Scene& scene)
{
    std::vector<DrawDataT> drawData;
    drawData.reserve(scene.meshes.size());
    for (const auto& mesh : scene.meshes)
    {
        drawData.push_back(DrawDataT{
            .albedoID = mesh.albedoTextureIdx,
            .normalID = mesh.normalTextureIdx,
            .metallicRoughnessID = mesh.metallicRoughnessTextureIdx,
            .transform = mesh.transform,
        });
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
