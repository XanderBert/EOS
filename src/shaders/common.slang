module common;

[[vk::binding(1, 0)]] SamplerState kSamplers[];
[[vk::binding(0, 0)]] Texture2D kTextures2D[];

public float4 textureBindless2D(uint textureid, uint samplerid, float2 uv)
{
    return kTextures2D[NonUniformResourceIndex(textureid)].Sample(kSamplers[NonUniformResourceIndex(samplerid)], uv);
}

public func loadBuffer<T>(uint64_t address) -> T
{
    return *((Ptr<T>)address);
}

public struct Material
{
    public float r;
    public float g;
    public float b;
    public float metallic;
    public float roughness;
};
