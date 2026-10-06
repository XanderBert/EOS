// Bindless resource access.
//
// Every texture, sampler, storage image and acceleration structure lives in EOS's one global descriptor set (set 0),
// at the index of its handle. Shaders refer to a resource with DescriptorHandle<T>: an 8-byte value that can sit in any
// struct, buffer or push constant, and is used like the resource itself:
//
//     DescriptorHandle<Texture2D> albedo;
//     DescriptorHandle<SamplerState> linearSampler;
//     float4 color = albedo.Sample(linearSampler, uv);
//
// The C++ side writes EOS::DescriptorHandle, built from a TextureHandle, SamplerHandle or AccelStructHandle.
// Buffers are not bindless descriptors: pass their device address and read them through a pointer (T*).
module "eos/bindless";

// Bindings of the global descriptor set. Must match EOS::Bindings (src/enums.h).
public static const uint kBindingSampledTextures         = 0;   // every Texture* type: 2D, 2D array, cube, 3D
public static const uint kBindingSamplers                = 1;   // SamplerState and SamplerComparisonState
public static const uint kBindingStorageTextures         = 2;   // every RWTexture* type
public static const uint kBindingAccelerationStructures  = 3;

// Value of an EOS::DescriptorHandle that refers to nothing.
public static const uint kInvalidDescriptorIndex = 0xFFFFFFFF;

// The bindless arrays, untyped: Slang materializes one aliased array per resource type a shader casts to
// (Texture2D, Texture2DArray, TextureCube, ...), all at the same binding. They are declared here with an explicit set
// and binding rather than left to Slang's automatic bindless heap, which moves to another set when a program declares
// specialization constants.
[[vk::binding(kBindingSampledTextures, 0)]]        __DynamicResource<__DynamicResourceKind.General> gBindlessSampledTextures[];
[[vk::binding(kBindingSamplers, 0)]]               __DynamicResource<__DynamicResourceKind.Sampler> gBindlessSamplers[];
[[vk::binding(kBindingStorageTextures, 0)]]        __DynamicResource<__DynamicResourceKind.General> gBindlessStorageTextures[];
[[vk::binding(kBindingAccelerationStructures, 0)]] __DynamicResource<__DynamicResourceKind.General> gBindlessAccelerationStructures[];

// Resolves DescriptorHandle<T> to the array T lives in. Every texture type aliases binding 0 (and every storage
// texture type binding 2), so one index space covers 2D, array, cube and 3D textures; the descriptor at an index is
// the texture's own view, so a texture has to be accessed with the type it was created as.
// Indices are always treated as non-uniform: a handle read from a buffer can differ between invocations.
export T getDescriptorFromHandle<T : IOpaqueDescriptor>(DescriptorHandle<T> handle)
{
    __target_switch
    {
    case spirv:
        const uint index = ((uint2)handle).x;
        if (T.kind == DescriptorKind.Sampler)
            return gBindlessSamplers[NonUniformResourceIndex(index)].asOpaqueDescriptor<T>();
        if (T.kind == DescriptorKind.AccelerationStructure)
            return gBindlessAccelerationStructures[NonUniformResourceIndex(index)].asOpaqueDescriptor<T>();
        if (T.kind == DescriptorKind.Texture && T.descriptorAccess == DescriptorAccess.Read)
            return gBindlessSampledTextures[NonUniformResourceIndex(index)].asOpaqueDescriptor<T>();
        if (T.kind == DescriptorKind.Texture)
            return gBindlessStorageTextures[NonUniformResourceIndex(index)].asOpaqueDescriptor<T>();
        // Buffers, texel buffers and combined image samplers have no place in EOS's descriptor set.
        return defaultGetDescriptorFromHandle(handle);
    default:
        return defaultGetDescriptorFromHandle(handle);
    }
}

// Whether a handle refers to a resource, for optional resources such as a material without a normal map.
public bool IsValid<T : IOpaqueDescriptor>(DescriptorHandle<T> handle)
{
    return ((uint2)handle).x != kInvalidDescriptorIndex;
}
