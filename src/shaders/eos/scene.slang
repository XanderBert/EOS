// The scene a gltfScene graph node loads, as shaders see it. Every mesh is in one vertex buffer and one index buffer,
// read through pointers (vertex pulling), so rasterization and ray tracing read the same data.
//
// A raster pass marked [DrawScene] draws every instance of the scene on its Scene* pin:
//
//     import eos.pass;
//     import eos.scene;
//     import eos.view;
//
//     [Pass] [DrawScene] [Cull(CullMode.Back)]
//     struct GBuffer
//     {
//         [Input] Scene* scene;
//         [Input] View* view;
//         [Output] [Format("Z_F32")] DepthTarget depth;
//     };
//     [[vk::push_constant]] GBuffer pass;
//
//     [shader("vertex")]
//     VSOutput vertexMain(uint vertexID : SV_VulkanVertexID, uint instanceID : SV_VulkanInstanceID)
//     {
//         const MeshVertex vertex = pass.scene.vertices[vertexID];
//         const MeshInstance instance = pass.scene.instances[instanceID];
//         ...
//     }
//
// Each draw's vertex offset and first instance are its instance's, so SV_VulkanVertexID indexes Scene.vertices and
// SV_VulkanInstanceID Scene.instances directly (SV_VertexID and SV_InstanceID would leave them out).
module "eos/scene";

import eos.core;
import eos.bindless;
import eos.material;

// A vertex of a mesh.
[CppExport]
public struct MeshVertex
{
    public float3 position;
    public float3 normal;
    public float2 uv;
    public float4 tangent;      // xyz: tangent, w: bitangent sign (glTF); (1, 0, 0, 1) when the mesh has none
};

// A mesh placed in the scene with its material: one draw. Instances are sorted by their material's alpha mode
// (opaque, then alpha-tested, then blended). An instance's index is also its index in the scene's TLAS.
[CppExport]
public struct MeshInstance
{
    public float4x4 transform;      // object to world
    public uint firstIndex;         // into Scene.indices
    public uint indexCount;
    public int vertexOffset;        // added to the mesh's indices: they index Scene.vertices from here
    public uint materialIndex;      // into Scene.materials
};

[CppExport]
public struct Scene
{
    public MeshVertex* vertices;
    public uint* indices;
    public MeshInstance* instances;
    public StandardMaterialData* materials;
    public DescriptorHandle<RaytracingAccelerationStructure> tlas;     // invalid on devices without ray tracing
    public uint instanceCount;
    public uint materialCount;

    // The material of an instance.
    public StandardMaterialData GetMaterial(uint instance)
    {
        return materials[instances[instance].materialIndex];
    }
};

// On a [Pass] struct (eos.pass): the pass draws every instance of the scene on its Scene* pin, one indirect draw per
// fragment entry point. Its vertex shader reads the vertices itself (see the top of this file).
[__AttributeUsage(_AttributeTargets.Struct)] public struct DrawSceneAttribute {};

// On a fragment entry point of a [DrawScene] pass: it draws the instances whose material has this alpha mode, so a
// depth pass can draw opaque instances with a fragment shader that never discards (keeping early depth testing) and
// alpha-tested ones with one that does. An entry point may have several; one without any draws every instance.
[__AttributeUsage(_AttributeTargets.Function)] public struct MaterialsAttribute { AlphaMode mode; };
