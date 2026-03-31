module bindings;

[[vk::binding(0, 0)]] Texture2D kTextures2D[];
[[vk::binding(1, 0)]] SamplerState kSamplers[];
[[vk::binding(2, 0)]] RWTexture2D<float4> kStorageImages2D[];
[[vk::binding(3, 0)]] Texture2DArray kTextures2DArray[];
[[vk::binding(4, 0)]] RaytracingAccelerationStructure kAccelerationStructures[];

public RaytracingAccelerationStructure GetAccelerationStructure(uint AccelerationStructureId)
{
    return kAccelerationStructures[NonUniformResourceIndex(AccelerationStructureId)];
}

public void ImageBindless2DStore(uint imageId, int2 pixel, float4 value)
{
    kStorageImages2D[NonUniformResourceIndex(imageId)][pixel] = value;
}

public int2 ImageBindless2DSize(uint imageId)
{
    int2 size;
    kStorageImages2D[NonUniformResourceIndex(imageId)].GetDimensions(size.x, size.y);
    return size;
}

public float4 TextureBindless2D(uint textureId, uint samplerId, float2 uv)
{
    return kTextures2D[NonUniformResourceIndex(textureId)].Sample(kSamplers[NonUniformResourceIndex(samplerId)], uv);
}

public float4 TextureBindless2DLod(uint textureId, uint samplerId, float2 uv, float lod)
{
    return kTextures2D[NonUniformResourceIndex(textureId)].SampleLevel(kSamplers[NonUniformResourceIndex(samplerId)], uv, lod);
}

public float4 TextureBindless2DLoad(uint textureId, int2 pixel)
{
    return kTextures2D[NonUniformResourceIndex(textureId)].Load(int3(pixel, 0));
}

public float4 TextureBindless2DArray(uint textureId, uint samplerId, float3 uvw)
{
    return kTextures2DArray[NonUniformResourceIndex(textureId)].Sample(kSamplers[NonUniformResourceIndex(samplerId)], uvw);
}

public T LoadBuffer<T>(uint64_t address)
{
    return *((Ptr<T>)address);
}

public T LoadBufferArray<T>(uint64_t address, uint64_t index)
{
    return *((Ptr<T>)address + index);
}
