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

public struct MaterialHandles
{
    public uint albedo;
    public uint normal;
    public uint metallicRoughness;
}

public struct Material
{
    public float r;
    public float g;
    public float b;
    public float metallic;
    public float roughness;
};

public struct VSInput
{
    public uint vertexID : SV_VertexID;
    public float3 Pos : POSITION;
    public float3 Normal;
    public float2 UV  : TEXCOORD0;
};

public struct VSOutput
{
    public float4 Pos : SV_Position;
    public float3 Normal;
    public float2 UV  : TEXCOORD0;
};